add_executable(cpfgenheader main.cpp)

target_compile_definitions(cpfgenheader PRIVATE
    CPFGENHEADER_DEFAULT_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
    CPFGENHEADER_DEFAULT_OUTPUT_PATH="${PROJECT_SOURCE_DIR}/include/cpflib"
)

target_link_libraries(cpfgenheader PRIVATE
    cpf_project_options
    cpf_project_warnings
)

if (CPF_AUTO_BUILD_HEADER)
    add_custom_command(TARGET cpfgenheader POST_BUILD
        COMMAND $<TARGET_FILE:cpfgenheader>
        COMMENT "Regenerating include/cpflib"
        VERBATIM
    )
endif ()