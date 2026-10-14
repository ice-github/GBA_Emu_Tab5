cmake_minimum_required(VERSION 3.16)

# Tab5 GBA bench app skeleton (M1 firmware side, not yet device-verified).
# Build with: . ~/esp/esp-idf-v5.5.4/export.sh && idf.py -C firmware build
set(EXTRA_COMPONENT_DIRS ../components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gba_bench)
