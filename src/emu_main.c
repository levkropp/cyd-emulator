/*
 * emu_main.c — Entry point for CYD emulator
 *
 * Thread architecture:
 *   Main thread: SDL init, window creation, event loop (60 FPS), panel rendering
 *   App thread:  runs app_main() from the ESP32 firmware
 *
 * The display framebuffer and touch state are shared via mutexes.
 * A custom-rendered menu bar provides File/View/Help menus.
 * An info panel is rendered to the right of the display.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>

#include <SDL2/SDL.h>

#include "display.h"
#include "font.h"
#include "sdcard.h"
#include "payload.h"
#include "emu_board.h"
#include "emu_json.h"

/* ---- Globals from other emulator modules ---- */

/* From emu_display.c */
extern uint16_t emu_framebuf[DISPLAY_WIDTH * DISPLAY_HEIGHT];
extern pthread_mutex_t emu_framebuf_mutex;

/* From emu_touch.c */
extern void emu_touch_update(int down, int x, int y);
extern volatile int emu_app_running;
extern char emu_touch_log[][40];
extern int  emu_touch_log_head;
#define TOUCH_LOG_LINES 8

/* From emu_sdcard.c */
extern const char *emu_sdcard_path;
extern uint64_t emu_sdcard_size_bytes;
extern int emu_sdcard_enabled;
extern int emu_turbo_mode;

/* From emu_payload.c */
extern const char *emu_payload_path;

/* From esp_log.h (ring buffer) */
extern char emu_log_ring[][48];
extern int  emu_log_head;
#define EMU_LOG_LINES 64

/* From esp_chip_info.h */
int emu_chip_model = 1;  /* CHIP_ESP32 */
int emu_chip_cores = 2;

/* From main.c (app firmware) */
extern void app_main(void);

/* ---- Board profile ---- */
const struct board_profile *emu_active_board = NULL;

/* ---- Log ring buffer storage (declared in esp_log.h, defined here) ---- */
char emu_log_ring[EMU_LOG_LINES][48];
int  emu_log_head = 0;

/* ---- Layout constants ---- */
#define PANEL_CHARS      40
#define PANEL_WIDTH      (PANEL_CHARS * FONT_WIDTH)  /* 320px */
#define MENU_BAR_HEIGHT  24

/* ---- Colors (ARGB8888) ---- */
#define PANEL_BG     0xFF1A1A2E   /* dark blue-gray */
#define PANEL_FG     0xFFCCCCCC   /* light gray */
#define PANEL_HEAD   0xFF00CCAA   /* teal for headers */
#define PANEL_DIM    0xFF666666   /* dim gray */
#define PANEL_GREEN  0xFF00CC00
#define PANEL_RED    0xFFCC4444
#define PANEL_YELLOW 0xFFCCCC00

#define MENU_BG      0xFF2D2D44   /* slightly lighter than panel */
#define MENU_FG      0xFFCCCCCC
#define MENU_HI_BG   0xFF4444AA   /* highlighted item background */
#define MENU_HI_FG   0xFFFFFFFF   /* highlighted item text */
#define MENU_SEP_CLR 0xFF444466   /* separator line */
#define MENU_DIM     0xFF888888
#define TURBO_GREEN  0xFF00FF00
#define DROP_BG      0xFF252540   /* dropdown background */
#define DROP_BORDER  0xFF555577   /* dropdown border */

/* ---- File-scope SDL objects ---- */
static SDL_Window   *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture  *s_disp_tex;
static SDL_Texture  *s_panel_tex;
static SDL_Texture  *s_menu_tex;
static SDL_Texture  *s_drop_tex;

static int scale = 2;
static int disp_w, disp_h, win_w, win_h;

/* ---- Mutable board profile ---- */
static struct board_profile active;
static int board_explicit = 0;

/* ---- Path buffers for dynamically loaded paths ---- */
static char sdcard_path_buf[512];
static char payload_path_buf[512];

/* ---- App thread ---- */
static pthread_t app_thread;
static int app_thread_valid = 0;
static int emu_window_running = 1;  /* main event loop flag */

/* ---- Menu state ---- */
enum { MENU_CLOSED = 0, MENU_FILE, MENU_VIEW, MENU_HELP };
static int menu_open = MENU_CLOSED;
static int menu_hover = -1;

/* Menu header pixel positions */
#define MENU_HDR_H   MENU_BAR_HEIGHT
static const struct { const char *label; int x, w; } menu_hdrs[] = {
    { " File ", 0, 48 },
    { " View ", 48, 48 },
    { " Help ", 96, 48 },
};
#define MENU_HDR_COUNT 3

/* Dropdown dimensions */
#define DROP_CHARS    24
#define DROP_W        (DROP_CHARS * FONT_WIDTH)   /* 192px */
#define DROP_ITEM_H   FONT_HEIGHT                 /* 16px per item */

#define FILE_ITEMS    6
#define VIEW_ITEMS    5
#define HELP_ITEMS    2

/* ---- Pixel buffer helpers ---- */

static void fill_rect_buf(uint32_t *buf, int bw, int bh,
                          int rx, int ry, int rw, int rh, uint32_t color)
{
    for (int y = ry; y < ry + rh && y < bh; y++) {
        if (y < 0) continue;
        for (int x = rx; x < rx + rw && x < bw; x++) {
            if (x < 0) continue;
            buf[y * bw + x] = color;
        }
    }
}

/* Render a glyph at pixel position (px, py) — foreground only, no bg fill */
static void render_glyph(uint32_t *buf, int bw, int bh,
                         int px, int py, char c, uint32_t fg)
{
    if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
    const uint8_t *glyph = font_data[c - FONT_FIRST];
    for (int row = 0; row < FONT_HEIGHT; row++) {
        int y = py + row;
        if (y < 0 || y >= bh) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            int x = px + col;
            if (x < 0 || x >= bw) continue;
            if (bits & (0x80 >> col))
                buf[y * bw + x] = fg;
        }
    }
}

