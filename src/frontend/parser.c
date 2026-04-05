#include "omega/compiler.h"

#include <stdlib.h>
#include <string.h>

static const Token *p_peek(const Parser *p) {
    if (p->pos >= p->count) {
        return &p->tokens[p->count - 1u];
    }
    return &p->tokens[p->pos];
}

static const Token *p_peek_n(const Parser *p, size_t n) {
    size_t idx = p->pos + n;
    if (idx >= p->count) {
        return &p->tokens[p->count - 1u];
    }
    return &p->tokens[idx];
}

static const Token *p_prev(const Parser *p) {
    if (p->pos == 0u) {
        return &p->tokens[0u];
    }
    return &p->tokens[p->pos - 1u];
}

static bool p_check(const Parser *p, TokenKind kind) {
    return p_peek(p)->kind == kind;
}

static const Token *p_advance(Parser *p) {
    const Token *t = p_peek(p);
    if (p->pos < p->count) {
        p->pos++;
    }
    return t;
}

static bool p_match(Parser *p, TokenKind kind) {
    if (!p_check(p, kind)) {
        return false;
    }
    (void)p_advance(p);
    return true;
}

static const Token *p_expect(Parser *p, TokenKind kind, const char *message) {
    const Token *t = p_peek(p);
    if (t->kind != kind) {
        status_fail(&p->st, t->line, t->column, message);
        return NULL;
    }
    return p_advance(p);
}

static bool token_is_type(TokenKind kind) {
    return kind == TOK_TYPE_UINT32 || kind == TOK_TYPE_INT32 || kind == TOK_TYPE_FLOAT32 || kind == TOK_TYPE_STRING ||
           kind == TOK_TYPE_BOOLEAN || kind == TOK_TYPE_VECTOR || kind == TOK_TYPE_VOID || kind == TOK_TYPE_PTR;
}

static bool looks_like_type_start(const Parser *p) {
    TokenKind k = p_peek(p)->kind;
    if (token_is_type(k)) {
        return true;
    }
    /* namespace-qualified type: IDENTIFIER <-> type_keyword */
    if (k == TOK_IDENTIFIER && p_peek_n(p, 1u)->kind == TOK_NS_LINK) {
        return token_is_type(p_peek_n(p, 2u)->kind);
    }
    return false;
}

static OmegaType parse_type(Parser *p) {
    /* consume optional namespace prefix: IDENTIFIER <-> */
    if (p_check(p, TOK_IDENTIFIER) && p_peek_n(p, 1u)->kind == TOK_NS_LINK) {
        (void)p_advance(p); /* namespace name */
        (void)p_advance(p); /* <-> */
    }

    const Token *t = p_peek(p);
    switch (t->kind) {
        case TOK_TYPE_UINT32:
            (void)p_advance(p);
            return OMEGA_TYPE_UINT32;
        case TOK_TYPE_INT32:
            (void)p_advance(p);
            return OMEGA_TYPE_INT32;
        case TOK_TYPE_FLOAT32:
            (void)p_advance(p);
            return OMEGA_TYPE_FLOAT32;
        case TOK_TYPE_STRING:
            (void)p_advance(p);
            return OMEGA_TYPE_STRING;
        case TOK_TYPE_BOOLEAN:
            (void)p_advance(p);
            return OMEGA_TYPE_BOOLEAN;
        case TOK_TYPE_VECTOR:
            (void)p_advance(p);
            if (p_match(p, TOK_LEFT_ARROW)) {
                const Token *elem = p_peek(p);
                if (!p_match(p, TOK_TYPE_INT32)) {
                    status_fail(&p->st, elem->line, elem->column, "vector currently supports only int32 elements");
                    return OMEGA_TYPE_INVALID;
                }
            }
            return OMEGA_TYPE_VECTOR;
        case TOK_TYPE_PTR:
            (void)p_advance(p);
            return OMEGA_TYPE_PTR;
        case TOK_TYPE_VOID:
            (void)p_advance(p);
            return OMEGA_TYPE_VOID;
        default:
            status_fail(&p->st, t->line, t->column,
                        "expected a type (uint32/int32/float32/string/boolean/vector<-int32>)");
            return OMEGA_TYPE_INVALID;
    }
}

static ASTNode *parse_expression(Parser *p);
static ASTNode *parse_statement(Parser *p);
static ASTNode *parse_block(Parser *p);

