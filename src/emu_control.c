/*
 * emu_control.c — Unix domain socket control interface
 *
 * A connection-per-command server polled from the SDL main loop.
 * Client connects, sends one text line, gets a response, connection closes.
 *
 * Commands:
 *   tap <x> <y>         Press + 50ms + release
 *   touch_down <x> <y>  Press at coords
 *   touch_up             Release
 *   screenshot <path>   Save display as 24-bit BMP
 *   status              Emulator info
 *   log                 Recent UART output lines
 *   quit                Clean shutdown
 */

#include "emu_control.h"
#include "display.h"
#include "emu_flexe.h"
#include "emu_board.h"

#include "xtensa.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

#include <SDL2/SDL.h>

/* Externs from other emulator modules */
extern uint16_t emu_framebuf[DISPLAY_WIDTH * DISPLAY_HEIGHT];
extern pthread_mutex_t emu_framebuf_mutex;
extern void emu_touch_update(int down, int x, int y);
extern volatile int emu_app_running;
extern const struct board_profile *emu_active_board;

#define EMU_LOG_LINES 64
extern char emu_log_ring[][48];
extern int  emu_log_head;

static int listen_fd = -1;
static char sock_path[108]; /* match sun_path size */

/* ---- Socket setup ---- */

int emu_control_init(const char *socket_path)
{
    if (!socket_path || !socket_path[0])
        return -1;

    snprintf(sock_path, sizeof(sock_path), "%s", socket_path);

    /* Remove stale socket */
    unlink(sock_path);

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("control: socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("control: bind");
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }

    if (listen(listen_fd, 4) < 0) {
        perror("control: listen");
        close(listen_fd);
        unlink(sock_path);
        listen_fd = -1;
        return -1;
    }

    /* Non-blocking accept */
    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

    return 0;
}

/* ---- BMP writer ---- */

static int write_bmp(const char *path)
{
    int w = DISPLAY_WIDTH;
    int h = DISPLAY_HEIGHT;
    int row_stride = w * 3;
    int row_pad = (4 - (row_stride % 4)) % 4;
    int padded_row = row_stride + row_pad;
    uint32_t img_size = (uint32_t)(padded_row * h);
    uint32_t file_size = 54 + img_size;

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    /* 14-byte file header */
    uint8_t fhdr[14] = {0};
    fhdr[0] = 'B'; fhdr[1] = 'M';
    fhdr[2] = file_size & 0xFF;
    fhdr[3] = (file_size >> 8) & 0xFF;
    fhdr[4] = (file_size >> 16) & 0xFF;
    fhdr[5] = (file_size >> 24) & 0xFF;
    /* reserved = 0 */
    fhdr[10] = 54; /* pixel data offset */
    fwrite(fhdr, 1, 14, f);

    /* 40-byte DIB header (BITMAPINFOHEADER) */
    uint8_t dhdr[40] = {0};
    dhdr[0] = 40; /* header size */
    dhdr[4] = w & 0xFF; dhdr[5] = (w >> 8) & 0xFF;
    dhdr[8] = h & 0xFF; dhdr[9] = (h >> 8) & 0xFF;
    dhdr[12] = 1; /* planes */
    dhdr[14] = 24; /* bpp */
    /* compression = 0 (BI_RGB) */
    dhdr[20] = img_size & 0xFF;
    dhdr[21] = (img_size >> 8) & 0xFF;
    dhdr[22] = (img_size >> 16) & 0xFF;
    dhdr[23] = (img_size >> 24) & 0xFF;
    fwrite(dhdr, 1, 40, f);

    /* Pixel data: bottom-to-top rows, BGR order */
    uint8_t *row = malloc(padded_row);
    if (!row) { fclose(f); return -1; }
    memset(row, 0, padded_row);

    pthread_mutex_lock(&emu_framebuf_mutex);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint16_t c = emu_framebuf[y * w + x];
            row[x * 3 + 0] = (c & 0x1F) << 3;           /* B */
            row[x * 3 + 1] = ((c >> 5) & 0x3F) << 2;    /* G */
            row[x * 3 + 2] = ((c >> 11) & 0x1F) << 3;   /* R */
        }
        fwrite(row, 1, padded_row, f);
    }
    pthread_mutex_unlock(&emu_framebuf_mutex);

    free(row);
    fclose(f);
    return 0;
}