/* Render a string at pixel position — foreground only */
static void render_text(uint32_t *buf, int bw, int bh,
                        int px, int py, const char *s, uint32_t fg)
{
    while (*s) {
        render_glyph(buf, bw, bh, px, py, *s, fg);
        px += FONT_WIDTH;
        s++;
    }
}

/* ---- Panel rendering (character-grid based) ---- */

static void panel_char(uint32_t *buf, int pw, int ph,
                       int cx, int cy, char c, uint32_t fg, uint32_t bg)
{
    if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
    const uint8_t *glyph = font_data[c - FONT_FIRST];

    int px = cx * FONT_WIDTH;
    int py = cy * FONT_HEIGHT;

    for (int row = 0; row < FONT_HEIGHT; row++) {
        int y = py + row;
        if (y < 0 || y >= ph) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            int x = px + col;
            if (x < 0 || x >= pw) continue;
            buf[y * pw + x] = (bits & (0x80 >> col)) ? fg : bg;
        }
    }
}

static void panel_string(uint32_t *buf, int pw, int ph,
                         int cx, int cy, const char *s, uint32_t fg, uint32_t bg)
{
    while (*s) {
        if (cx >= PANEL_CHARS) break;
        panel_char(buf, pw, ph, cx, cy, *s, fg, bg);
        cx++;
        s++;
    }
}

