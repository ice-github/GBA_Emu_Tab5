cmake_minimum_required(VERSION 3.16)

# Tab5 GBA bench app skeleton (M1 firmware side, not yet device-verified).
# Build with: . ~/esp/esp-idf-v5.5.4/export.sh && idf.py -C firmware build
option(GBA_CORE_INIT_TEST "Build the silent mGBA init/deinit test app" OFF)
option(GBA_SD_PROBE_TEST "Build the no-format read-only SD probe app" OFF)
option(GBA_SD_ROM_LOAD_TEST "Build the silent SD ROM load/unload test app" OFF)
option(GBA_SD_FRAME_TEST "Build the silent SD ROM frame CRC test app" OFF)
option(GBA_SD_PERF_TEST "Build the silent SD ROM frame timing test app" OFF)
set(GBA_SD_FRAME_COUNT "1" CACHE STRING "Frames to execute in SD frame test modes")

set(EXTRA_COMPONENT_DIRS ../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gba_bench)
