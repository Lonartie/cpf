include(${CMAKE_CURRENT_LIST_DIR}/cmake/cpf.cmake)

add_library(cpflib INTERFACE)

target_include_directories(cpflib INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(cpflib INTERFACE
        cpf_project_options
)

