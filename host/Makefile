CC ?= gcc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BENCH_SRCS := $(ROOT)/components/bench/bench_core.c \
  $(ROOT)/components/bench/cases_mem.c \
  $(ROOT)/components/bench/cases_branch.c \
  $(ROOT)/components/bench/cases_pixel.c \
  $(ROOT)/host/bench_host.c
BENCH_INC := -I$(ROOT)/components/bench/include

all: bench_host

bench_host: $(BENCH_SRCS)
	$(CC) $(CFLAGS) $(BENCH_INC) -o $@ $(BENCH_SRCS)

# gba-smoke links the host mGBA build (see benchmarks/README.md).
# Configure once, e.g.:
#   cmake -S third_party/mgba -B /tmp/opencode/mgba-host -G Ninja \
#     -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
#     -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_TEST=OFF -DBUILD_SUITE=OFF \
#     -DBUILD_LIBRETRO=OFF -DUSE_FFMPEG=OFF -DUSE_LUA=OFF -DUSE_SQLITE3=OFF
MGBA_BUILD ?= /tmp/opencode/mgba-host

smoke: host/gba-smoke.c
	$(CC) $(CFLAGS) -I$(ROOT)/third_party/mgba/include \
	  -I$(MGBA_BUILD)/include -o /tmp/opencode/gba-smoke \
	  host/gba-smoke.c -L$(MGBA_BUILD) -lmgba \
	  -Wl,-rpath,$(MGBA_BUILD)

check: bench_host
	./bench_host --config=host-check --git-rev=local | python3 $(ROOT)/tools/bench/validate.py

clean:
	rm -f bench_host

.PHONY: all check clean
