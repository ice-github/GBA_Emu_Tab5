CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BENCH_SRCS := $(ROOT)/components/bench/bench_core.c \
  $(ROOT)/components/bench/cases_mem.c \
  $(ROOT)/components/bench/cases_memplacement.c \
  $(ROOT)/components/bench/cases_branch.c \
  $(ROOT)/components/bench/cases_pixel.c \
  $(ROOT)/components/bench/cases_scale.c \
  $(ROOT)/components/bench/cases_rot45x.c \
  $(ROOT)/components/bench/cases_rotsplit.c \
  $(ROOT)/components/bench/cases_store.c \
  $(ROOT)/components/gba_scale/gba_scale.c \
  $(ROOT)/host/bench_host.c
BENCH_INC := -I$(ROOT)/components/bench/include \
  -I$(ROOT)/components/gba_scale/include

SCALE_SRCS := $(ROOT)/components/gba_scale/gba_scale.c \
  $(ROOT)/host/scale_test.c
SCALE_INC := -I$(ROOT)/components/gba_scale/include

all: bench_host scale_test

bench_host: $(BENCH_SRCS)
	$(CC) $(CFLAGS) $(BENCH_INC) -o $@ $(BENCH_SRCS)

# gba-smoke links the host mGBA build (see benchmarks/README.md).
# Configure once, e.g.:
#   cmake -S third_party/mgba -B /tmp/opencode/mgba-host -G Ninja \
#     -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
#     -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_TEST=OFF -DBUILD_SUITE=OFF \
#     -DBUILD_LIBRETRO=OFF -DUSE_FFMPEG=OFF -DUSE_LUA=OFF -DUSE_SQLITE3=OFF
MGBA_BUILD ?= /tmp/opencode/mgba-host

smoke: gba-smoke.c
	$(CC) $(CFLAGS) -I$(ROOT)/third_party/mgba/include \
	  -I$(MGBA_BUILD)/include -o gba-smoke \
	  gba-smoke.c -L$(MGBA_BUILD) -lmgba \
	  -Wl,-rpath,$(MGBA_BUILD)

# RGB565 smoke: color_t MUST match the core build (uint16_t). The device
# port uses this configuration; never compare its CRCs with native32.
MGBA_BUILD_565 ?= /tmp/opencode/mgba-host-565

smoke565: gba-smoke.c
	$(CC) $(CFLAGS) -DCOLOR_16_BIT -DCOLOR_5_6_5 \
	  -I$(ROOT)/third_party/mgba/include \
	  -I$(MGBA_BUILD_565)/include -o gba-smoke-565 \
	  gba-smoke.c -L$(MGBA_BUILD_565) -lmgba \
	  -Wl,-rpath,$(MGBA_BUILD_565)

scale_test: $(SCALE_SRCS)
	$(CC) $(CFLAGS) $(SCALE_INC) -o $@ $(SCALE_SRCS)

check: bench_host scale_test
	./bench_host --config=host-check --git-rev=local | python3 $(ROOT)/tools/bench/validate.py
	./scale_test

clean:
	rm -f bench_host gba-smoke gba-smoke-565 scale_test

.PHONY: all check clean
