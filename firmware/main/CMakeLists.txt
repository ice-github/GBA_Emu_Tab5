set(main_srcs "bench_main.c")
# `esp_lcd`, `esp_driver_ledc` and `esp_driver_i2c` are needed by the
# GBA_DISPLAY_TEST panel bring-up. They are listed unconditionally on purpose:
# ESP-IDF resolves component REQUIRES in an early-expansion pass that does not
# see project build options, so a conditional REQUIRES is silently dropped (and
# the including source then fails to find the header). The Tab5 BSP already
# brings these components into the build, so this adds no components, and
# --gc-sections keeps them out of the image when unused.
set(main_requires bench mgba_port fatfs esp_driver_gpio
                  esp_driver_sdmmc sdmmc mbedtls esp_timer
                  esp_lcd esp_driver_ledc esp_driver_i2c gba_scale
                  esp_driver_ppa)
set(main_defs "")

# Per-frame CRC verification in the stream test. ON (default) checks every
# emulated framebuffer against the host vector; OFF (-D
# GBA_STREAM_VERIFY_CRC=OFF) skips the checksum so the pure
# run/convert/present/wait pipeline can be timed. Correctness must be
# established by a verifying run first; a non-verifying run proves nothing
# about pixels.
option(GBA_STREAM_VERIFY_CRC
    "Verify per-frame framebuffer CRCs in GBA_DISPLAY_STREAM_TEST" ON)

# mGBA idle-loop handling in the stream test. OFF (default) keeps the
# long-standing REMOVE setting (which, with idleLoop NONE, never halts, so
# busy-wait loops burn full cycles). ON (-D GBA_IDLE_DETECT=ON) switches the
# core to DETECT, letting it skip busy-wait loops. This changes how many host
# cycles emulate a frame, so a run with this ON must pass the same CRC gate;
# a CRC mismatch here means the game idles in a way DETECT mis-handles.
option(GBA_IDLE_DETECT
    "Use mGBA idle-loop detection (DETECT) in GBA_DISPLAY_STREAM_TEST" OFF)

# 64 B-aligned PSRAM ROM image allocation. ON (default): use
# heap_caps_aligned_alloc(64, ...) so SDMMC multi-block DMA can target the
# buffer directly instead of single-sector DMA+memcpy bounces (measured
# 11.49 vs 1.70 MiB/s on GS1 8 MiB, identical SHA-256). OFF keeps the
# long-standing heap_caps_malloc (~4 B aligned) for comparison.
# Startup-only effect.
option(GBA_ALIGNED_ROM_ALLOC
    "Allocate the PSRAM ROM image 64 B-aligned for SD DMA" ON)

# Renderer timing shim (drawScanline/finishFrame wall time) in the stream
# test. OFF (default) leaves the renderer untouched; ON installs in-place
# trampolines around the frame loop and logs totals. Observational only:
# CRCs must match the OFF run.
option(GBA_RENDER_TIMING
    "Time drawScanline/finishFrame in GBA_DISPLAY_STREAM_TEST" OFF)

# Frame accounting (emulated-DMA bytes/calls + audio synthesis time) in the
# stream test. OFF (default) leaves memory accessors and the audio event
# untouched; ON wraps them and logs totals. The per-memop branch adds cost,
# so compare against an unwrapped run and keep CRCs matching.
option(GBA_FRAME_ACCOUNT
    "Count DMA bytes/calls and time audio in GBA_DISPLAY_STREAM_TEST" OFF)

# At most one optional firmware test mode may be selected at a time.
set(_modes GBA_CORE_INIT_TEST GBA_SD_PROBE_TEST GBA_SD_ROM_LOAD_TEST
           GBA_SD_FRAME_TEST GBA_SD_PERF_TEST GBA_DISPLAY_TEST
           GBA_DISPLAY_ROM_TEST GBA_DISPLAY_STREAM_TEST
           GBA_DISPLAY_PPA_STREAM_TEST GBA_DISPLAY_PPA_OVERLAP_TEST
           GBA_DISPLAY_PPA_ASYNC_TEST
           GBA_DISPLAY_ANIM_TEST
           GBA_PPA_BENCH_TEST)
