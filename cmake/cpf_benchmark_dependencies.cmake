include_guard(GLOBAL)

include(FetchContent)

function(cpf_make_benchmark_available)
    if (TARGET benchmark::benchmark)
        return()
    endif ()

    FetchContent_Declare(cpm
        URL https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.42.1/CPM.cmake
        DOWNLOAD_NO_EXTRACT TRUE
    )
    FetchContent_MakeAvailable(cpm)
    include(${cpm_SOURCE_DIR}/CPM.cmake)

    CPMAddPackage(
        NAME benchmark
        GITHUB_REPOSITORY google/benchmark
        VERSION 1.9.4
        OPTIONS
            "BENCHMARK_ENABLE_GTEST_TESTS OFF"
            "BENCHMARK_ENABLE_INSTALL OFF"
            "BENCHMARK_ENABLE_TESTING OFF"
            "BENCHMARK_ENABLE_WERROR OFF"
    )

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
        if (TARGET benchmark)
            target_compile_options(benchmark PRIVATE -Wno-error=c2y-extensions -Wno-c2y-extensions)
        endif ()
        if (TARGET benchmark_main)
            target_compile_options(benchmark_main PRIVATE -Wno-error=c2y-extensions -Wno-c2y-extensions)
        endif ()
    endif ()
endfunction()

