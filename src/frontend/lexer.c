#include "omega/compiler.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char lex_peek(const Lexer *lx) {
    if (lx->pos >= lx->len) {
        return '\0';
    }
    return lx->src[lx->pos];
}

static char lex_peek_n(const Lexer *lx, size_t n) {
    size_t p = lx->pos + n;
    if (p >= lx->len) {
        return '\0';
    }
    return lx->src[p];
}

static char lex_advance(Lexer *lx) {
    char c = lex_peek(lx);
    if (c == '\0') {
        return c;
    }
    lx->pos++;
    if (c == '\n') {
        lx->line++;
        lx->column = 1;
    } else {
        lx->column++;
    }
    return c;
}

static void lex_emit(Lexer *lx, TokenKind kind, const char *start, size_t length, uint32_t line, uint32_t column) {
    Token t;
    t.kind = kind;
    t.lexeme = xstrndup_local(start, length);
    t.line = line;
    t.column = column;
    token_vec_push(&lx->tokens, t);
}

static void lex_emit_owned(Lexer *lx, TokenKind kind, char *owned, uint32_t line, uint32_t column) {
    Token t;
    t.kind = kind;
    t.lexeme = owned;
    t.line = line;
    t.column = column;
    token_vec_push(&lx->tokens, t);
}

static bool is_ident_start(char c) {
    return (bool)(isalpha((unsigned char)c) || c == '_');
}

static bool is_ident_part(char c) {
    return (bool)(isalnum((unsigned char)c) || c == '_');
}

static void lex_skip_ws_and_comments(Lexer *lx) {
    for (;;) {
        char c = lex_peek(lx);
        if (c == '\0') {
            return;
        }
        if (isspace((unsigned char)c)) {
            (void)lex_advance(lx);
            continue;
        }
        if (c == '/' && lex_peek_n(lx, 1u) == '/') {
            while (lex_peek(lx) != '\0' && lex_peek(lx) != '\n') {
                (void)lex_advance(lx);
            }
            continue;
        }
        return;
    }
}

static void lex_identifier_or_keyword(Lexer *lx) {
    uint32_t line = lx->line;
    uint32_t col = lx->column;
    size_t start = lx->pos;
    (void)lex_advance(lx);
    while (is_ident_part(lex_peek(lx))) {
        (void)lex_advance(lx);
    }

    size_t end = lx->pos;
    char *text = xstrndup_local(lx->src + start, end - start);
    TokenKind kind = TOK_IDENTIFIER;

    if (strcmp(text, "import") == 0) {
        kind = TOK_IMPORT;
    } else if (strcmp(text, "if") == 0) {
        kind = TOK_IF;
    } else if (strcmp(text, "else") == 0) {
        kind = TOK_ELSE;
    } else if (strcmp(text, "while") == 0) {
        kind = TOK_WHILE;
    } else if (strcmp(text, "main") == 0) {
        kind = TOK_MAIN;
    } else if (strcmp(text, "uint32") == 0) {
        kind = TOK_TYPE_UINT32;
    } else if (strcmp(text, "int32") == 0) {
        kind = TOK_TYPE_INT32;
    } else if (strcmp(text, "float32") == 0) {
        kind = TOK_TYPE_FLOAT32;
    } else if (strcmp(text, "string") == 0) {
        kind = TOK_TYPE_STRING;
    } else if (strcmp(text, "boolean") == 0) {
        kind = TOK_TYPE_BOOLEAN;
    } else if (strcmp(text, "vector") == 0) {
        kind = TOK_TYPE_VECTOR;
    } else if (strcmp(text, "true") == 0) {
        kind = TOK_BOOL_TRUE;
    } else if (strcmp(text, "false") == 0) {
        kind = TOK_BOOL_FALSE;
    }

    lex_emit_owned(lx, kind, text, line, col);
}

