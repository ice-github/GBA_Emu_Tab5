#include "bench.h"
#include <stdint.h>

/* Interpreter-dispatch-like workload: a computed-goto-free threaded loop
   over a fixed pseudorandom opcode stream. Models branch-mispredict-heavy
   CPU emulation dispatch without any GBA semantics. */

#define DISPATCH_OPS 4096
static uint8_t s_ops[DISPATCH_OPS];

static uint32_t s_branch_init_done;

static void branch_data_init(void) {
    if (!s_branch_init_done) {
        uint32_t x = 0x243f6a88u;
        for (int i = 0; i < DISPATCH_OPS; i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            s_ops[i] = (uint8_t)(x & 15u);
        }
        s_branch_init_done = 1;
    }
}

static uint32_t run_dispatch(uint32_t ops) {
    if (ops > DISPATCH_OPS) ops = DISPATCH_OPS;
    /* Fixed seeds per call: no accumulated state, so repeated calls with
       the same input produce the same token (verifiable). */
    uint32_t r0 = 0x12345678u, r1 = 0x9e3779b9u, r2 = 0x85ebca6bu,
             r3 = 0x27d4eb2du;
    for (uint32_t i = 0; i < ops; i++) {
        switch (s_ops[i]) {
            case 0: r0 += r1 + (uint32_t)i; break;
            case 1: r1 ^= r0 + 0x9e3779b9u; break;
            case 2: r2 += (r0 >> 3) | (r1 << 7); break;
            case 3: r3 = r3 * 33u + r2; break;
            case 4: r0 = (r0 << 5) | (r0 >> 27); break;
            case 5: r1 += r2 ^ r3; break;
            case 6: r2 ^= (uint32_t)i * 0x85ebca6bu; break;
            case 7: r3 += r0 - r1; break;
            case 8: r0 ^= r3 + (r2 | 1u); break;
            case 9: r1 = (r1 << 11) ^ (r1 >> 5); break;
            case 10: r2 += r1 + r0; break;
            case 11: r3 ^= r2 - (uint32_t)i; break;
            case 12: r0 += r2 * 3u + 1u; break;
            case 13: r1 ^= r0 >> 13; break;
            case 14: r2 = r2 + (r3 << 1); break;
            default: r3 ^= (uint32_t)i + r0; break;
        }
    }
    return r0 ^ r1 ^ r2 ^ r3;
}

/* Memory-map-like workload: masked address decode per access, cycling
   EWRAM/IWRAM/palette/VRAM/ROM-ish regions like a real GBA bus map. */
static EXT_RAM_BSS_ATTR uint8_t s_mapmem[64 * 1024];

static const uint32_t kMapBases[] = {
    0x02000000u, /* EWRAM 256K */
    0x03000000u, /* IWRAM 32K */
    0x05000000u, /* palette 1K */
    0x06000000u, /* VRAM 96K */
    0x08000000u, /* ROM */
};
static const uint32_t kMapMasks[] = {
    0x3ffffu, 0x7fffu, 0x3ffu, 0x1ffffu, 0x1ffffffu,
};
#define MAP_REGIONS \
    (sizeof(kMapBases) / sizeof(kMapBases[0]))

static uint32_t run_map_decode(uint32_t ops) {
    if (ops > 8192) ops = 8192;
    uint32_t acc = 0;
    for (uint32_t i = 0; i < ops; i++) {
        uint32_t sel = (i * 2654435761u >> 16) % MAP_REGIONS;
        uint32_t addr = kMapBases[sel] + ((i * 40503u) & kMapMasks[sel]);
        uint32_t region = (addr >> 24) & 0x0fu;
        uint32_t off;
        switch (region) {
            case 0x2: off = addr & 0x3ffffu; break;      /* EWRAM 256K */
            case 0x3: off = (addr & 0x7fffu) + 0x40000u; break; /* IWRAM */
            case 0x6: off = (addr & 0x17fffu) + 0x48000u; break; /* VRAM */
            default: off = (addr & 0xffffu); break;
        }
        acc += s_mapmem[off & (sizeof(s_mapmem) - 1)] + (uint8_t)i;
    }
    return acc;
}

static const bench_case_t kCases[] = {
    {"dispatch_4k", "16-way dispatch over 4k ops", "ops/s", "dram", 10, 100,
     run_dispatch, run_dispatch, DISPATCH_OPS},
    {"map_decode_8k", "region decode over 8k accesses, 5 regions", "ops/s",
     "psram", 10, 100, run_map_decode, run_map_decode, 8192},
};

void bench_cases_branch_register(void) {
    branch_data_init();
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++)
        bench_register(&kCases[i]);
}
