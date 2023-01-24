add_compiler_rt_component(heap_boobytraps)
add_compiler_rt_component(hardened_heap_boobytraps)
add_compiler_rt_component(boobytraps)

set(HEAPBOOBYTRAPS_SOURCES
        heap_boobytraps.c
        heapbt_common.c
        )

set(HARDENEDHEAPBOOBYTRAPS_SOURCES
        hardened_heap_boobytraps.c
        heapbt_common.c
        )

set(BOOBYTRAPS_SOURCES
        boobytraps.c
        )


include_directories(..)

foreach(arch ${ALL_R2C_SUPPORTED_ARCH})
    message(STATUS "Building boobytraps RT library for ${arch}")
    add_compiler_rt_runtime(clang_rt.boobytraps
            STATIC
            ARCHS ${arch}
            SOURCES ${BOOBYTRAPS_SOURCES}
            CFLAGS ${BOOBYTRAPS_CFLAGS}
            PARENT_TARGET boobytraps)

    message(STATUS "Building heap boobytrap RT library for ${arch}")
    add_compiler_rt_runtime(clang_rt.heap_boobytraps
            STATIC
            ARCHS ${arch}
            SOURCES ${HEAPBOOBYTRAPS_SOURCES}
            CFLAGS ${HEAPBOOBYTRAPS_CFLAGS}
            PARENT_TARGET heap_boobytraps)

    message(STATUS "Building hardened heap boobytrap RT library for ${arch}")
    add_compiler_rt_runtime(clang_rt.hardened_heap_boobytraps
            STATIC
            ARCHS ${arch}
            SOURCES ${HARDENEDHEAPBOOBYTRAPS_SOURCES}
            CFLAGS ${HARDENEDHEAPBOOBYTRAPS_CFLAGS}
            PARENT_TARGET hardened_heap_boobytraps)
endforeach()
