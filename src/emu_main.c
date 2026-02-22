/*
 * emu_main.c — Entry point for CYD emulator
 *
 * Thread architecture:
 *   Main thread: SDL init, window creation, event loop (60 FPS), panel rendering
 *   App thread:  runs app_main() from the ESP32 firmware
 *
 * The display framebuffer and touch state are shared via mutexes.
 * A console info panel is rendered to the right of the display.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "display.h"
#include "font.h"
#include "emu_board.h"

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

/* ---- Panel constants ---- */
#define PANEL_CHARS  40
#define PANEL_WIDTH  (PANEL_CHARS * FONT_WIDTH)  /* 320px */
#define PANEL_BG     0xFF1A1A2E   /* dark blue-gray */
#define PANEL_FG     0xFFCCCCCC   /* light gray */
#define PANEL_HEAD   0xFF00CCAA   /* teal for headers */
#define PANEL_DIM    0xFF666666   /* dim gray */
#define PANEL_GREEN  0xFF00CC00
#define PANEL_RED    0xFFCC4444
#define PANEL_YELLOW 0xFFCCCC00

static int scale = 2;

/* ---- Panel rendering ---- */

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
    /* Pad to full width */
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
    /* Clear panel */
    for (int i = 0; i < pw * ph; i++)
        buf[i] = PANEL_BG;

    const struct board_profile *b = emu_active_board;
    int row = 0;

    /* Board section */
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

    /* Touch section */
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

    /* Log section */
    panel_line(buf, pw, ph, row++, PANEL_HEAD, " Log");
    panel_separator(buf, pw, ph, row++);

    /* Show as many log lines as fit */
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

/* ---- App thread ---- */

static void *app_thread_func(void *arg)
{
    (void)arg;
    app_main();
    emu_app_running = 0;
    return NULL;
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
        "Required:\n"
        "  --payload <file>        Path to payload.bin\n"
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
        "  --scale <n>             Display scale factor 1-8 (default: 2)\n"
        "\n"
        "Controls:\n"
        "  Click          Tap on touchscreen\n"
        "  Q / Ctrl+C     Quit\n",
        prog);
}

/* Signal handler for clean shutdown on SIGTERM/SIGINT */
static void signal_handler(int sig)
{
    (void)sig;
    emu_app_running = 0;
}

int main(int argc, char *argv[])
{
    /* Handle SIGTERM/SIGINT for clean shutdown (e.g. from timeout, Ctrl+C) */
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    emu_sdcard_path = "sd.img";
    emu_active_board = &board_profiles[BOARD_DEFAULT_INDEX];

    /* Override storage */
    const char *board_name = NULL;
    const char *chip_override = NULL;
    const char *touch_override = NULL;
    int sdcard_slots_override = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
            emu_payload_path = argv[++i];
        } else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) {
            board_name = argv[++i];
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
            if (scale > 8) scale = 8;
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

    /* We need a mutable copy for overrides */
    static struct board_profile active;
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

    if (sdcard_slots_override >= 0) {
        active.sd_slots = sdcard_slots_override;
    }

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

    /* Set chip info globals for esp_chip_info.h shim */
    emu_chip_model = active.chip_model;
    emu_chip_cores = active.cores;

    /* Set SD card enabled from board profile */
    emu_sdcard_enabled = (active.sd_slots > 0) ? 1 : 0;

    if (!emu_payload_path) {
        fprintf(stderr, "Error: --payload is required\n");
        usage(argv[0]);
        return 1;
    }

    /* Print startup banner */
    printf("\n");
    printf("  CYD Emulator\n");
    printf("  Board:   %s\n", active.model);
    printf("  Chip:    %s (%d cores)\n", active.chip_name, active.cores);
    printf("  Display: %s %dx%d\n", active.display_size, active.display_width, active.display_height);
    printf("  Touch:   %s\n", active.touch_type);
    printf("  SD:      %d slot(s)\n", active.sd_slots);
    printf("  USB:     %s\n", active.usb_type);
    printf("  Payload: %s\n", emu_payload_path);
    printf("\n");

    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int disp_w = DISPLAY_WIDTH * scale;
    int disp_h = DISPLAY_HEIGHT * scale;
    int win_w = disp_w + PANEL_WIDTH;
    int win_h = disp_h;

    char title[128];
    snprintf(title, sizeof(title), "CYD Emulator - %s", active.model);

    SDL_Window *window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h,
        SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Display texture (left side) */
    SDL_Texture *disp_tex = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        DISPLAY_WIDTH, DISPLAY_HEIGHT);

    /* Panel texture (right side) */
    SDL_Texture *panel_tex = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        PANEL_WIDTH, disp_h);

    if (!disp_tex || !panel_tex) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Start the app thread */
    emu_app_running = 1;
    pthread_t app_thread;
    if (pthread_create(&app_thread, NULL, app_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create app thread\n");
        SDL_Quit();
        return 1;
    }

    /* Pixel buffers */
    uint32_t disp_pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    uint32_t *panel_pixels = calloc((size_t)PANEL_WIDTH * (size_t)disp_h, sizeof(uint32_t));
    if (!panel_pixels) {
        fprintf(stderr, "Failed to allocate panel buffer\n");
        SDL_Quit();
        return 1;
    }

    /* Main event loop */
    while (emu_app_running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                emu_app_running = 0;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT && ev.button.x < disp_w) {
                    emu_touch_update(1, ev.button.x / scale, ev.button.y / scale);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    emu_touch_update(0, ev.button.x / scale, ev.button.y / scale);
                }
                break;
            case SDL_MOUSEMOTION:
                if ((ev.motion.state & SDL_BUTTON_LMASK) && ev.motion.x < disp_w) {
                    emu_touch_update(1, ev.motion.x / scale, ev.motion.y / scale);
                }
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_q ||
                    (ev.key.keysym.sym == SDLK_c && (ev.key.keysym.mod & KMOD_CTRL))) {
                    emu_app_running = 0;
                }
                break;
            }
        }

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

        /* Render info panel */
        render_panel(panel_pixels, PANEL_WIDTH, disp_h);

        /* Update textures */
        SDL_UpdateTexture(disp_tex, NULL, disp_pixels, DISPLAY_WIDTH * sizeof(uint32_t));
        SDL_UpdateTexture(panel_tex, NULL, panel_pixels, PANEL_WIDTH * sizeof(uint32_t));

        /* Draw: display on left, panel on right */
        SDL_RenderClear(renderer);

        SDL_Rect disp_rect = { 0, 0, disp_w, disp_h };
        SDL_RenderCopy(renderer, disp_tex, NULL, &disp_rect);

        SDL_Rect panel_rect = { disp_w, 0, PANEL_WIDTH, disp_h };
        SDL_RenderCopy(renderer, panel_tex, NULL, &panel_rect);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    /* Clean shutdown — app thread exits via pthread_exit in vTaskDelay/touch_wait_tap */
    emu_app_running = 0;
    pthread_join(app_thread, NULL);

    free(panel_pixels);
    SDL_DestroyTexture(disp_tex);
    SDL_DestroyTexture(panel_tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("Emulator exited.\n");
    return 0;
}
