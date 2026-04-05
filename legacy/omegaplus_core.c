#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum TokenKind {
    TOK_EOF = 0,

    TOK_IDENTIFIER,
    TOK_STRING,
    TOK_UINT_LITERAL,
    TOK_INT_LITERAL,
    TOK_BOOL_TRUE,
    TOK_BOOL_FALSE,

    TOK_IMPORT,
    TOK_IF,
    TOK_MAIN,
    TOK_TYPE_UINT32,
    TOK_TYPE_INT32,
    TOK_TYPE_BOOLEAN,

    TOK_LPAREN,      // (
    TOK_RPAREN,      // )
    TOK_LBRACE,      // {
    TOK_RBRACE,      // }
    TOK_COMMA,       // ,
    TOK_PERIOD,      // .
    TOK_ASSIGN,      // =
    TOK_BANG,        // !
    TOK_ARROW,       // ->

    TOK_LOGIC_NOT,   // ~
    TOK_LOGIC_OR,    // \/
    TOK_LOGIC_AND,   /* /\ */
    TOK_NS_LINK      // <->
} TokenKind;

typedef struct Token {
    TokenKind kind;
    const char *lexeme;
    uint32_t line;
    uint32_t column;
} Token;

typedef enum OmegaType {
    OMEGA_TYPE_INVALID = 0,
    OMEGA_TYPE_UINT32,
    OMEGA_TYPE_INT32,
    OMEGA_TYPE_BOOLEAN,
    OMEGA_TYPE_VOID
} OmegaType;

typedef enum ASTNodeKind {
    AST_INVALID = 0,
    AST_IDENTIFIER,
    AST_UINT_LITERAL,
    AST_INT_LITERAL,
    AST_BOOL_LITERAL,
    AST_UNARY_EXPR,
    AST_BINARY_EXPR,
    AST_RETURN_STATEMENT
} ASTNodeKind;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeKind kind;
    OmegaType type;
    uint32_t line;
    uint32_t column;
    union {
        struct {
            const char *name;
        } ident;
        struct {
            uint32_t value;
        } u32_lit;
        struct {
            int32_t value;
        } i32_lit;
        struct {
            bool value;
        } bool_lit;
        struct {
            TokenKind op;
            ASTNode *operand;
        } unary;
        struct {
            TokenKind op;
            ASTNode *left;
            ASTNode *right;
        } binary;
        struct {
            ASTNode *value;
        } ret_stmt;
    } as;
};

typedef struct Parser {
    const Token *tokens;
    size_t count;
    size_t pos;
    const char *error;
} Parser;

static ASTNode *new_ast_node(ASTNodeKind kind, uint32_t line, uint32_t column) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(*n));
    if (n == NULL) {
        fprintf(stderr, "fatal: out of memory while allocating AST node\n");
        exit(EXIT_FAILURE);
    }
    n->kind = kind;
    n->type = OMEGA_TYPE_INVALID;
    n->line = line;
    n->column = column;
    return n;
}

static const Token *peek_token(const Parser *p) {
    if (p->pos >= p->count) {
        return &p->tokens[p->count - 1];
    }
    return &p->tokens[p->pos];
}

static const Token *advance_token(Parser *p) {
    const Token *t = peek_token(p);
    if (p->pos < p->count) {
        p->pos++;
    }
    return t;
}

static bool match_token(Parser *p, TokenKind kind) {
    if (peek_token(p)->kind != kind) {
        return false;
    }
    (void)advance_token(p);
    return true;
}

static const Token *expect_token(Parser *p, TokenKind kind, const char *message) {
    const Token *t = peek_token(p);
    if (t->kind != kind) {
        p->error = message;
        return NULL;
    }
    return advance_token(p);
}

static ASTNode *parse_expression(Parser *p);

static ASTNode *parse_primary(Parser *p) {
    const Token *t = peek_token(p);
    switch (t->kind) {
        case TOK_IDENTIFIER: {
            ASTNode *n = new_ast_node(AST_IDENTIFIER, t->line, t->column);
            n->as.ident.name = t->lexeme;
            (void)advance_token(p);
            return n;
        }
        case TOK_BOOL_TRUE:
        case TOK_BOOL_FALSE: {
            ASTNode *n = new_ast_node(AST_BOOL_LITERAL, t->line, t->column);
            n->type = OMEGA_TYPE_BOOLEAN;
            n->as.bool_lit.value = (t->kind == TOK_BOOL_TRUE);
            (void)advance_token(p);
            return n;
        }
        case TOK_UINT_LITERAL: {
            ASTNode *n = new_ast_node(AST_UINT_LITERAL, t->line, t->column);
            n->type = OMEGA_TYPE_UINT32;
            n->as.u32_lit.value = (uint32_t)strtoul(t->lexeme, NULL, 10);
            (void)advance_token(p);
            return n;
        }
        case TOK_INT_LITERAL: {
            ASTNode *n = new_ast_node(AST_INT_LITERAL, t->line, t->column);
            n->type = OMEGA_TYPE_INT32;
            n->as.i32_lit.value = (int32_t)strtol(t->lexeme, NULL, 10);
            (void)advance_token(p);
            return n;
        }
        case TOK_LPAREN: {
            (void)advance_token(p);
            ASTNode *expr = parse_expression(p);
            if (expr == NULL) {
                return NULL;
            }
            if (expect_token(p, TOK_RPAREN, "expected ')' after expression") == NULL) {
                return NULL;
            }
            return expr;
        }
        default:
            p->error = "expected primary expression";
            return NULL;
    }
}

