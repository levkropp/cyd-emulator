/*
 * emu_json.h — Save/load emulator state as JSON + SD card image
 */
#ifndef EMU_JSON_H
#define EMU_JSON_H

#include <stdint.h>
#include "emu_board.h"

/* State passed to save/load */
struct emu_state {
    const struct board_profile *board;
    int scale;
    int turbo;
    const char *payload_path;
    uint64_t sdcard_size_bytes;
};

/*
 * Save state: writes <base>.json with config, copies SD image to <base>.img.
 * Returns 0 on success, -1 on error.
 */
int emu_json_save_state(const char *base_path, const struct emu_state *state,
                        const char *sdcard_img_path);

/*
 * Load state: reads <base>.json, fills out state struct.
 * Caller must apply the state and switch SD image to <base>.img.
 * Returns 0 on success, -1 on error.
 * Strings in *state point to static buffers valid until next call.
 */
int emu_json_load_state(const char *json_path, struct emu_state *state,
                        struct board_profile *board_out);

#endif /* EMU_JSON_H */
