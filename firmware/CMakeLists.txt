cmake_minimum_required(VERSION 3.16)

# Tab5 GBA bench app skeleton (M1 firmware side, not yet device-verified).
# Build with: . ~/esp/esp-idf-v5.5.4/export.sh && idf.py -C firmware build
option(GBA_CORE_INIT_TEST "Build the silent mGBA init/deinit test app" OFF)
option(GBA_SD_PROBE_TEST "Build the no-format read-only SD probe app" OFF)
option(GBA_SD_ROM_LOAD_TEST "Build the silent SD ROM load/unload test app" OFF)
option(GBA_SD_FRAME_TEST "Build the silent SD ROM frame CRC test app" OFF)
option(GBA_SD_PERF_TEST "Build the silent SD ROM frame timing test app" OFF)
option(GBA_DISPLAY_TEST "Build the ROM-free fixed-image panel test app" OFF)
option(GBA_DISPLAY_ROM_TEST "Build the verified still-frame panel test app" OFF)
option(GBA_DISPLAY_STREAM_TEST "Build the continuous frame display test app" OFF)
option(GBA_DISPLAY_PPA_ASYNC_TEST "Build the experimental async-PPA stream test" OFF)
option(GBA_DISPLAY_ANIM_TEST "Build the ROM-free animated pattern test app" OFF)
option(GBA_ARMCORE_INTERNAL "Allocate mGBA ARMCore in internal DRAM (A/B only)" OFF)
set(GBA_DISPLAY_ANIM_FRAMES "600" CACHE STRING "Frames for the ROM-free animated display test")
set(GBA_SD_FRAME_COUNT "1" CACHE STRING "Frames to execute in SD frame test modes")

set(EXTRA_COMPONENT_DIRS ../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gba_bench)
