add_library(cpfgenlib STATIC
        analysis/grammar_analysis.cpp
        analysis/rule_complexity.cpp
        codegen/code_generator.cpp
        parser/grammar_loader.cpp
        parser/grammar_parser.cpp
        model/grammar.cpp
)

target_include_directories(cpfgenlib PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cpfgenlib PUBLIC
        cpflib
        cpf_project_options
)

target_link_libraries(cpfgenlib PRIVATE
        cpf_project_warnings
)

add_executable(cpfgen
        main.cpp
)

target_include_directories(cpfgen PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cpfgen PRIVATE
        cpfgenlib
        cpf_project_options
        cpf_project_warnings
)

