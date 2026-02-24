/*
 * emu_control.h — Unix domain socket control interface
 *
 * Provides a scriptable control interface for automated interaction
 * with the running emulator (screenshots, touch injection, status).
 * Polled from the SDL main loop — no extra threads.
 */

#ifndef EMU_CONTROL_H
#define EMU_CONTROL_H

/* Initialize the control socket server. Returns 0 on success, -1 on error. */
int  emu_control_init(const char *socket_path);

/* Poll for incoming commands (non-blocking). Call once per frame. */
void emu_control_poll(void);

/* Shut down the control socket and clean up. */
void emu_control_shutdown(void);

#endif /* EMU_CONTROL_H */
