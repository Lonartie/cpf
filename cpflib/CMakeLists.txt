include(${CMAKE_CURRENT_LIST_DIR}/cmake/cpf.cmake)

add_library(cpflib STATIC
        analysis/complexity.cpp
        analysis/grammar_analysis.cpp
        model/grammar.cpp
        parser/grammar_loader.cpp
        parser/grammar_parser.cpp
        runtime/api.cpp
        runtime/dynamic.cpp
        runtime/runtime.cpp
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

