include_guard(GLOBAL)

include(CheckIPOSupported)

function(cpf_enable_ipo_if_supported)
    if (NOT CPF_ENABLE_IPO)
        return()
    endif ()

    check_ipo_supported(RESULT cpf_ipo_supported OUTPUT cpf_ipo_output)
    if (cpf_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON PARENT_SCOPE)
    else ()
        message(STATUS "Interprocedural optimization disabled: ${cpf_ipo_output}")
    endif ()
endfunction()

function(cpf_add_project_targets)
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
endfunction()

