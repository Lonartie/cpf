project(cpflib)

add_library(cpflib STATIC
        model/rule.cpp
)

target_include_directories(cpflib PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
)