static void panel_line(uint32_t *buf, int pw, int ph,
                       int cy, uint32_t fg, const char *fmt, ...)
{
    char line[PANEL_CHARS + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    int len = (int)strlen(line);
    while (len < PANEL_CHARS) line[len++] = ' ';
    line[PANEL_CHARS] = '\0';
    panel_string(buf, pw, ph, 0, cy, line, fg, PANEL_BG);
}

static void panel_separator(uint32_t *buf, int pw, int ph, int cy)
{
    char sep[PANEL_CHARS + 1];
    memset(sep, '-', PANEL_CHARS);
    sep[PANEL_CHARS] = '\0';
    panel_string(buf, pw, ph, 0, cy, sep, PANEL_DIM, PANEL_BG);
}

static void render_panel(uint32_t *buf, int pw, int ph)
{
    for (int i = 0; i < pw * ph; i++)
        buf[i] = PANEL_BG;

    const struct board_profile *b = emu_active_board;
    int row = 0;

    panel_line(buf, pw, ph, row++, PANEL_HEAD, " Board");
    panel_separator(buf, pw, ph, row++);
    panel_line(buf, pw, ph, row++, PANEL_FG, "  Model: %s", b->model);
    panel_line(buf, pw, ph, row++, PANEL_FG, "  Chip:  %s (%d cores)", b->chip_name, b->cores);
    panel_line(buf, pw, ph, row++, PANEL_FG, "  LCD:   %s %dx%d",
               b->display_size, b->display_width, b->display_height);
    panel_line(buf, pw, ph, row++, PANEL_FG, "  Touch: %s", b->touch_type);
    panel_line(buf, pw, ph, row++, PANEL_FG, "  SD:    %d slot%s",
               b->sd_slots, b->sd_slots != 1 ? "s" : "");
    panel_line(buf, pw, ph, row++, PANEL_FG, "  USB:   %s", b->usb_type);
    row++;

    panel_line(buf, pw, ph, row++, PANEL_HEAD, " Touch Events");
    panel_separator(buf, pw, ph, row++);
    for (int i = 0; i < TOUCH_LOG_LINES; i++) {
        int idx = (emu_touch_log_head - TOUCH_LOG_LINES + i + TOUCH_LOG_LINES) % TOUCH_LOG_LINES;
        if (emu_touch_log[idx][0]) {
            uint32_t color = PANEL_FG;
            if (strncmp(emu_touch_log[idx], "TAP", 3) == 0)
                color = PANEL_GREEN;
            else if (strncmp(emu_touch_log[idx], "DOWN", 4) == 0)
                color = PANEL_YELLOW;
            panel_line(buf, pw, ph, row, color, "  %s", emu_touch_log[idx]);
        }
        row++;
    }
    row++;

    panel_line(buf, pw, ph, row++, PANEL_HEAD, " Log");
    panel_separator(buf, pw, ph, row++);

    int max_log_rows = ph / FONT_HEIGHT - row;
    if (max_log_rows > EMU_LOG_LINES) max_log_rows = EMU_LOG_LINES;

    for (int i = 0; i < max_log_rows; i++) {
        int idx = (emu_log_head - max_log_rows + i + EMU_LOG_LINES) % EMU_LOG_LINES;
        if (emu_log_ring[idx][0]) {
            uint32_t color = PANEL_DIM;
            if (emu_log_ring[idx][1] == 'E') color = PANEL_RED;
            else if (emu_log_ring[idx][1] == 'W') color = PANEL_YELLOW;
            panel_line(buf, pw, ph, row, color, " %s", emu_log_ring[idx]);
        }
        row++;
    }
}

/* ---- Menu bar rendering ---- */

static void render_menu_bar(uint32_t *buf, int bw, int bh)
{
    /* Fill background */
    fill_rect_buf(buf, bw, bh, 0, 0, bw, bh, MENU_BG);

    /* Bottom border */
    fill_rect_buf(buf, bw, bh, 0, bh - 1, bw, 1, DROP_BORDER);

    int text_y = (bh - FONT_HEIGHT) / 2;  /* vertically center text */

    /* Render menu headers */
    for (int i = 0; i < MENU_HDR_COUNT; i++) {
        uint32_t bg = MENU_BG;
        uint32_t fg = MENU_FG;
        if (menu_open == i + 1) {
            bg = MENU_HI_BG;
            fg = MENU_HI_FG;
        }
        fill_rect_buf(buf, bw, bh, menu_hdrs[i].x, 0, menu_hdrs[i].w, bh - 1, bg);
        render_text(buf, bw, bh, menu_hdrs[i].x, text_y, menu_hdrs[i].label, fg);
    }

    /* Turbo/speed indicator on the right */
    if (emu_turbo_mode) {
        const char *ind = "[TURBO]";
        int iw = 7 * FONT_WIDTH;
        int ix = bw - iw - FONT_WIDTH;
        render_text(buf, bw, bh, ix, text_y, ind, TURBO_GREEN);
    } else {
        char ind[8];
        snprintf(ind, sizeof(ind), "[%dx]", scale);
        int iw = (int)strlen(ind) * FONT_WIDTH;
        int ix = bw - iw - FONT_WIDTH;
        render_text(buf, bw, bh, ix, text_y, ind, MENU_DIM);
    }
}

/* ---- Dropdown menu ---- */

static int dropdown_item_count(void)
{
    switch (menu_open) {
    case MENU_FILE: return FILE_ITEMS;
    case MENU_VIEW: return VIEW_ITEMS;
    case MENU_HELP: return HELP_ITEMS;
    default: return 0;
    }
}

static int dropdown_is_separator(int item)
{
    return (menu_open == MENU_FILE && item == 4);
}

static void dropdown_item_label(int item, char *buf, int bufsize)
{
    buf[0] = '\0';
    switch (menu_open) {
    case MENU_FILE:
        switch (item) {
        case 0: snprintf(buf, bufsize, " Load Payload..."); break;
        case 1: snprintf(buf, bufsize, " Attach SD Image..."); break;
        case 2: snprintf(buf, bufsize, " Save State..."); break;
        case 3: snprintf(buf, bufsize, " Load State..."); break;
        case 4: break; /* separator */
        case 5: snprintf(buf, bufsize, " Quit             Q"); break;
        }
        break;
    case MENU_VIEW:
        switch (item) {
        case 0:
            snprintf(buf, bufsize, " %s Turbo Mode   Tab",
                     emu_turbo_mode ? "[x]" : "[ ]");
            break;
        case 1: snprintf(buf, bufsize, " %s Scale 1x", scale == 1 ? ">" : " "); break;
        case 2: snprintf(buf, bufsize, " %s Scale 2x", scale == 2 ? ">" : " "); break;
        case 3: snprintf(buf, bufsize, " %s Scale 3x", scale == 3 ? ">" : " "); break;
        case 4: snprintf(buf, bufsize, " %s Scale 4x", scale == 4 ? ">" : " "); break;
        }
        break;
    case MENU_HELP:
        switch (item) {
        case 0: snprintf(buf, bufsize, " Controls"); break;
        case 1: snprintf(buf, bufsize, " About"); break;
        }
        break;
    }
}

/* Render dropdown into buffer. Returns total height in pixels. */
static int render_dropdown(uint32_t *buf, int bw, int bh)
{
    int count = dropdown_item_count();
    if (count == 0) return 0;

    int h = count * DROP_ITEM_H + 2;  /* +2 for top/bottom border */
    if (h > bh) h = bh;

    /* Fill background */
    fill_rect_buf(buf, bw, bh, 0, 0, bw, h, DROP_BG);

    /* Border */
    fill_rect_buf(buf, bw, bh, 0, 0, bw, 1, DROP_BORDER);        /* top */
    fill_rect_buf(buf, bw, bh, 0, h - 1, bw, 1, DROP_BORDER);    /* bottom */
    fill_rect_buf(buf, bw, bh, 0, 0, 1, h, DROP_BORDER);          /* left */
    fill_rect_buf(buf, bw, bh, bw - 1, 0, 1, h, DROP_BORDER);    /* right */

    for (int i = 0; i < count; i++) {
        int iy = 1 + i * DROP_ITEM_H;  /* +1 for top border */

        if (dropdown_is_separator(i)) {
            int sep_y = iy + DROP_ITEM_H / 2;
            fill_rect_buf(buf, bw, bh, 4, sep_y, bw - 8, 1, MENU_SEP_CLR);
            continue;
        }

        /* Highlight hovered item */
        uint32_t fg = MENU_FG;
        if (i == menu_hover) {
            fill_rect_buf(buf, bw, bh, 1, iy, bw - 2, DROP_ITEM_H, MENU_HI_BG);
            fg = MENU_HI_FG;
        }

        char label[DROP_CHARS + 1];
        dropdown_item_label(i, label, sizeof(label));
        render_text(buf, bw, bh, 0, iy, label, fg);
    }

    return h;
}

/* Get the dropdown's screen X position */
static int dropdown_screen_x(void)
{
    if (menu_open >= 1 && menu_open <= MENU_HDR_COUNT)
        return menu_hdrs[menu_open - 1].x;
    return 0;
}

/* ---- Zenity file dialogs ---- */

static int zenity_available(void)
{
    return access("/usr/bin/zenity", X_OK) == 0;
}

/* Returns malloc'd path or NULL. Caller must free(). */
static char *zenity_open(const char *title, const char *filter)
{
    if (!zenity_available()) {
        printf("zenity not available\n");
        return NULL;
    }

    char cmd[512];
    if (filter)
        snprintf(cmd, sizeof(cmd),
                 "zenity --file-selection --title='%s' --file-filter='%s' 2>/dev/null",
                 title, filter);
    else
        snprintf(cmd, sizeof(cmd),
                 "zenity --file-selection --title='%s' 2>/dev/null", title);

    FILE *f = popen(cmd, "r");
    if (!f) return NULL;

    char buf[1024];
    char *result = NULL;
    if (fgets(buf, sizeof(buf), f)) {
        /* Strip trailing newline */
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len > 0)
            result = strdup(buf);
    }
    pclose(f);
    return result;
}

