project(cpfgen)

add_executable(cpfgen
        main.cpp
)

target_link_libraries(cpfgen PRIVATE
        cpflib
)