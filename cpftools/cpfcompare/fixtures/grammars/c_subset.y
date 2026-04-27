%{
#include <stddef.h>

int c_subset_lex(void);
void c_subset_error(const char* message);
%}

%define api.prefix {c_subset_}
%define parse.error verbose

%token IDENTIFIER
%token INT_LITERAL
%token KW_INT "int"
%token KW_VOID "void"
%token KW_IF "if"
%token KW_ELSE "else"
%token KW_WHILE "while"
%token KW_FOR "for"
%token KW_RETURN "return"
%token EQ_OP "=="
%token NE_OP "!="
%token LE_OP "<="
%token GE_OP ">="
%token AND_OP "&&"
%token OR_OP "||"

%start translation_unit

%%
translation_unit
    : external_declaration_list
    ;

external_declaration_list
    : external_declaration
    | external_declaration_list external_declaration
    ;

external_declaration
    : function_definition
    | declaration
    ;

function_definition
    : type_specifier IDENTIFIER '(' parameter_list_opt ')' compound_statement
    ;

parameter_list_opt
    : /* empty */
    | parameter_list
    ;

parameter_list
    : parameter_declaration
    | parameter_list ',' parameter_declaration
    ;

parameter_declaration
    : type_specifier declarator
    ;

declaration
    : type_specifier init_declarator_list_opt ';'
    ;

init_declarator_list_opt
    : /* empty */
    | init_declarator_list
    ;

init_declarator_list
    : init_declarator
    | init_declarator_list ',' init_declarator
    ;

init_declarator
    : declarator
    | declarator '=' assignment_expression
    ;

declarator
    : IDENTIFIER
    | IDENTIFIER '[' INT_LITERAL ']'
    ;

type_specifier
    : KW_INT
    | KW_VOID
    ;

compound_statement
    : '{' block_item_list_opt '}'
    ;

block_item_list_opt
    : /* empty */
    | block_item_list
    ;

block_item_list
    : block_item
    | block_item_list block_item
    ;

block_item
    : declaration
    | statement
    ;

statement
    : matched_statement
    | unmatched_statement
    ;

matched_statement
    : compound_statement
    | expression_statement
    | jump_statement
    | matched_iteration_statement
    | KW_IF '(' expression ')' matched_statement KW_ELSE matched_statement
    ;

unmatched_statement
    : KW_IF '(' expression ')' statement
    | KW_IF '(' expression ')' matched_statement KW_ELSE unmatched_statement
    | unmatched_iteration_statement
    ;

matched_iteration_statement
    : KW_WHILE '(' expression ')' matched_statement
    | KW_FOR '(' expression_opt ';' expression_opt ';' expression_opt ')' matched_statement
    ;

unmatched_iteration_statement
    : KW_WHILE '(' expression ')' unmatched_statement
    | KW_FOR '(' expression_opt ';' expression_opt ';' expression_opt ')' unmatched_statement
    ;

expression_statement
    : ';'
    | expression ';'
    ;


jump_statement
    : KW_RETURN ';'
    | KW_RETURN expression ';'
    ;

expression_opt
    : /* empty */
    | expression
    ;

expression
    : assignment_expression
    | expression ',' assignment_expression
    ;

assignment_expression
    : unary_expression '=' assignment_expression
    | logical_or_expression
    ;

logical_or_expression
    : logical_and_expression
    | logical_or_expression OR_OP logical_and_expression
    ;

logical_and_expression
    : equality_expression
    | logical_and_expression AND_OP equality_expression
    ;

equality_expression
    : relational_expression
    | equality_expression EQ_OP relational_expression
    | equality_expression NE_OP relational_expression
    ;

relational_expression
    : additive_expression
    | relational_expression '<' additive_expression
    | relational_expression '>' additive_expression
    | relational_expression LE_OP additive_expression
    | relational_expression GE_OP additive_expression
    ;

additive_expression
    : multiplicative_expression
    | additive_expression '+' multiplicative_expression
    | additive_expression '-' multiplicative_expression
    ;

multiplicative_expression
    : unary_expression
    | multiplicative_expression '*' unary_expression
    | multiplicative_expression '/' unary_expression
    | multiplicative_expression '%' unary_expression
    ;

unary_expression
    : postfix_expression
    | '+' unary_expression
    | '-' unary_expression
    | '!' unary_expression
    ;

postfix_expression
    : primary_expression
    | postfix_expression '(' argument_expression_list_opt ')'
    | postfix_expression '[' expression ']'
    ;

argument_expression_list_opt
    : /* empty */
    | argument_expression_list
    ;

argument_expression_list
    : assignment_expression
    | argument_expression_list ',' assignment_expression
    ;

primary_expression
    : IDENTIFIER
    | INT_LITERAL
    | '(' expression ')'
    ;
%%