/* Returns malloc'd path or NULL. Caller must free(). */
static char *zenity_save(const char *title, const char *filter)
{
    if (!zenity_available()) {
        printf("zenity not available\n");
        return NULL;
    }

    char cmd[512];
    if (filter)
        snprintf(cmd, sizeof(cmd),
                 "zenity --file-selection --save --confirm-overwrite "
                 "--title='%s' --file-filter='%s' 2>/dev/null",
                 title, filter);
    else
        snprintf(cmd, sizeof(cmd),
                 "zenity --file-selection --save --confirm-overwrite "
                 "--title='%s' 2>/dev/null", title);

    FILE *f = popen(cmd, "r");
    if (!f) return NULL;

    char buf[1024];
    char *result = NULL;
    if (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len > 0)
            result = strdup(buf);
    }
    pclose(f);
    return result;
}

/* ---- App thread management ---- */

static void *app_thread_func(void *arg)
{
    (void)arg;
    app_main();
    emu_app_running = 0;
    return NULL;
}

/* From emu_freertos.c */
extern void emu_freertos_shutdown(void);
/* From emu_timer.c */
extern void emu_esp_timer_shutdown(void);

static void stop_app_thread(void)
{
    if (!app_thread_valid) return;
    emu_app_running = 0;
    pthread_join(app_thread, NULL);
    app_thread_valid = 0;
    emu_freertos_shutdown();
    emu_esp_timer_shutdown();
}

static int start_app_thread(void)
{
    emu_app_running = 1;
    if (pthread_create(&app_thread, NULL, app_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create app thread\n");
        return -1;
    }
    app_thread_valid = 1;
    return 0;
}

/* ---- Board application helper ---- */

static void apply_board(const struct board_profile *b)
{
    memcpy(&active, b, sizeof(active));
    emu_active_board = &active;
    emu_chip_model = active.chip_model;
    emu_chip_cores = active.cores;
    emu_sdcard_enabled = (active.sd_slots > 0) ? 1 : 0;

    if (s_window) {
        char title[128];
        snprintf(title, sizeof(title), "CYD Emulator - %s", active.model);
        SDL_SetWindowTitle(s_window, title);
    }
}

/* ---- Board selection dialog ---- */

/* Show zenity radiolist of board profiles. Returns 0 on selection, -1 on cancel. */
static int show_board_dialog(void)
{
    if (!zenity_available()) {
        printf("zenity not available, using default board\n");
        return 0;
    }

    /* Build zenity command dynamically from board_profiles[] */
    char cmd[4096];
    int pos = snprintf(cmd, sizeof(cmd),
        "zenity --list --radiolist "
        "--title='Select CYD Board' "
        "--text='Which board is this payload for?' "
        "--column='' --column='Model' --column='Chip' --column='Display' --column='Touch' "
        "--width=600 --height=500 --print-column=2 ");

    for (int i = 0; i < BOARD_COUNT && pos < (int)sizeof(cmd) - 200; i++) {
        const struct board_profile *b = &board_profiles[i];
        char disp_info[32];
        snprintf(disp_info, sizeof(disp_info), "%s %dx%d",
                 b->display_size, b->display_width, b->display_height);

        pos += snprintf(cmd + pos, sizeof(cmd) - pos,
            "%s '%s' '%s' '%s' '%s' ",
            (strcasecmp(b->model, active.model) == 0) ? "TRUE" : "FALSE",
            b->model, b->chip_name, disp_info, b->touch_type);
    }
    /* Redirect stderr to /dev/null */
    snprintf(cmd + pos, sizeof(cmd) - pos, "2>/dev/null");

    FILE *f = popen(cmd, "r");
    if (!f) return -1;

    char buf[64];
    char *result = NULL;
    if (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len > 0)
            result = buf;
    }
    int status = pclose(f);

    if (!result || status != 0)
        return -1;

    const struct board_profile *b = board_find(result);
    if (!b) {
        fprintf(stderr, "Unknown board from dialog: %s\n", result);
        return -1;
    }

    apply_board(b);
    return 0;
}

/* ---- Window resize (scale change) ---- */

static uint32_t *panel_pixels = NULL;
static uint32_t *menu_pixels = NULL;

static void update_layout(void)
{
    disp_w = DISPLAY_WIDTH * scale;
    disp_h = DISPLAY_HEIGHT * scale;
    win_w = disp_w + PANEL_WIDTH;
    win_h = MENU_BAR_HEIGHT + disp_h;
}

static void resize_window(int new_scale)
{
    if (new_scale < 1) new_scale = 1;
    if (new_scale > 4) new_scale = 4;
    if (new_scale == scale) return;

    scale = new_scale;
    update_layout();

    SDL_SetWindowSize(s_window, win_w, win_h);

    /* Recreate panel texture (height changed) */
    if (s_panel_tex) SDL_DestroyTexture(s_panel_tex);
    s_panel_tex = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        PANEL_WIDTH, disp_h);

    /* Recreate menu bar texture (width changed) */
    if (s_menu_tex) SDL_DestroyTexture(s_menu_tex);
    s_menu_tex = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        win_w, MENU_BAR_HEIGHT);

    /* Realloc pixel buffers */
    free(panel_pixels);
    panel_pixels = calloc((size_t)PANEL_WIDTH * (size_t)disp_h, sizeof(uint32_t));

    free(menu_pixels);
    menu_pixels = calloc((size_t)win_w * MENU_BAR_HEIGHT, sizeof(uint32_t));
}

