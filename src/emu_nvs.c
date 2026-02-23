/*
 * emu_nvs.c -- NVS emulation via file-backed key-value store
 *
 * Each namespace gets a binary file in ~/.cyd-emulator/nvs/.
 * Format: sequence of (key_len[1], key[key_len], val_len[4LE], val[val_len])
 * On commit, the entire file is rewritten.
 *
 * Simple and correct, not optimized for large datasets.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "nvs";

/* ---- In-memory key-value entry ---- */

#define NVS_MAX_KEY_LEN   16
#define NVS_MAX_ENTRIES   128

struct nvs_entry {
    char key[NVS_MAX_KEY_LEN];
    void *data;
    size_t size;
};

struct nvs_namespace {
    char name[NVS_MAX_KEY_LEN];
    nvs_open_mode_t mode;
    struct nvs_entry entries[NVS_MAX_ENTRIES];
    int count;
    int dirty;
    char filepath[512];
};

/* ---- Handle management ---- */

#define MAX_NVS_HANDLES 16
static struct nvs_namespace *handles[MAX_NVS_HANDLES];

static nvs_handle_t alloc_handle(struct nvs_namespace *ns)
{
    for (int i = 0; i < MAX_NVS_HANDLES; i++) {
        if (!handles[i]) {
            handles[i] = ns;
            return (nvs_handle_t)(i + 1);  /* 1-based */
        }
    }
    return 0;
}

static struct nvs_namespace *get_ns(nvs_handle_t handle)
{
    if (handle == 0 || handle > MAX_NVS_HANDLES) return NULL;
    return handles[handle - 1];
}

/* ---- Directory setup ---- */

static void ensure_nvs_dir(void)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.cyd-emulator", home);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.cyd-emulator/nvs", home);
    mkdir(dir, 0755);
}

static void ns_filepath(const char *namespace_name, char *buf, size_t bufsize)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buf, bufsize, "%s/.cyd-emulator/nvs/%s.nvs", home, namespace_name);
}

/* ---- File I/O ---- */

static void ns_load(struct nvs_namespace *ns)
{
    FILE *f = fopen(ns->filepath, "rb");
    if (!f) return;

    ns->count = 0;
    while (ns->count < NVS_MAX_ENTRIES) {
        uint8_t klen;
        if (fread(&klen, 1, 1, f) != 1) break;
        if (klen == 0 || klen >= NVS_MAX_KEY_LEN) break;

        struct nvs_entry *e = &ns->entries[ns->count];
        if (fread(e->key, 1, klen, f) != klen) break;
        e->key[klen] = '\0';

        uint32_t vlen;
        if (fread(&vlen, 1, 4, f) != 4) break;
        if (vlen > 1024 * 1024) break;  /* sanity limit: 1MB */

        e->data = malloc(vlen);
        if (!e->data) break;
        if (fread(e->data, 1, vlen, f) != vlen) {
            free(e->data);
            e->data = NULL;
            break;
        }
        e->size = vlen;
        ns->count++;
    }

    fclose(f);
}

static void ns_save(struct nvs_namespace *ns)
{
    FILE *f = fopen(ns->filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot write %s: %s", ns->filepath, strerror(errno));
        return;
    }

    for (int i = 0; i < ns->count; i++) {
        struct nvs_entry *e = &ns->entries[i];
        uint8_t klen = (uint8_t)strlen(e->key);
        uint32_t vlen = (uint32_t)e->size;
        fwrite(&klen, 1, 1, f);
        fwrite(e->key, 1, klen, f);
        fwrite(&vlen, 1, 4, f);
        fwrite(e->data, 1, e->size, f);
    }

    fclose(f);
    ns->dirty = 0;
}

/* ---- Entry lookup ---- */

static struct nvs_entry *find_entry(struct nvs_namespace *ns, const char *key)
{
    for (int i = 0; i < ns->count; i++) {
        if (strcmp(ns->entries[i].key, key) == 0)
            return &ns->entries[i];
    }
    return NULL;
}

static esp_err_t set_entry(struct nvs_namespace *ns, const char *key,
                            const void *data, size_t size)
{
    if (ns->mode == NVS_READONLY) return ESP_FAIL;

    struct nvs_entry *e = find_entry(ns, key);
    if (!e) {
        if (ns->count >= NVS_MAX_ENTRIES) return ESP_FAIL;
        e = &ns->entries[ns->count++];
        strncpy(e->key, key, NVS_MAX_KEY_LEN - 1);
        e->key[NVS_MAX_KEY_LEN - 1] = '\0';
        e->data = NULL;
        e->size = 0;
    }

    void *new_data = malloc(size);
    if (!new_data) return ESP_FAIL;
    memcpy(new_data, data, size);

    free(e->data);
    e->data = new_data;
    e->size = size;
    ns->dirty = 1;
    return ESP_OK;
}

static esp_err_t get_entry(struct nvs_namespace *ns, const char *key,
                            void *out, size_t expected_size)
{
    struct nvs_entry *e = find_entry(ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->size != expected_size) return ESP_FAIL;
    memcpy(out, e->data, expected_size);
    return ESP_OK;
}

