cmake_minimum_required(VERSION 3.24)
project(cpf LANGUAGES CXX)

include(CheckIPOSupported)
include(CTest)

option(CPF_BUILD_BENCHMARKS "Build cpfbench parser benchmarks" OFF)
option(CPF_BUILD_YACC_COMPARE "Build the yacc-vs-CPF comparison lab" OFF)
option(CPF_ENABLE_IPO "Enable interprocedural optimization when supported" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CPF_ENABLE_IPO)
    check_ipo_supported(RESULT cpf_ipo_supported OUTPUT cpf_ipo_output)
    if (cpf_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    else ()
        message(STATUS "Interprocedural optimization disabled: ${cpf_ipo_output}")
    endif ()
endif ()

add_library(cpf_project_options INTERFACE)
target_compile_features(cpf_project_options INTERFACE cxx_std_20)

add_library(cpf_project_warnings INTERFACE)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(cpf_project_warnings INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
    )
elseif (MSVC)
    target_compile_options(cpf_project_warnings INTERFACE
            /W4
            /WX
            /permissive-
    )
endif ()

add_subdirectory(cpflib)
add_subdirectory(cpfgen)
add_subdirectory(cpftools)