/* ---- Save/Load state orchestration ---- */

static void do_save_state(void)
{
    char *path = zenity_save("Save Emulator State", "*.json *.img");
    if (!path) return;

    /* Strip .json or .img extension if user added one */
    size_t len = strlen(path);
    if (len > 5 && strcmp(path + len - 5, ".json") == 0)
        path[len - 5] = '\0';
    else if (len > 4 && strcmp(path + len - 4, ".img") == 0)
        path[len - 4] = '\0';

    stop_app_thread();
    sdcard_deinit();  /* flushes and closes SD file */

    struct emu_state state = {
        .board = &active,
        .scale = scale,
        .turbo = emu_turbo_mode,
        .payload_path = emu_payload_path,
        .sdcard_size_bytes = emu_sdcard_size_bytes,
    };

    int ret = emu_json_save_state(path, &state, emu_sdcard_path);
    if (ret != 0)
        printf("Save state failed\n");

    free(path);

    /* Reopen SD card and restart app */
    sdcard_init();
    start_app_thread();
}

static void do_load_state(void)
{
    char *json_path = zenity_open("Load Emulator State", "State files | *.json");
    if (!json_path) return;

    struct board_profile loaded_board;
    struct emu_state state = { 0 };

    if (emu_json_load_state(json_path, &state, &loaded_board) != 0) {
        printf("Load state failed: %s\n", json_path);
        free(json_path);
        return;
    }

    stop_app_thread();
    sdcard_deinit();

    /* Apply board config */
    apply_board(&loaded_board);

    /* Apply emulation settings */
    emu_turbo_mode = state.turbo;

    /* Derive image path from JSON path */
    char img_path[512];
    snprintf(img_path, sizeof(img_path), "%s", json_path);
    size_t jlen = strlen(img_path);
    if (jlen > 5 && strcmp(img_path + jlen - 5, ".json") == 0)
        strcpy(img_path + jlen - 5, ".img");
    else
        snprintf(img_path + jlen, sizeof(img_path) - jlen, ".img");

    strncpy(sdcard_path_buf, img_path, sizeof(sdcard_path_buf) - 1);
    sdcard_path_buf[sizeof(sdcard_path_buf) - 1] = '\0';
    emu_sdcard_path = sdcard_path_buf;

    /* Apply payload path if provided */
    if (state.payload_path && state.payload_path[0]) {
        strncpy(payload_path_buf, state.payload_path, sizeof(payload_path_buf) - 1);
        payload_path_buf[sizeof(payload_path_buf) - 1] = '\0';
        emu_payload_path = payload_path_buf;
    }

    if (state.sdcard_size_bytes > 0)
        emu_sdcard_size_bytes = state.sdcard_size_bytes;

    /* Apply scale (resize window) */
    if (state.scale >= 1 && state.scale <= 4)
        resize_window(state.scale);

    free(json_path);

    /* Reinitialize and restart */
    payload_init();
    sdcard_init();
    start_app_thread();
}

static void do_attach_sd(void)
{
    char *path = zenity_open("Attach SD Card Image", "Disk images | *.img");
    if (!path) return;

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("Cannot stat %s\n", path);
        free(path);
        return;
    }

    stop_app_thread();
    sdcard_deinit();

    emu_sdcard_size_bytes = (uint64_t)st.st_size;
    strncpy(sdcard_path_buf, path, sizeof(sdcard_path_buf) - 1);
    sdcard_path_buf[sizeof(sdcard_path_buf) - 1] = '\0';
    emu_sdcard_path = sdcard_path_buf;
    free(path);

    sdcard_init();
    start_app_thread();
}

static void do_load_payload(void)
{
    char *path = zenity_open("Load Payload", "Payload files | *.bin");
    if (!path) return;

    stop_app_thread();
    sdcard_deinit();

    strncpy(payload_path_buf, path, sizeof(payload_path_buf) - 1);
    payload_path_buf[sizeof(payload_path_buf) - 1] = '\0';
    emu_payload_path = payload_path_buf;
    free(path);

    payload_init();

    /* Show board selection unless --board was explicit */
    if (!board_explicit) {
        if (show_board_dialog() < 0) {
            /* User cancelled — stay in "no payload" state */
            emu_payload_path = NULL;
            return;
        }
    }

    sdcard_init();
    start_app_thread();
}

/* ---- Dropdown action execution ---- */

static void dropdown_execute(int item)
{
    switch (menu_open) {
    case MENU_FILE:
        switch (item) {
        case 0: do_load_payload(); break;
        case 1: do_attach_sd(); break;
        case 2: do_save_state(); break;
        case 3: do_load_state(); break;
        case 5: emu_window_running = 0; emu_app_running = 0; break;  /* Quit */
        }
        break;
    case MENU_VIEW:
        switch (item) {
        case 0: emu_turbo_mode = !emu_turbo_mode; break;
        case 1: resize_window(1); break;
        case 2: resize_window(2); break;
        case 3: resize_window(3); break;
        case 4: resize_window(4); break;
        }
        break;
    case MENU_HELP:
        switch (item) {
        case 0:
            printf("\n  Controls:\n"
                   "  Click on display   Tap touchscreen\n"
                   "  Tab                Toggle turbo mode\n"
                   "  Q / Ctrl+C         Quit\n"
                   "  File menu          Load payload, save/load state\n"
                   "  View menu          Turbo mode, display scale\n\n");
            break;
        case 1:
            printf("\n  CYD Emulator v3\n"
                   "  ESP32 Cheap Yellow Display emulator\n"
                   "  SDL2 + custom rendering\n\n");
            break;
        }
        break;
    }
    menu_open = MENU_CLOSED;
    menu_hover = -1;
}

/* ---- Menu event handling ---- */

