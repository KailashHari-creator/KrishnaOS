#include "lexer.h"

#include <stddef.h>
#include <stdbool.h>

static bool is_space(char character)
{
    return character == ' ' ||
        character == '\t' ||
        character == '\r' ||
        character == '\n';
}

static bool token_append(
    struct venu_token *token,
    char character
)
{
    if (token->length >=
        VENU_MAX_TOKEN_LENGTH - 1) {
        return false;
    }

    token->text[token->length++] =
        character;

    token->text[token->length] = '\0';

    return true;
}

enum venu_lexer_result venu_lex(
    const char *line,
    size_t length,
    struct venu_token_list *result
)
{
    if (line == NULL || result == NULL) {
        return VENU_LEXER_TOKEN_TOO_LONG;
    }

    result->count = 0;

    size_t position = 0;

    while (position < length) {
        while (position < length &&
               is_space(line[position])) {
            position++;
        }

        if (position >= length ||
            line[position] == '#') {
            break;
        }

        if (result->count >=
            VENU_MAX_TOKENS) {
            return
                VENU_LEXER_TOO_MANY_TOKENS;
        }

        struct venu_token *token =
            &result->tokens[result->count];

        token->length = 0;
        token->text[0] = '\0';

        char quote = '\0';
        bool token_started = false;

        while (position < length) {
            char character =
                line[position];

            if (quote != '\0') {
                if (character == quote) {
                    quote = '\0';
                    position++;
                    token_started = true;
                    continue;
                }

                if (character == '\\' &&
                    quote == '"') {
                    position++;

                    if (position >= length) {
                        return
                            VENU_LEXER_TRAILING_ESCAPE;
                    }

                    character =
                        line[position];
                }

                if (!token_append(
                        token,
                        character
                    )) {
                    return
                        VENU_LEXER_TOKEN_TOO_LONG;
                }

                token_started = true;
                position++;
                continue;
            }

            if (character == '\'' ||
                character == '"') {
                quote = character;
                token_started = true;
                position++;
                continue;
            }

            if (character == '\\') {
                position++;

                if (position >= length) {
                    return
                        VENU_LEXER_TRAILING_ESCAPE;
                }

                if (!token_append(
                        token,
                        line[position]
                    )) {
                    return
                        VENU_LEXER_TOKEN_TOO_LONG;
                }

                token_started = true;
                position++;
                continue;
            }

            if (character == '#') {
                position = length;
                break;
            }

            if (is_space(character)) {
                break;
            }

            if (!token_append(
                    token,
                    character
                )) {
                return
                    VENU_LEXER_TOKEN_TOO_LONG;
            }

            token_started = true;
            position++;
        }

        if (quote != '\0') {
            return
                VENU_LEXER_UNTERMINATED_QUOTE;
        }

        if (token_started) {
            result->count++;
        }

        while (position < length &&
               is_space(line[position])) {
            position++;
        }
    }

    return VENU_LEXER_OK;
}