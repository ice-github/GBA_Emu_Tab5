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

check: bench_host
	./bench_host --config=host-check --git-rev=local | python3 $(ROOT)/tools/bench/validate.py

clean:
	rm -f bench_host

.PHONY: all check clean