/* Returns which menu header (1-3) the x coordinate is over, or 0 */
static int menu_hdr_hit(int mx)
{
    for (int i = 0; i < MENU_HDR_COUNT; i++) {
        if (mx >= menu_hdrs[i].x && mx < menu_hdrs[i].x + menu_hdrs[i].w)
            return i + 1;
    }
    return 0;
}

/* Handle mouse click. Returns 1 if the menu consumed the event. */
static int menu_handle_click(int mx, int my)
{
    /* Click in menu bar area? */
    if (my < MENU_BAR_HEIGHT) {
        int hdr = menu_hdr_hit(mx);
        if (hdr) {
            if (menu_open == hdr)
                menu_open = MENU_CLOSED;
            else
                menu_open = hdr;
            menu_hover = -1;
            return 1;
        }
        /* Click on menu bar but not on a header — close menu */
        menu_open = MENU_CLOSED;
        menu_hover = -1;
        return 1;
    }

    /* Click in dropdown area? */
    if (menu_open != MENU_CLOSED) {
        int dx = dropdown_screen_x();
        int dy = MENU_BAR_HEIGHT;
        int dh = dropdown_item_count() * DROP_ITEM_H + 2;

        if (mx >= dx && mx < dx + DROP_W && my >= dy && my < dy + dh) {
            int item = (my - dy - 1) / DROP_ITEM_H;  /* -1 for top border */
            if (item >= 0 && item < dropdown_item_count() && !dropdown_is_separator(item)) {
                dropdown_execute(item);
            }
            return 1;
        }

        /* Click outside dropdown — close it */
        menu_open = MENU_CLOSED;
        menu_hover = -1;
        return 1;
    }

    return 0;
}

/* Update hover state based on mouse position */
static void menu_handle_motion(int mx, int my)
{
    if (menu_open == MENU_CLOSED) return;

    /* Check if mouse is over a different header */
    if (my < MENU_BAR_HEIGHT) {
        int hdr = menu_hdr_hit(mx);
        if (hdr && hdr != menu_open) {
            menu_open = hdr;
            menu_hover = -1;
        }
        return;
    }

    /* Check if mouse is in dropdown */
    int dx = dropdown_screen_x();
    int dy = MENU_BAR_HEIGHT;
    int dh = dropdown_item_count() * DROP_ITEM_H + 2;

    if (mx >= dx && mx < dx + DROP_W && my >= dy && my < dy + dh) {
        int item = (my - dy - 1) / DROP_ITEM_H;
        if (item >= 0 && item < dropdown_item_count() && !dropdown_is_separator(item))
            menu_hover = item;
        else
            menu_hover = -1;
    } else {
        menu_hover = -1;
    }
}

/* ---- Argument parsing ---- */

static uint64_t parse_size(const char *s)
{
    char *end;
    uint64_t val = strtoull(s, &end, 10);
    if (*end == 'G' || *end == 'g') val *= 1024ULL * 1024 * 1024;
    else if (*end == 'M' || *end == 'm') val *= 1024ULL * 1024;
    else if (*end == 'K' || *end == 'k') val *= 1024ULL;
    return val;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Payload:\n"
        "  --payload <file>        Path to payload.bin (or use File > Load Payload)\n"
        "\n"
        "Board selection:\n"
        "  --board <model>         Select a CYD board profile (default: 2432S028R)\n"
        "  --board list            Show all available board profiles\n"
        "\n"
        "Board overrides:\n"
        "  --chip esp32|esp32s3    Override chip model\n"
        "  --no-sdcard             Simulate no SD card slot\n"
        "  --sdcard-slots <n>      Number of SD card slots (0-2)\n"
        "  --touch resistive|capacitive|none  Override touch type label\n"
        "\n"
        "Emulation:\n"
        "  --sdcard <file>         SD card image path (default: sd.img)\n"
        "  --sdcard-size <size>    SD card size, e.g. 4G (default: 4G)\n"
        "  --scale <n>             Display scale factor 1-4 (default: 2)\n"
        "  --turbo                 Start in turbo mode (instant SD I/O)\n"
        "\n"
        "Controls:\n"
        "  Click on display   Tap touchscreen\n"
        "  Tab                Toggle turbo mode\n"
        "  Q / Ctrl+C         Quit\n",
        prog);
}

/* Signal handler for clean shutdown */
static void signal_handler(int sig)
{
    (void)sig;
    emu_window_running = 0;
    emu_app_running = 0;
}

/* ---- Main ---- */

