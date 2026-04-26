add_executable(cpflibtests
        codegen/cpfgen_cli_tests.cpp
        codegen/generator_tests.cpp
        parser/grammar_tests.cpp
        main.cpp
        runtime/complexity_tests.cpp
        runtime/dynamic_runtime_tests.cpp
        runtime/runtime_tests.cpp
)

target_include_directories(cpflibtests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
)

target_compile_definitions(cpflibtests PRIVATE
        CPFGEN_EXECUTABLE_PATH="$<TARGET_FILE:cpfgen>"
)

add_dependencies(cpflibtests cpfgen)

target_link_libraries(cpflibtests PRIVATE
        cpfgenlib
        cpf_project_options
        cpf_project_warnings
)

if (BUILD_TESTING)
    cpf_discover_doctest_tests(cpflibtests)
endif ()