static ASTNode *make_call_node(uint32_t line, uint32_t col, ASTNode *receiver, bool ns_call, char *ns, char *name,
                               NodeVec args) {
    ASTNode *n = new_node(AST_CALL_EXPR, line, col);
    n->as.call_expr.receiver_expr = receiver;
    n->as.call_expr.is_namespace_call = ns_call;
    n->as.call_expr.ns_name = ns;
    n->as.call_expr.func_name = name;
    n->as.call_expr.arg_count = args.len;
    n->as.call_expr.args = args.data;
    n->as.call_expr.builtin_kind = OMEGA_BUILTIN_NONE;
    return n;
}

static ASTNode *make_member_node(uint32_t line, uint32_t col, ASTNode *receiver, char *member_name) {
    ASTNode *n = new_node(AST_MEMBER_EXPR, line, col);
    n->as.member_expr.receiver_expr = receiver;
    n->as.member_expr.member_name = member_name;
    n->as.member_expr.builtin_kind = OMEGA_BUILTIN_NONE;
    return n;
}

static ASTNode *parse_call_args(Parser *p, uint32_t line, uint32_t col, ASTNode *receiver, bool ns_call, char *ns,
                                char *name) {
    if (p_expect(p, TOK_LPAREN, "expected '(' in function call") == NULL) {
        return NULL;
    }
    NodeVec args = {0};
    if (!p_check(p, TOK_RPAREN)) {
        while (true) {
            ASTNode *arg = parse_expression(p);
            if (arg == NULL) {
                return NULL;
            }
            node_vec_push(&args, arg);
            if (!p_match(p, TOK_COMMA)) {
                break;
            }
        }
    }
    if (p_expect(p, TOK_RPAREN, "expected ')' after arguments") == NULL) {
        return NULL;
    }
    return make_call_node(line, col, receiver, ns_call, ns, name, args);
}

static bool looks_like_member_access(const Parser *p) {
    return p_check(p, TOK_ARROW) && p_peek_n(p, 1u)->kind == TOK_IDENTIFIER;
}

static ASTNode *parse_postfix_suffixes(Parser *p, ASTNode *base) {
    ASTNode *expr = base;
    while (true) {
        if (p_match(p, TOK_LBRACKET)) {
            const Token *open = p_prev(p);
            ASTNode *index = parse_expression(p);
            if (index == NULL) {
                return NULL;
            }
            if (p_expect(p, TOK_RBRACKET, "expected ']' after index expression") == NULL) {
                return NULL;
            }
            ASTNode *n = new_node(AST_INDEX_EXPR, open->line, open->column);
            n->as.index_expr.base_expr = expr;
            n->as.index_expr.index_expr = index;
            expr = n;
            continue;
        }

        if (looks_like_member_access(p)) {
            const Token *dot = p_expect(p, TOK_ARROW, "expected '->' before member name");
            const Token *member = p_expect(p, TOK_IDENTIFIER, "expected member name after '->'");
            if (dot == NULL || member == NULL) {
                return NULL;
            }
            char *name = xstrdup_local(member->lexeme);
            if (p_check(p, TOK_LPAREN)) {
                expr = parse_call_args(p, dot->line, dot->column, expr, false, NULL, name);
                if (expr == NULL) {
                    return NULL;
                }
            } else {
                expr = make_member_node(dot->line, dot->column, expr, name);
            }
            continue;
        }

        break;
    }
    return expr;
}

