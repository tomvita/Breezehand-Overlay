/*
 * C bridge to the Breeze search engine in common/.
 *
 * The engine (breeze::RunStartSearch / RunContinueSearch) is already headless
 * and already driven off a worker thread by the overlay, with progress and
 * abort exposed through SearchRunControl atomics. So this file is deliberately
 * thin: it owns a worker thread and the atomics, and exposes a handful of
 * extern "C" entry points for main.c to call. It does not reimplement anything.
 */

#include <atomic>
#include <string>
#include <thread>
#include <cstdio>
#include <cstring>

#include "breeze_search_exec.hpp"
#include "breeze_search_compat.hpp"
#include "search_types.hpp"

namespace {

    std::atomic<bool> g_pause{false};
    std::atomic<bool> g_abort{false};
    std::atomic<bool> g_is_paused{false};
    std::atomic<u64>  g_cur{0};
    std::atomic<u64>  g_total{0};
    std::atomic<bool> g_running{false};
    std::atomic<bool> g_ok{false};

    std::thread g_worker;
    std::string g_error;
    std::string g_out_stem;
    breeze::SearchRunStats g_stats;

    breeze::SearchRunControl MakeControl() {
        breeze::SearchRunControl c;
        c.pauseRequested  = &g_pause;
        c.abortRequested  = &g_abort;
        c.progressCurrent = &g_cur;
        c.progressTotal   = &g_total;
        c.isPaused        = &g_is_paused;
        return c;
    }

    void JoinIfDone() {
        if (!g_running.load() && g_worker.joinable()) {
            g_worker.join();
        }
    }

}

extern "C" {

int bhnet_search_start(const Search_condition *cond, const char *out_stem,
                       int is_continue, const char *source_path);

/*
 * Builds the Search_condition here rather than in main.c: search_types.hpp uses
 * C++ default member initialisers, so a C translation unit cannot include it.
 * 0 = started, 1 = already running, 2 = bad type.
 */
int bhnet_search_begin(unsigned int type, unsigned int mode,
                       unsigned long long v1, unsigned long long v2,
                       int is_continue, const char *out_stem, const char *source_path) {
    if (type >= (unsigned)SEARCH_TYPE_NONE) return 2;

    Search_condition cond{};
    cond.searchType         = (searchType_t)type;
    cond.searchMode         = (searchMode_t)mode;
    cond.searchValue_1._u64 = v1;
    cond.searchValue_2._u64 = v2;
    cond.search_step        = is_continue ? search_step_secondary : search_step_primary;

    return bhnet_search_start(&cond, out_stem, is_continue, source_path);
}

/* 0 = started, 1 = already running. */
int bhnet_search_start(const Search_condition *cond, const char *out_stem,
                       int is_continue, const char *source_path) {
    if (g_running.load()) return 1;
    JoinIfDone();

    g_pause.store(false);
    g_abort.store(false);
    g_cur.store(0);
    g_total.store(0);
    g_ok.store(false);
    g_error.clear();
    g_stats = breeze::SearchRunStats{};
    g_out_stem = out_stem ? out_stem : "";

    Search_condition c = *cond;
    std::string stem = g_out_stem;
    std::string src  = source_path ? source_path : "";
    bool cont = (is_continue != 0);

    g_running.store(true);
    g_worker = std::thread([c, stem, src, cont]() {
        std::string err;
        auto ctl = MakeControl();
        bool ok;
        if (cont) {
            ok = breeze::RunContinueSearch(c, src, stem, g_stats, &err, &ctl);
        } else {
            ok = breeze::RunStartSearch(c, stem, g_stats, &err, &ctl);
        }
        g_error = err;
        g_ok.store(ok);
        g_running.store(false);
    });
    return 0;
}

int  bhnet_search_running(void) { return g_running.load() ? 1 : 0; }
void bhnet_search_abort(void)   { g_abort.store(true); }

/* Formats run state. Safe to call while running (reports progress instead). */
void bhnet_search_status(char *out, size_t out_len) {
    if (g_running.load()) {
        std::snprintf(out, out_len, "running cur=%llu total=%llu paused=%d",
                      (unsigned long long)g_cur.load(),
                      (unsigned long long)g_total.load(),
                      g_is_paused.load() ? 1 : 0);
        return;
    }
    JoinIfDone();
    if (g_out_stem.empty()) {
        std::snprintf(out, out_len, "idle");
        return;
    }
    if (!g_ok.load()) {
        std::snprintf(out, out_len, "failed %s",
                      g_error.empty() ? "(no detail)" : g_error.c_str());
        return;
    }
    std::snprintf(out, out_len,
                  "done entries=%llu bytes=%llu scanned=%llu secs=%u aborted=%d stem=%s",
                  (unsigned long long)g_stats.entriesWritten,
                  (unsigned long long)g_stats.bytesWritten,
                  (unsigned long long)g_stats.bytesScanned,
                  g_stats.secondsTaken,
                  g_stats.aborted ? 1 : 0,
                  g_out_stem.c_str());
}

/*
 * Reads candidate records straight out of the .dat the engine just wrote, using
 * the same structs as the writer so the layout cannot drift. Emits one
 * "<address> <value>" line per record. Returns the count, or -1 on error.
 */
int bhnet_search_results(const char *stem, unsigned long long offset,
                         unsigned int count, char *out, size_t out_len) {
    std::string path = std::string("sdmc:/switch/Breeze/") + (stem ? stem : "") + ".dat";
    FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) return -1;

    breeze::BreezeFileHeader_t hdr{};
    if (std::fread(&hdr, sizeof(hdr), 1, fp) != 1) { std::fclose(fp); return -1; }
    if (std::memcmp(hdr.MAGIC, "BREEZE00E", 9) != 0) { std::fclose(fp); return -1; }

    const size_t rec_size = sizeof(u64) * 2;
    if (std::fseek(fp, (long)(sizeof(hdr) + offset * rec_size), SEEK_SET) != 0) {
        std::fclose(fp);
        return -1;
    }

    size_t used = 0;
    unsigned int n = 0;
    for (; n < count; n++) {
        u64 pair[2];
        if (std::fread(pair, sizeof(pair), 1, fp) != 1) break;

        char line[64];
        int len = std::snprintf(line, sizeof(line), "%010llx %016llx\n",
                                (unsigned long long)pair[0],
                                (unsigned long long)pair[1]);
        if (len <= 0 || used + (size_t)len + 1 >= out_len) break;
        std::memcpy(out + used, line, (size_t)len);
        used += (size_t)len;
    }
    out[used] = '\0';
    std::fclose(fp);
    return (int)n;
}

} /* extern "C" */
