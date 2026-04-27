include_guard(GLOBAL)

function(cpf_link_grammars target)
    if (NOT TARGET ${target})
        message(FATAL_ERROR "cpf_link_grammars: target '${target}' does not exist")
    endif ()

    if (NOT TARGET cpfgen)
        message(FATAL_ERROR "cpf_link_grammars: target 'cpfgen' must exist before linking grammars")
    endif ()

    cmake_parse_arguments(CPF_LINK_GRAMMARS "" "NAMESPACE" "" ${ARGN})
    if (CPF_LINK_GRAMMARS_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "cpf_link_grammars: missing values for arguments: ${CPF_LINK_GRAMMARS_KEYWORDS_MISSING_VALUES}")
    endif ()

    if (DEFINED CPF_LINK_GRAMMARS_NAMESPACE AND NOT CPF_LINK_GRAMMARS_NAMESPACE MATCHES "^[_A-Za-z][_A-Za-z0-9]*(::[_A-Za-z][_A-Za-z0-9]*)*$")
        message(FATAL_ERROR "cpf_link_grammars: namespace '${CPF_LINK_GRAMMARS_NAMESPACE}' is not a valid C++ namespace")
    endif ()

    set(grammar_files ${CPF_LINK_GRAMMARS_UNPARSED_ARGUMENTS})
    if (grammar_files STREQUAL "")
        message(FATAL_ERROR "cpf_link_grammars: expected at least one .cpf grammar for target '${target}'")
    endif ()

    set(generated_directory ${CMAKE_CURRENT_BINARY_DIR}/cpf_link_grammars/${target})
    set(generated_sources)
    set(generated_headers)
    set(grammar_stems)

    foreach (grammar_file IN LISTS grammar_files)
        get_filename_component(grammar_absolute "${grammar_file}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if (NOT EXISTS "${grammar_absolute}")
            message(FATAL_ERROR "cpf_link_grammars: grammar '${grammar_file}' does not exist")
        endif ()

        get_filename_component(grammar_extension "${grammar_absolute}" EXT)
        if (NOT grammar_extension STREQUAL ".cpf")
            message(FATAL_ERROR "cpf_link_grammars: grammar '${grammar_file}' must use the .cpf extension")
        endif ()

        get_filename_component(grammar_stem "${grammar_absolute}" NAME_WE)
        if (grammar_stem IN_LIST grammar_stems)
            message(FATAL_ERROR "cpf_link_grammars: duplicate grammar stem '${grammar_stem}' for target '${target}'")
        endif ()
        list(APPEND grammar_stems ${grammar_stem})

        set(generated_header ${generated_directory}/${grammar_stem}.h)
        set(generated_source ${generated_directory}/${grammar_stem}.cpp)
        set(generated_depfile ${generated_directory}/${grammar_stem}.d)
        set(cpfgen_command $<TARGET_FILE:cpfgen> ${grammar_absolute} ${generated_directory})
        if (DEFINED CPF_LINK_GRAMMARS_NAMESPACE AND NOT CPF_LINK_GRAMMARS_NAMESPACE STREQUAL "")
            list(APPEND cpfgen_command --namespace ${CPF_LINK_GRAMMARS_NAMESPACE})
        endif ()
        list(APPEND cpfgen_command --depfile ${generated_depfile})

        add_custom_command(
            OUTPUT ${generated_header} ${generated_source}
            DEPFILE ${generated_depfile}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${generated_directory}
            COMMAND ${cpfgen_command}
            DEPENDS cpfgen ${grammar_absolute}
            VERBATIM
        )

        list(APPEND generated_headers ${generated_header})
        list(APPEND generated_sources ${generated_source})
    endforeach ()

    set_source_files_properties(${generated_headers} ${generated_sources} PROPERTIES GENERATED TRUE)

    get_property(cpf_link_grammars_call_count GLOBAL PROPERTY CPF_LINK_GRAMMARS_CALL_COUNT)
    if (NOT DEFINED cpf_link_grammars_call_count OR cpf_link_grammars_call_count STREQUAL "CPF_LINK_GRAMMARS_CALL_COUNT-NOTFOUND")
        set(cpf_link_grammars_call_count 0)
    endif ()

    math(EXPR cpf_link_grammars_call_count "${cpf_link_grammars_call_count} + 1")
    set_property(GLOBAL PROPERTY CPF_LINK_GRAMMARS_CALL_COUNT ${cpf_link_grammars_call_count})

    set(cpf_link_grammars_target ${target}_cpf_grammars_${cpf_link_grammars_call_count})
    add_custom_target(${cpf_link_grammars_target}
        DEPENDS ${generated_headers} ${generated_sources}
    )

    add_dependencies(${target} cpfgen ${cpf_link_grammars_target})
    target_sources(${target} PRIVATE ${generated_headers} ${generated_sources})
    target_include_directories(${target} PUBLIC ${generated_directory})
endfunction()

