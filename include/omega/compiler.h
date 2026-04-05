#ifndef OMEGA_COMPILER_H
#define OMEGA_COMPILER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "omega/modules.h"

typedef enum TokenKind {
    TOK_EOF = 0,

    TOK_IDENTIFIER,
    TOK_STRING,
    TOK_UINT_LITERAL,
    TOK_INT_LITERAL,
    TOK_FLOAT_LITERAL,
    TOK_BOOL_TRUE,
    TOK_BOOL_FALSE,

    TOK_IMPORT,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_MAIN,
    TOK_TYPE_UINT32,
    TOK_TYPE_INT32,
    TOK_TYPE_FLOAT32,
    TOK_TYPE_STRING,
    TOK_TYPE_BOOLEAN,
    TOK_TYPE_VECTOR,

    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_PERIOD,
    TOK_ASSIGN,
    TOK_EQEQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_BANG,
    TOK_ARROW,
    TOK_LEFT_ARROW,

    TOK_LOGIC_NOT,
    TOK_LOGIC_OR,
    TOK_LOGIC_AND,
    TOK_NS_LINK,
    TOK_EXTERN,
    TOK_MODULE,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_TYPE_VOID,
    TOK_TYPE_PTR
} TokenKind;

typedef struct Token {
    TokenKind kind;
    char *lexeme;
    uint32_t line;
    uint32_t column;
} Token;

typedef enum OmegaType {
    OMEGA_TYPE_INVALID = 0,
    OMEGA_TYPE_UINT32,
    OMEGA_TYPE_INT32,
    OMEGA_TYPE_FLOAT32,
    OMEGA_TYPE_BOOLEAN,
    OMEGA_TYPE_STRING,
    OMEGA_TYPE_VECTOR,
    OMEGA_TYPE_PTR,
    OMEGA_TYPE_VOID
} OmegaType;

typedef struct Param {
    OmegaType type;
    char *name;
    uint32_t line;
    uint32_t column;
} Param;

typedef enum ASTNodeKind {
    AST_INVALID = 0,
    AST_PROGRAM,
    AST_IMPORT,
    AST_FUNCTION,
    AST_BLOCK,
    AST_VAR_DECL,
    AST_ASSIGN_STMT,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_EXPR_STMT,
    AST_RETURN_STMT,
    AST_CALL_EXPR,
    AST_MEMBER_EXPR,
    AST_IDENTIFIER,
    AST_UINT_LITERAL,
    AST_INT_LITERAL,
    AST_FLOAT_LITERAL,
    AST_BOOL_LITERAL,
    AST_STRING_LITERAL,
    AST_INDEX_EXPR,
    AST_UNARY_EXPR,
    AST_BINARY_EXPR,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT
} ASTNodeKind;

typedef struct ASTNode ASTNode;

struct ASTNode {
    ASTNodeKind kind;
    OmegaType inferred_type;
    uint32_t line;
    uint32_t column;
    union {
        struct {
            ASTNode **decls;
            size_t decl_count;
        } program;
        struct {
            char *module;
            char *file_path;    /* non-NULL for import "file.c". */
            bool is_file_import;
        } import_stmt;
        struct {
            char *name;
            OmegaType return_type;
            Param *params;
            size_t param_count;
            ASTNode *body;
            bool is_entrypoint;
            bool is_extern;
            char *module_name;  /* non-NULL for module { } declarations */
        } function;
        struct {
            ASTNode **statements;
            size_t statement_count;
        } block;
        struct {
            OmegaType var_type;
            char *name;
            ASTNode *init_expr;
        } var_decl;
        struct {
            char *name;
            ASTNode *value_expr;
        } assign_stmt;
        struct {
            ASTNode *cond_expr;
            ASTNode *then_block;
            ASTNode *else_block;
        } if_stmt;
        struct {
            ASTNode *cond_expr;
            ASTNode *body_block;
        } while_stmt;
        struct {
            ASTNode *expr;
        } expr_stmt;
        struct {
            ASTNode *value_expr;
        } return_stmt;
        struct {
            ASTNode *receiver_expr;
            bool is_namespace_call;
            char *ns_name;
            char *func_name;
            ASTNode **args;
            size_t arg_count;
            OmegaBuiltinKind builtin_kind;
        } call_expr;
        struct {
            ASTNode *receiver_expr;
            char *member_name;
            OmegaBuiltinKind builtin_kind;
        } member_expr;
        struct {
            char *name;
        } ident;
        struct {
            uint32_t value;
        } u32_lit;
        struct {
            int32_t value;
        } i32_lit;
        struct {
            float value;
        } f32_lit;
        struct {
            bool value;
        } bool_lit;
        struct {
            char *value;
        } str_lit;
        struct {
            ASTNode *base_expr;
            ASTNode *index_expr;
        } index_expr;
        struct {
            TokenKind op;
            ASTNode *operand;
        } unary_expr;
        struct {
            TokenKind op;
            ASTNode *left;
            ASTNode *right;
        } binary_expr;
    } as;
};

