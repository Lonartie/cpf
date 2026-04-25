add_executable(cpflibtests
        codegen/generator_tests.cpp
        parser/grammar_tests.cpp
        main.cpp
        runtime/runtime_tests.cpp
)

target_include_directories(cpflibtests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
)

target_link_libraries(cpflibtests PRIVATE
        cpflib
        cpfgenlib
        cpf_project_options
        cpf_project_warnings
)

if (BUILD_TESTING)
   cpf_discover_doctest_tests(cpflibtests)
endif ()

