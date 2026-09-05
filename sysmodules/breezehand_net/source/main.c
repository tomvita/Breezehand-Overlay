/*
 * breezehand-net -- a small TCP command server exposing the cheat-process
 * memory of the running game.
 *
 * Why this exists: the Atmosphere GDB stub halts the game on every attach, so
 * observing a value that changes over a second or two is impractical -- every
 * sample steals game time from the window you are trying to observe. dmnt's
 * cheat-process path reads and writes memory of a *running* game, so a sampling
 * loop here does not disturb it.
 *
 * The sampling loop deliberately runs on-device: `watch` fills a buffer locally
 * at the requested rate and only ships the result afterwards. Putting the
 * network in the sample loop would reintroduce the problem this exists to fix.
 *
 * Not started at boot. There is intentionally no flags/boot2.flag; Breeze
 * launches this with pmshellLaunchProgram when the user toggles it on, and the
 * ini gate below is a second line of defence in case someone installs it with a
 * boot2 flag of their own.
 */

#include <switch.h>
#include <switch/services/bsd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>

#include "dmntcht.h"
#include "input.h"

#define BHNET_VERSION "0.1.0"
#define INI_PATH "sdmc:/switch/breeze/config.ini"
#define LOG_PATH "sdmc:/switch/breeze/breezehand_net.log"

#define DEFAULT_PORT 6800

/* Socket buffers. Deliberately small: this is a line protocol, not a file
 * transfer, and every byte here is static memory competing with the user's
 * other sysmodules. ftpsrv's sizing cost ~4 MB and prevented the module from
 * launching alongside them. */
#define TCP_TX_BUF_SIZE     (1 << 13)
#define TCP_RX_BUF_SIZE     (1 << 13)
#define TCP_TX_BUF_SIZE_MAX (1 << 15)
#define TCP_RX_BUF_SIZE_MAX (1 << 15)
#define UDP_TX_BUF_SIZE     0
#define UDP_RX_BUF_SIZE     0
#define SB_EFFICIENCY       2

#define ALIGN_MSS(v) ((((v) + 1500 - 1) / 1500) * 1500)
#define SOCKET_TMEM_SIZE \
    ((((( \
      ALIGN_MSS(TCP_TX_BUF_SIZE_MAX) \
    + ALIGN_MSS(TCP_RX_BUF_SIZE_MAX)) \
    + 0) + 0) + 0xFFF) &~ 0xFFF) * SB_EFFICIENCY
#define NUMBER_OF_SOCKETS 2

static alignas(0x1000) u8 SOCKET_TRANSFER_MEM[SOCKET_TMEM_SIZE * NUMBER_OF_SOCKETS];

/* Sysmodules get no heap by default. */
/* The search engine allocates a scan buffer + output buffer from here
 * (see BREEZE_SCAN_BUFFER_BYTES in the Makefile), so this must comfortably
 * exceed their sum. */
#define BHNET_HEAP_SIZE (1024 * 1024)
static alignas(0x1000) u8 g_heap[BHNET_HEAP_SIZE];

/* Largest single peek, and the watch sample buffer. */
#define MAX_PEEK       0x1000
#define WATCH_MAX_BYTES (256 * 1024)   /* 65536 u32 samples -- ample */

static u8  g_watch_buf[WATCH_MAX_BYTES];
static u8  g_io_buf[MAX_PEEK];
static char g_line[8192];
static char g_out[MAX_PEEK * 2 + 256];

static void logf_(const char *fmt, ...) {
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputs("\n", f);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Tiny ini reader. Avoids pulling minIni in for two lookups.          */
/* ------------------------------------------------------------------ */

static bool ini_lookup(const char *path, const char *section, const char *key, char *out, size_t out_len) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    bool in_section = false;
    bool found = false;

    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == ';' || *s == '\0') continue;

        if (*s == '[') {
            char *end = strchr(s, ']');
            if (!end) continue;
            *end = '\0';
            in_section = (strcasecmp(s + 1, section) == 0);
            continue;
        }
        if (!in_section) continue;

        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';

        char *k = s;
        char *ke = k + strlen(k);
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = '\0';
        if (strcasecmp(k, key) != 0) continue;

        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *ve = v + strlen(v);
        while (ve > v && (ve[-1] == '\n' || ve[-1] == '\r' || ve[-1] == ' ' || ve[-1] == '\t')) *--ve = '\0';

        snprintf(out, out_len, "%s", v);
        found = true;
        break;
    }
    fclose(f);
    return found;
}

