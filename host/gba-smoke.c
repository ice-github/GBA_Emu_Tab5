/* gba-smoke: run a GBA ROM headless for N frames with fixed input.
 * Prints one JSON line per sampled frame:
 *   {"frame":N,"fb_crc32":"........"}
 * plus a final summary line. Core execution time and CRC time are measured
 * separately ("run_ns_total" vs "crc_ns_total") so hashing never pollutes
 * the emulation throughput number.
 * Timing model: mGBA advances 280896 cycles per frame (~59.7275fps).
 * Same mCore API shape the Tab5 firmware port uses
 * (find -> init -> config -> load -> video buffer -> reset -> runFrame).
 * Usage: gba-smoke ROM.gba [--frames=N] [--sample-every=K]
 */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba/gba/core.h>
#include <mgba-util/crc32.h>

#define FB_W 240
#define FB_H 160

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void discard_mgba_log(struct mLogger* logger, int category,
                             enum mLogLevel level, const char* format,
                             va_list args) {
    (void)logger;
    (void)category;
    (void)level;
    (void)format;
    (void)args;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ROM.gba [--frames=N] [--sample-every=K]\n",
                argv[0]);
        return 2;
    }
    long frames = 600, sample_every = 60;
    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--frames=", 9) == 0)
            frames = atol(argv[i] + 9);
        else if (strncmp(argv[i], "--sample-every=", 15) == 0)
            sample_every = atol(argv[i] + 15);
        else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 2;
        }
    }
    if (frames < 1 || sample_every < 1) {
        fprintf(stderr, "frames and sample-every must be >= 1\n");
        return 2;
    }

    struct mLogger logger = {.log = discard_mgba_log, .filter = NULL};
    mLogSetDefaultLogger(&logger);
    struct mCore* core = mCoreFind(argv[1]);
    if (!core) {
        fprintf(stderr, "mCoreFind failed\n");
        return 1;
    }
    core->init(core);
    mCoreInitConfig(core, "gbaSmoke");
    /* Fixed, deterministic config: HLE BIOS, no frameskip. */
    mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");

    static color_t fb[FB_W * FB_H];
    if (!mCoreLoadFile(core, argv[1])) {
        fprintf(stderr, "load failed: %s\n", argv[1]);
        return 1;
    }
    core->setVideoBuffer(core, fb, FB_W);
    core->reset(core);
    /* No input pressed for the whole run (deterministic). */
    core->clearKeys(core, 0xFFFFFFFFu);

    uint64_t run_ns = 0, crc_ns = 0;
    for (long f = 1; f <= frames; f++) {
        uint64_t t0 = now_ns();
        core->runFrame(core);
        uint64_t t1 = now_ns();
        run_ns += t1 - t0;
        if (f % sample_every == 0) {
            uint64_t c0 = now_ns();
            uint32_t crc = doCrc32(fb, sizeof(fb));
            uint64_t c1 = now_ns();
            crc_ns += c1 - c0;
            printf("{\"frame\":%ld,\"fb_crc32\":\"%08x\"}\n", f, crc);
            fflush(stdout);
        }
    }
    double run_s = (double)run_ns / 1e9;
    printf("{\"summary\":true,\"frames\":%ld,\"run_s\":%.3f,"
           "\"crc_s\":%.3f,\"fps\":%.2f,\"realtime_x\":%.3f}\n",
           frames, run_s, (double)crc_ns / 1e9, (double)frames / run_s,
           ((double)frames / run_s) / 59.7275);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    return 0;
}