set(_selected "")
foreach(_mode ${_modes})
    if(${_mode})
        list(APPEND _selected ${_mode})
    endif()
endforeach()
list(LENGTH _selected _selected_count)
if(_selected_count GREATER 1)
    message(FATAL_ERROR
        "Select only one GBA firmware test mode (got: ${_selected})")
endif()

# ROM selection for the SD/ROM modes. The name is turned into
# GBA_ROM_PROFILE_<NAME>=1, which firmware/main/gba_rom_profile.h requires; that
# header carries the ROM path/size/SHA-256 and the per-frame CRC vector.
set(GBA_ROM_PROFILE "gs1" CACHE STRING
    "ROM and verification vector used by the SD/ROM modes (gs1|ffta)")
string(TOUPPER "${GBA_ROM_PROFILE}" _rom_profile_upper)
if(NOT _rom_profile_upper MATCHES "^(GS1|FFTA)$")
    message(FATAL_ERROR
        "GBA_ROM_PROFILE must be gs1 or ffta (got '${GBA_ROM_PROFILE}')")
endif()
# Every mode that includes gba_sd.h (see GBA_HAS_SD_PATHS in bench_main.c) needs
# a profile, because that header pulls in the ROM path/size/SHA-256/CRC vector.
set(_rom_modes GBA_SD_PROBE_TEST GBA_SD_ROM_LOAD_TEST GBA_SD_FRAME_TEST
               GBA_SD_PERF_TEST GBA_DISPLAY_ROM_TEST GBA_DISPLAY_STREAM_TEST
               GBA_DISPLAY_PPA_STREAM_TEST GBA_DISPLAY_PPA_OVERLAP_TEST
               GBA_DISPLAY_PPA_ASYNC_TEST)
set(_rom_mode_selected FALSE)
foreach(_mode ${_rom_modes})
    if(${_mode})
        set(_rom_mode_selected TRUE)
    endif()
endforeach()
if(_rom_mode_selected)
    list(APPEND main_defs GBA_ROM_PROFILE_${_rom_profile_upper}=1)
endif()

if(GBA_CORE_INIT_TEST)
    list(APPEND main_defs
        MGBA_CORE_INIT_TEST=1
        MGBA_CORE_INIT_ITERATIONS=20)
endif()

if(GBA_SD_PROBE_TEST)
    list(APPEND main_defs GBA_SD_PROBE_TEST=1)
endif()

if(GBA_SD_ROM_LOAD_TEST)
    list(APPEND main_defs GBA_SD_ROM_LOAD_TEST=1)
endif()

if(GBA_SD_FRAME_TEST OR GBA_SD_PERF_TEST)
    if(NOT GBA_SD_FRAME_COUNT MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "GBA_SD_FRAME_COUNT must be a positive integer")
    endif()
endif()

if(GBA_SD_FRAME_TEST)
    list(APPEND main_defs
        GBA_SD_FRAME_TEST=1
        GBA_SD_FRAME_COUNT=${GBA_SD_FRAME_COUNT})
endif()

if(GBA_SD_PERF_TEST)
    list(APPEND main_defs
        GBA_SD_PERF_TEST=1
        GBA_SD_FRAME_COUNT=${GBA_SD_FRAME_COUNT})
endif()

if(GBA_DISPLAY_TEST)
    list(APPEND main_srcs display_test.c)
    list(APPEND main_defs GBA_DISPLAY_TEST=1)
endif()

if(GBA_DISPLAY_ROM_TEST)
    list(APPEND main_srcs display_test.c display_rom_test.c)
    list(APPEND main_defs GBA_DISPLAY_ROM_TEST=1)
endif()

if(GBA_DISPLAY_STREAM_TEST)
    list(APPEND main_srcs display_test.c display_rom_test.c)
    list(APPEND main_defs GBA_DISPLAY_STREAM_TEST=1)
    if(NOT GBA_STREAM_VERIFY_CRC)
        list(APPEND main_defs GBA_STREAM_NO_VERIFY=1)
    endif()
    if(GBA_IDLE_DETECT)
        list(APPEND main_defs GBA_IDLE_DETECT=1)
    endif()
    if(GBA_RENDER_TIMING)
        list(APPEND main_defs GBA_RENDER_TIMING=1)
    endif()
    if(GBA_FRAME_ACCOUNT)
        list(APPEND main_defs GBA_FRAME_ACCOUNT=1)
    endif()