static ASTNode *parse_atom(Parser *p) {
    const Token *t = p_peek(p);
    ASTNode *base = NULL;

    switch (t->kind) {
        case TOK_BOOL_TRUE:
        case TOK_BOOL_FALSE:
            base = new_node(AST_BOOL_LITERAL, t->line, t->column);
            base->as.bool_lit.value = (t->kind == TOK_BOOL_TRUE);
            base->inferred_type = OMEGA_TYPE_BOOLEAN;
            (void)p_advance(p);
            break;
        case TOK_UINT_LITERAL: {
            base = new_node(AST_UINT_LITERAL, t->line, t->column);
            size_t len = strlen(t->lexeme);
            char *digits = xstrndup_local(t->lexeme, len - 1u);
            base->as.u32_lit.value = (uint32_t)strtoul(digits, NULL, 10);
            base->inferred_type = OMEGA_TYPE_UINT32;
            free(digits);
            (void)p_advance(p);
            break;
        }
        case TOK_INT_LITERAL: {
            base = new_node(AST_INT_LITERAL, t->line, t->column);
            size_t len = strlen(t->lexeme);
            char *digits = xstrndup_local(t->lexeme, len - 1u);
            base->as.i32_lit.value = (int32_t)strtol(digits, NULL, 10);
            base->inferred_type = OMEGA_TYPE_INT32;
            free(digits);
            (void)p_advance(p);
            break;
        }
        case TOK_FLOAT_LITERAL: {
            base = new_node(AST_FLOAT_LITERAL, t->line, t->column);
            size_t len = strlen(t->lexeme);
            char *digits = xstrndup_local(t->lexeme, len - 1u);
            base->as.f32_lit.value = strtof(digits, NULL);
            base->inferred_type = OMEGA_TYPE_FLOAT32;
            free(digits);
            (void)p_advance(p);
            break;
        }
        case TOK_STRING:
            base = new_node(AST_STRING_LITERAL, t->line, t->column);
            base->as.str_lit.value = xstrdup_local(t->lexeme);
            base->inferred_type = OMEGA_TYPE_STRING;
            (void)p_advance(p);
            break;
        case TOK_IDENTIFIER:
        case TOK_MAIN: {
            const Token *id = p_advance(p);
            char *left = xstrdup_local(id->lexeme);
            if (p_match(p, TOK_NS_LINK)) {
                const Token *rhs = p_expect(p, TOK_IDENTIFIER, "expected namespace target function name");
                if (rhs == NULL) {
                    return NULL;
                }
                base = parse_call_args(p, id->line, id->column, NULL, true, left, xstrdup_local(rhs->lexeme));
            } else if (p_check(p, TOK_LPAREN)) {
                base = parse_call_args(p, id->line, id->column, NULL, false, NULL, left);
            } else {
                base = new_node(AST_IDENTIFIER, id->line, id->column);
                base->as.ident.name = left;
            }
            if (base == NULL) {
                return NULL;
            }
            break;
        }
        case TOK_LPAREN:
            (void)p_advance(p);
            base = parse_expression(p);
            if (base == NULL) {
                return NULL;
            }
            if (p_expect(p, TOK_RPAREN, "expected ')' after expression") == NULL) {
                return NULL;
            }
            break;
        default:
            status_fail(&p->st, t->line, t->column, "expected primary expression");
            return NULL;
    }

    return parse_postfix_suffixes(p, base);
}

static ASTNode *parse_unary(Parser *p) {
    if (p_match(p, TOK_LOGIC_NOT)) {
        const Token *op = p_prev(p);
        ASTNode *operand = parse_unary(p);
        if (operand == NULL) {
            return NULL;
        }
        ASTNode *n = new_node(AST_UNARY_EXPR, op->line, op->column);
        n->as.unary_expr.op = TOK_LOGIC_NOT;
        n->as.unary_expr.operand = operand;
        return n;
    }
    if (p_match(p, TOK_MINUS)) {
        const Token *op = p_prev(p);
        ASTNode *operand = parse_unary(p);
        if (operand == NULL) {
            return NULL;
        }
        ASTNode *n = new_node(AST_UNARY_EXPR, op->line, op->column);
        n->as.unary_expr.op = TOK_MINUS;
        n->as.unary_expr.operand = operand;
        return n;
    }
    return parse_atom(p);
}

static ASTNode *parse_multiplicative(Parser *p) {
    ASTNode *left = parse_unary(p);
    if (left == NULL) {
        return NULL;
    }
    while (true) {
        TokenKind op_kind;
        if (p_match(p, TOK_STAR)) {
            op_kind = TOK_STAR;
        } else if (p_match(p, TOK_SLASH)) {
            op_kind = TOK_SLASH;
        } else if (p_match(p, TOK_PERCENT)) {
            op_kind = TOK_PERCENT;
        } else {
            break;
        }
        const Token *op = p_prev(p);
        ASTNode *right = parse_unary(p);
        if (right == NULL) {
            return NULL;
        }
        ASTNode *bin = new_node(AST_BINARY_EXPR, op->line, op->column);
        bin->as.binary_expr.op = op_kind;
        bin->as.binary_expr.left = left;
        bin->as.binary_expr.right = right;
        left = bin;
    }
    return left;
}