/* ---- Command handlers ---- */

static void send_str(int fd, const char *s)
{
    size_t len = strlen(s);
    while (len > 0) {
        ssize_t n = send(fd, s, len, MSG_NOSIGNAL);
        if (n <= 0) break;
        s += n;
        len -= n;
    }
}

static void handle_tap(int fd, const char *args)
{
    int x = 0, y = 0;
    if (sscanf(args, "%d %d", &x, &y) != 2) {
        send_str(fd, "ERR usage: tap <x> <y>\n");
        return;
    }
    emu_touch_update(1, x, y);
    usleep(50000);
    emu_touch_update(0, x, y);
    send_str(fd, "OK\n");
}

static void handle_touch_down(int fd, const char *args)
{
    int x = 0, y = 0;
    if (sscanf(args, "%d %d", &x, &y) != 2) {
        send_str(fd, "ERR usage: touch_down <x> <y>\n");
        return;
    }
    emu_touch_update(1, x, y);
    send_str(fd, "OK\n");
}

static void handle_touch_up(int fd)
{
    emu_touch_update(0, 0, 0);
    send_str(fd, "OK\n");
}

static void handle_screenshot(int fd, const char *args)
{
    /* Skip leading whitespace */
    while (*args == ' ') args++;
    if (!*args) {
        send_str(fd, "ERR usage: screenshot <path>\n");
        return;
    }

    /* Strip trailing whitespace/newline from path */
    char path[512];
    snprintf(path, sizeof(path), "%s", args);
    size_t len = strlen(path);
    while (len > 0 && (path[len-1] == ' ' || path[len-1] == '\n' || path[len-1] == '\r'))
        path[--len] = '\0';

    if (write_bmp(path) == 0) {
        char resp[600];
        snprintf(resp, sizeof(resp), "OK %s\n", path);
        send_str(fd, resp);
    } else {
        char resp[600];
        snprintf(resp, sizeof(resp), "ERR failed to write %s: %s\n", path, strerror(errno));
        send_str(fd, resp);
    }
}

static void handle_status(int fd)
{
    char resp[256];
    snprintf(resp, sizeof(resp), "OK board=%s display=%dx%d running=%d mode=flexe\n",
             emu_active_board ? emu_active_board->model : "unknown",
             DISPLAY_WIDTH, DISPLAY_HEIGHT,
             emu_app_running ? 1 : 0);
    send_str(fd, resp);
}

static void handle_log(int fd)
{
    for (int i = 0; i < EMU_LOG_LINES; i++) {
        int idx = (emu_log_head - EMU_LOG_LINES + i + EMU_LOG_LINES) % EMU_LOG_LINES;
        if (emu_log_ring[idx][0]) {
            char line[64];
            snprintf(line, sizeof(line), "LOG %s\n", emu_log_ring[idx]);
            send_str(fd, line);
        }
    }
    send_str(fd, "OK\n");
}

static void handle_quit(int fd)
{
    send_str(fd, "OK\n");
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_QUIT;
    SDL_PushEvent(&ev);
}

/* ---- Debug command handlers ---- */

static void handle_break(int fd, const char *args)
{
    if (!emu_flexe_active()) {
        send_str(fd, "ERR flexe not active\n");
        return;
    }
    uint32_t addr = (uint32_t)strtoul(args, NULL, 0);
    xtensa_cpu_t *cpu = emu_flexe_get_cpu();
    if (xtensa_set_breakpoint(cpu, addr) == 0) {
        char resp[64];
        snprintf(resp, sizeof(resp), "OK breakpoint set at 0x%08X\n", addr);
        send_str(fd, resp);
    } else {
        send_str(fd, "ERR too many breakpoints\n");
    }
}