static long ini_get_long(const char *section, const char *key, long dflt) {
    char buf[64];
    if (!ini_lookup(INI_PATH, section, key, buf, sizeof(buf))) return dflt;
    return strtol(buf, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

static u32 socketSelectVersion(void) {
    if (hosversionBefore(3,0,0))  return 1;
    if (hosversionBefore(4,0,0))  return 2;
    if (hosversionBefore(5,0,0))  return 3;
    if (hosversionBefore(6,0,0))  return 4;
    if (hosversionBefore(8,0,0))  return 5;
    if (hosversionBefore(9,0,0))  return 6;
    if (hosversionBefore(13,0,0)) return 7;
    if (hosversionBefore(16,0,0)) return 8;
    return 9;
}

void __libnx_initheap(void) {
    extern char *fake_heap_start;
    extern char *fake_heap_end;
    fake_heap_start = (char *)g_heap;
    fake_heap_end   = (char *)g_heap + sizeof(g_heap);
}

void __appInit(void) {
    Result rc;

    if (R_FAILED(rc = smInitialize())) diagAbortWithResult(rc);

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    if (R_FAILED(rc = fsInitialize())) diagAbortWithResult(rc);
    if (R_FAILED(rc = fsdevMountSdmc())) diagAbortWithResult(rc);

    const BsdInitConfig bsd_config = {
        .version             = socketSelectVersion(),
        .tmem_buffer         = SOCKET_TRANSFER_MEM,
        .tmem_buffer_size    = sizeof(SOCKET_TRANSFER_MEM),
        .tcp_tx_buf_size     = TCP_TX_BUF_SIZE,
        .tcp_rx_buf_size     = TCP_RX_BUF_SIZE,
        .tcp_tx_buf_max_size = TCP_TX_BUF_SIZE_MAX,
        .tcp_rx_buf_max_size = TCP_RX_BUF_SIZE_MAX,
        .udp_tx_buf_size     = UDP_TX_BUF_SIZE,
        .udp_rx_buf_size     = UDP_RX_BUF_SIZE,
        .sb_efficiency       = SB_EFFICIENCY,
    };
    if (R_FAILED(rc = bsdInitialize(&bsd_config, 1, BsdServiceType_Auto)))
        diagAbortWithResult(rc);

    bhnet_input_init();
}

void __appExit(void) {
    dmntchtExit();
    bsdExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t hex_to_bytes(const char *s, u8 *out, size_t max) {
    size_t n = 0;
    while (s[0] && s[1] && n < max) {
        int hi = hexval(s[0]), lo = hexval(s[1]);
        if (hi < 0 || lo < 0) break;
        out[n++] = (u8)((hi << 4) | lo);
        s += 2;
    }
    return n;
}

static void bytes_to_hex(const u8 *in, size_t n, char *out) {
    static const char *H = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2]     = H[in[i] >> 4];
        out[i * 2 + 1] = H[in[i] & 0xF];
    }
    out[n * 2] = '\0';
}

static void send_str(int fd, const char *s) {
    size_t len = strlen(s);
    while (len) {
        ssize_t w = bsdSend(fd, s, len, 0);
        if (w <= 0) return;
        s += w; len -= (size_t)w;
    }
}

static void send_line(int fd, const char *s) {
    send_str(fd, s);
    send_str(fd, "\n");
}

static void send_err(int fd, const char *what) {
    char buf[256];
    snprintf(buf, sizeof(buf), "-ERR %s", what);
    send_line(fd, buf);
}

/* Ensure dmnt has the game open; harmless (idempotent) if it already does. */
static bool ensure_cheat_process(void) {
    bool has = false;
    if (R_FAILED(dmntchtHasCheatProcess(&has))) return false;
    if (has) return true;
    if (R_FAILED(dmntchtForceOpenCheatProcess())) return false;
    return R_SUCCEEDED(dmntchtHasCheatProcess(&has)) && has;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

static void cmd_meta(int fd) {
    DmntCheatProcessMetadata md = {0};
    if (!ensure_cheat_process()) { send_err(fd, "no cheat process"); return; }
    if (R_FAILED(dmntchtGetCheatProcessMetadata(&md))) { send_err(fd, "metadata failed"); return; }
    char buf[512];
    snprintf(buf, sizeof(buf),
             "+OK pid=%lu titleid=%016lx main=%010lx main_size=%lx heap=%010lx heap_size=%lx alias=%010lx alias_size=%lx",
             md.process_id, md.title_id,
             md.main_nso_extents.base, md.main_nso_extents.size,
             md.heap_extents.base, md.heap_extents.size,
             md.alias_extents.base, md.alias_extents.size);
    send_line(fd, buf);
}

static void cmd_peek(int fd, char *args) {
    u64 addr = 0; u32 len = 0;
    if (sscanf(args, "%lx %u", &addr, &len) != 2) { send_err(fd, "usage: peek <hexaddr> <len>"); return; }
    if (len == 0 || len > MAX_PEEK) { send_err(fd, "len out of range"); return; }
    if (!ensure_cheat_process()) { send_err(fd, "no cheat process"); return; }
    if (R_FAILED(dmntchtReadCheatProcessMemory(addr, g_io_buf, len))) { send_err(fd, "read failed"); return; }
    g_out[0] = '+'; g_out[1] = 'O'; g_out[2] = 'K'; g_out[3] = ' ';
    bytes_to_hex(g_io_buf, len, g_out + 4);
    send_line(fd, g_out);
}

static void cmd_poke(int fd, char *args) {
    u64 addr = 0; char hex[4096];
    if (sscanf(args, "%lx %4095s", &addr, hex) != 2) { send_err(fd, "usage: poke <hexaddr> <hexbytes>"); return; }
    size_t n = hex_to_bytes(hex, g_io_buf, MAX_PEEK);
    if (n == 0) { send_err(fd, "bad hex"); return; }
    if (!ensure_cheat_process()) { send_err(fd, "no cheat process"); return; }
    if (R_FAILED(dmntchtWriteCheatProcessMemory(addr, g_io_buf, n))) { send_err(fd, "write failed"); return; }
    char buf[64];
    snprintf(buf, sizeof(buf), "+OK %zu", n);
    send_line(fd, buf);
}

/*
 * watch <hexaddr> <len> <hz> <ms>
 *
 * Samples locally at <hz> for <ms> milliseconds, then returns every sample as
 * one hex blob per line. The game keeps running throughout -- this is the whole
 * point of the module.
 */
static void cmd_watch(int fd, char *args) {
    u64 addr = 0; u32 len = 0, hz = 0, ms = 0;
    unsigned long long trig_mask = 0; unsigned int trig_to = 0;
    int got = sscanf(args, "%lx %u %u %u %llx %u", &addr, &len, &hz, &ms, &trig_mask, &trig_to);
    if (got < 4) {
        send_err(fd, "usage: watch <hexaddr> <len> <hz> <ms> [<hexmask> <timeout_ms>]"); return;
    }
    /* Optional trigger: wait on-device for a button edge, so sampling starts on
     * the exact frame of the press rather than a network round trip later. */
    s64 waited = -2;
    if (got == 6 && trig_mask != 0) {
        waited = bhnet_wait_press(trig_mask, trig_to ? trig_to : 30000);
        if (waited < 0) { send_err(fd, "timeout waiting for trigger button"); return; }
    }
    if (len == 0 || len > MAX_PEEK)      { send_err(fd, "len out of range"); return; }
    if (hz == 0 || hz > 1000)            { send_err(fd, "hz out of range (1-1000)"); return; }
    if (ms == 0 || ms > 60000)           { send_err(fd, "ms out of range (1-60000)"); return; }
    if (!ensure_cheat_process())         { send_err(fd, "no cheat process"); return; }

    u32 want = (u32)(((u64)hz * ms) / 1000);
    if (want == 0) want = 1;
    if ((u64)want * len > WATCH_MAX_BYTES) want = (u32)(WATCH_MAX_BYTES / len);

    const u64 period_ns = 1000000000ULL / hz;
    u64 next = armTicksToNs(armGetSystemTick());
    u32 nsamp = 0, failed = 0;

    for (u32 i = 0; i < want; i++) {
        if (R_FAILED(dmntchtReadCheatProcessMemory(addr, g_watch_buf + (size_t)nsamp * len, len))) {
            failed++;
        } else {
            nsamp++;
        }
        next += period_ns;
        s64 delta = (s64)(next - armTicksToNs(armGetSystemTick()));
        if (delta > 0) svcSleepThread(delta);
    }

    char hdr[128];
    snprintf(hdr, sizeof(hdr), "+OK samples=%u len=%u hz=%u failed=%u trigwait=%lld", nsamp, len, hz, failed, (long long)waited);
    send_line(fd, hdr);
    for (u32 i = 0; i < nsamp; i++) {
        bytes_to_hex(g_watch_buf + (size_t)i * len, len, g_out);
        send_line(fd, g_out);
    }
    send_line(fd, ".");
}

/* search_bridge.cpp */
int  bhnet_search_begin(unsigned int type, unsigned int mode,
                        unsigned long long v1, unsigned long long v2,
                        int is_continue, const char *out_stem, const char *source_path);
int  bhnet_search_running(void);
void bhnet_search_abort(void);
void bhnet_search_status(char *out, size_t out_len);
int  bhnet_search_results(const char *stem, unsigned long long offset,
                          unsigned int count, char *out, size_t out_len);

static int g_search_level = -1;   /* -1 = none yet; stems are bhnet<level> */

static void search_stem(int level, char *out, size_t n) {
    snprintf(out, n, "bhnet%d", level);
}

static void cmd_search(int fd, char *args) {
    char sub[32] = "";
    if (sscanf(args, "%31s", sub) != 1) { send_err(fd, "usage: search start|cont|status|abort|results"); return; }
    char *rest = args + strlen(sub);

    if (!strcasecmp(sub, "status")) {
        char st[512];
        bhnet_search_status(st, sizeof(st));
        char buf[600];
        snprintf(buf, sizeof(buf), "+OK %s level=%d", st, g_search_level);
        send_line(fd, buf);
        return;
    }
    if (!strcasecmp(sub, "abort")) { bhnet_search_abort(); send_line(fd, "+OK aborting"); return; }

    if (!strcasecmp(sub, "results")) {
        unsigned long long off = 0; unsigned int cnt = 0; int lvl = g_search_level;
        if (sscanf(rest, "%llu %u %d", &off, &cnt, &lvl) < 2) { send_err(fd, "usage: search results <offset> <count> [level]"); return; }
        if (lvl < 0) { send_err(fd, "no search has been run"); return; }
        if (cnt == 0 || cnt > 200) { send_err(fd, "count 1-200"); return; }
        char stem[32]; search_stem(lvl, stem, sizeof(stem));
        int n = bhnet_search_results(stem, off, cnt, g_out, sizeof(g_out));
        if (n < 0) { send_err(fd, "cannot read candidate file"); return; }
        char hdr[96]; snprintf(hdr, sizeof(hdr), "+OK records=%d stem=%s", n, stem);
        send_line(fd, hdr);
        send_str(fd, g_out);
        send_line(fd, ".");
        return;
    }

    int is_cont = !strcasecmp(sub, "cont");
    if (!is_cont && strcasecmp(sub, "start")) { send_err(fd, "unknown subcommand"); return; }
    if (bhnet_search_running()) { send_err(fd, "search already running"); return; }

    unsigned int type = 0, mode = 0;
    unsigned long long v1 = 0, v2 = 0;
    int got = sscanf(rest, "%u %u %llx %llx", &type, &mode, &v1, &v2);
    if (got < 3) { send_err(fd, "usage: search start|cont <type> <mode> <hexval1> [hexval2]"); return; }

    if (!ensure_cheat_process()) { send_err(fd, "no cheat process"); return; }

    char out_stem[32], src_stem[32], src_path[128];
    int next = is_cont ? g_search_level + 1 : 0;
    search_stem(next, out_stem, sizeof(out_stem));
    src_path[0] = 0;
    if (is_cont) {
        if (g_search_level < 0) { send_err(fd, "no previous search to continue"); return; }
        search_stem(g_search_level, src_stem, sizeof(src_stem));
        snprintf(src_path, sizeof(src_path), "sdmc:/switch/Breeze/%s.dat", src_stem);
    }

    int rc = bhnet_search_begin(type, mode, v1, v2, is_cont, out_stem, src_path);
    if (rc == 2) { send_err(fd, "bad search type"); return; }
    if (rc != 0) { send_err(fd, "search already running"); return; }
    g_search_level = next;

    char buf[128];
    snprintf(buf, sizeof(buf), "+OK started %s stem=%s", is_cont ? "continue" : "start", out_stem);
    send_line(fd, buf);
}

static void cmd_buttons(int fd) {
    u64 b = bhnet_buttons();
    char buf[128];
    snprintf(buf, sizeof(buf), "+OK %016llx", (unsigned long long)b);
    send_line(fd, buf);
}

static void cmd_waitkey(int fd, char *args) {
    unsigned long long mask = 0; unsigned int to = 0;
    if (sscanf(args, "%llx %u", &mask, &to) != 2) { send_err(fd, "usage: waitkey <hexmask> <timeout_ms>"); return; }
    if (to == 0 || to > 300000) { send_err(fd, "timeout 1-300000"); return; }
    s64 ms = bhnet_wait_press(mask, to);
    if (ms < 0) { send_err(fd, "timeout"); return; }
    char buf[96];
    snprintf(buf, sizeof(buf), "+OK pressed after %lld ms", (long long)ms);
    send_line(fd, buf);
}

static void cmd_freeze(int fd, char *args) {
    unsigned long long mask = 0; unsigned int to = 0;
    if (sscanf(args, "%llx %u", &mask, &to) == 2) {
        s64 ms = bhnet_wait_press(mask, to);
        if (ms < 0) { send_err(fd, "timeout waiting for button"); return; }
        Result rc = bhnet_freeze();
        char buf[128];
        snprintf(buf, sizeof(buf), "+OK frozen %lld ms after arm (rc=0x%08x)", (long long)ms, rc);
        send_line(fd, buf);
        return;
    }
    Result rc = bhnet_freeze();
    if (R_FAILED(rc)) { send_err(fd, "pause failed"); return; }
    send_line(fd, "+OK frozen");
}

static void cmd_resume(int fd) {
    Result rc = bhnet_resume();
    if (R_FAILED(rc)) { send_err(fd, "resume failed"); return; }
    send_line(fd, "+OK resumed");
}

/* Hand the address to dmnt to hold. Unlike repeated pokes from the client, this
 * has no network in the loop -- the cheat VM maintains it, so it does not lose a
 * race against a value the game decrements every frame. */
static void cmd_hold(int fd, char *args) {
    u64 addr = 0, width = 4, val = 0;
    int n = sscanf(args, "%lx %lu", &addr, &width);
    if (n < 1) { send_err(fd, "usage: hold <hexaddr> [width]"); return; }
    if (width != 1 && width != 2 && width != 4 && width != 8) { send_err(fd, "width must be 1,2,4,8"); return; }
    if (!ensure_cheat_process()) { send_err(fd, "no cheat process"); return; }
    Result rc = dmntchtEnableFrozenAddress(addr, width, &val);
    if (R_FAILED(rc)) { send_err(fd, "freeze failed"); return; }
    char buf[96];
    snprintf(buf, sizeof(buf), "+OK held 0x%010lx width=%lu at value %lu", addr, width, val);
    send_line(fd, buf);
}

static void cmd_unhold(int fd, char *args) {
    u64 addr = 0;
    if (sscanf(args, "%lx", &addr) != 1) { send_err(fd, "usage: unhold <hexaddr>"); return; }
    Result rc = dmntchtDisableFrozenAddress(addr);
    if (R_FAILED(rc)) { send_err(fd, "unfreeze failed"); return; }
    send_line(fd, "+OK released");
}

static void cmd_holds(int fd) {
    u64 count = 0;
    if (R_FAILED(dmntchtGetFrozenAddressCount(&count))) { send_err(fd, "query failed"); return; }
    char buf[64];
    snprintf(buf, sizeof(buf), "+OK frozen=%lu", count);
    send_line(fd, buf);
    DmntFrozenAddressEntry ents[32];
    u64 got = 0;
    if (R_SUCCEEDED(dmntchtGetFrozenAddresses(ents, 32, 0, &got))) {
        for (u64 i = 0; i < got; i++) {
            snprintf(buf, sizeof(buf), "%010lx %u %lu", ents[i].address, (unsigned)ents[i].value.width, (unsigned long)ents[i].value.value);
            send_line(fd, buf);
        }
    }
    send_line(fd, ".");
}

static void handle_line(int fd, char *line, bool *quit) {
    while (*line == ' ' || *line == '\t') line++;
    char *sp = strchr(line, ' ');
    char *args = sp ? sp + 1 : (char *)"";
    if (sp) *sp = '\0';

    if      (!strcasecmp(line, "ping"))  send_line(fd, "+OK breezehand-net " BHNET_VERSION);
    else if (!strcasecmp(line, "meta"))  cmd_meta(fd);
    else if (!strcasecmp(line, "peek"))  cmd_peek(fd, args);
    else if (!strcasecmp(line, "poke"))  cmd_poke(fd, args);
    else if (!strcasecmp(line, "watch")) cmd_watch(fd, args);
    else if (!strcasecmp(line, "search")) cmd_search(fd, args);
    else if (!strcasecmp(line, "buttons")) cmd_buttons(fd);
    else if (!strcasecmp(line, "waitkey")) cmd_waitkey(fd, args);
    else if (!strcasecmp(line, "freeze")) cmd_freeze(fd, args);
    else if (!strcasecmp(line, "resume")) cmd_resume(fd);
    else if (!strcasecmp(line, "hold")) cmd_hold(fd, args);
    else if (!strcasecmp(line, "unhold")) cmd_unhold(fd, args);
    else if (!strcasecmp(line, "holds")) cmd_holds(fd);
    else if (!strcasecmp(line, "quit"))  { send_line(fd, "+OK bye"); *quit = true; }
    else if (*line)                      send_err(fd, "unknown command");
}

static void serve(int cfd) {
    size_t used = 0;
    bool quit = false;
    send_line(cfd, "+OK breezehand-net " BHNET_VERSION " ready");
    while (!quit) {
        ssize_t r = bsdRecv(cfd, g_line + used, sizeof(g_line) - 1 - used, 0);
        if (r <= 0) break;
        used += (size_t)r;
        g_line[used] = '\0';

        char *start = g_line, *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            size_t l = strlen(start);
            if (l && start[l - 1] == '\r') start[l - 1] = '\0';
            handle_line(cfd, start, &quit);
            if (quit) break;
            start = nl + 1;
        }
        size_t rem = used - (size_t)(start - g_line);
        memmove(g_line, start, rem);
        used = rem;
        if (used >= sizeof(g_line) - 1) used = 0; /* overlong line: drop */
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Gate. Breeze writes enable=1 when the user toggles the feature on; if the
     * module is present but not enabled we exit immediately rather than open a
     * port. */
    logf_("--- breezehand-net " BHNET_VERSION " starting ---");

    long en = ini_get_long("Breezehand-Net", "enable", -1);
    logf_("ini enable=%ld (path %s)", en, INI_PATH);
    if (en <= 0) { logf_("exit: not enabled (or ini unreadable)"); return 0; }

    const int port = (int)ini_get_long("Breezehand-Net", "port", DEFAULT_PORT);
    logf_("port=%d", port);

    Result rc = dmntchtInitialize();
    logf_("dmntchtInitialize -> 0x%08x", rc);
    if (R_FAILED(rc)) { logf_("exit: dmnt:cht unavailable"); return 0; }

    int sfd = bsdSocket(AF_INET, SOCK_STREAM, 0);
    logf_("socket -> %d (errno %d)", sfd, errno);
    if (sfd < 0) { logf_("exit: socket failed"); return 0; }

    int yes = 1;
    bsdSetSockOpt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bsdBind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        logf_("exit: bind failed errno=%d", errno); bsdClose(sfd); return 0;
    }
    if (bsdListen(sfd, 1) < 0) {
        logf_("exit: listen failed errno=%d", errno); bsdClose(sfd); return 0;
    }
    logf_("listening on port %d -- ready", port);

    /* One client at a time. The workload is a single operator driving it, and
     * serialising keeps the cheat-process access trivially safe. */
    for (;;) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int cfd = bsdAccept(sfd, (struct sockaddr *)&cli, &clilen);
        if (cfd < 0) {
            if (cfd == -1) { logf_("accept failed"); }
            break;
        }
        serve(cfd);
        bsdClose(cfd);
    }

    bsdClose(sfd);
    return 0;
}
