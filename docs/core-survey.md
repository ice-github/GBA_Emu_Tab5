# GBA core candidate survey (M3)

Date accessed (all URLs): 2026-10-14 (UTC).
Status: document survey only. No core code vendored, no ROM/BIOS fetched.
Unmeasured performance/memory claims are marked **[hypothesis]**.

Target context: ESP32-P4 (RISC-V, no ARM/x86 dynarec reuse), 32 MB PSRAM
total (a 32 MB ROM cannot fully coexist with framebuffers/state),
ESP-IDF v5.5.4, user provides ROMs (never committed/fetched here).

Candidates: mGBA and gpSP (libretro fork + notaz original).

---

## 1. mGBA

- Upstream: https://github.com/mgba-emu/mgba (site: https://mgba.io/)
- License: **MPL-2.0**. Verified: `LICENSE` file at
  https://raw.githubusercontent.com/mgba-emu/mgba/master/LICENSE contains
  the full Mozilla Public License Version 2.0 text, and README
  "Copyright" section states "distributed under the Mozilla Public
  License version 2.0" (Copyright (c) 2013–2026 Jeffrey Pfau).
  Bundled third-party libs (inih/BSD-3, LZMA/public domain,
  MurmurHash3/public domain, SQLite3/public domain, etc.) listed in README.
- Core-vs-frontend layout (verified via GitHub contents API, `src/` and
  `include/mgba/` listings):
  - Portable core: `src/arm/` (CPU), `src/gba/` (PPU/APU/DMA/timers/memory/
    saves), `src/core/`, `src/gb/`, `src/sm83/`, `src/util/`,
    `src/debugger/`, `src/feature/`, `src/script/`;
    public headers under `include/mgba/{core,gb,gba,debugger,feature,…}`.
  - Frontends/platforms, one dir each under `src/platform/`:
    `qt`, `sdl`, `libretro`, `3ds`, `psp2` (Vita), `switch`, `wii`,
    `windows`, `posix`, plus `example` (minimal frontend sample),
    `test`, and `headless-main.c` (headless tool).
  - Build selection via CMake options (verified in root `CMakeLists.txt`):
    `BUILD_QT`, `BUILD_SDL`, `BUILD_LIBRETRO` (default OFF),
    `BUILD_HEADLESS`, `BUILD_TEST`, `BUILD_SUITE`, `BUILD_EXAMPLE`,
    `BUILD_STATIC`/`BUILD_SHARED`, `BUILD_GL`/`BUILD_GLES2`/`BUILD_GLES3`,
    `LIBMGBA_ONLY` (minimal core path).
- Interpreter availability: **yes, interpreter-only design — no dynarec
  to port**. `src/arm/` contains `arm.c`, `isa-arm.c`, `isa-thumb.c`,
  `decoder-arm.c`, `decoder-thumb.c`, `decoder.c` (verified listing);
  there are no per-ISA JIT backend directories (contrast gpSP's
  `arm/`/`mips/`/`x86/` stub dirs). The CPU is emulated in portable C,
  so it compiles for RISC-V wherever the C toolchain works.
  **[hypothesis]** throughput on ESP32-P4 is unmeasured; see §4.
- Approximate core memory needs (from source constants + GBA hardware
  sizes; static accounting, **not measured on device**):
  - `include/mgba/internal/gba/memory.h` defines (verified):
    BIOS 16 KB (`0x4000`), EWRAM 256 KB (`0x40000`),
    IWRAM 32 KB (`0x8000`), IO 1 KB, palette RAM 1 KB,
    VRAM 96 KB (`0x18000`), OAM 1 KB, ROM windows up to 32 MB,
    SRAM up to 32–64 KB, flash up to 128 KB, EEPROM up to 8 KB.
  - `src/gba/memory.c` (verified): EWRAM+IWRAM allocated together
    (`anonymousMemoryMap(GBA_SIZE_EWRAM + GBA_SIZE_IWRAM)` ≈ 288 KB);
    **ROM is mapped whole** (`memory->rom`, `romSize`) — a 32 MB ROM
    means ~32 MB resident by default. No demand-paging layer was found.
  - Rule of thumb: system RAM ≈ 288 KB + 96 KB VRAM + small IO/palette/
    OAM (~100 KB) + video/audio/frontend buffers; ROM adds its full
    file size on top. Worst case ≈ 32 MB ROM + ~0.5–2 MB
    **[hypothesis — needs on-device measurement; code size, audio
    buffers, savestates, and framebuffer count not yet accounted]**.
    One 240×160 RGB565 frame is ~75 KB.
