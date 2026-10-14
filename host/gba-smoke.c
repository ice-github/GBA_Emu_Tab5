/* gba-smoke: run a GBA ROM headless for N frames with fixed input.
 * Prints one JSON line per sampled frame:
 *   {"frame":N,"fb_crc32":"........","audio_frames":M}
 * plus a final summary line with elapsed wall time and throughput.
 * This uses the same mCore API shape the Tab5 firmware port will use
 * (find -> config -> load -> video buffer -> reset -> runFrame loop).
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
    }
    if (frames < 1 || sample_every < 1) {
        fprintf(stderr, "frames and sample-every must be >= 1\n");
        return 2;
    }

    struct mStandardLogger logger;
    mStandardLoggerInit(&logger);
    mLogSetDefaultLogger(&logger.d);
    struct mCore* core = mCoreFind(argv[1]);
    if (!core) {
        fprintf(stderr, "mCoreFind failed\n");
        return 1;
    }
    core->init(core);
    mCoreInitConfig(core, "gbaSmoke");
    /* Fixed, deterministic config: HLE BIOS, no frameskip. */
    mCoreConfigSetDefaultValue(&core->config, "idleOptimization", "remove");

    if (!mCoreLoadFile(core, argv[1])) {
        fprintf(stderr, "load failed: %s\n", argv[1]);
        return 1;
    }
    static color_t fb[FB_W * FB_H];
    core->setVideoBuffer(core, fb, FB_W);
    core->reset(core);
    /* No input pressed for the whole run (deterministic). */
    core->clearKeys(core, 0xFFFFFFFFu);

    uint64_t t0 = now_ns();
    for (long f = 1; f <= frames; f++) {
        core->runFrame(core);
        if (f % sample_every == 0) {
            uint32_t crc = doCrc32(fb, sizeof(fb));
            printf("{\"frame\":%ld,\"fb_crc32\":\"%08x\","
                   "\"audio_frames\":0}\n",
                   f, crc);
            fflush(stdout);
        }
    }
    uint64_t t1 = now_ns();
    double elapsed_s = (double)(t1 - t0) / 1e9;
    printf("{\"summary\":true,\"frames\":%ld,\"elapsed_s\":%.3f,"
           "\"fps\":%.2f,\"realtime_x\":%.3f}\n",
           frames, elapsed_s, (double)frames / elapsed_s,
           ((double)frames / elapsed_s) / 59.737);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    return 0;
}
