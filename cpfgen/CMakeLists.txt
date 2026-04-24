add_executable(cpfgen
        main.cpp
)

target_link_libraries(cpfgen PRIVATE
        cpflib
        cpf_project_options
        cpf_project_warnings
)

