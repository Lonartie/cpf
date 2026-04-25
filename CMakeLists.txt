cmake_minimum_required(VERSION 3.24)
project(cpf LANGUAGES CXX)

include(CTest)

option(CPF_BUILD_BENCHMARKS "Build cpfbench parser benchmarks" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

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
