enable_language(C)

find_program(CPF_YACC_EXECUTABLE NAMES bison yacc REQUIRED)

set(yacc_compare_generated_dir ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(yacc_compare_parser_c ${yacc_compare_generated_dir}/c_subset_parser.c)
set(yacc_compare_parser_h ${yacc_compare_generated_dir}/c_subset_parser.h)
set(yacc_compare_grammar ${CMAKE_CURRENT_LIST_DIR}/fixtures/grammars/c_subset.y)

add_custom_command(
    OUTPUT ${yacc_compare_parser_c} ${yacc_compare_parser_h}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${yacc_compare_generated_dir}
    COMMAND ${CPF_YACC_EXECUTABLE}
            --defines=${yacc_compare_parser_h}
            --output=${yacc_compare_parser_c}
            ${yacc_compare_grammar}
    DEPENDS ${yacc_compare_grammar}
    VERBATIM
)

set_source_files_properties(
    ${yacc_compare_parser_c}
    ${yacc_compare_parser_h}
    PROPERTIES GENERATED TRUE
)

add_executable(cpfcompare
    main.cpp
    yacc_driver.c
    ${yacc_compare_parser_c}
)

set_target_properties(cpfcompare PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

target_compile_definitions(cpfcompare PRIVATE
    CPF_YACC_COMPARE_FIXTURE_PATH="${CMAKE_CURRENT_LIST_DIR}/fixtures/programs/c_subset_fixture.c"
    CPF_YACC_COMPARE_GRAMMAR_PATH="${CMAKE_CURRENT_LIST_DIR}/fixtures/grammars/c_subset.cpf"
)

target_include_directories(cpfcompare PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${yacc_compare_generated_dir}
)

cpf_link_grammars(cpfcompare
    NAMESPACE cpfyacc::c_subset
    ${CMAKE_CURRENT_LIST_DIR}/fixtures/grammars/c_subset.cpf
)

target_link_libraries(cpfcompare PRIVATE
    cpf
    cpf_project_options
    cpf_project_warnings
)

add_test(NAME yacc_compare_smoke COMMAND cpfcompare --verify-only)