idf_component_register(SRCS "bench_main.c"
                       REQUIRES bench mgba_port fatfs esp_driver_gpio
                                esp_driver_sdmmc sdmmc mbedtls esp_timer)

if(GBA_CORE_INIT_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        MGBA_CORE_INIT_TEST=1
        MGBA_CORE_INIT_ITERATIONS=20)
endif()

if(GBA_SD_PROBE_TEST AND GBA_CORE_INIT_TEST)
    message(FATAL_ERROR "Select only one GBA firmware test mode")
endif()

if(GBA_SD_ROM_LOAD_TEST AND (GBA_CORE_INIT_TEST OR GBA_SD_PROBE_TEST))
    message(FATAL_ERROR "Select only one GBA firmware test mode")
endif()

if(GBA_SD_FRAME_TEST AND (GBA_CORE_INIT_TEST OR GBA_SD_PROBE_TEST OR
                          GBA_SD_ROM_LOAD_TEST OR GBA_SD_PERF_TEST))
    message(FATAL_ERROR "Select only one GBA firmware test mode")
endif()

if(GBA_SD_PERF_TEST AND (GBA_CORE_INIT_TEST OR GBA_SD_PROBE_TEST OR
                         GBA_SD_ROM_LOAD_TEST))
    message(FATAL_ERROR "Select only one GBA firmware test mode")
endif()

if(GBA_SD_PROBE_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GBA_SD_PROBE_TEST=1)
endif()

if(GBA_SD_ROM_LOAD_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE GBA_SD_ROM_LOAD_TEST=1)
endif()

if(GBA_SD_FRAME_TEST)
    if(NOT GBA_SD_FRAME_COUNT MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "GBA_SD_FRAME_COUNT must be a positive integer")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        GBA_SD_FRAME_TEST=1
        GBA_SD_FRAME_COUNT=${GBA_SD_FRAME_COUNT})
endif()

if(GBA_SD_PERF_TEST)
    if(NOT GBA_SD_FRAME_COUNT MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "GBA_SD_FRAME_COUNT must be a positive integer")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        GBA_SD_PERF_TEST=1
        GBA_SD_FRAME_COUNT=${GBA_SD_FRAME_COUNT})
endif()
