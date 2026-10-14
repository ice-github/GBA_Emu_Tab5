idf_component_register(SRCS "bench_core.c" "cases_mem.c" "cases_branch.c"
                            "cases_pixel.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_timer)
