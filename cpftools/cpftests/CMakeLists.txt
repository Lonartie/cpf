set(cpftests_sources
    attributes/attribute_tests.cpp
    codegen/cpfgen_cli_tests.cpp
    codegen/cpfgenheader_tests.cpp
    codegen/generator_tests.cpp
    main.cpp
    parser/grammar_tests.cpp
    runtime/complexity_tests.cpp
    runtime/dynamic_runtime_tests.cpp
    runtime/library_runtime_tests.cpp
    runtime/runtime_tests.cpp
)

set(cpftests_grammar_files
    fixtures/grammars/calculator.cpf
    fixtures/grammars/grouped.cpf
    fixtures/grammars/imported_bundle.cpf
    fixtures/grammars/lookahead.cpf
    fixtures/grammars/lexer_priority.cpf
    fixtures/grammars/message.cpf
    fixtures/grammars/merged_definitions.cpf
    fixtures/grammars/templates.cpf
    fixtures/grammars/tokens.cpf
    fixtures/grammars/quantified.cpf
    fixtures/grammars/attr_prec_numeric.cpf
    fixtures/grammars/attr_prec_labels.cpf
    fixtures/grammars/attr_dir.cpf
    fixtures/grammars/default_attrs.cpf
    fixtures/grammars/default_labels.cpf
    fixtures/grammars/ambiguous_choice.cpf
    fixtures/grammars/error_choice.cpf
    fixtures/grammars/custom_errors.cpf
)

add_executable(cpftests ${cpftests_sources})

add_executable(cpfheadertests ${cpftests_sources})
target_sources(cpfheadertests PRIVATE
    single_header/cpflib_implementation.cpp
)

target_compile_definitions(cpftests PRIVATE
    CPFGEN_EXECUTABLE_PATH="$<TARGET_FILE:cpfgen>"
    CPFGENHEADER_EXECUTABLE_PATH="$<TARGET_FILE:cpfgenheader>"
    CPF_SOURCE_ROOT_PATH="${PROJECT_SOURCE_DIR}"
    CPF_SINGLE_HEADER_PATH="${PROJECT_SOURCE_DIR}/include/cpflib"
)

target_compile_definitions(cpfheadertests PRIVATE
    CPFGEN_EXECUTABLE_PATH="$<TARGET_FILE:cpfgen>"
    CPFGENHEADER_EXECUTABLE_PATH="$<TARGET_FILE:cpfgenheader>"
    CPF_SOURCE_ROOT_PATH="${PROJECT_SOURCE_DIR}"
    CPF_SINGLE_HEADER_PATH="${PROJECT_SOURCE_DIR}/include/cpflib"
)

add_dependencies(cpftests cpfgen cpfgenheader)
add_dependencies(cpfheadertests cpfgen cpfgenheader)

cpf_link_grammars(cpftests ${cpftests_grammar_files})

cpf_link_grammars(cpfheadertests ${cpftests_grammar_files})

cpf_link_grammars(cpftests
    NAMESPACE generated::fixtures
    fixtures/grammars/namespaced_calculator.cpf
)

cpf_link_grammars(cpfheadertests
    NAMESPACE generated::fixtures
    fixtures/grammars/namespaced_calculator.cpf
)

target_include_directories(cpftests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
)

target_include_directories(cpfheadertests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
)

target_link_libraries(cpftests PRIVATE
    cpfgenlib
    cpf_project_options
    cpf_project_warnings
)

target_link_libraries(cpfheadertests PRIVATE
    cpfgenlib
    cpf_project_options
    cpf_project_warnings
)

if (BUILD_TESTING)
    cpf_discover_doctest_tests(cpftests)
    cpf_discover_doctest_tests(cpfheadertests)
endif ()
