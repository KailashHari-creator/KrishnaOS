#ifndef VENU_LEXER_H
#define VENU_LEXER_H

#include <stddef.h>

#define VENU_MAX_TOKENS       16
#define VENU_MAX_TOKEN_LENGTH 80

enum venu_lexer_result {
    VENU_LEXER_OK = 0,
    VENU_LEXER_TOO_MANY_TOKENS,
    VENU_LEXER_TOKEN_TOO_LONG,
    VENU_LEXER_UNTERMINATED_QUOTE,
    VENU_LEXER_TRAILING_ESCAPE
};

struct venu_token {
    char text[VENU_MAX_TOKEN_LENGTH];
    size_t length;
};

struct venu_token_list {
    struct venu_token tokens[VENU_MAX_TOKENS];
    size_t count;
};

enum venu_lexer_result venu_lex(
    const char *line,
    size_t length,
    struct venu_token_list *result
);

#endif