static ASTNode *parse_additive(Parser *p) {
    ASTNode *left = parse_multiplicative(p);
    if (left == NULL) {
        return NULL;
    }
    while (true) {
        TokenKind op_kind;
        if (p_match(p, TOK_PLUS)) {
            op_kind = TOK_PLUS;
        } else if (p_match(p, TOK_MINUS)) {
            op_kind = TOK_MINUS;
        } else {
            break;
        }
        const Token *op = p_prev(p);
        ASTNode *right = parse_multiplicative(p);
        if (right == NULL) {
            return NULL;
        }
        ASTNode *bin = new_node(AST_BINARY_EXPR, op->line, op->column);
        bin->as.binary_expr.op = op_kind;
        bin->as.binary_expr.left = left;
        bin->as.binary_expr.right = right;
        left = bin;
    }
    return left;
}

static ASTNode *parse_comparison(Parser *p) {
    ASTNode *left = parse_additive(p);
    if (left == NULL) {
        return NULL;
    }
    while (true) {
        TokenKind op_kind;
        if (p_match(p, TOK_LE)) {
            op_kind = TOK_LE;
        } else if (p_match(p, TOK_LT)) {
            op_kind = TOK_LT;
        } else if (p_match(p, TOK_GE)) {
            op_kind = TOK_GE;
        } else if (p_match(p, TOK_GT)) {
            op_kind = TOK_GT;
        } else {
            break;
        }
        const Token *op = p_prev(p);
        ASTNode *right = parse_additive(p);
        if (right == NULL) {
            return NULL;
        }
        ASTNode *bin = new_node(AST_BINARY_EXPR, op->line, op->column);
        bin->as.binary_expr.op = op_kind;
        bin->as.binary_expr.left = left;
        bin->as.binary_expr.right = right;
        left = bin;
    }
    return left;
}

static ASTNode *parse_equality(Parser *p) {
    ASTNode *left = parse_comparison(p);
    if (left == NULL) {
        return NULL;
    }
    while (true) {
        TokenKind op_kind;
        if (p_match(p, TOK_EQEQ)) {
            op_kind = TOK_EQEQ;
        } else if (p_match(p, TOK_NEQ)) {
            op_kind = TOK_NEQ;
        } else {
            break;
        }
        const Token *op = p_prev(p);
        ASTNode *right = parse_comparison(p);
        if (right == NULL) {
            return NULL;
        }
        ASTNode *bin = new_node(AST_BINARY_EXPR, op->line, op->column);
        bin->as.binary_expr.op = op_kind;
        bin->as.binary_expr.left = left;
        bin->as.binary_expr.right = right;
        left = bin;
    }
    return left;
}

static ASTNode *parse_logic_and(Parser *p) {
    ASTNode *left = parse_equality(p);
    if (left == NULL) {
        return NULL;
    }
    while (p_match(p, TOK_LOGIC_AND)) {
        const Token *op = p_prev(p);
        ASTNode *right = parse_equality(p);
        if (right == NULL) {
            return NULL;
        }
        ASTNode *bin = new_node(AST_BINARY_EXPR, op->line, op->column);
        bin->as.binary_expr.op = TOK_LOGIC_AND;
        bin->as.binary_expr.left = left;
        bin->as.binary_expr.right = right;
        left = bin;
    }
    return left;
}

static ASTNode *parse_logic_or(Parser *p) {
    ASTNode *left = parse_logic_and(p);
    if (left == NULL) {
        return NULL;
    }
    while (p_match(p, TOK_LOGIC_OR)) {
        const Token *op = p_prev(p);
        ASTNode *right = parse_logic_and(p);
        if (right == NULL) {
            return NULL;
        }
        ASTNode *bin = new_node(AST_BINARY_EXPR, op->line, op->column);
        bin->as.binary_expr.op = TOK_LOGIC_OR;
        bin->as.binary_expr.left = left;
        bin->as.binary_expr.right = right;
        left = bin;
    }
    return left;
}

static ASTNode *parse_expression(Parser *p) {
    return parse_logic_or(p);
}

static bool looks_like_return_statement(const Parser *p) {
    size_t i = p->pos;
    int depth = 0;
    while (i < p->count) {
        TokenKind k = p->tokens[i].kind;
        if (k == TOK_LPAREN) {
            depth++;
        } else if (k == TOK_RPAREN) {
            if (depth > 0) {
                depth--;
            }
        } else if (depth == 0) {
            if (k == TOK_BANG) {
                return true;
            }
            if (k == TOK_PERIOD || k == TOK_RBRACE) {
                return false;
            }
        }
        i++;
    }
    return false;
}

