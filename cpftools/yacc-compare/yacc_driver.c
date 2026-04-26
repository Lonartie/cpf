#include "yacc_driver.h"

#include "c_subset_parser.h"

#include <ctype.h>
#include <string.h>

typedef struct lexer_state {
    const char* input;
    size_t length;
    size_t offset;
    size_t line;
    size_t column;
} lexer_state;

static lexer_state* current_lexer = NULL;
static yacc_parse_result current_result = {false, 1, 1, "parser not started"};
static char current_message[256];

static void set_error_message(const char* message) {
    size_t available = sizeof(current_message) - 1U;
    size_t length = 0;
    if (message != NULL) {
        length = strlen(message);
        if (length > available) {
            length = available;
        }
        memcpy(current_message, message, length);
    }
    current_message[length] = '\0';
    current_result.message = current_message;
}

static int has_input(void) {
    return current_lexer != NULL && current_lexer->offset < current_lexer->length;
}

static int peek_char(size_t lookahead) {
    size_t position = current_lexer->offset + lookahead;
    if (current_lexer == NULL || position >= current_lexer->length) {
        return 0;
    }
    return (unsigned char) current_lexer->input[position];
}

static int advance_char(void) {
    int value = peek_char(0);
    if (value == 0) {
        return 0;
    }
    current_lexer->offset += 1;
    if (value == '\n') {
        current_lexer->line += 1;
        current_lexer->column = 1;
    } else {
        current_lexer->column += 1;
    }
    return value;
}

static void skip_whitespace_and_comments(void) {
    while (has_input()) {
        if (isspace(peek_char(0)) != 0) {
            advance_char();
            continue;
        }
        if (peek_char(0) == '/' && peek_char(1) == '/') {
            while (has_input() && peek_char(0) != '\n') {
                advance_char();
            }
            continue;
        }
        break;
    }
}

static int lex_identifier_or_keyword(void) {
    char buffer[128];
    size_t length = 0;
    while (has_input()) {
        int value = peek_char(0);
        if (isalnum(value) == 0 && value != '_') {
            break;
        }
        if (length + 1U < sizeof(buffer)) {
            buffer[length] = (char) advance_char();
            length += 1;
        } else {
            advance_char();
        }
    }
    buffer[length] = '\0';

    if (strcmp(buffer, "int") == 0) {
        return KW_INT;
    }
    if (strcmp(buffer, "void") == 0) {
        return KW_VOID;
    }
    if (strcmp(buffer, "if") == 0) {
        return KW_IF;
    }
    if (strcmp(buffer, "else") == 0) {
        return KW_ELSE;
    }
    if (strcmp(buffer, "while") == 0) {
        return KW_WHILE;
    }
    if (strcmp(buffer, "for") == 0) {
        return KW_FOR;
    }
    if (strcmp(buffer, "return") == 0) {
        return KW_RETURN;
    }
    return IDENTIFIER;
}

static int lex_integer_literal(void) {
    while (has_input() && isdigit(peek_char(0)) != 0) {
        advance_char();
    }
    return INT_LITERAL;
}

int c_subset_lex(void) {
    skip_whitespace_and_comments();
    if (!has_input()) {
        return 0;
    }

    if (isalpha(peek_char(0)) != 0 || peek_char(0) == '_') {
        return lex_identifier_or_keyword();
    }
    if (isdigit(peek_char(0)) != 0) {
        return lex_integer_literal();
    }

    if (peek_char(0) == '=' && peek_char(1) == '=') {
        advance_char();
        advance_char();
        return EQ_OP;
    }
    if (peek_char(0) == '!' && peek_char(1) == '=') {
        advance_char();
        advance_char();
        return NE_OP;
    }
    if (peek_char(0) == '<' && peek_char(1) == '=') {
        advance_char();
        advance_char();
        return LE_OP;
    }
    if (peek_char(0) == '>' && peek_char(1) == '=') {
        advance_char();
        advance_char();
        return GE_OP;
    }
    if (peek_char(0) == '&' && peek_char(1) == '&') {
        advance_char();
        advance_char();
        return AND_OP;
    }
    if (peek_char(0) == '|' && peek_char(1) == '|') {
        advance_char();
        advance_char();
        return OR_OP;
    }

    if (strchr("(){}[];,+-*/%<>=!", peek_char(0)) != NULL) {
        return advance_char();
    }

    current_result.line = current_lexer->line;
    current_result.column = current_lexer->column;
    set_error_message("invalid character");
    return advance_char();
}

void c_subset_error(const char* message) {
    if (current_result.success) {
        return;
    }
    current_result.line = current_lexer != NULL ? current_lexer->line : 1;
    current_result.column = current_lexer != NULL ? current_lexer->column : 1;
    set_error_message(message != NULL ? message : "syntax error");
}

yacc_parse_result yacc_parse_source(const char* input, size_t length) {
    lexer_state lexer;
    lexer.input = input;
    lexer.length = length;
    lexer.offset = 0;
    lexer.line = 1;
    lexer.column = 1;

    current_lexer = &lexer;
    current_result.success = false;
    current_result.line = 1;
    current_result.column = 1;
    current_result.message = current_message;
    set_error_message("syntax error");

    if (c_subset_parse() == 0) {
        current_result.success = true;
        current_result.line = 0;
        current_result.column = 0;
        set_error_message("ok");
    }

    current_lexer = NULL;
    return current_result;
}


