/*
 * emu_json.c — Save/load emulator state (JSON config + SD card image)
 *
 * Writer: fprintf-based, straightforward.
 * Reader: Minimal token-based parser for our known schema.
 * No external JSON library needed.
 */

#include "emu_json.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "emu_json";

/* ---- Writer ---- */

int emu_json_save_state(const char *base_path, const struct emu_state *state,
                        const char *sdcard_img_path)
{
    /* Write JSON config */
    char json_path[512];
    snprintf(json_path, sizeof(json_path), "%s.json", base_path);

    FILE *f = fopen(json_path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Cannot create %s", json_path);
        return -1;
    }

    /* Timestamp */
    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", t);

    const struct board_profile *b = state->board;

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"timestamp\": \"%s\",\n", ts);
    fprintf(f, "  \"board\": {\n");
    fprintf(f, "    \"model\": \"%s\",\n", b->model);
    fprintf(f, "    \"chip_name\": \"%s\",\n", b->chip_name);
    fprintf(f, "    \"chip_model\": %d,\n", b->chip_model);
    fprintf(f, "    \"cores\": %d,\n", b->cores);
    fprintf(f, "    \"display_size\": \"%s\",\n", b->display_size);
    fprintf(f, "    \"display_width\": %d,\n", b->display_width);
    fprintf(f, "    \"display_height\": %d,\n", b->display_height);
    fprintf(f, "    \"touch_type\": \"%s\",\n", b->touch_type);
    fprintf(f, "    \"sd_slots\": %d,\n", b->sd_slots);
    fprintf(f, "    \"usb_otg\": %d,\n", b->usb_otg);
    fprintf(f, "    \"usb_type\": \"%s\"\n", b->usb_type);
    fprintf(f, "  },\n");
    fprintf(f, "  \"emulation\": {\n");
    fprintf(f, "    \"scale\": %d,\n", state->scale);
    fprintf(f, "    \"turbo\": %s,\n", state->turbo ? "true" : "false");
    fprintf(f, "    \"payload_path\": \"%s\",\n", state->payload_path ? state->payload_path : "");
    fprintf(f, "    \"sdcard_size_bytes\": %llu\n", (unsigned long long)state->sdcard_size_bytes);
    fprintf(f, "  }\n");
    fprintf(f, "}\n");
    fclose(f);

    ESP_LOGI(TAG, "Saved config: %s", json_path);

    /* Copy SD card image (sparse-preserving) */
    char img_path[512];
    snprintf(img_path, sizeof(img_path), "%s.img", base_path);

    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "cp --sparse=always '%s' '%s'",
             sdcard_img_path, img_path);

    ESP_LOGI(TAG, "Copying SD image...");
    int ret = system(cmd);
    if (ret != 0) {
        ESP_LOGE(TAG, "SD image copy failed (exit %d)", ret);
        return -1;
    }
    ESP_LOGI(TAG, "Saved SD image: %s", img_path);

    return 0;
}

/* ---- Reader (minimal JSON parser) ---- */

/* Skip whitespace */
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Read a quoted string into buf, return pointer past closing quote */
static const char *read_string(const char *p, char *buf, int bufsize)
{
    if (*p != '"') return NULL;
    p++;
    int i = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) {
            p++; /* skip escape, take next char literally */
        }
        if (i < bufsize - 1) buf[i++] = *p;
        p++;
    }
    buf[i] = '\0';
    if (*p == '"') p++;
    return p;
}

/* Read a number (integer), return pointer past it */
static const char *read_number(const char *p, long long *val)
{
    char *end;
    *val = strtoll(p, &end, 10);
    return end;
}

/* Read a boolean */
static const char *read_bool(const char *p, int *val)
{
    if (strncmp(p, "true", 4) == 0) { *val = 1; return p + 4; }
    if (strncmp(p, "false", 5) == 0) { *val = 0; return p + 5; }
    return NULL;
}

/* Static buffers for loaded string values */
static char s_model[32];
static char s_chip_name[32];
static char s_display_size[16];
static char s_touch_type[64];
static char s_usb_type[64];
static char s_payload_path[512];

int emu_json_load_state(const char *json_path, struct emu_state *state,
                        struct board_profile *board_out)
{
    FILE *f = fopen(json_path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s", json_path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len > 64 * 1024) {
        ESP_LOGE(TAG, "Invalid JSON file size");
        fclose(f);
        return -1;
    }

    char *data = malloc((size_t)len + 1);
    if (!data) { fclose(f); return -1; }
    fread(data, 1, (size_t)len, f);
    data[len] = '\0';
    fclose(f);

    /* Zero out board */
    memset(board_out, 0, sizeof(*board_out));

    /* Parse key-value pairs (simple flat scan) */
    const char *p = data;
    char key[64], strval[512];
    long long numval;
    int boolval;

    while (*p) {
        p = skip_ws(p);
        if (*p == '"') {
            /* Read key */
            p = read_string(p, key, sizeof(key));
            if (!p) break;
            p = skip_ws(p);
            if (*p == ':') p++;
            p = skip_ws(p);

            /* Read value based on what we find */
            if (*p == '"') {
                p = read_string(p, strval, sizeof(strval));
                if (!p) break;

                if (strcmp(key, "model") == 0) {
                    strncpy(s_model, strval, sizeof(s_model) - 1);
                    board_out->model = s_model;
                } else if (strcmp(key, "chip_name") == 0) {
                    strncpy(s_chip_name, strval, sizeof(s_chip_name) - 1);
                    board_out->chip_name = s_chip_name;
                } else if (strcmp(key, "display_size") == 0) {
                    strncpy(s_display_size, strval, sizeof(s_display_size) - 1);
                    board_out->display_size = s_display_size;
                } else if (strcmp(key, "touch_type") == 0) {
                    strncpy(s_touch_type, strval, sizeof(s_touch_type) - 1);
                    board_out->touch_type = s_touch_type;
                } else if (strcmp(key, "usb_type") == 0) {
                    strncpy(s_usb_type, strval, sizeof(s_usb_type) - 1);
                    board_out->usb_type = s_usb_type;
                } else if (strcmp(key, "payload_path") == 0) {
                    strncpy(s_payload_path, strval, sizeof(s_payload_path) - 1);
                    state->payload_path = s_payload_path;
                }
            } else if (*p == 't' || *p == 'f') {
                p = read_bool(p, &boolval);
                if (!p) break;
                if (strcmp(key, "turbo") == 0) state->turbo = boolval;
            } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
                p = read_number(p, &numval);
                if (strcmp(key, "chip_model") == 0) board_out->chip_model = (int)numval;
                else if (strcmp(key, "cores") == 0) board_out->cores = (int)numval;
                else if (strcmp(key, "display_width") == 0) board_out->display_width = (int)numval;
                else if (strcmp(key, "display_height") == 0) board_out->display_height = (int)numval;
                else if (strcmp(key, "sd_slots") == 0) board_out->sd_slots = (int)numval;
                else if (strcmp(key, "usb_otg") == 0) board_out->usb_otg = (int)numval;
                else if (strcmp(key, "scale") == 0) state->scale = (int)numval;
                else if (strcmp(key, "sdcard_size_bytes") == 0) state->sdcard_size_bytes = (uint64_t)numval;
            } else {
                p++; /* skip unknown value types (objects, arrays) */
            }
        } else {
            p++;
        }
    }

    free(data);

    state->board = board_out;
    ESP_LOGI(TAG, "Loaded state: board=%s, scale=%d, turbo=%d",
             board_out->model ? board_out->model : "?", state->scale, state->turbo);
    return 0;
}
