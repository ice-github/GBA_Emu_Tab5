# mGBA core port for ESP32-P4 (IDF). Grounded in upstream CMake
# (third_party/mgba @ 0.10.5): this is the MINIMAL_CORE=2 set used by the
# libretro target (CORE_SRC with M_CORE_GB off, no SIO/EXTRA/debuggers),
# minus renderers/gl.c (needs OpenGL) and minus test/ trees.
#
# Included groups and their upstream source (export_directory names):
#   ARM_SRC  <- src/arm/CMakeLists.txt SOURCE_FILES (no debugger/ subdir)
#   GBA_SRC  <- src/gba/CMakeLists.txt SOURCE_FILES (no TEST/DEBUGGER/SIO/EXTRA)
#   CORE     <- src/core/CMakeLists.txt SOURCE_FILES (no scripting: needs lua)
#   UTIL     <- src/util BASE_SOURCE_FILES + SOURCE_FILES (no GUI)
#   VFS      <- CORE_VFS_SRC non-Windows branch (mem/fifo/fd/dirent)
#   OS       <- src/platform/posix/memory.c REPLACED by memory_psram.c
#   THIRD    <- inih/*.c + blip_buf.c (root CMake THIRD_PARTY_SRC)
#   version.c generated from src/core/version.c.in (fixed values, no git)
#
# Compile definitions mirror the libretro minimal target:
#   MINIMAL_CORE=2, DISABLE_THREADING, MGBA_STANDALONE,
#   COLOR_16_BIT + COLOR_5_6_5 (Tab5 panel is RGB565; avoids XRGB convert),
#   M_CORE_GBA (GB core off).

set(MGBA_DIR ${CMAKE_CURRENT_LIST_DIR}/../../third_party/mgba)

set(MGBA_ARM
    src/arm/arm.c
    src/arm/decoder-arm.c
    src/arm/decoder.c
    src/arm/decoder-thumb.c
    src/arm/isa-arm.c
    src/arm/isa-thumb.c)

set(MGBA_GBA
    src/gb/audio.c
    src/gba/audio.c
    src/gba/bios.c
    src/gba/cart/ereader.c
    src/gba/cart/gpio.c
    src/gba/cart/matrix.c
    src/gba/cart/vfame.c
    src/gba/cheats.c
    src/gba/cheats/codebreaker.c
    src/gba/cheats/gameshark.c
    src/gba/cheats/parv3.c
    src/gba/core.c
    src/gba/dma.c
    src/gba/gba.c
    src/gba/hle-bios.c
    src/gba/input.c
    src/gba/io.c
    src/gba/memory.c
    src/gba/overrides.c
    src/gba/renderers/cache-set.c
    src/gba/renderers/common.c
    src/gba/renderers/software-bg.c
    src/gba/renderers/software-mode0.c
    src/gba/renderers/software-obj.c
    src/gba/renderers/video-software.c
    src/gba/savedata.c
    src/gba/serialize.c
    src/gba/sharkport.c
    src/gba/sio.c
    src/gba/sio/gbp.c
    src/gba/sio/joybus.c
    src/gba/timer.c
    src/gba/video.c)

set(MGBA_CORE
    src/core/bitmap-cache.c
    src/core/cache-set.c
    src/core/cheats.c
    src/core/config.c
    src/core/core.c
    src/core/directories.c
    src/core/input.c
    src/core/interface.c
    src/core/library.c
    src/core/lockstep.c
    src/core/log.c
    src/core/map-cache.c
    src/core/mem-search.c
    src/core/rewind.c
    src/core/serialize.c
    src/core/sync.c
    src/core/thread.c
    src/core/tile-cache.c
    src/core/timing.c)
    # NOTE: src/core/version.c is upstream-generated; replaced below by our
    # static version.c with pinned values.

set(MGBA_UTIL
    src/util/circle-buffer.c
    src/util/configuration.c
    src/util/crc32.c
    src/util/formatting.c
    src/util/gbk-table.c
    src/util/hash.c
    src/util/string.c
    src/util/table.c
    src/util/vfs.c
    src/util/convolve.c
    src/util/elf-read.c
    src/util/export.c
    src/util/patch.c
    src/util/patch-fast.c
    src/util/patch-ips.c
    src/util/patch-ups.c
    src/util/png-io.c
    src/util/ring-fifo.c
    src/util/text-codec.c
    src/util/vfs/vfs-mem.c
    src/util/vfs/vfs-fifo.c
    src/util/vfs/vfs-fd.c
    src/util/vfs/vfs-dirent.c)

set(MGBA_THIRD
    src/third-party/inih/ini.c
    src/third-party/blip_buf/blip_buf.c)

# version.c is upstream-generated; pin fixed values (no git at build time).
# Kept as a static file (see version.c) instead of configure_file so the
# component has no configure-time dependency on the submodule state.

set(MGBA_SRCS ${MGBA_ARM} ${MGBA_GBA} ${MGBA_CORE} ${MGBA_UTIL}
              ${MGBA_THIRD})
list(TRANSFORM MGBA_SRCS PREPEND ${MGBA_DIR}/)
list(APPEND MGBA_SRCS ${CMAKE_CURRENT_LIST_DIR}/version.c
                      ${CMAKE_CURRENT_LIST_DIR}/memory_psram.c
                      ${CMAKE_CURRENT_LIST_DIR}/mgba_render_timing.c)

idf_component_register(SRCS ${MGBA_SRCS}
                       INCLUDE_DIRS ../../third_party/mgba/include include
                       PRIV_REQUIRES heap log esp_timer)

# Upstream-style includes: core sources include sibling dirs as
# "gba/cheats/gameshark.h" (rooted at src/).
target_include_directories(${COMPONENT_LIB} PRIVATE ${MGBA_DIR}/src)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    MINIMAL_CORE=2
    DISABLE_THREADING
    MGBA_STANDALONE
    COLOR_16_BIT
    COLOR_5_6_5
    M_CORE_GBA)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    # Feature detection upstream performs via CMake find_function; the
    # riscv newlib provides these (verified in libc.a).
    HAVE_LOCALE
    HAVE_STRTOF_L
    HAVE_LOCALTIME_R)

# A/B experiment only: keep the hottest CPU state in internal DRAM while ROM,
# GBA board state and bulk emulated memory remain PSRAM-backed.
if(GBA_ARMCORE_INTERNAL)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GBA_ARMCORE_INTERNAL=1)
endif()

# Third-party code spelling: riscv newlib uses `long` for 32-bit types, so
# -Wformat flags upstream logging verbatim (same width, benign). gb/audio.c
# trips -Wmaybe-uninitialized the same way it does on some GCC versions
# upstream. Both are downgraded (not disabled) for this component only.
# -Wformat-truncation fires on upstream core/log.c only at -O2 and above
# (provably-null '%s' path the -Og analysis does not see); same treatment.
# -Wstringop-truncation fires on upstream util/string.c's strncpy+explicit-NUL
# idiom at -O2; likewise benign, likewise downgraded here, not upstream.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format
    -Wno-error=maybe-uninitialized
    -Wno-error=char-subscripts
    -Wno-error=format-truncation
    -Wno-error=stringop-truncation)