static ASTNode *parse_return_statement(Parser *p) {
    const Token *start = p_peek(p);
    ASTNode *value = parse_expression(p);
    if (value == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_BANG, "expected postfix '!' in return statement") == NULL) {
        return NULL;
    }
    (void)p_match(p, TOK_PERIOD);
    ASTNode *n = new_node(AST_RETURN_STMT, start->line, start->column);
    n->as.return_stmt.value_expr = value;
    return n;
}

static ASTNode *parse_if_statement(Parser *p) {
    const Token *if_tok = p_prev(p);
    if (p_expect(p, TOK_LPAREN, "expected '(' after if") == NULL) {
        return NULL;
    }
    ASTNode *cond = parse_expression(p);
    if (cond == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_RPAREN, "expected ')' after if condition") == NULL) {
        return NULL;
    }

    ASTNode *then_block = NULL;
    ASTNode *else_block = NULL;
    if (p_check(p, TOK_LBRACE)) {
        then_block = parse_block(p);
    } else {
        ASTNode *single = parse_statement(p);
        if (single != NULL) {
            NodeVec stmts = {0};
            node_vec_push(&stmts, single);
            then_block = new_node(AST_BLOCK, single->line, single->column);
            then_block->as.block.statement_count = stmts.len;
            then_block->as.block.statements = stmts.data;
        }
    }
    if (then_block == NULL) {
        return NULL;
    }

    if (p_match(p, TOK_ELSE)) {
        if (p_check(p, TOK_LBRACE)) {
            else_block = parse_block(p);
        } else {
            ASTNode *single = parse_statement(p);
            if (single != NULL) {
                NodeVec stmts = {0};
                node_vec_push(&stmts, single);
                else_block = new_node(AST_BLOCK, single->line, single->column);
                else_block->as.block.statement_count = stmts.len;
                else_block->as.block.statements = stmts.data;
            }
        }
        if (else_block == NULL) {
            return NULL;
        }
    }

    ASTNode *n = new_node(AST_IF_STMT, if_tok->line, if_tok->column);
    n->as.if_stmt.cond_expr = cond;
    n->as.if_stmt.then_block = then_block;
    n->as.if_stmt.else_block = else_block;
    return n;
}

static ASTNode *parse_while_statement(Parser *p) {
    const Token *while_tok = p_prev(p);
    if (p_expect(p, TOK_LPAREN, "expected '(' after while") == NULL) {
        return NULL;
    }
    ASTNode *cond = parse_expression(p);
    if (cond == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_RPAREN, "expected ')' after while condition") == NULL) {
        return NULL;
    }

    ASTNode *body_block = NULL;
    if (p_check(p, TOK_LBRACE)) {
        body_block = parse_block(p);
    } else {
        ASTNode *single = parse_statement(p);
        if (single != NULL) {
            NodeVec stmts = {0};
            node_vec_push(&stmts, single);
            body_block = new_node(AST_BLOCK, single->line, single->column);
            body_block->as.block.statement_count = stmts.len;
            body_block->as.block.statements = stmts.data;
        }
    }
    if (body_block == NULL) {
        return NULL;
    }

    ASTNode *n = new_node(AST_WHILE_STMT, while_tok->line, while_tok->column);
    n->as.while_stmt.cond_expr = cond;
    n->as.while_stmt.body_block = body_block;
    return n;
}

static ASTNode *parse_var_decl_statement(Parser *p) {
    const Token *start = p_peek(p);
    OmegaType t = parse_type(p);
    if (!p->st.ok) {
        return NULL;
    }

    NodeVec decls = {0};
    while (true) {
        const Token *name = p_expect(p, TOK_IDENTIFIER, "expected variable name");
        if (name == NULL) {
            return NULL;
        }
        ASTNode *init = NULL;
        if (p_match(p, TOK_ASSIGN)) {
            init = parse_expression(p);
            if (init == NULL) {
                return NULL;
            }
        }
        ASTNode *decl = new_node(AST_VAR_DECL, name->line, name->column);
        decl->as.var_decl.var_type = t;
        decl->as.var_decl.name = xstrdup_local(name->lexeme);
        decl->as.var_decl.init_expr = init;
        node_vec_push(&decls, decl);
        if (!p_match(p, TOK_COMMA)) {
            break;
        }
    }

    if (p_expect(p, TOK_PERIOD, "expected '.' after variable declaration") == NULL) {
        return NULL;
    }
    if (decls.len == 1u) {
        return decls.data[0];
    }

    ASTNode *block = new_node(AST_BLOCK, start->line, start->column);
    block->as.block.statement_count = decls.len;
    block->as.block.statements = decls.data;
    return block;
}

