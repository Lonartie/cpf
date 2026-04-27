set(cpf_sources
    analysis/complexity.cpp
    analysis/grammar_analysis.cpp
    model/grammar.cpp
    parser/grammar_loader.cpp
    parser/grammar_parser.cpp
    runtime/api.cpp
    runtime/dynamic.cpp
    runtime/runtime.cpp
)

add_library(cpf STATIC ${cpf_sources})

target_include_directories(cpf PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cpf
    PRIVATE
        cpf_project_options
        cpf_project_warnings
)

if (CPF_AUTO_BUILD_HEADER)
    # When cpf is built, also generate the single-header
    add_custom_command(TARGET cpf POST_BUILD
        COMMAND $<TARGET_FILE:cpfgenheader>
        COMMENT "Regenerating include/cpflib"
        VERBATIM
    )
endif ()
