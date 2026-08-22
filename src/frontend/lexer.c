#include "lexer.h"
#include "token.h"
#include "logger.h"
#include "utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>

#define IDENTIFIER_CAPACITY 8
#define DIGIT_CAPACITY 4
#define INT_CONVERTED_BUFFER_CAPACITY 32
#define STRING_CAPACITY 16
#define COMMENT_CAPACITY 16

#define KEYWORD_COUNT 19

static const char *keywords[KEYWORD_COUNT] = {
    "return", "asm", "if", "else", "while", "do", "break", "continue", "for", "sizeof", "cast", "export", "import", "from", "as", "using",
    "null", "true", "false"
};

Lexer create_lexer(char *path, bool tokenize_comments) {
    Lexer lex = { .path = path, .index = 0, .current_ln = 1, .current_col = 1, .tokenize_comments = tokenize_comments };
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL, 
            "No such file '%s'\n", path);
        exit(EXIT_FAILURE);
    }

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    lex.source = malloc(size + 1);
    size_t chars_read = fread(lex.source, sizeof(char), size, file);
    fclose(file);

    if (chars_read != size) {
        log(ERROR_CRITICAL, LOG_NOFILE, LOG_NOLN, LOG_NOCOL,
            "Failed to read file '%s'\n", path);
        free(lex.source);
        exit(EXIT_FAILURE);
    }

    lex.source[chars_read] = '\0';
    lex.source_length = chars_read;
    lex.current_char = lex.source[0];
    return lex;
}

void delete_lexer(Lexer *lexer) {
    free(lexer->source);
}

static void step(Lexer *lexer) {
    if (lexer->current_char == '\n') {
        lexer->current_ln++;
        lexer->current_col = 1;
    } else
        lexer->current_col++;

    lexer->current_char = lexer->source[++lexer->index];
}

static char peek(Lexer *lexer, const int offset) {
    if (lexer->index + offset >= lexer->source_length)
        return lexer->source[lexer->source_length - 1];
    else if ((int)lexer->index + offset < 1)
        return lexer->source[0];

    return lexer->source[lexer->index + offset];
}

static Token create_and_step(Lexer *lexer, Token_Type type, char *value, size_t length) {
    Token tok = create_token(type, value, length, lexer->current_ln, lexer->current_col);

    for (size_t i = 0; i < length; i++)
        step(lexer);

    return tok;
}

static bool is_keyword(const char *identifier, const size_t length) {
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (compare_string(keywords[i], strlen(keywords[i]), identifier, length))
            return true;
    }

    return false;
}

static Token lex_identifier(Lexer *lexer) {
    const size_t ln = lexer->current_ln;
    const size_t col = lexer->current_col;

    char *value = malloc(IDENTIFIER_CAPACITY + 1);
    size_t length = 0;
    size_t capacity = IDENTIFIER_CAPACITY;

    while (lexer->current_char == '_' || isalpha(lexer->current_char) || isdigit(lexer->current_char)) {
        if (length + 1 > capacity) {
            capacity *= 2;
            value = realloc(value, capacity);
        }

        value[length++] = lexer->current_char;
        step(lexer);
    }

    return create_token(is_keyword(value, length) ? TOK_KEYWORD : TOK_IDENTIFIER, 
        value, length, ln, col);
}



