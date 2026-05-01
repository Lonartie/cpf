file(GLOB_RECURSE CPP_FILES "*.cpp")

foreach (CPP_FILE IN LISTS CPP_FILES)
    # file base name
    get_filename_component(BASE_NAME ${CPP_FILE} NAME_WE)

    # complete folder path of file
    get_filename_component(FOLDER_PATH ${CPP_FILE} PATH)

    set(TARGET_NAME "cpfexample_${BASE_NAME}")

    add_executable(${TARGET_NAME} ${CPP_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE
            cpf
            cpf_project_options
            cpf_project_warnings
    )

    set(CPF_FILE "${FOLDER_PATH}/${BASE_NAME}.cpf")
    if (EXISTS ${CPF_FILE})
        cpf_link_grammars(${TARGET_NAME} ${CPF_FILE})
    endif ()

    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    set_tests_properties("${TARGET_NAME}" PROPERTIES LABELS "cpfexamples")
endforeach ()