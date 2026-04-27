#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yacc_parse_result {
    bool success;
    size_t line;
    size_t column;
    const char* message;
} yacc_parse_result;

yacc_parse_result yacc_parse_source(const char* input, size_t length);

#ifdef __cplusplus
}
#endif