static void handle_clearbreak(int fd, const char *args)
{
    if (!emu_flexe_active()) {
        send_str(fd, "ERR flexe not active\n");
        return;
    }
    xtensa_cpu_t *cpu = emu_flexe_get_cpu();
    while (*args == ' ') args++;
    if (strcmp(args, "all") == 0) {
        for (int i = 0; i < 16; i++)
            cpu->breakpoints[i] = 0;
        cpu->breakpoint_count = 0;
        send_str(fd, "OK all breakpoints cleared\n");
    } else {
        uint32_t addr = (uint32_t)strtoul(args, NULL, 0);
        xtensa_clear_breakpoint(cpu, addr);
        char resp[64];
        snprintf(resp, sizeof(resp), "OK cleared breakpoint at 0x%08X\n", addr);
        send_str(fd, resp);
    }
}

static void handle_pause(int fd)
{
    if (!emu_flexe_active()) {
        send_str(fd, "ERR flexe not active\n");
        return;
    }
    if (emu_flexe_debug_paused()) {
        xtensa_cpu_t *cpu = emu_flexe_get_cpu();
        char resp[64];
        snprintf(resp, sizeof(resp), "OK already paused at 0x%08X\n", cpu->pc);
        send_str(fd, resp);
        return;
    }
    emu_flexe_debug_break();
    if (emu_flexe_debug_wait_paused(2000)) {
        xtensa_cpu_t *cpu = emu_flexe_get_cpu();
        char resp[64];
        snprintf(resp, sizeof(resp), "OK paused at 0x%08X\n", cpu->pc);
        send_str(fd, resp);
    } else {
        send_str(fd, "ERR timeout waiting for pause\n");
    }
}

static void handle_continue(int fd)
{
    if (!emu_flexe_active()) {
        send_str(fd, "ERR flexe not active\n");
        return;
    }
    if (!emu_flexe_debug_paused()) {
        send_str(fd, "OK not paused\n");
        return;
    }
    emu_flexe_debug_continue();
    send_str(fd, "OK\n");
}

static void handle_step(int fd, const char *args)
{
    if (!emu_flexe_active()) {
        send_str(fd, "ERR flexe not active\n");
        return;
    }
    /* Must be paused to step */
    if (!emu_flexe_debug_paused()) {
        emu_flexe_debug_break();
        if (!emu_flexe_debug_wait_paused(2000)) {
            send_str(fd, "ERR timeout waiting for pause\n");
            return;
        }
    }

    int count = 1;
    if (args && *args)
        count = atoi(args);
    if (count < 1) count = 1;
    if (count > 100000) count = 100000;

    xtensa_cpu_t *cpu = emu_flexe_get_cpu();
    char line[256];

    /* Ensure CPU can execute steps even if it was stopped/halted */
    int was_stopped = !cpu->running;
    int was_halted = cpu->halted;
    if (was_stopped) cpu->running = true;
    if (was_halted) cpu->halted = false;

    for (int i = 0; i < count; i++) {
        /* Save registers before step */
        uint32_t old_regs[16];
        for (int r = 0; r < 16; r++)
            old_regs[r] = ar_read(cpu, r);
        uint32_t old_pc = cpu->pc;

        /* Disassemble current instruction */
        char dis[128];
        xtensa_disasm(cpu, old_pc, dis, sizeof(dis));

        /* Step — temporarily suppress breakpoints so we can step past them */
        int saved_bp_count = cpu->breakpoint_count;
        cpu->breakpoint_count = 0;
        cpu->breakpoint_hit = false;
        xtensa_step(cpu);
        cpu->breakpoint_count = saved_bp_count;

        /* Build step line */
        int pos = snprintf(line, sizeof(line), "STEP 0x%08X %s", old_pc, dis);

        /* Report changed registers */
        int first_change = 1;
        for (int r = 0; r < 16; r++) {
            uint32_t new_val = ar_read(cpu, r);
            if (new_val != old_regs[r]) {
                pos += snprintf(line + pos, sizeof(line) - pos,
                               "%s a%d=%08X",
                               first_change ? " |" : "", r, new_val);
                first_change = 0;
            }
        }
        pos += snprintf(line + pos, sizeof(line) - pos, "\n");
        send_str(fd, line);

        if (!cpu->running) break;
    }
    snprintf(line, sizeof(line), "OK pc=0x%08X\n", cpu->pc);
    send_str(fd, line);
}