static Token lex_digit(Lexer *lexer) {
    const size_t ln = lexer->current_ln;
    const size_t col = lexer->current_col;

    char *value = malloc(DIGIT_CAPACITY + 1);
    size_t length = 0;
    size_t capacity = DIGIT_CAPACITY;
    bool is_float = false;
    bool is_negative = false;
    bool is_hex = false;

    if (lexer->current_char == '0' && tolower(peek(lexer, 1)) == 'x') {
        // This is a hex number.
        is_hex = true;
        step(lexer);
        step(lexer);
    }

    while (isdigit(lexer->current_char) || (lexer->current_char == '.' && isdigit(peek(lexer, 1)) && !is_float) ||
            (lexer->current_char == '-' && (length == 0 || (length == 1 && is_negative))) ||
            (tolower(lexer->current_char) >= 'a' && tolower(lexer->current_char) <= 'f' && is_hex)) {
        // +1 extra for possible 'f', 'o', 'b' etc...
        if (length + 2 > capacity) {
            capacity *= 2;
            value = realloc(value, capacity);
        }

        if (lexer->current_char == '.')
            is_float = true;
        else if (lexer->current_char == '-')
            is_negative = true;

        value[length++] = lexer->current_char;
        step(lexer);
    }

    if (tolower(lexer->current_char) == 'f') {
        value[length++] = lexer->current_char;
        step(lexer);
        is_float = true;
    }

    if (is_float)
        return create_token(TOK_FLOAT, value, length, ln, col);

    if (tolower(lexer->current_char) == 'h')
        is_hex = true;
    else if (tolower(lexer->current_char) == 'o' && is_hex)
        log(ERROR_CRITICAL, lexer->path, ln, col, "Invalid combination of hex prefix and octal suffix\n");

    if (tolower(lexer->current_char) != 'o' && !is_hex)
        return create_token(TOK_INT, value, length, ln, col);


    value[length] = '\0';

    int64_t i64;
    uint64_t u64;
    char *endptr;

    if (tolower(lexer->current_char) == 'h' || tolower(lexer->current_char) == 'o')
        step(lexer);

    if (is_negative) {
        errno = 0;
        i64 = strtoll(value, &endptr, is_hex ? 16 : 8);
    } else {
        errno = 0;
        u64 = strtoul(value, &endptr, is_hex ? 16 : 8);
    }

    check_literal_conversion(lexer->path, ln, col, errno, value, endptr);
    value = realloc(value, INT_CONVERTED_BUFFER_CAPACITY + 1);

    if (is_negative)
        sprintf(value, "%" PRId64, i64);
    else
        sprintf(value, "%" PRIu64, u64);

    return create_token(TOK_INT, value, strlen(value), ln, col);
}

static void skip_comment(Lexer *lexer) {
    step(lexer);

    if (lexer->current_char == '/') {
        while (lexer->current_char != '\0' && lexer->current_char != '\n')
            step(lexer);

        if (lexer->current_char == '\n')
            step(lexer);

        return;
    }

    step(lexer);

    while (lexer->current_char != '\0' && (lexer->current_char != '*' || peek(lexer, 1) != '/'))
        step(lexer);

    step(lexer);
    step(lexer);
}

static Token lex_string(Lexer *lexer) {
    const size_t ln = lexer->current_ln;
    const size_t col = lexer->current_col;
    step(lexer);

    char *value = malloc(STRING_CAPACITY + 1);
    size_t length = 0;
    size_t capacity = STRING_CAPACITY;

    while (lexer->current_char != '\0' && (lexer->current_char != '"' || peek(lexer, -1) == '\\')) {
        if (length + 1 > capacity) {
            capacity *= 2;
            value = realloc(value, capacity);
        }

        value[length++] = lexer->current_char;
        step(lexer);
    }

    if (lexer->current_char != '"')
        log(ERROR_CRITICAL, lexer->path, ln, col,
            "Unclosed string literal\n");
    else
        step(lexer);

    // Concatenate the following string if present.
    while (isspace(lexer->current_char))
        step(lexer);

    if (lexer->current_char == '"') {
        Token next;

        while (lexer->current_char == '"') {
            next = lex_string(lexer);
            value = realloc(value, (length + next.length + 1) * sizeof(char));
            strcat(value, next.value);
            free(next.value);
            length += next.length;

            while (isspace(lexer->current_char))
                step(lexer);
        }
    }

    return create_token(TOK_STRING, value, length, ln, col);
}

static Token lex_character(Lexer *lexer) {
    const size_t ln = lexer->current_ln;
    const size_t col = lexer->current_col;
    step(lexer);

    int code = 0;

    if (lexer->current_char == '\\') {
        step(lexer);

        switch (lexer->current_char) {
            case 'n':
                code = '\n';
                break;
            case 't':
                code = '\t';
                break;
            case 'r':
                code = '\r';
                break;
            case '0': break;
            case '\'':
            case '"':
            case '\\':
                code = (int)lexer->current_char;
                break;
            default:
                log(ERROR_CRITICAL, lexer->path, ln, col,
                    "Unsupported escape sequence '\\%c'\n", lexer->current_char);
                break;
        }
    } else
        code = (int)lexer->current_char;

    step(lexer);

    char *value = malloc(16);
    sprintf(value, "%d", code);

    if (lexer->current_char != '\'')
        log(ERROR_CRITICAL, lexer->path, ln, col,
            "Unclosed character constant\n");
    else
        step(lexer);

    return create_token(TOK_INT, value, strlen(value), ln, col);
}

Token lex_comment(Lexer *lexer) {
    const size_t ln = lexer->current_ln;
    const size_t col = lexer->current_col;

    char *value = malloc(COMMENT_CAPACITY + 1);
    size_t length = 0;
    size_t capacity = COMMENT_CAPACITY;
    step(lexer);

    const bool is_oneline = lexer->current_char == '/';
    step(lexer);

    while (lexer->current_char != '\0' && ((is_oneline && lexer->current_char != '\n') ||
            (!is_oneline && (lexer->current_char != '*' || peek(lexer, 1) != '/')))) {
        if (length + 3 > capacity) {
            capacity *= 2;
            value = realloc(value, capacity + 1);
        }

        value[length++] = lexer->current_char;
        step(lexer);
    }

    if (!is_oneline)
        step(lexer);

    step(lexer);
    return create_token(TOK_COMMENT, value, length, ln, col);
}

