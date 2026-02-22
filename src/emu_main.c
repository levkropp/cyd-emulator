/*
 * emu_main.c — Entry point for CYD emulator
 *
 * Thread architecture:
 *   Main thread: SDL init, window creation, event loop (60 FPS)
 *   App thread:  runs app_main() from the ESP32 firmware
 *
 * The display framebuffer and touch state are shared via mutexes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <SDL2/SDL.h>

#include "display.h"

/* From emu_display.c */
extern uint16_t emu_framebuf[DISPLAY_WIDTH * DISPLAY_HEIGHT];
extern pthread_mutex_t emu_framebuf_mutex;

/* From emu_touch.c */
extern void emu_touch_update(int down, int x, int y);
extern volatile int emu_app_running;

/* From emu_sdcard.c */
extern const char *emu_sdcard_path;
extern uint64_t emu_sdcard_size_bytes;

/* From emu_payload.c */
extern const char *emu_payload_path;

/* From main.c (app firmware) */
extern void app_main(void);

static int scale = 2;

static void *app_thread_func(void *arg)
{
    (void)arg;
    app_main();
    emu_app_running = 0;
    return NULL;
}

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
        "  --payload <file>      Path to payload.bin (required)\n"
        "  --sdcard <file>       Path to SD card image (default: sd.img)\n"
        "  --sdcard-size <size>  SD card size, e.g. 4G (default: 4G)\n"
        "  --scale <n>           Window scale factor (default: 2)\n",
        prog);
}

int main(int argc, char *argv[])
{
    emu_sdcard_path = "sd.img";
    emu_app_running = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--payload") == 0 && i + 1 < argc) {
            emu_payload_path = argv[++i];
        } else if (strcmp(argv[i], "--sdcard") == 0 && i + 1 < argc) {
            emu_sdcard_path = argv[++i];
        } else if (strcmp(argv[i], "--sdcard-size") == 0 && i + 1 < argc) {
            emu_sdcard_size_bytes = parse_size(argv[++i]);
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
            if (scale < 1) scale = 1;
            if (scale > 8) scale = 8;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!emu_payload_path) {
        fprintf(stderr, "Error: --payload is required\n");
        usage(argv[0]);
        return 1;
    }

    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int win_w = DISPLAY_WIDTH * scale;
    int win_h = DISPLAY_HEIGHT * scale;

    SDL_Window *window = SDL_CreateWindow(
        "CYD Emulator",
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
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Start the app thread */
    pthread_t app_thread;
    if (pthread_create(&app_thread, NULL, app_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create app thread\n");
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Pixel conversion buffer */
    uint32_t pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];

    /* Main event loop */
    while (emu_app_running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                emu_app_running = 0;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    emu_touch_update(1, ev.button.x / scale, ev.button.y / scale);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    emu_touch_update(0, ev.button.x / scale, ev.button.y / scale);
                }
                break;
            case SDL_MOUSEMOTION:
                if (ev.motion.state & SDL_BUTTON_LMASK) {
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
            pixels[i] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        pthread_mutex_unlock(&emu_framebuf_mutex);

        SDL_UpdateTexture(texture, NULL, pixels, DISPLAY_WIDTH * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        SDL_Delay(16);
    }

    /* Signal shutdown and give app thread time to notice */
    emu_app_running = 0;

    /* Wait briefly for app thread, then detach if stuck */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;
    if (pthread_timedjoin_np(app_thread, NULL, &ts) != 0) {
        pthread_cancel(app_thread);
        pthread_join(app_thread, NULL);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("Emulator exited.\n");
    return 0;
}