static ASTNode *parse_assignment_statement(Parser *p) {
    const Token *name = p_expect(p, TOK_IDENTIFIER, "expected assignment target");
    if (name == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_ASSIGN, "expected '=' in assignment statement") == NULL) {
        return NULL;
    }
    ASTNode *value = parse_expression(p);
    if (value == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_PERIOD, "expected '.' after assignment statement") == NULL) {
        return NULL;
    }

    ASTNode *n = new_node(AST_ASSIGN_STMT, name->line, name->column);
    n->as.assign_stmt.name = xstrdup_local(name->lexeme);
    n->as.assign_stmt.value_expr = value;
    return n;
}

static ASTNode *parse_expr_statement(Parser *p) {
    const Token *start = p_peek(p);
    ASTNode *expr = parse_expression(p);
    if (expr == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_PERIOD, "expected '.' after expression statement") == NULL) {
        return NULL;
    }
    ASTNode *n = new_node(AST_EXPR_STMT, start->line, start->column);
    n->as.expr_stmt.expr = expr;
    return n;
}

static ASTNode *parse_statement(Parser *p) {
    if (p_match(p, TOK_IF)) {
        return parse_if_statement(p);
    }
    if (p_match(p, TOK_WHILE)) {
        return parse_while_statement(p);
    }
    if (p_match(p, TOK_BREAK)) {
        const Token *t = p_prev(p);
        if (p_expect(p, TOK_PERIOD, "expected '.' after break") == NULL) {
            return NULL;
        }
        return new_node(AST_BREAK_STMT, t->line, t->column);
    }
    if (p_match(p, TOK_CONTINUE)) {
        const Token *t = p_prev(p);
        if (p_expect(p, TOK_PERIOD, "expected '.' after continue") == NULL) {
            return NULL;
        }
        return new_node(AST_CONTINUE_STMT, t->line, t->column);
    }
    if (looks_like_type_start(p)) {
        return parse_var_decl_statement(p);
    }
    if (p_check(p, TOK_IDENTIFIER) && p_peek_n(p, 1u)->kind == TOK_ASSIGN) {
        return parse_assignment_statement(p);
    }
    if (looks_like_return_statement(p)) {
        return parse_return_statement(p);
    }
    return parse_expr_statement(p);
}

static ASTNode *parse_block(Parser *p) {
    const Token *brace = p_expect(p, TOK_LBRACE, "expected '{' to start block");
    if (brace == NULL) {
        return NULL;
    }
    NodeVec stmts = {0};
    while (!p_check(p, TOK_RBRACE) && !p_check(p, TOK_EOF)) {
        ASTNode *stmt = parse_statement(p);
        if (stmt == NULL) {
            return NULL;
        }
        node_vec_push(&stmts, stmt);
    }
    if (p_expect(p, TOK_RBRACE, "expected '}' to close block") == NULL) {
        return NULL;
    }
    ASTNode *n = new_node(AST_BLOCK, brace->line, brace->column);
    n->as.block.statement_count = stmts.len;
    n->as.block.statements = stmts.data;
    return n;
}

static ASTNode *parse_import_decl(Parser *p) {
    const Token *kw = p_prev(p);
    const Token *name = p_peek(p);

    if (p_match(p, TOK_STRING)) {
        /* import "file.c". */
        if (p_expect(p, TOK_PERIOD, "expected '.' after import file path") == NULL) {
            return NULL;
        }
        ASTNode *n = new_node(AST_IMPORT, kw->line, kw->column);
        n->as.import_stmt.module = xstrdup_local("<file>");
        n->as.import_stmt.file_path = xstrdup_local(name->lexeme);
        n->as.import_stmt.is_file_import = true;
        return n;
    }

    if (!(p_match(p, TOK_IDENTIFIER) || p_match(p, TOK_TYPE_VECTOR))) {
        status_fail(&p->st, name->line, name->column, "expected module name or file path after import");
        return NULL;
    }
    if (p_expect(p, TOK_PERIOD, "expected '.' after import statement") == NULL) {
        return NULL;
    }
    ASTNode *n = new_node(AST_IMPORT, kw->line, kw->column);
    n->as.import_stmt.module = xstrdup_local(name->lexeme);
    n->as.import_stmt.is_file_import = false;
    return n;
}

