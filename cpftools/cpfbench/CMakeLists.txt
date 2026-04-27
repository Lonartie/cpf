include(cpf_benchmark_dependencies)

set(cpfbench_sources
    main.cpp
    calculator_benchmarks.cpp
)

cpf_make_benchmark_available()

add_executable(cpfbench ${cpfbench_sources})

target_compile_definitions(cpfbench PRIVATE
    CPF_BENCH_CALCULATOR_GRAMMAR_PATH="${CMAKE_CURRENT_LIST_DIR}/fixtures/grammars/calculator.cpf"
)

cpf_link_grammars(cpfbench
    NAMESPACE cpfbench::calculator
    ${CMAKE_CURRENT_LIST_DIR}/fixtures/grammars/calculator.cpf
)

target_include_directories(cpfbench PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cpfbench PRIVATE
    benchmark::benchmark
    cpf
    cpf_project_options
)
