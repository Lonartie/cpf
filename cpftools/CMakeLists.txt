add_subdirectory(cpfgenheader)

if (CPF_BUILD_TESTS)
    add_subdirectory(cpftests)
endif()

if (CPF_BUILD_BENCHMARKS)
    add_subdirectory(cpfbench)
endif ()

if (CPF_BUILD_COMPARE)
    add_subdirectory(cpfcompare)
endif ()
