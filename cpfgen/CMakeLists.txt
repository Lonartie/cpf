set(cpfgenlib_sources
    analysis/rule_complexity.cpp
    codegen/code_generator.cpp
)

add_library(cpfgenlib STATIC ${cpfgenlib_sources})

target_include_directories(cpfgenlib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cpfgenlib
    PUBLIC
        cpf
        cpf_project_options
    PRIVATE
        cpf_project_warnings
)

add_executable(cpfgen main.cpp)

target_include_directories(cpfgen PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cpfgen PRIVATE
    cpfgenlib
    cpf_project_options
    cpf_project_warnings
)
