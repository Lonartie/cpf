file(GLOB_RECURSE CPP_FILES "*.cpp")

foreach (CPP_FILE IN LISTS CPP_FILES)
    get_filename_component(BASE_NAME ${CPP_FILE} NAME_WE)
    set(TARGET_NAME "cpfexample_${BASE_NAME}")
    add_executable(${TARGET_NAME} ${CPP_FILE})
    target_link_libraries(${TARGET_NAME} PRIVATE
            cpf_project_options
            cpf_project_warnings
    )

    set(CPF_FILE "${CMAKE_CURRENT_LIST_DIR}/${BASE_NAME}.cpf")
    if (EXISTS ${CPF_FILE})
        cpf_link_grammars(${TARGET_NAME} ${CPF_FILE})
    endif ()

    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
endforeach ()