static ASTNode *parse_function_decl(Parser *p) {
    const Token *start = p_peek(p);
    OmegaType ret_t = parse_type(p);
    if (!p->st.ok) {
        return NULL;
    }
    const Token *name = p_expect(p, TOK_IDENTIFIER, "expected function name");
    if (name == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_LPAREN, "expected '(' after function name") == NULL) {
        return NULL;
    }

    ParamVec params = {0};
    if (!p_check(p, TOK_RPAREN)) {
        while (true) {
            OmegaType ptype = parse_type(p);
            if (!p->st.ok) {
                return NULL;
            }
            const Token *pname = p_expect(p, TOK_IDENTIFIER, "expected parameter name");
            if (pname == NULL) {
                return NULL;
            }
            Param param;
            param.type = ptype;
            param.name = xstrdup_local(pname->lexeme);
            param.line = pname->line;
            param.column = pname->column;
            param_vec_push(&params, param);
            if (!p_match(p, TOK_COMMA)) {
                break;
            }
        }
    }
    if (p_expect(p, TOK_RPAREN, "expected ')' after parameter list") == NULL) {
        return NULL;
    }

    ASTNode *body = parse_block(p);
    if (body == NULL) {
        return NULL;
    }

    ASTNode *fn = new_node(AST_FUNCTION, start->line, start->column);
    fn->as.function.name = xstrdup_local(name->lexeme);
    fn->as.function.return_type = ret_t;
    fn->as.function.params = params.data;
    fn->as.function.param_count = params.len;
    fn->as.function.body = body;
    fn->as.function.is_entrypoint = false;
    return fn;
}

static ASTNode *parse_entrypoint_decl(Parser *p) {
    const Token *arrow = p_prev(p);
    const Token *name = p_peek(p);
    if (!(p_match(p, TOK_MAIN) || p_match(p, TOK_IDENTIFIER))) {
        status_fail(&p->st, name->line, name->column, "expected main after '->'");
        return NULL;
    }
    if (strcmp(p_prev(p)->lexeme, "main") != 0) {
        status_fail(&p->st, p_prev(p)->line, p_prev(p)->column, "entrypoint must be named main");
        return NULL;
    }
    if (p_expect(p, TOK_LPAREN, "expected '(' after main") == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_RPAREN, "expected ')' after main(") == NULL) {
        return NULL;
    }
    ASTNode *body = parse_block(p);
    if (body == NULL) {
        return NULL;
    }

    ASTNode *fn = new_node(AST_FUNCTION, arrow->line, arrow->column);
    fn->as.function.name = xstrdup_local("main");
    fn->as.function.return_type = OMEGA_TYPE_INT32;
    fn->as.function.params = NULL;
    fn->as.function.param_count = 0u;
    fn->as.function.body = body;
    fn->as.function.is_entrypoint = true;
    return fn;
}