static ASTNode *parse_unary(Parser *p) {
    const Token *t = peek_token(p);
    if (t->kind == TOK_LOGIC_NOT) {
        (void)advance_token(p);
        ASTNode *operand = parse_unary(p);
        if (operand == NULL) {
            return NULL;
        }
        ASTNode *n = new_ast_node(AST_UNARY_EXPR, t->line, t->column);
        n->as.unary.op = TOK_LOGIC_NOT;
        n->as.unary.operand = operand;
        return n;
    }
    return parse_primary(p);
}

static ASTNode *parse_logic_and(Parser *p) {
    ASTNode *left = parse_unary(p);
    if (left == NULL) {
        return NULL;
    }

    while (match_token(p, TOK_LOGIC_AND)) {
        const Token *op = &p->tokens[p->pos - 1];
        ASTNode *right = parse_unary(p);
        if (right == NULL) {
            return NULL;
        }
        ASTNode *bin = new_ast_node(AST_BINARY_EXPR, op->line, op->column);
        bin->as.binary.op = TOK_LOGIC_AND;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }

    return left;
}

static ASTNode *parse_logic_or(Parser *p) {
    ASTNode *left = parse_logic_and(p);
    if (left == NULL) {
        return NULL;
    }

    while (match_token(p, TOK_LOGIC_OR)) {
        const Token *op = &p->tokens[p->pos - 1];
        ASTNode *right = parse_logic_and(p);
        if (right == NULL) {
            return NULL;
        }
        ASTNode *bin = new_ast_node(AST_BINARY_EXPR, op->line, op->column);
        bin->as.binary.op = TOK_LOGIC_OR;
        bin->as.binary.left = left;
        bin->as.binary.right = right;
        left = bin;
    }

    return left;
}

static ASTNode *parse_expression(Parser *p) {
    return parse_logic_or(p);
}

/*
 * ReturnStatement grammar:
 *   ReturnStatement := Expression '!' [ '.' ]
 *
 * OmegaPlus uses postfix '!' as return. Period is optional here so this
 * function works with both:
 *   x!
 *   x!.
 */
ASTNode *parse_return_statement(Parser *p) {
    const Token *start = peek_token(p);

    ASTNode *value = parse_expression(p);
    if (value == NULL) {
        return NULL;
    }

    if (expect_token(p, TOK_BANG, "expected postfix '!' return operator") == NULL) {
        return NULL;
    }

    (void)match_token(p, TOK_PERIOD);

    ASTNode *ret = new_ast_node(AST_RETURN_STATEMENT, start->line, start->column);
    ret->as.ret_stmt.value = value;
    ret->type = OMEGA_TYPE_VOID;
    return ret;
}

typedef struct Codegen {
    FILE *out;
    uint32_t next_reg;
} Codegen;

static uint32_t alloc_wreg(Codegen *cg) {
    if (cg->next_reg > 7u) {
        fprintf(stderr, "register allocator exhausted: only w0-w7 are allowed\n");
        exit(EXIT_FAILURE);
    }
    return cg->next_reg++;
}

static uint32_t generate_expr_asm(Codegen *cg, const ASTNode *node);

/*
 * Emits ARM64 logic ops directly:
 *   \/ -> orr wD, wL, wR
 *   /\ -> and wD, wL, wR
 */
uint32_t generate_logic_asm(Codegen *cg, const ASTNode *node) {
    assert(cg != NULL);
    assert(node != NULL);
    assert(node->kind == AST_BINARY_EXPR);
    assert(node->as.binary.op == TOK_LOGIC_OR || node->as.binary.op == TOK_LOGIC_AND);

    uint32_t left = generate_expr_asm(cg, node->as.binary.left);
    uint32_t right = generate_expr_asm(cg, node->as.binary.right);
    uint32_t dst = alloc_wreg(cg);

    if (node->as.binary.op == TOK_LOGIC_OR) {
        fprintf(cg->out, "    orr w%u, w%u, w%u\n", dst, left, right);
    } else {
        fprintf(cg->out, "    and w%u, w%u, w%u\n", dst, left, right);
    }
    return dst;
}

static uint32_t generate_expr_asm(Codegen *cg, const ASTNode *node) {
    switch (node->kind) {
        case AST_BOOL_LITERAL: {
            uint32_t r = alloc_wreg(cg);
            fprintf(cg->out, "    mov w%u, #%u\n", r, node->as.bool_lit.value ? 1u : 0u);
            return r;
        }
        case AST_BINARY_EXPR:
            if (node->as.binary.op == TOK_LOGIC_OR || node->as.binary.op == TOK_LOGIC_AND) {
                return generate_logic_asm(cg, node);
            }
            break;
        default:
            break;
    }

    fprintf(stderr, "unsupported node kind in generate_expr_asm: %d\n", (int)node->kind);
    exit(EXIT_FAILURE);
}

void generate_return_stmt_asm(Codegen *cg, const ASTNode *ret_stmt) {
    assert(ret_stmt != NULL && ret_stmt->kind == AST_RETURN_STATEMENT);
    uint32_t r = generate_expr_asm(cg, ret_stmt->as.ret_stmt.value);
    fprintf(cg->out, "    mov w0, w%u\n", r);
    fprintf(cg->out, "    ret\n");
}