typedef struct {
    bool ok;
    uint32_t line;
    uint32_t column;
    char message[256];
} Status;

typedef struct {
    Token *data;
    size_t len;
    size_t cap;
} TokenVec;

typedef struct {
    ASTNode **data;
    size_t len;
    size_t cap;
} NodeVec;

typedef struct {
    Param *data;
    size_t len;
    size_t cap;
} ParamVec;

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    uint32_t line;
    uint32_t column;
    TokenVec tokens;
    Status st;
} Lexer;

typedef struct {
    const Token *tokens;
    size_t count;
    size_t pos;
    Status st;
} Parser;

typedef struct {
    char *name;
    OmegaType type;
    uint32_t line;
    uint32_t column;
} VarSymbol;

typedef struct Scope {
    VarSymbol *vars;
    size_t var_count;
    size_t var_cap;
    struct Scope *parent;
} Scope;

typedef struct {
    char *name;
    char *module_name;  /* non-NULL for user module functions */
    OmegaType return_type;
    Param *params;
    size_t param_count;
    ASTNode *decl_node;
    bool is_entrypoint;
    bool is_extern;
} FunctionSymbol;

typedef struct {
    FunctionSymbol *funcs;
    size_t func_count;
    size_t func_cap;
    FunctionSymbol *module_funcs;   /* user-defined module functions */
    size_t module_func_count;
    size_t module_func_cap;
    char **imports;
    size_t import_count;
    size_t import_cap;
    int loop_depth;                 /* for break/continue validation */
    Status st;
} Semantic;

typedef struct {
    char *value;
    char label[32];
} CStringEntry;

typedef struct {
    uint32_t bits;
    char label[32];
} CFloatEntry;

typedef struct {
    FILE *out;
    CStringEntry *strings;
    size_t string_count;
    size_t string_cap;
    CFloatEntry *floats;
    size_t float_count;
    size_t float_cap;
    int next_string_id;
    int next_float_id;
    int next_label_id;
    bool need_sort_helper;
} Codegen;

typedef struct {
    char *name;
    OmegaType type;
    int offset;
} LocalSlot;

typedef struct {
    char begin_label[64];
    char end_label[64];
} LoopLabel;

typedef struct {
    Codegen *cg;
    ASTNode *fn_node;
    LocalSlot *locals;
    size_t local_count;
    size_t local_cap;
    int next_offset;
    int frame_size;
    char return_label[64];
    LoopLabel *loops;
    size_t loop_depth;
    size_t loop_cap;
} FunctionCodegen;

void status_fail(Status *st, uint32_t line, uint32_t column, const char *message);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrndup_local(const char *s, size_t n);
char *xstrdup_local(const char *s);
void token_vec_push(TokenVec *v, Token t);
void node_vec_push(NodeVec *v, ASTNode *n);
void param_vec_push(ParamVec *v, Param p);
ASTNode *new_node(ASTNodeKind kind, uint32_t line, uint32_t column);
const char *type_name(OmegaType t);
bool type_is_numeric(OmegaType t);
bool type_is_integral(OmegaType t);
bool type_is_pointer_like(OmegaType t);
bool vector_type_allows_element(OmegaType t);
int align16(int n);
char *read_whole_file(const char *path);
char *strip_ext(const char *path);
void print_usage(const char *argv0);
bool run_macho_toolchain(const char *asm_path, const char *obj_path, const char *exe_path,
                         char **c_files, size_t c_file_count);

bool lex_all(Lexer *lx);
ASTNode *parse_program(Parser *p);
bool semantic_analyze(Semantic *sem, ASTNode *program);
bool generate_program_asm(ASTNode *program, const char *asm_path, Status *st);

#endif
