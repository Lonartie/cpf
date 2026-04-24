function(cpf_link_grammars target)
   if (NOT TARGET ${target})
      message(FATAL_ERROR "cpf_link_grammars: target '${target}' does not exist")
   endif ()

   if (NOT TARGET cpfgen)
      message(FATAL_ERROR "cpf_link_grammars: target 'cpfgen' must exist before linking grammars")
   endif ()

   set(grammar_files ${ARGN})
   if (grammar_files STREQUAL "")
      message(FATAL_ERROR "cpf_link_grammars: expected at least one .cpf grammar for target '${target}'")
   endif ()

   set(generated_directory ${CMAKE_CURRENT_BINARY_DIR}/cpf_link_grammars/${target})
   set(generated_sources)
   set(generated_headers)
   set(grammar_stems)

   foreach (grammar_file IN LISTS grammar_files)
      get_filename_component(grammar_absolute ${grammar_file} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
      if (NOT EXISTS ${grammar_absolute})
         message(FATAL_ERROR "cpf_link_grammars: grammar '${grammar_file}' does not exist")
      endif ()

      get_filename_component(grammar_extension ${grammar_absolute} EXT)
      if (NOT grammar_extension STREQUAL ".cpf")
         message(FATAL_ERROR "cpf_link_grammars: grammar '${grammar_file}' must use the .cpf extension")
      endif ()

      get_filename_component(grammar_stem ${grammar_absolute} NAME_WE)
      if (grammar_stem IN_LIST grammar_stems)
         message(FATAL_ERROR "cpf_link_grammars: duplicate grammar stem '${grammar_stem}' for target '${target}'")
      endif ()
      list(APPEND grammar_stems ${grammar_stem})

      set(generated_header ${generated_directory}/${grammar_stem}.h)
      set(generated_source ${generated_directory}/${grammar_stem}.cpp)

      add_custom_command(
              OUTPUT ${generated_header} ${generated_source}
              COMMAND ${CMAKE_COMMAND} -E make_directory ${generated_directory}
              COMMAND $<TARGET_FILE:cpfgen> ${grammar_absolute} ${generated_directory}
              DEPENDS cpfgen ${grammar_absolute}
              VERBATIM
      )

      list(APPEND generated_headers ${generated_header})
      list(APPEND generated_sources ${generated_source})
   endforeach ()

   set_source_files_properties(${generated_headers} ${generated_sources} PROPERTIES GENERATED TRUE)

   add_dependencies(${target} cpfgen)
   target_sources(${target} PRIVATE ${generated_sources})
   target_include_directories(${target} PUBLIC ${generated_directory})
endfunction()

