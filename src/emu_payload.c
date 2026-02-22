/*
 * emu_payload.c — File-backed mmap payload (implements payload.h)
 *
 * Reads a payload.bin file (same format as the ESP32 flash partition)
 * via mmap, then parses the SURV manifest identically to payload.c.
 */

#include "payload.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static const char *TAG = "emu_payload";

/* Set by emu_main before payload_init is called */
const char *emu_payload_path = NULL;

/* On-disk structures (must match pack_payload.py and payload.c) */
#pragma pack(1)
struct payload_header {
    uint8_t  magic[4];
    uint8_t  version;
    uint8_t  arch_count;
    uint16_t reserved;
};

struct payload_arch_entry {
    char     name[16];
    uint32_t offset;
    uint32_t file_count;
};

struct payload_file_entry {
    char     path[128];
    uint32_t compressed_size;
    uint32_t original_size;
};
#pragma pack()

static const uint8_t *payload_base = NULL;
static size_t payload_size = 0;
static int s_arch_count = 0;
static struct payload_arch s_arches[PAYLOAD_MAX_ARCHES];

int payload_init(void)
{
    if (!emu_payload_path) {
        ESP_LOGE(TAG, "No payload path set (use --payload)");
        return -1;
    }

    int fd = open(emu_payload_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Cannot open %s", emu_payload_path);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) {
        ESP_LOGE(TAG, "Cannot stat %s", emu_payload_path);
        close(fd);
        return -1;
    }

    payload_size = (size_t)st.st_size;
    payload_base = (const uint8_t *)mmap(NULL, payload_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (payload_base == MAP_FAILED) {
        ESP_LOGE(TAG, "mmap failed for %s", emu_payload_path);
        payload_base = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "Payload: %s (%zu bytes)", emu_payload_path, payload_size);

    /* Parse header — identical logic to payload.c */
    if (payload_size < sizeof(struct payload_header)) return -1;
    const struct payload_header *hdr = (const struct payload_header *)payload_base;

    if (memcmp(hdr->magic, "SURV", 4) != 0) {
        ESP_LOGE(TAG, "Bad magic: %02x%02x%02x%02x",
                 hdr->magic[0], hdr->magic[1], hdr->magic[2], hdr->magic[3]);
        return -1;
    }
    if (hdr->version != 1) {
        ESP_LOGE(TAG, "Unknown version: %d", hdr->version);
        return -1;
    }

    s_arch_count = hdr->arch_count;
    if (s_arch_count > PAYLOAD_MAX_ARCHES)
        s_arch_count = PAYLOAD_MAX_ARCHES;

    ESP_LOGI(TAG, "Payload: version %d, %d architectures", hdr->version, s_arch_count);

    const struct payload_arch_entry *arch_table =
        (const struct payload_arch_entry *)(payload_base + sizeof(struct payload_header));

    for (int a = 0; a < s_arch_count; a++) {
        memcpy(s_arches[a].name, arch_table[a].name, 16);
        s_arches[a].name[15] = '\0';
        s_arches[a].file_count = (int)arch_table[a].file_count;
        if (s_arches[a].file_count > PAYLOAD_MAX_FILES)
            s_arches[a].file_count = PAYLOAD_MAX_FILES;

        const uint8_t *arch_data = payload_base + arch_table[a].offset;
        const struct payload_file_entry *file_entries =
            (const struct payload_file_entry *)arch_data;

        uint32_t data_offset = (uint32_t)(s_arches[a].file_count * sizeof(struct payload_file_entry));

        for (int f = 0; f < s_arches[a].file_count; f++) {
            memcpy(s_arches[a].files[f].path, file_entries[f].path, 128);
            s_arches[a].files[f].path[127] = '\0';
            s_arches[a].files[f].compressed_size = file_entries[f].compressed_size;
            s_arches[a].files[f].original_size = file_entries[f].original_size;
            s_arches[a].files[f].data_offset = data_offset;

            uint32_t stored = file_entries[f].compressed_size > 0
                            ? file_entries[f].compressed_size
                            : file_entries[f].original_size;
            data_offset += stored;
        }

        s_arches[a].data_start = arch_table[a].offset +
            (uint32_t)(s_arches[a].file_count * sizeof(struct payload_file_entry));

        ESP_LOGI(TAG, "  %s: %d files", s_arches[a].name, s_arches[a].file_count);
    }

    return 0;
}

int payload_arch_count(void)
{
    return s_arch_count;
}

const struct payload_arch *payload_get_arch(int index)
{
    if (index < 0 || index >= s_arch_count) return NULL;
    return &s_arches[index];
}

const struct payload_arch *payload_get_arch_by_name(const char *name)
{
    for (int i = 0; i < s_arch_count; i++) {
        if (strcmp(s_arches[i].name, name) == 0)
            return &s_arches[i];
    }
    return NULL;
}

const uint8_t *payload_file_data(const struct payload_arch *arch,
                                  const struct payload_file *file)
{
    if (!payload_base || !arch || !file) return NULL;
    return payload_base + (arch->data_start - (uint32_t)(arch->file_count * sizeof(struct payload_file_entry)))
         + file->data_offset;
}