static void lex_number(Lexer *lx) {
    uint32_t line = lx->line;
    uint32_t col = lx->column;
    size_t start = lx->pos;
    while (isdigit((unsigned char)lex_peek(lx))) {
        (void)lex_advance(lx);
    }

    bool saw_dot = false;
    if (lex_peek(lx) == '.' && isdigit((unsigned char)lex_peek_n(lx, 1u))) {
        saw_dot = true;
        (void)lex_advance(lx);
        while (isdigit((unsigned char)lex_peek(lx))) {
            (void)lex_advance(lx);
        }
    }

    char suffix = lex_peek(lx);
    if (suffix != 'u' && suffix != 'i' && suffix != 'b' && suffix != 'f') {
        status_fail(&lx->st, line, col, "numeric literal requires a suffix: u, i, b, or f");
        return;
    }
    (void)lex_advance(lx);

    size_t len = lx->pos - start;
    const char *lit = lx->src + start;
    if (suffix == 'f') {
        lex_emit(lx, TOK_FLOAT_LITERAL, lit, len, line, col);
        return;
    }
    if (saw_dot) {
        status_fail(&lx->st, line, col, "decimal literals must use the 'f' suffix");
        return;
    }
    if (suffix == 'u') {
        lex_emit(lx, TOK_UINT_LITERAL, lit, len, line, col);
        return;
    }
    if (suffix == 'i') {
        lex_emit(lx, TOK_INT_LITERAL, lit, len, line, col);
        return;
    }

    if (len != 2u || (lit[0] != '0' && lit[0] != '1')) {
        status_fail(&lx->st, line, col, "boolean b-literal must be 0b or 1b");
        return;
    }
    if (lit[0] == '1') {
        lex_emit(lx, TOK_BOOL_TRUE, "true", 4u, line, col);
    } else {
        lex_emit(lx, TOK_BOOL_FALSE, "false", 5u, line, col);
    }
}

static void append_char(char **buf, size_t *len, size_t *cap, char c) {
    if (*len + 1u >= *cap) {
        size_t next = (*cap == 0u) ? 16u : (*cap * 2u);
        *buf = (char *)xrealloc(*buf, next);
        *cap = next;
    }
    (*buf)[(*len)++] = c;
    (*buf)[*len] = '\0';
}

static void lex_string(Lexer *lx) {
    uint32_t line = lx->line;
    uint32_t col = lx->column;
    (void)lex_advance(lx);

    char *buf = NULL;
    size_t len = 0u;
    size_t cap = 0u;

    while (true) {
        char c = lex_peek(lx);
        if (c == '\0') {
            free(buf);
            status_fail(&lx->st, line, col, "unterminated string literal");
            return;
        }
        if (c == '"') {
            (void)lex_advance(lx);
            break;
        }
        if (c == '\\') {
            (void)lex_advance(lx);
            char esc = lex_peek(lx);
            if (esc == '\0') {
                free(buf);
                status_fail(&lx->st, line, col, "unterminated escape sequence");
                return;
            }
            (void)lex_advance(lx);
            switch (esc) {
                case 'n':
                    append_char(&buf, &len, &cap, '\n');
                    break;
                case 't':
                    append_char(&buf, &len, &cap, '\t');
                    break;
                case '\\':
                    append_char(&buf, &len, &cap, '\\');
                    break;
                case '"':
                    append_char(&buf, &len, &cap, '"');
                    break;
                default:
                    free(buf);
                    status_fail(&lx->st, line, col, "unsupported string escape");
                    return;
            }
            continue;
        }
        (void)lex_advance(lx);
        append_char(&buf, &len, &cap, c);
    }

    if (buf == NULL) {
        buf = xstrdup_local("");
    }
    lex_emit_owned(lx, TOK_STRING, buf, line, col);
}

