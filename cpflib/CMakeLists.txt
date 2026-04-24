include(${CMAKE_CURRENT_LIST_DIR}/cmake/cpf.cmake)

add_library(cpflib STATIC
        codegen/code_generator.cpp
        parser/grammar_parser.cpp
        model/grammar.cpp
)

target_include_directories(cpflib PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cpflib PUBLIC
        cpf_project_options
)

target_link_libraries(cpflib PRIVATE
        cpf_project_warnings
)
