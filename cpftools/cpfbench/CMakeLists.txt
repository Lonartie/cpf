include(FetchContent)

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

target_compile_options(benchmark PRIVATE -Wno-error=c2y-extensions -Wno-c2y-extensions)
target_compile_options(benchmark_main PRIVATE -Wno-error=c2y-extensions -Wno-c2y-extensions)

add_executable(cpfbench
        main.cpp
        calculator_benchmarks.cpp
        simple_c_benchmarks.cpp
)

cpf_link_grammars(cpfbench
        NAMESPACE cpfbench::calculator
        ${CMAKE_CURRENT_LIST_DIR}/fixtures/grammars/calculator.cpf
)

cpf_link_grammars(cpfbench
        NAMESPACE cpfbench::simple_c
        ${CMAKE_CURRENT_LIST_DIR}/fixtures/grammars/simple_c.cpf
)

target_include_directories(cpfbench PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cpfbench PRIVATE
        benchmark::benchmark
        cpflib
        cpf_project_options
)

