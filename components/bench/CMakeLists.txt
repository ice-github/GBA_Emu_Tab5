idf_component_register(SRCS "bench_core.c" "cases_mem.c" "cases_memplacement.c" "cases_branch.c"
                            "cases_pixel.c" "cases_scale.c" "cases_rot45x.c"
                            "cases_rotsplit.c" "cases_store.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_timer gba_scale)