static bool parse_module_block(Parser *p, NodeVec *out_decls) {
    const Token *name_tok = p_expect(p, TOK_IDENTIFIER, "expected module name after 'module'");
    if (name_tok == NULL) {
        return false;
    }
    char *module_name = xstrdup_local(name_tok->lexeme);
    if (p_expect(p, TOK_LBRACE, "expected '{' after module name") == NULL) {
        return false;
    }
    while (!p_check(p, TOK_RBRACE) && !p_check(p, TOK_EOF)) {
        const Token *start = p_peek(p);
        OmegaType ret_t = parse_type(p);
        if (!p->st.ok) {
            return false;
        }
        const Token *fname = p_expect(p, TOK_IDENTIFIER, "expected function name in module");
        if (fname == NULL) {
            return false;
        }
        if (p_expect(p, TOK_LPAREN, "expected '(' after function name") == NULL) {
            return false;
        }
        ParamVec params = {0};
        if (!p_check(p, TOK_RPAREN)) {
            while (true) {
                OmegaType ptype = parse_type(p);
                if (!p->st.ok) {
                    return false;
                }
                const Token *pname = p_expect(p, TOK_IDENTIFIER, "expected parameter name");
                if (pname == NULL) {
                    return false;
                }
                Param param;
                param.type = ptype;
                param.name = xstrdup_local(pname->lexeme);
                param.line = pname->line;
                param.column = pname->column;
                param_vec_push(&params, param);
                if (!p_match(p, TOK_COMMA)) {
                    break;
                }
            }
        }
        if (p_expect(p, TOK_RPAREN, "expected ')' after parameters") == NULL) {
            return false;
        }
        if (p_expect(p, TOK_PERIOD, "expected '.' after function declaration in module") == NULL) {
            return false;
        }
        ASTNode *fn = new_node(AST_FUNCTION, start->line, start->column);
        fn->as.function.name = xstrdup_local(fname->lexeme);
        fn->as.function.return_type = ret_t;
        fn->as.function.params = params.data;
        fn->as.function.param_count = params.len;
        fn->as.function.body = NULL;
        fn->as.function.is_entrypoint = false;
        fn->as.function.is_extern = true;
        fn->as.function.module_name = module_name;
        node_vec_push(out_decls, fn);
    }
    if (p_expect(p, TOK_RBRACE, "expected '}' to close module block") == NULL) {
        return false;
    }
    return true;
}

static ASTNode *parse_extern_decl(Parser *p) {
    const Token *start = p_peek(p);
    OmegaType ret_t = parse_type(p);
    if (!p->st.ok) {
        return NULL;
    }
    const Token *name = p_expect(p, TOK_IDENTIFIER, "expected function name after extern");
    if (name == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_LPAREN, "expected '(' after extern function name") == NULL) {
        return NULL;
    }

    ParamVec params = {0};
    if (!p_check(p, TOK_RPAREN)) {
        while (true) {
            OmegaType ptype = parse_type(p);
            if (!p->st.ok) {
                return NULL;
            }
            const Token *pname = p_expect(p, TOK_IDENTIFIER, "expected parameter name");
            if (pname == NULL) {
                return NULL;
            }
            Param param;
            param.type = ptype;
            param.name = xstrdup_local(pname->lexeme);
            param.line = pname->line;
            param.column = pname->column;
            param_vec_push(&params, param);
            if (!p_match(p, TOK_COMMA)) {
                break;
            }
        }
    }
    if (p_expect(p, TOK_RPAREN, "expected ')' after extern parameter list") == NULL) {
        return NULL;
    }
    if (p_expect(p, TOK_PERIOD, "expected '.' after extern declaration") == NULL) {
        return NULL;
    }

    ASTNode *fn = new_node(AST_FUNCTION, start->line, start->column);
    fn->as.function.name = xstrdup_local(name->lexeme);
    fn->as.function.return_type = ret_t;
    fn->as.function.params = params.data;
    fn->as.function.param_count = params.len;
    fn->as.function.body = NULL;
    fn->as.function.is_entrypoint = false;
    fn->as.function.is_extern = true;
    return fn;
}

ASTNode *parse_program(Parser *p) {
    NodeVec decls = {0};
    while (!p_check(p, TOK_EOF)) {
        if (p_match(p, TOK_IMPORT)) {
            ASTNode *d = parse_import_decl(p);
            if (d == NULL) return NULL;
            node_vec_push(&decls, d);
        } else if (p_match(p, TOK_MODULE)) {
            if (!parse_module_block(p, &decls)) return NULL;
        } else if (p_match(p, TOK_EXTERN)) {
            ASTNode *d = parse_extern_decl(p);
            if (d == NULL) return NULL;
            node_vec_push(&decls, d);
        } else if (looks_like_type_start(p)) {
            ASTNode *d = parse_function_decl(p);
            if (d == NULL) return NULL;
            node_vec_push(&decls, d);
        } else if (p_match(p, TOK_ARROW)) {
            ASTNode *d = parse_entrypoint_decl(p);
            if (d == NULL) return NULL;
            node_vec_push(&decls, d);
        } else {
            const Token *t = p_peek(p);
            status_fail(&p->st, t->line, t->column, "unexpected token at top level");
            return NULL;
        }
    }
    ASTNode *program = new_node(AST_PROGRAM, 1u, 1u);
    program->as.program.decls = decls.data;
    program->as.program.decl_count = decls.len;
    return program;
}