- BIOS/HLE situation: **built-in HLE BIOS, external BIOS optional**.
  Verified: `src/gba/hle-bios.c` embeds a 16 KB `hleBios[GBA_SIZE_BIOS]`
  image (plus `hle-bios.s` sources), alongside `src/gba/bios.c`;
  README features list states "A built-in BIOS implementation, and
  ability to load external BIOS files." No BIOS file is required to boot.
- ESP-IDF / RISC-V portability notes:
  - POSIX: README "Dependencies" states mGBA has **no hard
    dependencies**; every listed dep (Qt/SDL/zlib/libpng/ffmpeg/SQLite/
    Lua/…) is optional and disables its feature when absent. CMake +
    GCC/Clang supported (both have RISC-V backends).
  - `src/platform/example/` + `headless-main.c` give a minimal,
    non-Qt/non-SDL bring-up path; `BUILD_TEST`/`BUILD_SUITE` and
    `src/platform/test`, `src/gba/test` give a correctness-test story.
  - Existing constrained ports (`3ds`, `psp2`, `switch`, `wii`) prove
    the core separates from desktop OS services; `PORTING.md`
    (verified) requires new ports live under `src/platform/` with
    minimal tree-wide changes and prefers upstreaming.
  - Porting risks / to-verify on ESP32-P4: thread/OSAL abstraction fit
    to FreeRTOS, `anonymousMemoryMap`/`mappedMemoryMap` backend onto
    PSRAM (caps), file I/O for ROM streaming if full mapping is
    unaffordable, software-renderer path without OpenGL
    (`BUILD_GL` off), and CMake-under-IDF integration (IDF uses CMake,
    but toolchain/sysroot wiring is unproven here).
    All **[hypothesis — design review only, no build attempted]**.

## 2. gpSP (libretro fork + notaz original)