static void handle_regs(int fd)
{
    if (!emu_flexe_active()) {
        send_str(fd, "ERR flexe not active\n");
        return;
    }
    if (!emu_flexe_debug_paused()) {
        send_str(fd, "ERR not paused\n");
        return;
    }

    xtensa_cpu_t *cpu = emu_flexe_get_cpu();
    char line[256];

    snprintf(line, sizeof(line), "REG PC=0x%08X\n", cpu->pc);
    send_str(fd, line);

    uint32_t ps = cpu->ps;
    snprintf(line, sizeof(line),
             "REG PS=0x%08X INTLEVEL=%u EXCM=%u UM=%u RING=%u OWB=%u CALLINC=%u WOE=%u\n",
             ps, ps & 0xF, (ps >> 4) & 1, (ps >> 5) & 1, (ps >> 6) & 3,
             (ps >> 8) & 0xF, (ps >> 16) & 3, (ps >> 18) & 1);
    send_str(fd, line);

    /* Windowed registers a0-a15 */
    for (int r = 0; r < 16; r += 4) {
        snprintf(line, sizeof(line), "REG a%d=%08X a%d=%08X a%d=%08X a%d=%08X\n",
                 r, ar_read(cpu, r), r+1, ar_read(cpu, r+1),
                 r+2, ar_read(cpu, r+2), r+3, ar_read(cpu, r+3));
        send_str(fd, line);
    }

    snprintf(line, sizeof(line), "REG SAR=%u WindowBase=%u\n",
             cpu->sar, cpu->windowbase);
    send_str(fd, line);

    snprintf(line, sizeof(line), "REG LBEG=0x%08X LEND=0x%08X LCOUNT=%u\n",
             cpu->lbeg, cpu->lend, cpu->lcount);
    send_str(fd, line);

    snprintf(line, sizeof(line), "REG cycles=%llu\n", (unsigned long long)cpu->cycle_count);
    send_str(fd, line);

    send_str(fd, "OK\n");
}

static void handle_memdump(int fd, const char *args)
{
    if (!emu_flexe_active()) {
        send_str(fd, "ERR flexe not active\n");
        return;
    }

    uint32_t addr = 0;
    int len = 0;
    if (sscanf(args, "%i %i", &addr, &len) < 2) {
        send_str(fd, "ERR usage: memdump <addr> <len>\n");
        return;
    }
    if (len < 0) len = 0;
    if (len > 4096) len = 4096;

    char line[80];
    for (int off = 0; off < len; off += 16) {
        int n = len - off;
        if (n > 16) n = 16;
        int pos = snprintf(line, sizeof(line), "MEM 0x%08X:", addr + off);
        for (int i = 0; i < n; i++)
            pos += snprintf(line + pos, sizeof(line) - pos, " %02X",
                           emu_flexe_mem_read8(addr + off + i));
        pos += snprintf(line + pos, sizeof(line) - pos, "\n");
        send_str(fd, line);
    }
    send_str(fd, "OK\n");
}

