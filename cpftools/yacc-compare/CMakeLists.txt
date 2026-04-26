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

set_source_files_properties(${yacc_compare_parser_c} ${yacc_compare_parser_h} PROPERTIES GENERATED TRUE)

add_library(yacc_compare_yacc STATIC
        ${yacc_compare_parser_c}
        yacc_driver.c
)

set_target_properties(yacc_compare_yacc PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
)

target_include_directories(yacc_compare_yacc PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${yacc_compare_generated_dir}
)

add_executable(yacc_compare
        main.cpp
)

target_compile_definitions(yacc_compare PRIVATE
        CPF_YACC_COMPARE_CPF_GRAMMAR_PATH="${CMAKE_CURRENT_LIST_DIR}/fixtures/grammars/c_subset.cpf"
        CPF_YACC_COMPARE_FIXTURE_PATH="${CMAKE_CURRENT_LIST_DIR}/fixtures/programs/c_subset_fixture.c"
)

target_include_directories(yacc_compare PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(yacc_compare PRIVATE
        yacc_compare_yacc
        cpflib
        cpf_project_options
        cpf_project_warnings
)

if (BUILD_TESTING)
    add_test(NAME yacc_compare_smoke COMMAND yacc_compare --verify-only)
endif ()