- Upstream URLs:
  - Libretro fork (primary candidate): https://github.com/libretro/gpsp
    ("gpSP for libretro", forked from notaz/gpsp; README credits
    notaz upstream and davidgfnet's fix/feature branch
    https://github.com/davidgfnet/gpsp).
  - notaz original: https://github.com/notaz/gpsp
    ("gpSP fork tuned for ARM Linux", 59 commits).
  - Original author release (Exophase, gameplaySP for PSP) readme text
    is preserved as `original_readme.txt` in the libretro fork.
- License: **GPL-2.0 family (strong copyleft)**. Verified: `COPYING` at
  https://raw.githubusercontent.com/libretro/gpsp/master/COPYING is the
  full GNU GPL Version 2 text; GitHub labels the repo GPL-2.0.
  Nuance/flat-out correction to the task brief's "GPL-2.0-only": source
  headers (e.g. `gba_memory.h`, verified) grant
  "either version 2 of the License, or (at your option) any later
  version", i.e. the file-level grant reads as **GPL-2.0-or-later**;
  either way it is strong copyleft, unlike mGBA's file-level MPL-2.0.
  (Flag only — not legal advice; see §3.)
- Dynarec target list with explicit RISC-V absence:
  - README (verified, `README.md` raw) states: "supports **x86/x64,
    ARMv6/7 and ARMv8 and MIPS (32 and 64 bits)**, for other platforms
    an interpreter is available (albeit slower)."
  - Repo top-level backend dirs (verified listing): `arm/`, `mips/`,
    `x86/` (+ `3ds/`, `jni/`, `libretro/`). **There is no `riscv/`
    backend.**
  - `Makefile`/`Makefile.common` (verified): `CPU_ARCH` takes only
    `x86_32 | arm | arm64 | mips`; per-stub assembly is
    `x86/x86_stub.S`, `arm/arm_stub.S`, `arm/arm64_stub.S`,
    `mips/mips_stub.S` — no RISC-V stub. `HAVE_DYNAREC` is set only for
    those targets; `cpu_threaded.c` (dynarec driver) compiles **only**
    under `HAVE_DYNAREC=1`.
  - Consequence for ESP32-P4: only the **interpreter fallback** runs —
    the fast path that justifies gpSP cannot be reused on RISC-V.
- Interpreter fallback: present but second-class (upstream's own words:
  "albeit slower"). Build wiring (`Makefile.common`, verified) always
  compiles `cpu.cc`, `video.cc`, `gba_memory.c`, `memmap.c`, `sound.c`,
  etc.; `cpu_threaded.c` is added only with a dynarec target.
- Memory model / ROM buffering (32 MB PSRAM implications):
  - Demand-paged ROM (verified in `gba_memory.c`): 32 KB pages;
    `gamepak_buffers[32]` of `gamepak_buffer_blocksize = 1 MB`
    (`1024*1024`, verified line 378); `ROM_BUFFER_SIZE` default **32**
    (`gpsp_config.h`, verified) → up to 32 MB resident worst case,
    shrinkable at compile time (precedents in `Makefile`: PS2 builds
    `-DROM_BUFFER_SIZE=16`, RS90 `-DROM_BUFFER_SIZE=4`).
  - `gamepak_must_swap()` reports when buffers < ROM size; overflow
    pages stream from storage via `filestream_*` (SD-card paging path
    exists by design — matches the original PSP page-swapping design
    in `readme.txt`: "ROMS are 'demand loaded'… page swapped out").
  - Resident system RAM (verified sizes in `gba_memory.h`): EWRAM
    **doubled** `256 KB × 2` (SMC-detection double buffer),
    IWRAM **doubled** `32 KB × 2`, VRAM 96 KB, BIOS 16 KB × 2
    (`bios_rom` + `open_gba_bios_rom`), backup buffer 128 KB
    (`gamepak_backup[1024*128]`), plus translation cache when dynarec
    is on (irrelevant on RISC-V) and JIT `mmap` machinery in
    `memmap.c` (needs an ESP-IDF executable-memory story — likely
    dead code on the interpreter path, **to verify**).
  - Fit sketch for 32 MB PSRAM: e.g. `ROM_BUFFER_SIZE=4` (4 MB ROM
    window) + ~0.8 MB doubled RAM + VRAM/BIOS/backup ≈ single-digit MB
    **[hypothesis — paging jitter, SD throughput, and audio/video
    buffer totals unmeasured]**.
- BIOS requirement situation:
  - Original (Exophase `readme.txt`, verified): **requires** an
    authentic `gba_bios.bin` (md5 `a860e8c0b6d573d191e4ec7db1b1e4f6`);
    "will make no effort to run without one".
  - Libretro fork: ships an **open-source BIOS replacement**,
    `bios/open_gba_bios.bin` (16 KB, verified present) built from
    `bios/source` (Normmatt/VBA-M/ReGBA lineage, GPL2 per
    `bios/README.md`, verified), linked via `bios_data.S`
    (`SOURCES_ASM`, verified); README "recommends using the original
    though" for accuracy. Prototype can boot without a user BIOS, with
    an accuracy caveat.

## 3. Comparison table for Tab5

| Axis | mGBA | gpSP (libretro fork) |
|---|---|---|
| Portability effort to RISC-V/IDF | Lower structural risk: portable-C interpreter, no JIT port needed; CMake feature flags; `example`/headless bring-up path; constrained-console ports exist. New `src/platform/*idf*` port still required (threads, mmap-on-PSRAM, file I/O, soft renderer) **[hypothesis]**. | Medium–high: dynarec (the point of gpSP) is unusable on RISC-V; left with the slow interpreter **plus** libretro-frontend coupling, stale codebase, and JIT/`mmap` code to stub out. Demand-paging already fits small RAM, but SD-paging latency on IDF is unproven. |
| License impact on this MIT repo (flag only, no legal advice) | MPL-2.0 (file-level copyleft): modified MPL files stay MPL with notices/source availability; proprietary/new files can stay MIT. Vendoring or submodule both workable; keep `LICENSE` + notices. | GPL (strong copyleft, headers grant 2.0-or-later): distributing firmware containing gpSP likely obligates corresponding-source disclosure for the whole linked work. Vendoring GPL code into this MIT repo relicenses the combined firmware in practice. Prefer submodule + written-source offer over copying files; get review before shipping. |
| Memory fit (32 MB PSRAM, 32 MB ROM worst case) | Default maps full ROM — 32 MB ROM ≈ 32 MB resident + ~0.5–2 MB **[hypothesis]**, does **not** fit with framebuffers/state. Needs a streaming/chunked-ROM change (new work). Small-ROM (< ~16 MB) prototypes fit trivially. | Demand paging designed for this: `ROM_BUFFER_SIZE` tunable (4 MB precedent exists) + SD-backed page faults; resident ≈ buffers + ~1 MB system/backup **[hypothesis]**. Better worst-case story, paid in jitter complexity. |
| Correctness-test story | Stronger: `BUILD_TEST`/`BUILD_SUITE`, `src/platform/test`, `src/gba/test`, GDB-remote debug support, high-accuracy reputation, active upstream. | Weaker: accuracy known lower (original readme: "very cycle inaccurate"); per-game `game_config.txt` idle-loop hacks; libretro `tests/` dir exists but upstream is low-activity; open-BIOS accuracy caveat. |
| Performance story | Interpreter accuracy costs CPU; ESP32-P4 headroom unknown — **must measure** (frameskip/audio off headless first). | Interpreter path is explicitly "slower" and accuracy hacks (idle-loop elimination) may be needed per game; dynarec rewrite is a listed TODO, not a plan. No free lunch on RISC-V. |
| Key risks | CPU budget miss on interpreter; ROM-streaming work; CMake/IDF + PSRAM-mmap integration unknowns. | Slow/inaccurate interpreter; libretro API surface to re-implement or carry; GPL compliance burden; bit-rot (toolchain/JIT assumptions). |

## 4. Recommendation

**Prototype mGBA first, headless-interpreter on device.**

Reasons:
1. It runs on RISC-V as-is (portable C, no JIT backend to write),
   while gpSP's advantage (dynarec) is structurally unavailable here.
2. Built-in HLE BIOS removes BIOS-file friction for early bring-up
   (user ROMs still required, never committed).
3. Cleaner core/frontend split (`example`, `headless-main.c`,
   `BUILD_LIBRETRO OFF` + `LIBMGBA_ONLY`) and a real test suite give a
   faster correctness loop than gpSP's per-game hacks.
4. MPL-2.0 is the lower-friction license for this MIT repo (still keep
   notices/source; flag only, not legal advice).
5. gpSP's memory-paging design is genuinely better for 32 MB ROMs, but
   that pays off only if the CPU story works — and on RISC-V both cores
   are interpreter-bound, where mGBA has the accuracy/test edge.

First prototype shape (proposal, not started): `BUILD_HEADLESS`-style
minimal IDF app, no audio, no LCD (frame counter + checksums over UART/
console), small ROMs first (< 16 MB), measure frames/s, heap high-water,
and per-frame cycle counts before any display/audio work.

**What would change the pick after real-ROM measurement:**
- mGBA interpreter misses the frame budget by a wide margin **and**
  gpSP-interpreter + frameskip gets meaningfully closer on the same
  ROMs (same harness, same clocks) → promote gpSP interpreter, or a
  hybrid (gpSP paging ideas applied to mGBA).
- Memory measurement shows full-ROM mapping is the binding constraint
  and a chunked-ROM mGBA change is costlier than carrying gpSP's pager
  → revisit gpSP or schedule mGBA streaming work.
- A RISC-V dynarec backend becomes available/credible (upstream or new
  work) → re-evaluate entirely; gpSP's TODO lists a dynarec rewrite,
  mGBA has no dynarec at all, so this favors a fresh look, not gpSP
  automatically.
- Accuracy testing (suite + known-good frames) shows the open-BIOS/
  interpreter path failing games mGBA passes → stays mGBA.

## Open questions (need device/data to close)

1. Real interpreter throughput of each core on ESP32-P4 (MHz-normalized,
   same ROM set) — the decision driver. **[unmeasured]**
2. Heap high-water for mGBA core + small ROM vs gpSP with
   `ROM_BUFFER_SIZE` ∈ {4, 8, 16}. **[unmeasured]**
3. SD-backed ROM page-fault latency/jitter under IDF for gpSP-style
   paging. **[unmeasured]**
4. mGBA `anonymousMemoryMap` on PSRAM with caps + IDF heap integration
   effort. **[hypothesis]**
5. gpSP libretro API surface actually needed vs re-implementing a thin
   native frontend. **[unreviewed]**
6. GPL compliance posture if gpSP is ever shipped (source offer,
   submodule vs vendor). **Needs reviewer input; not legal advice.**
7. Per-game compatibility delta (HLE/open BIOS vs external BIOS) on our
   target ROM set — user-provided ROMs only. **[untested]**
