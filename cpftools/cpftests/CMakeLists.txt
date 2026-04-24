add_executable(cpftests
        attributes/attribute_tests.cpp
        main.cpp
        runtime/runtime_tests.cpp
)

cpf_link_grammars(cpftests
        fixtures/grammars/calculator.cpf
        fixtures/grammars/message.cpf
        fixtures/grammars/attr_prec_numeric.cpf
        fixtures/grammars/attr_prec_labels.cpf
        fixtures/grammars/attr_dir.cpf
        fixtures/grammars/default_attrs.cpf
        fixtures/grammars/default_labels.cpf
        fixtures/grammars/ambiguous_choice.cpf
        fixtures/grammars/error_choice.cpf
)

target_include_directories(cpftests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
)

target_link_libraries(cpftests PRIVATE
        cpflib
        cpf_project_options
        cpf_project_warnings
)

if (BUILD_TESTING)
   add_test(NAME cpftests COMMAND cpftests)
endif ()
