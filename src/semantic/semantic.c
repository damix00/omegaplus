#include "omega/compiler.h"

#include <assert.h>
#include <string.h>

static void semantic_fail(Semantic *sem, uint32_t line, uint32_t column, const char *message) {
    status_fail(&sem->st, line, column, message);
}

static FunctionSymbol *find_function(Semantic *sem, const char *name) {
    for (size_t i = 0; i < sem->func_count; i++) {
        if (strcmp(sem->funcs[i].name, name) == 0) {
            return &sem->funcs[i];
        }
    }
    return NULL;
}

static FunctionSymbol *find_module_function(Semantic *sem, const char *module_name, const char *func_name) {
    for (size_t i = 0; i < sem->module_func_count; i++) {
        if (strcmp(sem->module_funcs[i].module_name, module_name) == 0 &&
            strcmp(sem->module_funcs[i].name, func_name) == 0) {
            return &sem->module_funcs[i];
        }
    }
    return NULL;
}

static void push_module_function_symbol(Semantic *sem, FunctionSymbol fn) {
    if (sem->module_func_count == sem->module_func_cap) {
        size_t next = (sem->module_func_cap == 0u) ? 8u : sem->module_func_cap * 2u;
        sem->module_funcs = (FunctionSymbol *)xrealloc(sem->module_funcs, next * sizeof(FunctionSymbol));
        sem->module_func_cap = next;
    }
    sem->module_funcs[sem->module_func_count++] = fn;
}

static void push_function_symbol(Semantic *sem, FunctionSymbol fn) {
    if (sem->func_count == sem->func_cap) {
        size_t next = (sem->func_cap == 0u) ? 8u : sem->func_cap * 2u;
        sem->funcs = (FunctionSymbol *)xrealloc(sem->funcs, next * sizeof(FunctionSymbol));
        sem->func_cap = next;
    }
    sem->funcs[sem->func_count++] = fn;
}

static bool sem_has_import(const Semantic *sem, const char *module_name) {
    for (size_t i = 0; i < sem->import_count; i++) {
        if (strcmp(sem->imports[i], module_name) == 0) {
            return true;
        }
    }
    return false;
}

static bool sem_has_vector_import(const Semantic *sem) {
    return sem_has_import(sem, "sti") || sem_has_import(sem, "vector");
}

static void sem_add_import(Semantic *sem, const char *module_name) {
    if (sem_has_import(sem, module_name)) {
        return;
    }
    if (sem->import_count == sem->import_cap) {
        size_t next = (sem->import_cap == 0u) ? 8u : sem->import_cap * 2u;
        sem->imports = (char **)xrealloc(sem->imports, next * sizeof(char *));
        sem->import_cap = next;
    }
    sem->imports[sem->import_count++] = xstrdup_local(module_name);
}

static void scope_add_var(Scope *scope, const VarSymbol *var, Status *st) {
    for (Scope *s = scope; s != NULL; s = s->parent) {
        for (size_t i = 0; i < s->var_count; i++) {
            if (strcmp(s->vars[i].name, var->name) == 0) {
                status_fail(st, var->line, var->column, "variable already declared in this function scope");
                return;
            }
        }
    }
    if (scope->var_count == scope->var_cap) {
        size_t next = (scope->var_cap == 0u) ? 8u : scope->var_cap * 2u;
        scope->vars = (VarSymbol *)xrealloc(scope->vars, next * sizeof(VarSymbol));
        scope->var_cap = next;
    }
    scope->vars[scope->var_count++] = *var;
}

static VarSymbol *scope_find_var(Scope *scope, const char *name) {
    for (Scope *s = scope; s != NULL; s = s->parent) {
        for (size_t i = 0; i < s->var_count; i++) {
            if (strcmp(s->vars[i].name, name) == 0) {
                return &s->vars[i];
            }
        }
    }
    return NULL;
}

static OmegaType analyze_expr(Semantic *sem, ASTNode *expr, Scope *scope);
static void analyze_block(Semantic *sem, ASTNode *block, Scope *scope, const FunctionSymbol *fn);