endif()

if(GBA_DISPLAY_PPA_STREAM_TEST)
    list(APPEND main_srcs display_test.c display_rom_test.c)
    list(APPEND main_defs GBA_DISPLAY_PPA_STREAM_TEST=1)
    if(NOT GBA_STREAM_VERIFY_CRC)
        list(APPEND main_defs GBA_STREAM_NO_VERIFY=1)
    endif()
    if(GBA_IDLE_DETECT)
        list(APPEND main_defs GBA_IDLE_DETECT=1)
    endif()
    if(GBA_RENDER_TIMING)
        list(APPEND main_defs GBA_RENDER_TIMING=1)
    endif()
    if(GBA_FRAME_ACCOUNT)
        list(APPEND main_defs GBA_FRAME_ACCOUNT=1)
    endif()
endif()

# Explicitly experimental overlap mode. Keep the serialized PPA stream as the
# default/reference; this option only moves the next CPU frame across scanout.
if(GBA_DISPLAY_PPA_OVERLAP_TEST)
    list(APPEND main_srcs display_test.c display_rom_test.c)
    list(APPEND main_defs
        GBA_DISPLAY_PPA_STREAM_TEST=1
        GBA_DISPLAY_PPA_OVERLAP_TEST=1)
    if(NOT GBA_STREAM_VERIFY_CRC)
        list(APPEND main_defs GBA_STREAM_NO_VERIFY=1)
    endif()
    if(GBA_IDLE_DETECT)
        list(APPEND main_defs GBA_IDLE_DETECT=1)
    endif()
    if(GBA_RENDER_TIMING)
        list(APPEND main_defs GBA_RENDER_TIMING=1)
    endif()
    if(GBA_FRAME_ACCOUNT)
        list(APPEND main_defs GBA_FRAME_ACCOUNT=1)
    endif()
endif()

# Experimental async PPA pipeline: snapshot the emulated framebuffer, enqueue
# a non-blocking transform, and run the following CPU frame before waiting for
# completion. Serialized and overlap modes remain unchanged references.
if(GBA_DISPLAY_PPA_ASYNC_TEST)
    list(APPEND main_srcs display_test.c display_rom_test.c)
    list(APPEND main_defs
        GBA_DISPLAY_PPA_STREAM_TEST=1
        GBA_DISPLAY_PPA_ASYNC_TEST=1)
    if(NOT GBA_STREAM_VERIFY_CRC)
        list(APPEND main_defs GBA_STREAM_NO_VERIFY=1)
    endif()
endif()

if(GBA_ALIGNED_ROM_ALLOC)
    list(APPEND main_defs GBA_ALIGNED_ROM_ALLOC=1)
endif()

if(GBA_DISPLAY_ANIM_TEST)
    if(NOT GBA_DISPLAY_ANIM_FRAMES MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "GBA_DISPLAY_ANIM_FRAMES must be a positive integer")
    endif()
    list(APPEND main_srcs display_test.c)
    list(APPEND main_defs
        GBA_DISPLAY_ANIM_TEST=1
        GBA_DISPLAY_ANIM_FRAMES=${GBA_DISPLAY_ANIM_FRAMES})
endif()

if(GBA_PPA_BENCH_TEST)
    list(APPEND main_srcs display_test.c display_ppa_test.c)
    list(APPEND main_defs GBA_PPA_BENCH_TEST=1)
endif()

idf_component_register(SRCS ${main_srcs} REQUIRES ${main_requires})

# mGBA internal headers (struct GBA for the idle-loop hook below). Upstream
# sources stay untouched; only our own file includes them.
target_include_directories(${COMPONENT_LIB} PRIVATE
    ../../third_party/mgba/include
    ../../third_party/mgba/src)

if(main_defs)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${main_defs})
endif()
