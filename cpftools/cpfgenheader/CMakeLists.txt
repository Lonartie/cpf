add_executable(cpfgenheader main.cpp)

file(GLOB_RECURSE cpf_single_header_inputs CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/cpf/*"
)

add_custom_command(
    OUTPUT ${PROJECT_SOURCE_DIR}/include/cpflib
    COMMAND $<TARGET_FILE:cpfgenheader>
    DEPENDS cpfgenheader ${cpf_single_header_inputs}
    COMMENT "Regenerating include/cpflib"
    VERBATIM
)

add_custom_target(cpf_single_header DEPENDS ${PROJECT_SOURCE_DIR}/include/cpflib)
add_dependencies(cpf_single_header cpf)

target_compile_definitions(cpfgenheader PRIVATE
    CPFGENHEADER_DEFAULT_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
    CPFGENHEADER_DEFAULT_OUTPUT_PATH="${PROJECT_SOURCE_DIR}/include/cpflib"
)

target_link_libraries(cpfgenheader PRIVATE
    cpf_project_options
    cpf_project_warnings
)