static void analyze_stmt(Semantic *sem, ASTNode *stmt, Scope *scope, const FunctionSymbol *fn) {
    if (!sem->st.ok) {
        return;
    }
    switch (stmt->kind) {
        case AST_BLOCK:
            analyze_block(sem, stmt, scope, fn);
            return;
        case AST_VAR_DECL: {
            if (stmt->as.var_decl.var_type == OMEGA_TYPE_VECTOR && !sem_has_vector_import(sem)) {
                semantic_fail(sem, stmt->line, stmt->column, "vector type requires 'import sti.' or 'import vector.'");
                return;
            }
            if (stmt->as.var_decl.init_expr != NULL) {
                OmegaType rhs = analyze_expr(sem, stmt->as.var_decl.init_expr, scope);
                if (!sem->st.ok) {
                    return;
                }
                if (rhs != stmt->as.var_decl.var_type) {
                    char msg[256];
                    (void)snprintf(msg, sizeof(msg), "type mismatch in declaration of '%s': expected %s, got %s",
                                   stmt->as.var_decl.name, type_name(stmt->as.var_decl.var_type), type_name(rhs));
                    semantic_fail(sem, stmt->line, stmt->column, msg);
                    return;
                }
            }
            VarSymbol var;
            var.name = stmt->as.var_decl.name;
            var.type = stmt->as.var_decl.var_type;
            var.line = stmt->line;
            var.column = stmt->column;
            scope_add_var(scope, &var, &sem->st);
            return;
        }
        case AST_ASSIGN_STMT: {
            VarSymbol *var = scope_find_var(scope, stmt->as.assign_stmt.name);
            if (var == NULL) {
                semantic_fail(sem, stmt->line, stmt->column, "assignment target is not declared");
                return;
            }
            OmegaType rhs = analyze_expr(sem, stmt->as.assign_stmt.value_expr, scope);
            if (!sem->st.ok) {
                return;
            }
            if (rhs != var->type) {
                char msg[256];
                (void)snprintf(msg, sizeof(msg), "assignment type mismatch for '%s': expected %s, got %s",
                               stmt->as.assign_stmt.name, type_name(var->type), type_name(rhs));
                semantic_fail(sem, stmt->line, stmt->column, msg);
            }
            return;
        }
        case AST_IF_STMT: {
            OmegaType cond = analyze_expr(sem, stmt->as.if_stmt.cond_expr, scope);
            if (!sem->st.ok) {
                return;
            }
            if (cond != OMEGA_TYPE_BOOLEAN) {
                semantic_fail(sem, stmt->line, stmt->column, "if condition must have type boolean");
                return;
            }
            Scope child = {0};
            child.parent = scope;
            analyze_block(sem, stmt->as.if_stmt.then_block, &child, fn);
            if (!sem->st.ok) {
                return;
            }
            if (stmt->as.if_stmt.else_block != NULL) {
                Scope else_child = {0};
                else_child.parent = scope;
                analyze_block(sem, stmt->as.if_stmt.else_block, &else_child, fn);
            }
            return;
        }
        case AST_WHILE_STMT: {
            OmegaType cond = analyze_expr(sem, stmt->as.while_stmt.cond_expr, scope);
            if (!sem->st.ok) {
                return;
            }
            if (cond != OMEGA_TYPE_BOOLEAN) {
                semantic_fail(sem, stmt->line, stmt->column, "while condition must have type boolean");
                return;
            }
            Scope child = {0};
            child.parent = scope;
            sem->loop_depth++;
            analyze_block(sem, stmt->as.while_stmt.body_block, &child, fn);
            sem->loop_depth--;
            return;
        }
        case AST_BREAK_STMT:
            if (sem->loop_depth == 0) {
                semantic_fail(sem, stmt->line, stmt->column, "break outside loop");
            }
            return;
        case AST_CONTINUE_STMT:
            if (sem->loop_depth == 0) {
                semantic_fail(sem, stmt->line, stmt->column, "continue outside loop");
            }
            return;
        case AST_EXPR_STMT:
            (void)analyze_expr(sem, stmt->as.expr_stmt.expr, scope);
            return;
        case AST_RETURN_STMT: {
            OmegaType got = analyze_expr(sem, stmt->as.return_stmt.value_expr, scope);
            if (!sem->st.ok) {
                return;
            }
            if (got != fn->return_type) {
                char msg[256];
                (void)snprintf(msg, sizeof(msg), "return type mismatch in %s: expected %s, got %s", fn->name,
                               type_name(fn->return_type), type_name(got));
                semantic_fail(sem, stmt->line, stmt->column, msg);
            }
            return;
        }
        default:
            semantic_fail(sem, stmt->line, stmt->column, "invalid statement node during semantic analysis");
            return;
    }
}

