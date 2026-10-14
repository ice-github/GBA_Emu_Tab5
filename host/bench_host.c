#include "bench.h"
#include <stdio.h>
#include <string.h>

static void emit_stdout(const char *line, void *ctx) {
    (void)ctx;
    puts(line);
}

int main(int argc, char **argv) {
    const char *config = "host-O2";
    const char *git_rev = "unknown";
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--config=", 9) == 0) config = argv[i] + 9;
        if (strncmp(argv[i], "--git-rev=", 10) == 0) git_rev = argv[i] + 10;
    }
    bench_run_all(emit_stdout, NULL, "host", config, git_rev, "none");
    return 0;
}