int main(int argc, char *argv[])
{
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    emu_sdcard_path = "sd.img";
    emu_active_board = &board_profiles[BOARD_DEFAULT_INDEX];

    const char *board_name = NULL;
    const char *chip_override = NULL;
    const char *touch_override = NULL;
    int sdcard_slots_override = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
            emu_payload_path = argv[++i];
        } else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) {
            board_name = argv[++i];
            board_explicit = 1;
            if (strcmp(board_name, "list") == 0 || strcmp(board_name, "help") == 0) {
                board_list();
                return 0;
            }
        } else if (strcmp(argv[i], "--sdcard") == 0 && i + 1 < argc) {
            emu_sdcard_path = argv[++i];
        } else if (strcmp(argv[i], "--sdcard-size") == 0 && i + 1 < argc) {
            emu_sdcard_size_bytes = parse_size(argv[++i]);
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
            if (scale < 1) scale = 1;
            if (scale > 4) scale = 4;
        } else if (strcmp(argv[i], "--turbo") == 0) {
            emu_turbo_mode = 1;
        } else if (strcmp(argv[i], "--chip") == 0 && i + 1 < argc) {
            chip_override = argv[++i];
        } else if (strcmp(argv[i], "--no-sdcard") == 0) {
            sdcard_slots_override = 0;
        } else if (strcmp(argv[i], "--sdcard-slots") == 0 && i + 1 < argc) {
            sdcard_slots_override = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--touch") == 0 && i + 1 < argc) {
            touch_override = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* Resolve board profile */
    if (board_name) {
        const struct board_profile *b = board_find(board_name);
        if (!b) {
            fprintf(stderr, "Unknown board: %s\n", board_name);
            fprintf(stderr, "Use --board list to see available profiles.\n");
            return 1;
        }
        emu_active_board = b;
    }

    memcpy(&active, emu_active_board, sizeof(active));
    emu_active_board = &active;

    /* Apply overrides */
    if (chip_override) {
        if (strcasecmp(chip_override, "esp32s3") == 0 || strcasecmp(chip_override, "esp32-s3") == 0) {
            active.chip_model = BOARD_CHIP_ESP32S3;
            active.chip_name = "ESP32-S3";
            active.cores = 2;
            active.usb_otg = 1;
            active.usb_type = "USB-C (OTG)";
        } else if (strcasecmp(chip_override, "esp32") == 0) {
            active.chip_model = BOARD_CHIP_ESP32;
            active.chip_name = "ESP32";
            active.cores = 2;
            active.usb_otg = 0;
            active.usb_type = "Micro-USB (UART)";
        } else {
            fprintf(stderr, "Unknown chip: %s (use esp32 or esp32s3)\n", chip_override);
            return 1;
        }
    }

    if (sdcard_slots_override >= 0)
        active.sd_slots = sdcard_slots_override;

    if (touch_override) {
        if (strcasecmp(touch_override, "resistive") == 0)
            active.touch_type = "XPT2046 (resistive)";
        else if (strcasecmp(touch_override, "capacitive") == 0)
            active.touch_type = "GT911 (capacitive)";
        else if (strcasecmp(touch_override, "none") == 0)
            active.touch_type = "None";
        else {
            fprintf(stderr, "Unknown touch type: %s\n", touch_override);
            return 1;
        }
    }

    emu_chip_model = active.chip_model;
    emu_chip_cores = active.cores;
    emu_sdcard_enabled = (active.sd_slots > 0) ? 1 : 0;

    printf("\n");
    printf("  CYD Emulator\n");
    printf("  Board:   %s\n", active.model);
    printf("  Chip:    %s (%d cores)\n", active.chip_name, active.cores);
    printf("  Display: %s %dx%d\n", active.display_size, active.display_width, active.display_height);
    printf("  Touch:   %s\n", active.touch_type);
    printf("  SD:      %d slot(s)\n", active.sd_slots);
    printf("  USB:     %s\n", active.usb_type);
    printf("  Payload: %s\n", emu_payload_path ? emu_payload_path : "(none)");
    printf("  Speed:   %s\n", emu_turbo_mode ? "turbo" : "normal (hardware-accurate)");
    printf("\n");

    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    update_layout();

    char title[128];
    snprintf(title, sizeof(title), "CYD Emulator - %s", active.model);

    s_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        SDL_WINDOW_SHOWN);
    if (!s_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    s_renderer = SDL_CreateRenderer(s_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_renderer)
        s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
    if (!s_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return 1;
    }

    /* Display texture (always native resolution, SDL scales it) */
    s_disp_tex = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        DISPLAY_WIDTH, DISPLAY_HEIGHT);

    /* Panel texture */
    s_panel_tex = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        PANEL_WIDTH, disp_h);

    /* Menu bar texture */
    s_menu_tex = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        win_w, MENU_BAR_HEIGHT);

    /* Dropdown texture (fixed max size) */
    int drop_max_h = 8 * DROP_ITEM_H + 2;
    s_drop_tex = SDL_CreateTexture(s_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        DROP_W, drop_max_h);

    if (!s_disp_tex || !s_panel_tex || !s_menu_tex || !s_drop_tex) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(s_window);
        SDL_Quit();
        return 1;
    }

    /* Start the app thread */
    if (emu_payload_path) {
        if (!board_explicit) {
            /* Show board dialog; if cancelled, enter waiting state */
            if (show_board_dialog() < 0) {
                emu_payload_path = NULL;
            }
        }
        if (emu_payload_path) {
            if (start_app_thread() != 0) {
                SDL_Quit();
                return 1;
            }
        }
    }
#ifdef EMU_STANDALONE
    else {
        /* Standalone mode: start app thread immediately (no payload required) */
        if (start_app_thread() != 0) {
            SDL_Quit();
            return 1;
        }
    }