static void analyze_block(Semantic *sem, ASTNode *block, Scope *scope, const FunctionSymbol *fn) {
    if (!sem->st.ok) {
        return;
    }
    assert(block->kind == AST_BLOCK);
    for (size_t i = 0; i < block->as.block.statement_count; i++) {
        analyze_stmt(sem, block->as.block.statements[i], scope, fn);
        if (!sem->st.ok) {
            return;
        }
    }
}

static OmegaType analyze_expr(Semantic *sem, ASTNode *expr, Scope *scope) {
    if (!sem->st.ok) {
        return OMEGA_TYPE_INVALID;
    }
    switch (expr->kind) {
        case AST_BOOL_LITERAL:
            expr->inferred_type = OMEGA_TYPE_BOOLEAN;
            return OMEGA_TYPE_BOOLEAN;
        case AST_UINT_LITERAL:
            expr->inferred_type = OMEGA_TYPE_UINT32;
            return OMEGA_TYPE_UINT32;
        case AST_INT_LITERAL:
            expr->inferred_type = OMEGA_TYPE_INT32;
            return OMEGA_TYPE_INT32;
        case AST_FLOAT_LITERAL:
            expr->inferred_type = OMEGA_TYPE_FLOAT32;
            return OMEGA_TYPE_FLOAT32;
        case AST_STRING_LITERAL:
            expr->inferred_type = OMEGA_TYPE_STRING;
            return OMEGA_TYPE_STRING;
        case AST_IDENTIFIER: {
            VarSymbol *v = scope_find_var(scope, expr->as.ident.name);
            if (v == NULL) {
                semantic_fail(sem, expr->line, expr->column, "undefined identifier");
                return OMEGA_TYPE_INVALID;
            }
            expr->inferred_type = v->type;
            return v->type;
        }
        case AST_MEMBER_EXPR: {
            OmegaType recv_t = analyze_expr(sem, expr->as.member_expr.receiver_expr, scope);
            if (!sem->st.ok) {
                return OMEGA_TYPE_INVALID;
            }
            if (recv_t != OMEGA_TYPE_VECTOR) {
                semantic_fail(sem, expr->line, expr->column, "member access requires a vector value");
                return OMEGA_TYPE_INVALID;
            }
            if (!sem_has_vector_import(sem)) {
                semantic_fail(sem, expr->line, expr->column, "module 'sti' (or 'vector') must be imported before use");
                return OMEGA_TYPE_INVALID;
            }
            const OmegaBuiltinSpec *builtin = omega_find_vector_property_builtin(expr->as.member_expr.member_name);
            if (builtin == NULL) {
                semantic_fail(sem, expr->line, expr->column, "unknown vector property");
                return OMEGA_TYPE_INVALID;
            }
            expr->as.member_expr.builtin_kind = builtin->kind;
            if (builtin->kind == OMEGA_BUILTIN_VECTOR_PROPERTY_SIZE) {
                expr->inferred_type = OMEGA_TYPE_UINT32;
                return OMEGA_TYPE_UINT32;
            }
            semantic_fail(sem, expr->line, expr->column, "unsupported vector property");
            return OMEGA_TYPE_INVALID;
        }
        case AST_INDEX_EXPR: {
            OmegaType base_t = analyze_expr(sem, expr->as.index_expr.base_expr, scope);
            OmegaType idx_t = analyze_expr(sem, expr->as.index_expr.index_expr, scope);
            if (!sem->st.ok) {
                return OMEGA_TYPE_INVALID;
            }
            if (base_t != OMEGA_TYPE_VECTOR) {
                semantic_fail(sem, expr->line, expr->column, "indexing requires a vector value");
                return OMEGA_TYPE_INVALID;
            }
            if (!type_is_integral(idx_t)) {
                semantic_fail(sem, expr->line, expr->column, "vector index must be uint32 or int32");
                return OMEGA_TYPE_INVALID;
            }
            expr->inferred_type = OMEGA_TYPE_INT32;
            return OMEGA_TYPE_INT32;
        }
        case AST_UNARY_EXPR: {
            OmegaType operand = analyze_expr(sem, expr->as.unary_expr.operand, scope);
            if (!sem->st.ok) {
                return OMEGA_TYPE_INVALID;
            }
            if (expr->as.unary_expr.op == TOK_LOGIC_NOT) {
                if (operand != OMEGA_TYPE_BOOLEAN) {
                    semantic_fail(sem, expr->line, expr->column, "operator '~' requires boolean operand");
                    return OMEGA_TYPE_INVALID;
                }
                expr->inferred_type = OMEGA_TYPE_BOOLEAN;
                return OMEGA_TYPE_BOOLEAN;
            }
            if (expr->as.unary_expr.op == TOK_MINUS) {
                if (!type_is_numeric(operand)) {
                    semantic_fail(sem, expr->line, expr->column, "unary '-' requires numeric operand");
                    return OMEGA_TYPE_INVALID;
                }
                expr->inferred_type = operand;
                return operand;
            }
            semantic_fail(sem, expr->line, expr->column, "unsupported unary operator");
            return OMEGA_TYPE_INVALID;
        }
        case AST_BINARY_EXPR: {
            OmegaType lhs = analyze_expr(sem, expr->as.binary_expr.left, scope);
            OmegaType rhs = analyze_expr(sem, expr->as.binary_expr.right, scope);
            if (!sem->st.ok) {
                return OMEGA_TYPE_INVALID;
            }
            if (expr->as.binary_expr.op == TOK_LOGIC_OR || expr->as.binary_expr.op == TOK_LOGIC_AND) {
                if (lhs != OMEGA_TYPE_BOOLEAN || rhs != OMEGA_TYPE_BOOLEAN) {
                    semantic_fail(sem, expr->line, expr->column, "logical operators require boolean operands");
                    return OMEGA_TYPE_INVALID;
                }
                expr->inferred_type = OMEGA_TYPE_BOOLEAN;
                return OMEGA_TYPE_BOOLEAN;
            }
            if (expr->as.binary_expr.op == TOK_PLUS || expr->as.binary_expr.op == TOK_MINUS ||
                expr->as.binary_expr.op == TOK_STAR || expr->as.binary_expr.op == TOK_SLASH ||
                expr->as.binary_expr.op == TOK_PERCENT) {
                if (!type_is_numeric(lhs) || lhs != rhs) {
                    semantic_fail(sem, expr->line, expr->column,
                                  "arithmetic operators require matching numeric operands (uint32/int32/float32)");
                    return OMEGA_TYPE_INVALID;
                }
                if (expr->as.binary_expr.op == TOK_PERCENT && !type_is_integral(lhs)) {
                    semantic_fail(sem, expr->line, expr->column, "operator '%' requires integral operands");
                    return OMEGA_TYPE_INVALID;
                }
                expr->inferred_type = lhs;
                return lhs;
            }
            if (expr->as.binary_expr.op == TOK_LE || expr->as.binary_expr.op == TOK_LT ||
                expr->as.binary_expr.op == TOK_GE || expr->as.binary_expr.op == TOK_GT) {
                if (!type_is_numeric(lhs) || lhs != rhs) {
                    semantic_fail(sem, expr->line, expr->column,
                                  "ordered comparisons require matching numeric operands (uint32/int32/float32)");
                    return OMEGA_TYPE_INVALID;
                }
                expr->inferred_type = OMEGA_TYPE_BOOLEAN;
                return OMEGA_TYPE_BOOLEAN;
            }
            if (expr->as.binary_expr.op == TOK_EQEQ || expr->as.binary_expr.op == TOK_NEQ) {
                if (lhs != rhs) {
                    semantic_fail(sem, expr->line, expr->column, "equality operators require matching operand types");
                    return OMEGA_TYPE_INVALID;
                }
                if (lhs != OMEGA_TYPE_UINT32 && lhs != OMEGA_TYPE_INT32 && lhs != OMEGA_TYPE_FLOAT32 &&
                    lhs != OMEGA_TYPE_BOOLEAN) {
                    semantic_fail(sem, expr->line, expr->column,
                                  "equality operators currently support uint32/int32/float32/boolean only");
                    return OMEGA_TYPE_INVALID;
                }
                expr->inferred_type = OMEGA_TYPE_BOOLEAN;
                return OMEGA_TYPE_BOOLEAN;
            }
            semantic_fail(sem, expr->line, expr->column, "unsupported binary operator");
            return OMEGA_TYPE_INVALID;
        }
        case AST_CALL_EXPR: {
            expr->as.call_expr.builtin_kind = OMEGA_BUILTIN_NONE;

            if (expr->as.call_expr.receiver_expr != NULL) {
                OmegaType recv_t = analyze_expr(sem, expr->as.call_expr.receiver_expr, scope);
                if (!sem->st.ok) {
                    return OMEGA_TYPE_INVALID;
                }
                if (recv_t != OMEGA_TYPE_VECTOR) {
                    semantic_fail(sem, expr->line, expr->column, "member calls currently require a vector receiver");
                    return OMEGA_TYPE_INVALID;
                }
                if (!sem_has_vector_import(sem)) {
                    semantic_fail(sem, expr->line, expr->column, "module 'sti' (or 'vector') must be imported before use");
                    return OMEGA_TYPE_INVALID;
                }

                const OmegaBuiltinSpec *builtin = omega_find_vector_method_builtin(expr->as.call_expr.func_name);
                if (builtin == NULL) {
                    semantic_fail(sem, expr->line, expr->column, "unknown vector method");
                    return OMEGA_TYPE_INVALID;
                }
                expr->as.call_expr.builtin_kind = builtin->kind;

                if (builtin->kind == OMEGA_BUILTIN_VECTOR_METHOD_PUSH_BACK) {
                    if (expr->as.call_expr.arg_count != 1u) {
                        semantic_fail(sem, expr->line, expr->column, "push_back expects exactly one argument");
                        return OMEGA_TYPE_INVALID;
                    }
                    OmegaType arg_t = analyze_expr(sem, expr->as.call_expr.args[0], scope);
                    if (!sem->st.ok) {
                        return OMEGA_TYPE_INVALID;
                    }
                    if (arg_t != OMEGA_TYPE_INT32) {
                        semantic_fail(sem, expr->line, expr->column, "push_back expects an int32 argument");
                        return OMEGA_TYPE_INVALID;
                    }
                    expr->inferred_type = OMEGA_TYPE_VOID;
                    return OMEGA_TYPE_VOID;
                }

                if (builtin->kind == OMEGA_BUILTIN_VECTOR_METHOD_POP_BACK) {
                    if (expr->as.call_expr.arg_count != 0u) {
                        semantic_fail(sem, expr->line, expr->column, "pop_back expects no arguments");
                        return OMEGA_TYPE_INVALID;
                    }
                    expr->inferred_type = OMEGA_TYPE_INT32;
                    return OMEGA_TYPE_INT32;
                }

                if (builtin->kind == OMEGA_BUILTIN_VECTOR_METHOD_CLEAR) {
                    if (expr->as.call_expr.arg_count != 0u) {
                        semantic_fail(sem, expr->line, expr->column, "clear expects no arguments");
                        return OMEGA_TYPE_INVALID;
                    }
                    expr->inferred_type = OMEGA_TYPE_VOID;
                    return OMEGA_TYPE_VOID;
                }

                if (builtin->kind == OMEGA_BUILTIN_VECTOR_METHOD_SORT) {
                    if (expr->as.call_expr.arg_count != 0u) {
                        semantic_fail(sem, expr->line, expr->column, "sort expects no arguments");
                        return OMEGA_TYPE_INVALID;
                    }
                    expr->inferred_type = OMEGA_TYPE_VOID;
                    return OMEGA_TYPE_VOID;
                }

                if (builtin->kind == OMEGA_BUILTIN_VECTOR_METHOD_FREE) {
                    if (expr->as.call_expr.arg_count != 0u) {
                        semantic_fail(sem, expr->line, expr->column, "free expects no arguments");
                        return OMEGA_TYPE_INVALID;
                    }
                    expr->inferred_type = OMEGA_TYPE_VOID;
                    return OMEGA_TYPE_VOID;
                }

                semantic_fail(sem, expr->line, expr->column, "unsupported vector method");
                return OMEGA_TYPE_INVALID;
            }

            const OmegaBuiltinSpec *builtin = NULL;
            const OmegaBuiltinSpec *missing_import_builtin = NULL;
            if (expr->as.call_expr.is_namespace_call) {
                builtin = omega_find_namespaced_builtin(expr->as.call_expr.ns_name, expr->as.call_expr.func_name);
                /* if not a known builtin, fall through to user module check */
            } else {
                builtin = omega_find_plain_builtin(expr->as.call_expr.func_name);
                if (builtin != NULL && builtin->requires_import && !sem_has_import(sem, builtin->module_name)) {
                    missing_import_builtin = builtin;
                    builtin = NULL;
                }
            }

            if (builtin != NULL) {
                expr->as.call_expr.builtin_kind = builtin->kind;
                if (builtin->kind == OMEGA_BUILTIN_PRINT) {
                    if (expr->as.call_expr.arg_count != 1u) {
                        semantic_fail(sem, expr->line, expr->column, "print expects exactly one argument");
                        return OMEGA_TYPE_INVALID;
                    }
                    OmegaType arg_t = analyze_expr(sem, expr->as.call_expr.args[0], scope);
                    if (!sem->st.ok) {
                        return OMEGA_TYPE_INVALID;
                    }
                    if (arg_t != OMEGA_TYPE_UINT32 && arg_t != OMEGA_TYPE_INT32 && arg_t != OMEGA_TYPE_BOOLEAN &&
                        arg_t != OMEGA_TYPE_FLOAT32 && arg_t != OMEGA_TYPE_STRING) {
                        semantic_fail(sem, expr->line, expr->column,
                                      "print supports uint32/int32/float32/boolean/string");
                        return OMEGA_TYPE_INVALID;
                    }
                    expr->inferred_type = OMEGA_TYPE_VOID;
                    return OMEGA_TYPE_VOID;
                }

                if (builtin->kind == OMEGA_BUILTIN_INPUT) {
                    if (expr->as.call_expr.arg_count != 1u) {
                        semantic_fail(sem, expr->line, expr->column, "input expects exactly one identifier argument");
                        return OMEGA_TYPE_INVALID;
                    }
                    ASTNode *arg = expr->as.call_expr.args[0];
                    if (arg->kind != AST_IDENTIFIER) {
                        semantic_fail(sem, expr->line, expr->column, "input argument must be an identifier");
                        return OMEGA_TYPE_INVALID;
                    }
                    VarSymbol *v = scope_find_var(scope, arg->as.ident.name);
                    if (v == NULL) {
                        semantic_fail(sem, expr->line, expr->column, "input target identifier is not declared");
                        return OMEGA_TYPE_INVALID;
                    }
                    if (v->type != OMEGA_TYPE_INT32 && v->type != OMEGA_TYPE_UINT32 && v->type != OMEGA_TYPE_FLOAT32 &&
                        v->type != OMEGA_TYPE_BOOLEAN) {
                        semantic_fail(sem, expr->line, expr->column,
                                      "input supports int32/uint32/float32/boolean targets");
                        return OMEGA_TYPE_INVALID;
                    }
                    expr->inferred_type = OMEGA_TYPE_VOID;
                    return OMEGA_TYPE_VOID;
                }
            }

            if (expr->as.call_expr.is_namespace_call) {
                FunctionSymbol *mod_fn = find_module_function(sem, expr->as.call_expr.ns_name,
                                                               expr->as.call_expr.func_name);
                if (mod_fn == NULL) {
                    semantic_fail(sem, expr->line, expr->column, "unknown namespaced function call");
                    return OMEGA_TYPE_INVALID;
                }
                if (expr->as.call_expr.arg_count != mod_fn->param_count) {
                    semantic_fail(sem, expr->line, expr->column, "module function call argument count mismatch");
                    return OMEGA_TYPE_INVALID;
                }
                for (size_t i = 0; i < mod_fn->param_count; i++) {
                    OmegaType got = analyze_expr(sem, expr->as.call_expr.args[i], scope);
                    if (!sem->st.ok) {
                        return OMEGA_TYPE_INVALID;
                    }
                    if (got != mod_fn->params[i].type) {
                        semantic_fail(sem, expr->line, expr->column, "module function call argument type mismatch");
                        return OMEGA_TYPE_INVALID;
                    }
                }
                expr->inferred_type = mod_fn->return_type;
                return mod_fn->return_type;
            }

            FunctionSymbol *fn = find_function(sem, expr->as.call_expr.func_name);
            if (fn == NULL) {
                if (missing_import_builtin != NULL) {
                    char msg[256];
                    (void)snprintf(msg, sizeof(msg), "module '%s' must be imported before calling '%s'",
                                   missing_import_builtin->module_name, expr->as.call_expr.func_name);
                    semantic_fail(sem, expr->line, expr->column, msg);
                    return OMEGA_TYPE_INVALID;
                }
                semantic_fail(sem, expr->line, expr->column, "unknown function in call");
                return OMEGA_TYPE_INVALID;
            }
            if (expr->as.call_expr.arg_count != fn->param_count) {
                semantic_fail(sem, expr->line, expr->column, "function call argument count mismatch");
                return OMEGA_TYPE_INVALID;
            }
            for (size_t i = 0; i < fn->param_count; i++) {
                OmegaType got = analyze_expr(sem, expr->as.call_expr.args[i], scope);
                if (!sem->st.ok) {
                    return OMEGA_TYPE_INVALID;
                }
                if (got != fn->params[i].type) {
                    semantic_fail(sem, expr->line, expr->column, "function call argument type mismatch");
                    return OMEGA_TYPE_INVALID;
                }
            }
            expr->inferred_type = fn->return_type;
            return fn->return_type;
        }
        default:
            semantic_fail(sem, expr->line, expr->column, "invalid expression node during semantic analysis");
            return OMEGA_TYPE_INVALID;
    }
}