/* ---- Public API ---- */

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                    nvs_handle_t *out_handle)
{
    if (!namespace_name || !out_handle) return ESP_FAIL;

    ensure_nvs_dir();

    struct nvs_namespace *ns = calloc(1, sizeof(*ns));
    if (!ns) return ESP_FAIL;

    strncpy(ns->name, namespace_name, NVS_MAX_KEY_LEN - 1);
    ns->mode = open_mode;
    ns_filepath(namespace_name, ns->filepath, sizeof(ns->filepath));
    ns_load(ns);

    nvs_handle_t h = alloc_handle(ns);
    if (h == 0) {
        /* Free loaded data */
        for (int i = 0; i < ns->count; i++) free(ns->entries[i].data);
        free(ns);
        return ESP_FAIL;
    }

    *out_handle = h;
    ESP_LOGI(TAG, "Opened namespace '%s' (%s)",
             namespace_name, open_mode == NVS_READWRITE ? "rw" : "ro");
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle)
{
    struct nvs_namespace *ns = get_ns(handle);
    if (!ns) return;

    if (ns->dirty) ns_save(ns);

    for (int i = 0; i < ns->count; i++)
        free(ns->entries[i].data);

    handles[handle - 1] = NULL;
    free(ns);
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    struct nvs_namespace *ns = get_ns(handle);
    if (!ns) return ESP_ERR_NVS_INVALID_HANDLE;
    if (ns->dirty) ns_save(ns);
    return ESP_OK;
}

/* ---- Typed setters ---- */

#define NVS_SET_IMPL(type, suffix) \
esp_err_t nvs_set_##suffix(nvs_handle_t handle, const char *key, type value) { \
    struct nvs_namespace *ns = get_ns(handle); \
    if (!ns) return ESP_ERR_NVS_INVALID_HANDLE; \
    return set_entry(ns, key, &value, sizeof(value)); \
}

NVS_SET_IMPL(int8_t,   i8)
NVS_SET_IMPL(uint8_t,  u8)
NVS_SET_IMPL(int16_t,  i16)
NVS_SET_IMPL(uint16_t, u16)
NVS_SET_IMPL(int32_t,  i32)
NVS_SET_IMPL(uint32_t, u32)
NVS_SET_IMPL(int64_t,  i64)
NVS_SET_IMPL(uint64_t, u64)

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    struct nvs_namespace *ns = get_ns(handle);
    if (!ns) return ESP_ERR_NVS_INVALID_HANDLE;
    return set_entry(ns, key, value, strlen(value) + 1);
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
                        const void *value, size_t length)
{
    struct nvs_namespace *ns = get_ns(handle);
    if (!ns) return ESP_ERR_NVS_INVALID_HANDLE;
    return set_entry(ns, key, value, length);
}

/* ---- Typed getters ---- */

#define NVS_GET_IMPL(type, suffix) \
esp_err_t nvs_get_##suffix(nvs_handle_t handle, const char *key, type *out_value) { \
    struct nvs_namespace *ns = get_ns(handle); \
    if (!ns) return ESP_ERR_NVS_INVALID_HANDLE; \
    return get_entry(ns, key, out_value, sizeof(*out_value)); \
}

NVS_GET_IMPL(int8_t,   i8)
NVS_GET_IMPL(uint8_t,  u8)
NVS_GET_IMPL(int16_t,  i16)
NVS_GET_IMPL(uint16_t, u16)
NVS_GET_IMPL(int32_t,  i32)
NVS_GET_IMPL(uint32_t, u32)
NVS_GET_IMPL(int64_t,  i64)
NVS_GET_IMPL(uint64_t, u64)

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key,
                       char *out_value, size_t *length)
{
    struct nvs_namespace *ns = get_ns(handle);
    if (!ns) return ESP_ERR_NVS_INVALID_HANDLE;

    struct nvs_entry *e = find_entry(ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;

    /* If out_value is NULL, return required length */
    if (!out_value) {
        if (length) *length = e->size;
        return ESP_OK;
    }

    if (!length || *length < e->size)
        return ESP_ERR_NVS_INVALID_LENGTH;

    memcpy(out_value, e->data, e->size);
    *length = e->size;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key,
                        void *out_value, size_t *length)
{
    struct nvs_namespace *ns = get_ns(handle);
    if (!ns) return ESP_ERR_NVS_INVALID_HANDLE;

    struct nvs_entry *e = find_entry(ns, key);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;

    if (!out_value) {
        if (length) *length = e->size;
        return ESP_OK;
    }

    if (!length || *length < e->size)
        return ESP_ERR_NVS_INVALID_LENGTH;

    memcpy(out_value, e->data, e->size);
    *length = e->size;
    return ESP_OK;
}

/* ---- Erase ---- */

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    struct nvs_namespace *ns = get_ns(handle);
    if (!ns) return ESP_ERR_NVS_INVALID_HANDLE;
    if (ns->mode == NVS_READONLY) return ESP_FAIL;

    for (int i = 0; i < ns->count; i++) {
        if (strcmp(ns->entries[i].key, key) == 0) {
            free(ns->entries[i].data);
            ns->entries[i] = ns->entries[--ns->count];
            ns->dirty = 1;
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_erase_all(nvs_handle_t handle)
{
    struct nvs_namespace *ns = get_ns(handle);
    if (!ns) return ESP_ERR_NVS_INVALID_HANDLE;
    if (ns->mode == NVS_READONLY) return ESP_FAIL;

    for (int i = 0; i < ns->count; i++)
        free(ns->entries[i].data);
    ns->count = 0;
    ns->dirty = 1;
    return ESP_OK;
}