Token get_next_token(Lexer *lexer) {
    while (isspace(lexer->current_char))
        step(lexer);

    if (lexer->current_char == '_' || isalpha(lexer->current_char))
        return lex_identifier(lexer);
    else if (isdigit(lexer->current_char) || (lexer->current_char == '-' && isdigit(peek(lexer, 1))))
        return lex_digit(lexer);

    switch (lexer->current_char) {
        case '\0': return create_token(TOK_EOF, "eof", 3, lexer->current_ln, lexer->current_col);
        case '"': return lex_string(lexer);
        case '\'': return lex_character(lexer);
        case '(': return create_and_step(lexer, TOK_LPAREN, "(", 1);
        case ')': return create_and_step(lexer, TOK_RPAREN, ")", 1);
        case '{': return create_and_step(lexer, TOK_LBRACE, "{", 1);
        case '}': return create_and_step(lexer, TOK_RBRACE, "}", 1);
        case ':': return create_and_step(lexer, TOK_COLON, ":", 1);
        case ';': return create_and_step(lexer, TOK_SEMICOLON, ";", 1);
        case ',': return create_and_step(lexer, TOK_COMMA, ",", 1);
        case '=':
            if (peek(lexer, 1) == '=')
                return create_and_step(lexer, TOK_EQ, "==", 2);
            return create_and_step(lexer, TOK_EQUAL, "=", 1);
        case '+': return create_and_step(lexer, TOK_PLUS, "+", 1);
        case '-': return create_and_step(lexer, TOK_MINUS, "-", 1);
        case '*': return create_and_step(lexer, TOK_STAR, "*", 1);
        case '/':
            if (peek(lexer, 1) != '/' && peek(lexer, 1) != '*')
                return create_and_step(lexer, TOK_SLASH, "/", 1);

            if (lexer->tokenize_comments)
                return lex_comment(lexer);

            skip_comment(lexer);
            return get_next_token(lexer);
        case '%': return create_and_step(lexer, TOK_PERCENT, "%", 1);
        case '&':
            if (peek(lexer, 1) == '&')
                return create_and_step(lexer, TOK_BOOL_AND, "&&", 2);
            return create_and_step(lexer, TOK_AND, "&", 1);
        case '|':
            if (peek(lexer, 1) == '|')
                return create_and_step(lexer, TOK_BOOL_OR, "||", 2);
            return create_and_step(lexer, TOK_OR, "|", 1);
        case '^': return create_and_step(lexer, TOK_XOR, "^", 1);
        case '<':
            if (peek(lexer, 1) == '<')
                return create_and_step(lexer, TOK_SHL, "<<", 2);
            if (peek(lexer, 1) == '=')
                return create_and_step(lexer, TOK_LTE, "<=", 2);
            return create_and_step(lexer, TOK_LT, "<", 1);
        case '>':
            if (peek(lexer, 1) == '>')
                return create_and_step(lexer, TOK_SHR, ">>", 2);
            if (peek(lexer, 1) == '=')
                return create_and_step(lexer, TOK_GTE, ">=", 2);
            return create_and_step(lexer, TOK_GT, ">", 1);
        case '!':
            if (peek(lexer, 1) == '=')
                return create_and_step(lexer, TOK_NEQ, "!=", 2);
            return create_and_step(lexer, TOK_BOOL_NOT, "!", 1);
        case '.':
            if (peek(lexer, 1) == '.')
                return create_and_step(lexer, TOK_RANGE, "..", 2);
            return create_and_step(lexer, TOK_ACCESS, ".", 1);
        case '[': return create_and_step(lexer, TOK_LSQUARE, "[", 1);
        case ']': return create_and_step(lexer, TOK_RSQUARE, "]", 1);
        case '~': return create_and_step(lexer, TOK_TILDE, "~", 1);
        case '@': return create_and_step(lexer, TOK_AT, "@", 1);
        case '#': return create_and_step(lexer, TOK_HASHTAG, "#", 1);
        default: break;
    }

    log(ERROR_CRITICAL, lexer->path, lexer->current_ln, lexer->current_col,
        "Unknown token '%c'\n", lexer->current_char);
    step(lexer);
    return get_next_token(lexer);
}