static bool block_returns_on_all_paths(const ASTNode *block);

static bool stmt_returns_on_all_paths(const ASTNode *stmt) {
    if (stmt == NULL) {
        return false;
    }
    switch (stmt->kind) {
        case AST_RETURN_STMT:
            return true;
        case AST_IF_STMT:
            if (stmt->as.if_stmt.else_block == NULL) {
                return false;
            }
            return block_returns_on_all_paths(stmt->as.if_stmt.then_block) &&
                   block_returns_on_all_paths(stmt->as.if_stmt.else_block);
        case AST_BLOCK:
            return block_returns_on_all_paths(stmt);
        case AST_WHILE_STMT:
            return false;
        default:
            return false;
    }
}

static bool block_returns_on_all_paths(const ASTNode *block) {
    if (block == NULL || block->kind != AST_BLOCK) {
        return false;
    }
    for (size_t i = 0; i < block->as.block.statement_count; i++) {
        const ASTNode *st = block->as.block.statements[i];
        if (stmt_returns_on_all_paths(st)) {
            return true;
        }
    }
    return false;
}

bool semantic_analyze(Semantic *sem, ASTNode *program) {
    sem->st.ok = true;
    if (program == NULL || program->kind != AST_PROGRAM) {
        semantic_fail(sem, 1u, 1u, "invalid AST root");
        return false;
    }

    for (size_t i = 0; i < program->as.program.decl_count; i++) {
        ASTNode *decl = program->as.program.decls[i];
        if (decl->kind != AST_IMPORT) {
            continue;
        }
        if (decl->as.import_stmt.is_file_import) {
            continue; /* file imports are resolved by the linker */
        }
        if (!omega_module_exists(decl->as.import_stmt.module)) {
            char msg[256];
            (void)snprintf(msg, sizeof(msg), "unknown module '%s' in import", decl->as.import_stmt.module);
            semantic_fail(sem, decl->line, decl->column, msg);
            return false;
        }
        sem_add_import(sem, decl->as.import_stmt.module);
    }

    bool saw_main = false;
    for (size_t i = 0; i < program->as.program.decl_count; i++) {
        ASTNode *decl = program->as.program.decls[i];
        if (decl->kind != AST_FUNCTION) {
            continue;
        }
        if (find_function(sem, decl->as.function.name) != NULL) {
            semantic_fail(sem, decl->line, decl->column, "duplicate function declaration");
            return false;
        }
        FunctionSymbol sym;
        sym.name = decl->as.function.name;
        sym.return_type = decl->as.function.return_type;
        sym.params = decl->as.function.params;
        sym.param_count = decl->as.function.param_count;
        sym.decl_node = decl;
        sym.is_entrypoint = decl->as.function.is_entrypoint;
        sym.is_extern = decl->as.function.is_extern;
        sym.module_name = decl->as.function.module_name;

        /* module functions go into their own table */
        if (sym.module_name != NULL) {
            if (find_module_function(sem, sym.module_name, sym.name) != NULL) {
                semantic_fail(sem, decl->line, decl->column, "duplicate module function declaration");
                return false;
            }
            push_module_function_symbol(sem, sym);
            continue;
        }

        if (sym.return_type == OMEGA_TYPE_VECTOR && !sem_has_vector_import(sem)) {
            semantic_fail(sem, decl->line, decl->column, "vector type requires 'import sti.' or 'import vector.'");
            return false;
        }
        for (size_t p = 0; p < sym.param_count; p++) {
            if (sym.params[p].type == OMEGA_TYPE_VECTOR && !sem_has_vector_import(sem)) {
                semantic_fail(sem, sym.params[p].line, sym.params[p].column, "vector type requires 'import sti.' or 'import vector.'");
                return false;
            }
        }
        if (sym.is_entrypoint) {
            saw_main = true;
        }
        push_function_symbol(sem, sym);
    }
    if (!saw_main) {
        semantic_fail(sem, 1u, 1u, "missing entrypoint: -> main()");
        return false;
    }

    for (size_t i = 0; i < sem->func_count; i++) {
        FunctionSymbol *fn = &sem->funcs[i];
        Scope root = {0};
        for (size_t p = 0; p < fn->param_count; p++) {
            VarSymbol v;
            v.name = fn->params[p].name;
            v.type = fn->params[p].type;
            v.line = fn->params[p].line;
            v.column = fn->params[p].column;
            scope_add_var(&root, &v, &sem->st);
            if (!sem->st.ok) {
                return false;
            }
        }
        if (!fn->is_extern) {
            sem->loop_depth = 0;
            analyze_block(sem, fn->decl_node->as.function.body, &root, fn);
            if (!sem->st.ok) {
                return false;
            }
            if (!fn->is_entrypoint && fn->return_type != OMEGA_TYPE_VOID &&
                !block_returns_on_all_paths(fn->decl_node->as.function.body)) {
                semantic_fail(sem, fn->decl_node->line, fn->decl_node->column,
                              "non-void function may not return on all paths");
                return false;
            }
        }
    }

    return sem->st.ok;
}
