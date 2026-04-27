set(cpftests_sources
    attributes/attribute_tests.cpp
    main.cpp
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

cpf_link_grammars(cpftests ${cpftests_grammar_files})

cpf_link_grammars(cpftests
    NAMESPACE generated::fixtures
    fixtures/grammars/namespaced_calculator.cpf
)

target_include_directories(cpftests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..
)

target_link_libraries(cpftests PRIVATE
    cpf
    cpf_project_options
    cpf_project_warnings
)

if (BUILD_TESTING)
    cpf_discover_doctest_tests(cpftests)
endif ()
