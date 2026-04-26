include(${CMAKE_CURRENT_LIST_DIR}/support/doctest.cmake)

add_subdirectory(cpflibtests)
add_subdirectory(cpftests)

if (CPF_BUILD_BENCHMARKS)
    add_subdirectory(cpfbench)
endif ()

if (CPF_BUILD_YACC_COMPARE)
    add_subdirectory(yacc-compare)
endif ()