#endif

    /* Pixel buffers */
    uint32_t disp_pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    panel_pixels = calloc((size_t)PANEL_WIDTH * (size_t)disp_h, sizeof(uint32_t));
    menu_pixels = calloc((size_t)win_w * MENU_BAR_HEIGHT, sizeof(uint32_t));
    uint32_t *drop_pixels = calloc((size_t)DROP_W * (size_t)drop_max_h, sizeof(uint32_t));

    if (!panel_pixels || !menu_pixels || !drop_pixels) {
        fprintf(stderr, "Failed to allocate pixel buffers\n");
        SDL_Quit();
        return 1;
    }

    /* ---- Main event loop ---- */
    while (emu_window_running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                emu_window_running = 0;
                emu_app_running = 0;
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    int mx = ev.button.x;
                    int my = ev.button.y;

                    /* Let menu system handle it first */
                    if (menu_handle_click(mx, my))
                        break;

                    /* Pass to display touch if in display area */
                    if (mx < disp_w && my >= MENU_BAR_HEIGHT) {
                        int tx = mx / scale;
                        int ty = (my - MENU_BAR_HEIGHT) / scale;
                        emu_touch_update(1, tx, ty);
                    }
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    int mx = ev.button.x;
                    int my = ev.button.y;
                    if (mx < disp_w && my >= MENU_BAR_HEIGHT) {
                        int tx = mx / scale;
                        int ty = (my - MENU_BAR_HEIGHT) / scale;
                        emu_touch_update(0, tx, ty);
                    } else {
                        emu_touch_update(0, 0, 0);
                    }
                }
                break;

            case SDL_MOUSEMOTION:
                /* Update menu hover */
                menu_handle_motion(ev.motion.x, ev.motion.y);

                /* Pass drag to touch */
                if ((ev.motion.state & SDL_BUTTON_LMASK) &&
                    ev.motion.x < disp_w && ev.motion.y >= MENU_BAR_HEIGHT &&
                    menu_open == MENU_CLOSED)
                {
                    int tx = ev.motion.x / scale;
                    int ty = (ev.motion.y - MENU_BAR_HEIGHT) / scale;
                    emu_touch_update(1, tx, ty);
                }
                break;

            case SDL_KEYDOWN:
                if (menu_open != MENU_CLOSED && ev.key.keysym.sym == SDLK_ESCAPE) {
                    menu_open = MENU_CLOSED;
                    menu_hover = -1;
                    break;
                }
                if (ev.key.keysym.sym == SDLK_TAB) {
                    emu_turbo_mode = !emu_turbo_mode;
                } else if (ev.key.keysym.sym == SDLK_q && menu_open == MENU_CLOSED) {
                    emu_window_running = 0;
                    emu_app_running = 0;
                } else if (ev.key.keysym.sym == SDLK_c && (ev.key.keysym.mod & KMOD_CTRL)) {
                    emu_window_running = 0;
                    emu_app_running = 0;
                }
                break;
            }
        }

        /* ---- Render ---- */

        /* Convert RGB565 framebuffer to ARGB8888 */
        pthread_mutex_lock(&emu_framebuf_mutex);
        for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
            uint16_t c = emu_framebuf[i];
            uint8_t r = ((c >> 11) & 0x1F) << 3;
            uint8_t g = ((c >> 5) & 0x3F) << 2;
            uint8_t b = (c & 0x1F) << 3;
            disp_pixels[i] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        pthread_mutex_unlock(&emu_framebuf_mutex);

        /* "No payload" overlay when app thread isn't running */
        if (!app_thread_valid) {
            /* Fill display with dark background */
            for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
                disp_pixels[i] = 0xFF1A1A2E;

            const char *line1 = "No payload loaded.";
            const char *line2 = "File > Load Payload...";
            int len1 = (int)strlen(line1);
            int len2 = (int)strlen(line2);
            int x1 = (DISPLAY_WIDTH - len1 * FONT_WIDTH) / 2;
            int x2 = (DISPLAY_WIDTH - len2 * FONT_WIDTH) / 2;
            int y1 = DISPLAY_HEIGHT / 2 - FONT_HEIGHT;
            int y2 = DISPLAY_HEIGHT / 2 + FONT_HEIGHT / 2;
            render_text(disp_pixels, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                        x1, y1, line1, 0xFFCCCCCC);
            render_text(disp_pixels, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                        x2, y2, line2, 0xFF888888);
        }

        /* Render info panel */
        render_panel(panel_pixels, PANEL_WIDTH, disp_h);

        /* Render menu bar */
        render_menu_bar(menu_pixels, win_w, MENU_BAR_HEIGHT);

        /* Update textures */
        SDL_UpdateTexture(s_disp_tex, NULL, disp_pixels, DISPLAY_WIDTH * sizeof(uint32_t));
        SDL_UpdateTexture(s_panel_tex, NULL, panel_pixels, PANEL_WIDTH * sizeof(uint32_t));
        SDL_UpdateTexture(s_menu_tex, NULL, menu_pixels, win_w * sizeof(uint32_t));

        /* Draw */
        SDL_RenderClear(s_renderer);

        /* Menu bar at top */
        SDL_Rect menu_rect = { 0, 0, win_w, MENU_BAR_HEIGHT };
        SDL_RenderCopy(s_renderer, s_menu_tex, NULL, &menu_rect);

        /* Display below menu bar, on the left */
        SDL_Rect disp_rect = { 0, MENU_BAR_HEIGHT, disp_w, disp_h };
        SDL_RenderCopy(s_renderer, s_disp_tex, NULL, &disp_rect);

        /* Panel below menu bar, on the right */
        SDL_Rect panel_rect = { disp_w, MENU_BAR_HEIGHT, PANEL_WIDTH, disp_h };
        SDL_RenderCopy(s_renderer, s_panel_tex, NULL, &panel_rect);

        /* Dropdown overlay (if menu is open) */
        if (menu_open != MENU_CLOSED) {
            int dh = render_dropdown(drop_pixels, DROP_W, drop_max_h);
            if (dh > 0) {
                SDL_Rect src = { 0, 0, DROP_W, dh };
                SDL_UpdateTexture(s_drop_tex, &src, drop_pixels, DROP_W * sizeof(uint32_t));
                SDL_Rect dst = { dropdown_screen_x(), MENU_BAR_HEIGHT, DROP_W, dh };
                SDL_RenderCopy(s_renderer, s_drop_tex, &src, &dst);
            }
        }

        SDL_RenderPresent(s_renderer);
        SDL_Delay(16);
    }

    /* Clean shutdown */
    stop_app_thread();

    free(panel_pixels);
    free(menu_pixels);
    free(drop_pixels);
    SDL_DestroyTexture(s_disp_tex);
    SDL_DestroyTexture(s_panel_tex);
    SDL_DestroyTexture(s_menu_tex);
    SDL_DestroyTexture(s_drop_tex);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(s_window);
    SDL_Quit();

    printf("Emulator exited.\n");
    return 0;
}
