function(cpf_discover_doctest_tests target)
   if (NOT TARGET ${target})
      message(FATAL_ERROR "cpf_discover_doctest_tests: target '${target}' does not exist")
   endif ()

   get_target_property(target_sources ${target} SOURCES)
   if (NOT target_sources)
      message(FATAL_ERROR "cpf_discover_doctest_tests: target '${target}' has no sources to inspect")
   endif ()

   set(discovered_test_cases)

   foreach (target_source IN LISTS target_sources)
      get_filename_component(source_extension ${target_source} EXT)
      if (NOT source_extension MATCHES "\\.(c|cc|cpp|cxx)$")
         continue()
      endif ()

      if (IS_ABSOLUTE "${target_source}")
         set(source_absolute ${target_source})
      else ()
         get_filename_component(source_absolute ${target_source} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
      endif ()

      if (NOT EXISTS ${source_absolute})
         continue()
      endif ()

      file(READ ${source_absolute} source_content)
      string(REGEX MATCHALL "TEST_CASE[ \t\r\n]*\\([ \t\r\n]*\"[^\"]+\"" source_test_cases "${source_content}")

      foreach (source_test_case IN LISTS source_test_cases)
         string(REGEX REPLACE "TEST_CASE[ \t\r\n]*\\([ \t\r\n]*\"([^\"]+)\"" "\\1" test_case_name "${source_test_case}")

         if ("${test_case_name}" IN_LIST discovered_test_cases)
            message(FATAL_ERROR "cpf_discover_doctest_tests: duplicate doctest test case '${test_case_name}' in target '${target}'")
         endif ()

         list(APPEND discovered_test_cases "${test_case_name}")

         add_test(
                 NAME "${target}::${test_case_name}"
                 COMMAND $<TARGET_FILE:${target}> "--test-case=${test_case_name}"
         )
         set_tests_properties("${target}::${test_case_name}" PROPERTIES LABELS ${target})
      endforeach ()
   endforeach ()

   if (discovered_test_cases STREQUAL "")
      message(FATAL_ERROR "cpf_discover_doctest_tests: no doctest TEST_CASE declarations found for target '${target}'")
   endif ()
endfunction()