static void handle_disasm(int fd, const char *args)
{
    if (!emu_flexe_active()) {
        send_str(fd, "ERR flexe not active\n");
        return;
    }

    uint32_t addr = 0;
    int count = 10;
    if (sscanf(args, "%i %i", &addr, &count) < 1) {
        send_str(fd, "ERR usage: disasm <addr> [count]\n");
        return;
    }
    if (count < 1) count = 1;
    if (count > 200) count = 200;

    xtensa_cpu_t *cpu = emu_flexe_get_cpu();
    char dis[128], line[192];
    for (int i = 0; i < count; i++) {
        int insn_len = xtensa_disasm(cpu, addr, dis, sizeof(dis));
        if (insn_len <= 0) {
            snprintf(line, sizeof(line), "DIS 0x%08X <invalid>\n", addr);
            send_str(fd, line);
            break;
        }
        snprintf(line, sizeof(line), "DIS 0x%08X %s\n", addr, dis);
        send_str(fd, line);
        addr += insn_len;
    }
    send_str(fd, "OK\n");
}

/* ---- Poll ---- */

void emu_control_poll(void)
{
    if (listen_fd < 0) return;

    /* Non-blocking accept */
    int client = accept(listen_fd, NULL, NULL);
    if (client < 0) return;  /* EAGAIN / EWOULDBLOCK — no client waiting */

    /* Set receive timeout so we don't block the main loop */
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; /* 100ms */
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Read one line */
    char buf[1024];
    ssize_t n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        close(client);
        return;
    }
    buf[n] = '\0';

    /* Strip trailing newline/CR */
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r'))
        buf[--n] = '\0';

    /* Parse command */
    if (strncmp(buf, "tap ", 4) == 0) {
        handle_tap(client, buf + 4);
    } else if (strncmp(buf, "touch_down ", 11) == 0) {
        handle_touch_down(client, buf + 11);
    } else if (strcmp(buf, "touch_up") == 0) {
        handle_touch_up(client);
    } else if (strncmp(buf, "screenshot ", 11) == 0) {
        handle_screenshot(client, buf + 11);
    } else if (strcmp(buf, "status") == 0) {
        handle_status(client);
    } else if (strcmp(buf, "log") == 0) {
        handle_log(client);
    } else if (strcmp(buf, "quit") == 0) {
        handle_quit(client);
    } else if (strncmp(buf, "peek ", 5) == 0) {
        uint32_t addr = (uint32_t)strtoul(buf + 5, NULL, 0);
        uint32_t val = emu_flexe_mem_read32(addr);
        char resp[64];
        snprintf(resp, sizeof(resp), "OK 0x%08X = 0x%08X (%u)\n", addr, val, val);
        send_str(client, resp);
    } else if (strncmp(buf, "break ", 6) == 0) {
        handle_break(client, buf + 6);
    } else if (strncmp(buf, "clearbreak ", 11) == 0) {
        handle_clearbreak(client, buf + 11);
    } else if (strcmp(buf, "clearbreak") == 0) {
        handle_clearbreak(client, "all");
    } else if (strcmp(buf, "pause") == 0) {
        handle_pause(client);
    } else if (strcmp(buf, "continue") == 0 || strcmp(buf, "c") == 0) {
        handle_continue(client);
    } else if (strncmp(buf, "step", 4) == 0 && (buf[4] == '\0' || buf[4] == ' ')) {
        handle_step(client, buf + 4);
    } else if (strcmp(buf, "regs") == 0) {
        handle_regs(client);
    } else if (strncmp(buf, "memdump ", 8) == 0) {
        handle_memdump(client, buf + 8);
    } else if (strncmp(buf, "disasm ", 7) == 0) {
        handle_disasm(client, buf + 7);
    } else {
        send_str(client, "ERR unknown command\n");
    }

    close(client);
}

/* ---- Shutdown ---- */

void emu_control_shutdown(void)
{
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }
    if (sock_path[0]) {
        unlink(sock_path);
        sock_path[0] = '\0';
    }
}