bool lex_all(Lexer *lx) {
    lx->st.ok = true;
    while (lx->st.ok) {
        lex_skip_ws_and_comments(lx);
        if (!lx->st.ok) {
            break;
        }

        char c = lex_peek(lx);
        if (c == '\0') {
            Token eof;
            eof.kind = TOK_EOF;
            eof.lexeme = xstrdup_local("<eof>");
            eof.line = lx->line;
            eof.column = lx->column;
            token_vec_push(&lx->tokens, eof);
            break;
        }

        uint32_t line = lx->line;
        uint32_t col = lx->column;

        if (is_ident_start(c)) {
            lex_identifier_or_keyword(lx);
            continue;
        }
        if (isdigit((unsigned char)c)) {
            lex_number(lx);
            continue;
        }
        if (c == '"') {
            lex_string(lx);
            continue;
        }

        if (c == '<' && lex_peek_n(lx, 1u) == '-' && lex_peek_n(lx, 2u) == '>') {
            lex_emit(lx, TOK_NS_LINK, lx->src + lx->pos, 3u, line, col);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            continue;
        }
        if (c == '<' && lex_peek_n(lx, 1u) == '-') {
            lex_emit(lx, TOK_LEFT_ARROW, lx->src + lx->pos, 2u, line, col);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            continue;
        }
        if (c == '-' && lex_peek_n(lx, 1u) == '>') {
            lex_emit(lx, TOK_ARROW, lx->src + lx->pos, 2u, line, col);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            continue;
        }
        if (c == '=' && lex_peek_n(lx, 1u) == '=') {
            lex_emit(lx, TOK_EQEQ, lx->src + lx->pos, 2u, line, col);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            continue;
        }
        if (c == '!' && lex_peek_n(lx, 1u) == '=') {
            lex_emit(lx, TOK_NEQ, lx->src + lx->pos, 2u, line, col);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            continue;
        }
        if (c == '<' && lex_peek_n(lx, 1u) == '=') {
            lex_emit(lx, TOK_LE, lx->src + lx->pos, 2u, line, col);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            continue;
        }
        if (c == '>' && lex_peek_n(lx, 1u) == '=') {
            lex_emit(lx, TOK_GE, lx->src + lx->pos, 2u, line, col);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            continue;
        }
        if (c == '\\' && lex_peek_n(lx, 1u) == '/') {
            lex_emit(lx, TOK_LOGIC_OR, lx->src + lx->pos, 2u, line, col);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            continue;
        }
        if (c == '/' && lex_peek_n(lx, 1u) == '\\') {
            lex_emit(lx, TOK_LOGIC_AND, lx->src + lx->pos, 2u, line, col);
            (void)lex_advance(lx);
            (void)lex_advance(lx);
            continue;
        }

        TokenKind k = TOK_EOF;
        bool single = true;
        switch (c) {
            case '(':
                k = TOK_LPAREN;
                break;
            case ')':
                k = TOK_RPAREN;
                break;
            case '{':
                k = TOK_LBRACE;
                break;
            case '}':
                k = TOK_RBRACE;
                break;
            case '[':
                k = TOK_LBRACKET;
                break;
            case ']':
                k = TOK_RBRACKET;
                break;
            case ',':
                k = TOK_COMMA;
                break;
            case '.':
                k = TOK_PERIOD;
                break;
            case '=':
                k = TOK_ASSIGN;
                break;
            case '<':
                k = TOK_LT;
                break;
            case '>':
                k = TOK_GT;
                break;
            case '+':
                k = TOK_PLUS;
                break;
            case '-':
                k = TOK_MINUS;
                break;
            case '*':
                k = TOK_STAR;
                break;
            case '/':
                k = TOK_SLASH;
                break;
            case '%':
                k = TOK_PERCENT;
                break;
            case '!':
                k = TOK_BANG;
                break;
            case '~':
                k = TOK_LOGIC_NOT;
                break;
            default:
                single = false;
                break;
        }
        if (single) {
            lex_emit(lx, k, lx->src + lx->pos, 1u, line, col);
            (void)lex_advance(lx);
            continue;
        }

        status_fail(&lx->st, line, col, "unexpected character in source");
    }
    return lx->st.ok;
}
