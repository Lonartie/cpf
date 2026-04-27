cmake_minimum_required(VERSION 3.24)
project(cpf LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(CTest)
include(cpf)
include(cpf_project_options)
include(doctest)

option(CPF_BUILD_BENCHMARKS "Build cpfbench parser benchmarks" OFF)
option(CPF_BUILD_YACC_COMPARE "Build the yacc-vs-CPF comparison lab" OFF)
option(CPF_ENABLE_IPO "Enable interprocedural optimization when supported" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cpf_enable_ipo_if_supported()
cpf_add_project_targets()

add_subdirectory(cpf)
add_subdirectory(cpfgen)
add_subdirectory(cpftools)
