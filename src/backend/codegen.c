#include "omega/compiler.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void codegen_emit(Codegen *cg, const char *text) {
    (void)fputs(text, cg->out);
}

static const char *intern_cstring(Codegen *cg, const char *value) {
    for (size_t i = 0; i < cg->string_count; i++) {
        if (strcmp(cg->strings[i].value, value) == 0) {
            return cg->strings[i].label;
        }
    }
    if (cg->string_count == cg->string_cap) {
        size_t next = (cg->string_cap == 0u) ? 8u : cg->string_cap * 2u;
        cg->strings = (CStringEntry *)xrealloc(cg->strings, next * sizeof(CStringEntry));
        cg->string_cap = next;
    }
    CStringEntry *e = &cg->strings[cg->string_count++];
    e->value = xstrdup_local(value);
    (void)snprintf(e->label, sizeof(e->label), "L_.str.%d", cg->next_string_id++);
    return e->label;
}

static const char *intern_float32_const(Codegen *cg, float value) {
    union {
        float f;
        uint32_t u;
    } conv;
    conv.f = value;
    for (size_t i = 0; i < cg->float_count; i++) {
        if (cg->floats[i].bits == conv.u) {
            return cg->floats[i].label;
        }
    }
    if (cg->float_count == cg->float_cap) {
        size_t next = (cg->float_cap == 0u) ? 8u : cg->float_cap * 2u;
        cg->floats = (CFloatEntry *)xrealloc(cg->floats, next * sizeof(CFloatEntry));
        cg->float_cap = next;
    }
    CFloatEntry *e = &cg->floats[cg->float_count++];
    e->bits = conv.u;
    (void)snprintf(e->label, sizeof(e->label), "L_.flt.%d", cg->next_float_id++);
    return e->label;
}

static void emit_string_asciz(FILE *out, const char *s) {
    (void)fputs("    .asciz \"", out);
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        unsigned char c = *p;
        switch (c) {
            case '\\':
                (void)fputs("\\\\", out);
                break;
            case '"':
                (void)fputs("\\\"", out);
                break;
            case '\n':
                (void)fputs("\\n", out);
                break;
            case '\t':
                (void)fputs("\\t", out);
                break;
            default:
                if (c < 32u || c > 126u) {
                    (void)fprintf(out, "\\%03o", c);
                } else {
                    (void)fputc(c, out);
                }
                break;
        }
    }
    (void)fputs("\"\n", out);
}

static LocalSlot *find_local(FunctionCodegen *fg, const char *name) {
    for (size_t i = 0; i < fg->local_count; i++) {
        if (strcmp(fg->locals[i].name, name) == 0) {
            return &fg->locals[i];
        }
    }
    return NULL;
}

static void add_local(FunctionCodegen *fg, const char *name, OmegaType type) {
    if (find_local(fg, name) != NULL) {
        return;
    }
    if (fg->local_count == fg->local_cap) {
        size_t next = (fg->local_cap == 0u) ? 8u : fg->local_cap * 2u;
        fg->locals = (LocalSlot *)xrealloc(fg->locals, next * sizeof(LocalSlot));
        fg->local_cap = next;
    }
    fg->next_offset += 16;
    LocalSlot *slot = &fg->locals[fg->local_count++];
    slot->name = xstrdup_local(name);
    slot->type = type;
    slot->offset = fg->next_offset;
}

static void collect_locals_from_block(FunctionCodegen *fg, const ASTNode *block) {
    if (block->kind != AST_BLOCK) {
        return;
    }
    for (size_t i = 0; i < block->as.block.statement_count; i++) {
        const ASTNode *st = block->as.block.statements[i];
        if (st->kind == AST_BLOCK) {
            collect_locals_from_block(fg, st);
        } else if (st->kind == AST_VAR_DECL) {
            add_local(fg, st->as.var_decl.name, st->as.var_decl.var_type);
        } else if (st->kind == AST_IF_STMT) {
            collect_locals_from_block(fg, st->as.if_stmt.then_block);
            if (st->as.if_stmt.else_block != NULL) {
                collect_locals_from_block(fg, st->as.if_stmt.else_block);
            }
        } else if (st->kind == AST_WHILE_STMT) {
            collect_locals_from_block(fg, st->as.while_stmt.body_block);
        }
    }
}

static void gen_expr(FunctionCodegen *fg, const ASTNode *expr);

static void generate_logic_asm(FunctionCodegen *fg, const ASTNode *node) {
    assert(node != NULL && node->kind == AST_BINARY_EXPR);
    gen_expr(fg, node->as.binary_expr.left);
    (void)fprintf(fg->cg->out, "    str w0, [sp, #-16]!\n");
    gen_expr(fg, node->as.binary_expr.right);
    (void)fprintf(fg->cg->out, "    ldr w1, [sp], #16\n");
    if (node->as.binary_expr.op == TOK_LOGIC_OR) {
        (void)fprintf(fg->cg->out, "    orr w0, w1, w0\n");
    } else {
        (void)fprintf(fg->cg->out, "    and w0, w1, w0\n");
    }
}

static void generate_arith_compare_asm(FunctionCodegen *fg, const ASTNode *node) {
    assert(node != NULL && node->kind == AST_BINARY_EXPR);
    if (node->as.binary_expr.left->inferred_type == OMEGA_TYPE_FLOAT32) {
        gen_expr(fg, node->as.binary_expr.left);
        (void)fprintf(fg->cg->out, "    str s0, [sp, #-16]!\n");
        gen_expr(fg, node->as.binary_expr.right);
        (void)fprintf(fg->cg->out, "    ldr s1, [sp], #16\n");

        if (node->as.binary_expr.op == TOK_PLUS) {
            (void)fprintf(fg->cg->out, "    fadd s0, s1, s0\n");
            return;
        }
        if (node->as.binary_expr.op == TOK_MINUS) {
            (void)fprintf(fg->cg->out, "    fsub s0, s1, s0\n");
            return;
        }
        if (node->as.binary_expr.op == TOK_STAR) {
            (void)fprintf(fg->cg->out, "    fmul s0, s1, s0\n");
            return;
        }
        if (node->as.binary_expr.op == TOK_SLASH) {
            (void)fprintf(fg->cg->out, "    fdiv s0, s1, s0\n");
            return;
        }

        (void)fprintf(fg->cg->out, "    fcmp s1, s0\n");
        if (node->as.binary_expr.op == TOK_EQEQ) {
            (void)fprintf(fg->cg->out, "    cset w0, eq\n");
            return;
        }
        if (node->as.binary_expr.op == TOK_NEQ) {
            (void)fprintf(fg->cg->out, "    cset w0, ne\n");
            return;
        }
        if (node->as.binary_expr.op == TOK_LE) {
            (void)fprintf(fg->cg->out, "    cset w0, le\n");
            return;
        }
        if (node->as.binary_expr.op == TOK_LT) {
            (void)fprintf(fg->cg->out, "    cset w0, lt\n");
            return;
        }
        if (node->as.binary_expr.op == TOK_GE) {
            (void)fprintf(fg->cg->out, "    cset w0, ge\n");
            return;
        }
        if (node->as.binary_expr.op == TOK_GT) {
            (void)fprintf(fg->cg->out, "    cset w0, gt\n");
            return;
        }

        fprintf(stderr, "codegen error: unsupported float operator\n");
        exit(EXIT_FAILURE);
    }

    gen_expr(fg, node->as.binary_expr.left);
    (void)fprintf(fg->cg->out, "    str w0, [sp, #-16]!\n");
    gen_expr(fg, node->as.binary_expr.right);
    (void)fprintf(fg->cg->out, "    ldr w1, [sp], #16\n");

    if (node->as.binary_expr.op == TOK_PLUS) {
        (void)fprintf(fg->cg->out, "    add w0, w1, w0\n");
        return;
    }
    if (node->as.binary_expr.op == TOK_MINUS) {
        (void)fprintf(fg->cg->out, "    sub w0, w1, w0\n");
        return;
    }
    if (node->as.binary_expr.op == TOK_STAR) {
        (void)fprintf(fg->cg->out, "    mul w0, w1, w0\n");
        return;
    }
    if (node->as.binary_expr.op == TOK_SLASH) {
        if (node->as.binary_expr.left->inferred_type == OMEGA_TYPE_UINT32) {
            (void)fprintf(fg->cg->out, "    udiv w0, w1, w0\n");
        } else {
            (void)fprintf(fg->cg->out, "    sdiv w0, w1, w0\n");
        }
        return;
    }
    if (node->as.binary_expr.op == TOK_PERCENT) {
        if (node->as.binary_expr.left->inferred_type == OMEGA_TYPE_UINT32) {
            (void)fprintf(fg->cg->out, "    udiv w2, w1, w0\n");
        } else {
            (void)fprintf(fg->cg->out, "    sdiv w2, w1, w0\n");
        }
        (void)fprintf(fg->cg->out, "    msub w0, w2, w0, w1\n");
        return;
    }

    (void)fprintf(fg->cg->out, "    cmp w1, w0\n");
    if (node->as.binary_expr.op == TOK_EQEQ) {
        (void)fprintf(fg->cg->out, "    cset w0, eq\n");
        return;
    }
    if (node->as.binary_expr.op == TOK_NEQ) {
        (void)fprintf(fg->cg->out, "    cset w0, ne\n");
        return;
    }
    if (node->as.binary_expr.op == TOK_LE) {
        if (node->as.binary_expr.left->inferred_type == OMEGA_TYPE_UINT32) {
            (void)fprintf(fg->cg->out, "    cset w0, ls\n");
        } else {
            (void)fprintf(fg->cg->out, "    cset w0, le\n");
        }
        return;
    }
    if (node->as.binary_expr.op == TOK_LT) {
        if (node->as.binary_expr.left->inferred_type == OMEGA_TYPE_UINT32) {
            (void)fprintf(fg->cg->out, "    cset w0, lo\n");
        } else {
            (void)fprintf(fg->cg->out, "    cset w0, lt\n");
        }
        return;
    }
    if (node->as.binary_expr.op == TOK_GE) {
        if (node->as.binary_expr.left->inferred_type == OMEGA_TYPE_UINT32) {
            (void)fprintf(fg->cg->out, "    cset w0, hs\n");
        } else {
            (void)fprintf(fg->cg->out, "    cset w0, ge\n");
        }
        return;
    }
    if (node->as.binary_expr.op == TOK_GT) {
        if (node->as.binary_expr.left->inferred_type == OMEGA_TYPE_UINT32) {
            (void)fprintf(fg->cg->out, "    cset w0, hi\n");
        } else {
            (void)fprintf(fg->cg->out, "    cset w0, gt\n");
        }
        return;
    }
}

static void gen_load_local(FunctionCodegen *fg, const LocalSlot *slot) {
    if (slot->type == OMEGA_TYPE_FLOAT32) {
        (void)fprintf(fg->cg->out, "    ldur s0, [x29, #-%d]\n", slot->offset);
    } else if (type_is_pointer_like(slot->type)) {
        (void)fprintf(fg->cg->out, "    ldur x0, [x29, #-%d]\n", slot->offset);
    } else {
        (void)fprintf(fg->cg->out, "    ldur w0, [x29, #-%d]\n", slot->offset);
    }
}

static void gen_store_local_from_result(FunctionCodegen *fg, const LocalSlot *slot) {
    if (slot->type == OMEGA_TYPE_FLOAT32) {
        (void)fprintf(fg->cg->out, "    stur s0, [x29, #-%d]\n", slot->offset);
    } else if (type_is_pointer_like(slot->type)) {
        (void)fprintf(fg->cg->out, "    stur x0, [x29, #-%d]\n", slot->offset);
    } else {
        (void)fprintf(fg->cg->out, "    stur w0, [x29, #-%d]\n", slot->offset);
    }
}

static void gen_call(FunctionCodegen *fg, const ASTNode *call) {
    assert(call->kind == AST_CALL_EXPR);

    if (call->as.call_expr.receiver_expr != NULL) {
        int id = fg->cg->next_label_id++;
        if (call->as.call_expr.builtin_kind == OMEGA_BUILTIN_VECTOR_METHOD_PUSH_BACK) {
            gen_expr(fg, call->as.call_expr.receiver_expr);
            (void)fprintf(fg->cg->out, "    str x0, [sp, #-16]!\n");
            gen_expr(fg, call->as.call_expr.args[0]);
            (void)fprintf(fg->cg->out, "    str w0, [sp, #-16]!\n");
            (void)fprintf(fg->cg->out, "    ldr w10, [sp], #16\n");
            (void)fprintf(fg->cg->out, "    ldr x9, [sp], #16\n");
            (void)fprintf(fg->cg->out, "    ldr w11, [x9, #8]\n");
            (void)fprintf(fg->cg->out, "    ldr w12, [x9, #12]\n");
            (void)fprintf(fg->cg->out, "    cmp w11, w12\n");
            (void)fprintf(fg->cg->out, "    b.ne L_vec_have_cap_%d\n", id);
            (void)fprintf(fg->cg->out, "    str w10, [sp, #-16]!\n");
            (void)fprintf(fg->cg->out, "    str x9, [sp, #-16]!\n");
            (void)fprintf(fg->cg->out, "    ldr w12, [x9, #12]\n");
            (void)fprintf(fg->cg->out, "    cmp w12, #0\n");
            (void)fprintf(fg->cg->out, "    mov w13, #4\n");
            (void)fprintf(fg->cg->out, "    b.eq L_vec_new_cap_ready_%d\n", id);
            (void)fprintf(fg->cg->out, "    lsl w13, w12, #1\n");
            (void)fprintf(fg->cg->out, "L_vec_new_cap_ready_%d:\n", id);
            (void)fprintf(fg->cg->out, "    str w13, [sp, #-16]!\n");
            (void)fprintf(fg->cg->out, "    ldr x0, [x9]\n");
            (void)fprintf(fg->cg->out, "    uxtw x1, w13\n");
            (void)fprintf(fg->cg->out, "    lsl x1, x1, #2\n");
            (void)fprintf(fg->cg->out, "    bl _realloc\n");
            (void)fprintf(fg->cg->out, "    ldr w13, [sp], #16\n");
            (void)fprintf(fg->cg->out, "    ldr x9, [sp], #16\n");
            (void)fprintf(fg->cg->out, "    ldr w10, [sp], #16\n");
            (void)fprintf(fg->cg->out, "    str x0, [x9]\n");
            (void)fprintf(fg->cg->out, "    str w13, [x9, #12]\n");
            (void)fprintf(fg->cg->out, "L_vec_have_cap_%d:\n", id);
            (void)fprintf(fg->cg->out, "    ldr w11, [x9, #8]\n");
            (void)fprintf(fg->cg->out, "    ldr x12, [x9]\n");
            (void)fprintf(fg->cg->out, "    uxtw x13, w11\n");
            (void)fprintf(fg->cg->out, "    lsl x13, x13, #2\n");
            (void)fprintf(fg->cg->out, "    add x12, x12, x13\n");
            (void)fprintf(fg->cg->out, "    str w10, [x12]\n");
            (void)fprintf(fg->cg->out, "    add w11, w11, #1\n");
            (void)fprintf(fg->cg->out, "    str w11, [x9, #8]\n");
            (void)fprintf(fg->cg->out, "    mov w0, #0\n");
            return;
        }

        if (call->as.call_expr.builtin_kind == OMEGA_BUILTIN_VECTOR_METHOD_POP_BACK) {
            gen_expr(fg, call->as.call_expr.receiver_expr);
            (void)fprintf(fg->cg->out, "    mov x9, x0\n");
            (void)fprintf(fg->cg->out, "    ldr w10, [x9, #8]\n");
            (void)fprintf(fg->cg->out, "    cbz w10, L_vec_pop_empty_%d\n", id);
            (void)fprintf(fg->cg->out, "    sub w10, w10, #1\n");
            (void)fprintf(fg->cg->out, "    str w10, [x9, #8]\n");
            (void)fprintf(fg->cg->out, "    ldr x11, [x9]\n");
            (void)fprintf(fg->cg->out, "    uxtw x12, w10\n");
            (void)fprintf(fg->cg->out, "    lsl x12, x12, #2\n");
            (void)fprintf(fg->cg->out, "    ldr w0, [x11, x12]\n");
            (void)fprintf(fg->cg->out, "    b L_vec_pop_done_%d\n", id);
            (void)fprintf(fg->cg->out, "L_vec_pop_empty_%d:\n", id);
            (void)fprintf(fg->cg->out, "    mov w0, #0\n");
            (void)fprintf(fg->cg->out, "L_vec_pop_done_%d:\n", id);
            return;
        }

        if (call->as.call_expr.builtin_kind == OMEGA_BUILTIN_VECTOR_METHOD_CLEAR) {
            gen_expr(fg, call->as.call_expr.receiver_expr);
            (void)fprintf(fg->cg->out, "    str wzr, [x0, #8]\n");
            (void)fprintf(fg->cg->out, "    mov w0, #0\n");
            return;
        }

        if (call->as.call_expr.builtin_kind == OMEGA_BUILTIN_VECTOR_METHOD_SORT) {
            gen_expr(fg, call->as.call_expr.receiver_expr);
            (void)fprintf(fg->cg->out, "    bl _omega_sort_i32_vec\n");
            (void)fprintf(fg->cg->out, "    mov w0, #0\n");
            fg->cg->need_sort_helper = true;
            return;
        }

        if (call->as.call_expr.builtin_kind == OMEGA_BUILTIN_VECTOR_METHOD_FREE) {
            gen_expr(fg, call->as.call_expr.receiver_expr);
            /* x0 = vec struct ptr; free data then struct */
            (void)fprintf(fg->cg->out, "    ldr x1, [x0]\n");      /* data ptr */
            (void)fprintf(fg->cg->out, "    str x0, [sp, #-16]!\n"); /* save struct ptr */
            (void)fprintf(fg->cg->out, "    mov x0, x1\n");
            (void)fprintf(fg->cg->out, "    bl _free\n");           /* free data */
            (void)fprintf(fg->cg->out, "    ldr x0, [sp], #16\n"); /* restore struct ptr */
            (void)fprintf(fg->cg->out, "    bl _free\n");           /* free struct */
            (void)fprintf(fg->cg->out, "    mov w0, #0\n");
            return;
        }

        fprintf(stderr, "codegen error: unsupported receiver call\n");
        exit(EXIT_FAILURE);
    }

    if (call->as.call_expr.arg_count > 8u) {
        fprintf(stderr, "codegen error: max 8 arguments supported\n");
        exit(EXIT_FAILURE);
    }

    if (call->as.call_expr.builtin_kind == OMEGA_BUILTIN_PRINT) {
        const ASTNode *arg = call->as.call_expr.args[0];
        if (arg->inferred_type == OMEGA_TYPE_STRING) {
            const char *fmt = intern_cstring(fg->cg, "%s");
            gen_expr(fg, arg);
            (void)fprintf(fg->cg->out, "    sub sp, sp, #16\n");
            (void)fprintf(fg->cg->out, "    str x0, [sp]\n");
            (void)fprintf(fg->cg->out, "    adrp x0, %s@PAGE\n", fmt);
            (void)fprintf(fg->cg->out, "    add x0, x0, %s@PAGEOFF\n", fmt);
            (void)fprintf(fg->cg->out, "    bl _printf\n");
            (void)fprintf(fg->cg->out, "    add sp, sp, #16\n");
            (void)fprintf(fg->cg->out, "    mov w0, #0\n");
            return;
        }

        gen_expr(fg, arg);
        (void)fprintf(fg->cg->out, "    sub sp, sp, #16\n");
        const char *fmt = NULL;
        if (arg->inferred_type == OMEGA_TYPE_FLOAT32) {
            fmt = intern_cstring(fg->cg, "%f");
            (void)fprintf(fg->cg->out, "    fcvt d0, s0\n");
            (void)fprintf(fg->cg->out, "    str d0, [sp]\n");
        } else if (arg->inferred_type == OMEGA_TYPE_UINT32) {
            fmt = intern_cstring(fg->cg, "%u");
            (void)fprintf(fg->cg->out, "    uxtw x8, w0\n");
            (void)fprintf(fg->cg->out, "    str x8, [sp]\n");
        } else {
            fmt = intern_cstring(fg->cg, "%d");
            (void)fprintf(fg->cg->out, "    sxtw x8, w0\n");
            (void)fprintf(fg->cg->out, "    str x8, [sp]\n");
        }
        (void)fprintf(fg->cg->out, "    adrp x0, %s@PAGE\n", fmt);
        (void)fprintf(fg->cg->out, "    add x0, x0, %s@PAGEOFF\n", fmt);
        (void)fprintf(fg->cg->out, "    bl _printf\n");
        (void)fprintf(fg->cg->out, "    add sp, sp, #16\n");
        (void)fprintf(fg->cg->out, "    mov w0, #0\n");
        return;
    }

    if (call->as.call_expr.builtin_kind == OMEGA_BUILTIN_INPUT) {
        const ASTNode *arg = call->as.call_expr.args[0];
        if (arg->kind != AST_IDENTIFIER) {
            fprintf(stderr, "codegen error: input argument must be an identifier\n");
            exit(EXIT_FAILURE);
        }
        LocalSlot *slot = find_local(fg, arg->as.ident.name);
        if (slot == NULL) {
            fprintf(stderr, "codegen error: input target '%s' not found\n", arg->as.ident.name);
            exit(EXIT_FAILURE);
        }
        if (slot->type == OMEGA_TYPE_STRING || slot->type == OMEGA_TYPE_VECTOR) {
            fprintf(stderr, "codegen error: input does not support string/vector targets\n");
            exit(EXIT_FAILURE);
        }

        const char *fmt = NULL;
        if (slot->type == OMEGA_TYPE_FLOAT32) {
            fmt = intern_cstring(fg->cg, "%f");
        } else if (slot->type == OMEGA_TYPE_UINT32) {
            fmt = intern_cstring(fg->cg, "%u");
        } else {
            fmt = intern_cstring(fg->cg, "%d");
        }

        (void)fprintf(fg->cg->out, "    sub sp, sp, #16\n");
        (void)fprintf(fg->cg->out, "    sub x8, x29, #%d\n", slot->offset);
        (void)fprintf(fg->cg->out, "    str x8, [sp]\n");
        (void)fprintf(fg->cg->out, "    adrp x0, %s@PAGE\n", fmt);
        (void)fprintf(fg->cg->out, "    add x0, x0, %s@PAGEOFF\n", fmt);
        (void)fprintf(fg->cg->out, "    bl _scanf\n");
        (void)fprintf(fg->cg->out, "    add sp, sp, #16\n");

        if (slot->type == OMEGA_TYPE_BOOLEAN) {
            (void)fprintf(fg->cg->out, "    ldur w10, [x29, #-%d]\n", slot->offset);
            (void)fprintf(fg->cg->out, "    cmp w10, #0\n");
            (void)fprintf(fg->cg->out, "    cset w10, ne\n");
            (void)fprintf(fg->cg->out, "    stur w10, [x29, #-%d]\n", slot->offset);
        }

        (void)fprintf(fg->cg->out, "    mov w0, #0\n");
        return;
    }

    for (size_t i = 0; i < call->as.call_expr.arg_count; i++) {
        gen_expr(fg, call->as.call_expr.args[i]);
        if (type_is_pointer_like(call->as.call_expr.args[i]->inferred_type)) {
            (void)fprintf(fg->cg->out, "    str x0, [sp, #-16]!\n");
        } else if (call->as.call_expr.args[i]->inferred_type == OMEGA_TYPE_FLOAT32) {
            (void)fprintf(fg->cg->out, "    str s0, [sp, #-16]!\n");
        } else {
            (void)fprintf(fg->cg->out, "    str w0, [sp, #-16]!\n");
        }
    }
    for (size_t idx = call->as.call_expr.arg_count; idx > 0u; idx--) {
        size_t i = idx - 1u;
        if (type_is_pointer_like(call->as.call_expr.args[i]->inferred_type)) {
            (void)fprintf(fg->cg->out, "    ldr x%zu, [sp], #16\n", i);
        } else if (call->as.call_expr.args[i]->inferred_type == OMEGA_TYPE_FLOAT32) {
            (void)fprintf(fg->cg->out, "    ldr s%zu, [sp], #16\n", i);
        } else {
            (void)fprintf(fg->cg->out, "    ldr w%zu, [sp], #16\n", i);
        }
    }
    if (call->as.call_expr.is_namespace_call) {
        (void)fprintf(fg->cg->out, "    bl _%s_%s\n", call->as.call_expr.ns_name, call->as.call_expr.func_name);
    } else {
        (void)fprintf(fg->cg->out, "    bl _%s\n", call->as.call_expr.func_name);
    }
}

static void gen_expr(FunctionCodegen *fg, const ASTNode *expr) {
    switch (expr->kind) {
        case AST_BOOL_LITERAL:
            (void)fprintf(fg->cg->out, "    mov w0, #%u\n", expr->as.bool_lit.value ? 1u : 0u);
            return;
        case AST_UINT_LITERAL:
            (void)fprintf(fg->cg->out, "    mov w0, #%u\n", expr->as.u32_lit.value);
            return;
        case AST_INT_LITERAL:
            (void)fprintf(fg->cg->out, "    mov w0, #%d\n", expr->as.i32_lit.value);
            return;
        case AST_FLOAT_LITERAL: {
            const char *label = intern_float32_const(fg->cg, expr->as.f32_lit.value);
            (void)fprintf(fg->cg->out, "    adrp x9, %s@PAGE\n", label);
            (void)fprintf(fg->cg->out, "    add x9, x9, %s@PAGEOFF\n", label);
            (void)fprintf(fg->cg->out, "    ldr s0, [x9]\n");
            return;
        }
        case AST_STRING_LITERAL: {
            const char *label = intern_cstring(fg->cg, expr->as.str_lit.value);
            (void)fprintf(fg->cg->out, "    adrp x0, %s@PAGE\n", label);
            (void)fprintf(fg->cg->out, "    add x0, x0, %s@PAGEOFF\n", label);
            return;
        }
        case AST_IDENTIFIER: {
            LocalSlot *slot = find_local(fg, expr->as.ident.name);
            if (slot == NULL) {
                fprintf(stderr, "codegen error: unknown local '%s'\n", expr->as.ident.name);
                exit(EXIT_FAILURE);
            }
            gen_load_local(fg, slot);
            return;
        }
        case AST_MEMBER_EXPR:
            if (expr->as.member_expr.builtin_kind == OMEGA_BUILTIN_VECTOR_PROPERTY_SIZE) {
                gen_expr(fg, expr->as.member_expr.receiver_expr);
                (void)fprintf(fg->cg->out, "    ldr w0, [x0, #8]\n");
                return;
            }
            fprintf(stderr, "codegen error: unsupported member expression\n");
            exit(EXIT_FAILURE);
        case AST_INDEX_EXPR:
            gen_expr(fg, expr->as.index_expr.base_expr);
            (void)fprintf(fg->cg->out, "    str x0, [sp, #-16]!\n");
            gen_expr(fg, expr->as.index_expr.index_expr);
            (void)fprintf(fg->cg->out, "    ldr x9, [sp], #16\n");
            (void)fprintf(fg->cg->out, "    ldr x10, [x9]\n");
            (void)fprintf(fg->cg->out, "    uxtw x11, w0\n");
            (void)fprintf(fg->cg->out, "    lsl x11, x11, #2\n");
            (void)fprintf(fg->cg->out, "    ldr w0, [x10, x11]\n");
            return;
        case AST_UNARY_EXPR:
            gen_expr(fg, expr->as.unary_expr.operand);
            if (expr->as.unary_expr.op == TOK_LOGIC_NOT) {
                (void)fprintf(fg->cg->out, "    cmp w0, #0\n");
                (void)fprintf(fg->cg->out, "    cset w0, eq\n");
                return;
            }
            if (expr->as.unary_expr.op == TOK_MINUS) {
                if (expr->inferred_type == OMEGA_TYPE_FLOAT32) {
                    (void)fprintf(fg->cg->out, "    fneg s0, s0\n");
                } else {
                    (void)fprintf(fg->cg->out, "    neg w0, w0\n");
                }
                return;
            }
            fprintf(stderr, "codegen error: unsupported unary op\n");
            exit(EXIT_FAILURE);
        case AST_BINARY_EXPR:
            if (expr->as.binary_expr.op == TOK_LOGIC_OR || expr->as.binary_expr.op == TOK_LOGIC_AND) {
                generate_logic_asm(fg, expr);
            } else {
                generate_arith_compare_asm(fg, expr);
            }
            return;
        case AST_CALL_EXPR:
            gen_call(fg, expr);
            return;
        default:
            fprintf(stderr, "codegen error: unsupported expression kind %d\n", (int)expr->kind);
            exit(EXIT_FAILURE);
    }
}

static void gen_stmt(FunctionCodegen *fg, const ASTNode *stmt);

static void gen_block(FunctionCodegen *fg, const ASTNode *block) {
    assert(block->kind == AST_BLOCK);
    for (size_t i = 0; i < block->as.block.statement_count; i++) {
        gen_stmt(fg, block->as.block.statements[i]);
    }
}

static void gen_stmt(FunctionCodegen *fg, const ASTNode *stmt) {
    switch (stmt->kind) {
        case AST_BLOCK:
            gen_block(fg, stmt);
            return;
        case AST_VAR_DECL: {
            LocalSlot *slot = find_local(fg, stmt->as.var_decl.name);
            if (slot == NULL) {
                fprintf(stderr, "codegen error: local slot missing for '%s'\n", stmt->as.var_decl.name);
                exit(EXIT_FAILURE);
            }
            if (stmt->as.var_decl.init_expr != NULL) {
                gen_expr(fg, stmt->as.var_decl.init_expr);
            } else if (slot->type == OMEGA_TYPE_FLOAT32) {
                (void)fprintf(fg->cg->out, "    fmov s0, #0.0\n");
            } else if (slot->type == OMEGA_TYPE_VECTOR) {
                (void)fprintf(fg->cg->out, "    mov x0, #16\n");
                (void)fprintf(fg->cg->out, "    bl _malloc\n");
                (void)fprintf(fg->cg->out, "    mov x9, #0\n");
                (void)fprintf(fg->cg->out, "    str x9, [x0]\n");
                (void)fprintf(fg->cg->out, "    str wzr, [x0, #8]\n");
                (void)fprintf(fg->cg->out, "    str wzr, [x0, #12]\n");
            } else if (slot->type == OMEGA_TYPE_STRING || slot->type == OMEGA_TYPE_PTR) {
                (void)fprintf(fg->cg->out, "    mov x0, #0\n");
            } else {
                (void)fprintf(fg->cg->out, "    mov w0, #0\n");
            }
            gen_store_local_from_result(fg, slot);
            return;
        }
        case AST_ASSIGN_STMT: {
            LocalSlot *slot = find_local(fg, stmt->as.assign_stmt.name);
            if (slot == NULL) {
                fprintf(stderr, "codegen error: assignment target '%s' not found\n", stmt->as.assign_stmt.name);
                exit(EXIT_FAILURE);
            }
            gen_expr(fg, stmt->as.assign_stmt.value_expr);
            gen_store_local_from_result(fg, slot);
            return;
        }
        case AST_EXPR_STMT:
            gen_expr(fg, stmt->as.expr_stmt.expr);
            return;
        case AST_IF_STMT: {
            int id = fg->cg->next_label_id++;
            gen_expr(fg, stmt->as.if_stmt.cond_expr);
            if (stmt->as.if_stmt.else_block == NULL) {
                (void)fprintf(fg->cg->out, "    cbz w0, L_if_end_%d\n", id);
                gen_block(fg, stmt->as.if_stmt.then_block);
                (void)fprintf(fg->cg->out, "L_if_end_%d:\n", id);
                return;
            }
            (void)fprintf(fg->cg->out, "    cbz w0, L_if_else_%d\n", id);
            gen_block(fg, stmt->as.if_stmt.then_block);
            (void)fprintf(fg->cg->out, "    b L_if_end_%d\n", id);
            (void)fprintf(fg->cg->out, "L_if_else_%d:\n", id);
            gen_block(fg, stmt->as.if_stmt.else_block);
            (void)fprintf(fg->cg->out, "L_if_end_%d:\n", id);
            return;
        }
        case AST_WHILE_STMT: {
            int id = fg->cg->next_label_id++;
            if (fg->loop_depth == fg->loop_cap) {
                size_t next = (fg->loop_cap == 0u) ? 4u : fg->loop_cap * 2u;
                fg->loops = (LoopLabel *)xrealloc(fg->loops, next * sizeof(LoopLabel));
                fg->loop_cap = next;
            }
            LoopLabel *lf = &fg->loops[fg->loop_depth++];
            (void)snprintf(lf->begin_label, sizeof(lf->begin_label), "L_while_begin_%d", id);
            (void)snprintf(lf->end_label, sizeof(lf->end_label), "L_while_end_%d", id);
            (void)fprintf(fg->cg->out, "%s:\n", lf->begin_label);
            gen_expr(fg, stmt->as.while_stmt.cond_expr);
            (void)fprintf(fg->cg->out, "    cbz w0, %s\n", lf->end_label);
            gen_block(fg, stmt->as.while_stmt.body_block);
            (void)fprintf(fg->cg->out, "    b %s\n", lf->begin_label);
            (void)fprintf(fg->cg->out, "%s:\n", lf->end_label);
            fg->loop_depth--;
            return;
        }
        case AST_BREAK_STMT:
            if (fg->loop_depth == 0u) {
                fprintf(stderr, "codegen error: break outside loop\n");
                exit(EXIT_FAILURE);
            }
            (void)fprintf(fg->cg->out, "    b %s\n", fg->loops[fg->loop_depth - 1u].end_label);
            return;
        case AST_CONTINUE_STMT:
            if (fg->loop_depth == 0u) {
                fprintf(stderr, "codegen error: continue outside loop\n");
                exit(EXIT_FAILURE);
            }
            (void)fprintf(fg->cg->out, "    b %s\n", fg->loops[fg->loop_depth - 1u].begin_label);
            return;
        case AST_RETURN_STMT:
            gen_expr(fg, stmt->as.return_stmt.value_expr);
            (void)fprintf(fg->cg->out, "    b %s\n", fg->return_label);
            return;
        default:
            fprintf(stderr, "codegen error: unsupported statement kind %d\n", (int)stmt->kind);
            exit(EXIT_FAILURE);
    }
}

static void gen_function(Codegen *cg, ASTNode *fn) {
    assert(fn->kind == AST_FUNCTION);
    FunctionCodegen fg;
    memset(&fg, 0, sizeof(fg));
    fg.cg = cg;
    fg.fn_node = fn;
    fg.next_offset = 0;
    (void)snprintf(fg.return_label, sizeof(fg.return_label), "L_ret_%s", fn->as.function.name);

    for (size_t i = 0; i < fn->as.function.param_count; i++) {
        add_local(&fg, fn->as.function.params[i].name, fn->as.function.params[i].type);
    }
    collect_locals_from_block(&fg, fn->as.function.body);
    fg.frame_size = align16(fg.next_offset);

    (void)fprintf(cg->out, "\n.globl _%s\n", fn->as.function.name);
    (void)fprintf(cg->out, ".p2align 2\n");
    (void)fprintf(cg->out, "_%s:\n", fn->as.function.name);
    codegen_emit(cg, "    stp x29, x30, [sp, #-16]!\n");
    codegen_emit(cg, "    mov x29, sp\n");
    if (fg.frame_size > 0) {
        (void)fprintf(cg->out, "    sub sp, sp, #%d\n", fg.frame_size);
    }

    for (size_t i = 0; i < fn->as.function.param_count; i++) {
        LocalSlot *slot = find_local(&fg, fn->as.function.params[i].name);
        if (slot == NULL) {
            fprintf(stderr, "codegen error: missing param slot\n");
            exit(EXIT_FAILURE);
        }
        if (i > 7u) {
            fprintf(stderr, "codegen error: max 8 parameters supported\n");
            exit(EXIT_FAILURE);
        }
        if (slot->type == OMEGA_TYPE_FLOAT32) {
            (void)fprintf(cg->out, "    stur s%zu, [x29, #-%d]\n", i, slot->offset);
        } else if (type_is_pointer_like(slot->type)) {
            (void)fprintf(cg->out, "    stur x%zu, [x29, #-%d]\n", i, slot->offset);
        } else {
            (void)fprintf(cg->out, "    stur w%zu, [x29, #-%d]\n", i, slot->offset);
        }
    }

    gen_block(&fg, fn->as.function.body);
    if (fn->as.function.return_type == OMEGA_TYPE_FLOAT32) {
        codegen_emit(cg, "    fmov s0, #0.0\n");
    } else if (type_is_pointer_like(fn->as.function.return_type)) {
        codegen_emit(cg, "    mov x0, #0\n");
    } else {
        codegen_emit(cg, "    mov w0, #0\n");
    }
    (void)fprintf(cg->out, "%s:\n", fg.return_label);
    if (fg.frame_size > 0) {
        (void)fprintf(cg->out, "    add sp, sp, #%d\n", fg.frame_size);
    }
    codegen_emit(cg, "    ldp x29, x30, [sp], #16\n");
    codegen_emit(cg, "    ret\n");
}

bool generate_program_asm(ASTNode *program, const char *asm_path, Status *st) {
    FILE *out = fopen(asm_path, "w");
    if (out == NULL) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "failed to open asm output '%s': %s", asm_path, strerror(errno));
        status_fail(st, 1u, 1u, msg);
        return false;
    }

    Codegen cg;
    memset(&cg, 0, sizeof(cg));
    cg.out = out;

    codegen_emit(&cg, ".section __TEXT,__text,regular,pure_instructions\n");
    for (size_t i = 0; i < program->as.program.decl_count; i++) {
        ASTNode *decl = program->as.program.decls[i];
        if (decl->kind == AST_FUNCTION && !decl->as.function.is_extern) {
            gen_function(&cg, decl);
        }
    }

    if (cg.string_count > 0u) {
        codegen_emit(&cg, "\n.section __TEXT,__cstring,cstring_literals\n");
        for (size_t i = 0; i < cg.string_count; i++) {
            (void)fprintf(cg.out, "%s:\n", cg.strings[i].label);
            emit_string_asciz(cg.out, cg.strings[i].value);
        }
    }

    if (cg.float_count > 0u) {
        codegen_emit(&cg, "\n.section __TEXT,__const\n");
        codegen_emit(&cg, ".p2align 2\n");
        for (size_t i = 0; i < cg.float_count; i++) {
            (void)fprintf(cg.out, "%s:\n", cg.floats[i].label);
            (void)fprintf(cg.out, "    .long 0x%08x\n", cg.floats[i].bits);
        }
    }

    if (cg.need_sort_helper) {
        codegen_emit(&cg, "\n; omega_sort_i32_vec: insertion sort for vector<-int32\n");
        codegen_emit(&cg, "; struct layout: { int32* data @ 0, uint32 len @ 8, uint32 cap @ 12 }\n");
        codegen_emit(&cg, ".globl _omega_sort_i32_vec\n");
        codegen_emit(&cg, ".p2align 2\n");
        codegen_emit(&cg, "_omega_sort_i32_vec:\n");
        codegen_emit(&cg, "    stp x29, x30, [sp, #-32]!\n");
        codegen_emit(&cg, "    mov x29, sp\n");
        codegen_emit(&cg, "    stp x19, x20, [sp, #16]\n");
        codegen_emit(&cg, "    ldr x19, [x0]\n");
        codegen_emit(&cg, "    ldr w20, [x0, #8]\n");
        codegen_emit(&cg, "    cmp w20, #2\n");
        codegen_emit(&cg, "    b.lt Lomega_isort_done\n");
        codegen_emit(&cg, "    mov w1, #1\n");
        codegen_emit(&cg, "Lomega_isort_outer:\n");
        codegen_emit(&cg, "    cmp w1, w20\n");
        codegen_emit(&cg, "    b.ge Lomega_isort_done\n");
        codegen_emit(&cg, "    uxtw x2, w1\n");
        codegen_emit(&cg, "    ldr w3, [x19, x2, lsl #2]\n");
        codegen_emit(&cg, "    sub w4, w1, #1\n");
        codegen_emit(&cg, "Lomega_isort_inner:\n");
        codegen_emit(&cg, "    cmp w4, #0\n");
        codegen_emit(&cg, "    b.lt Lomega_isort_place\n");
        codegen_emit(&cg, "    uxtw x5, w4\n");
        codegen_emit(&cg, "    ldr w6, [x19, x5, lsl #2]\n");
        codegen_emit(&cg, "    cmp w6, w3\n");
        codegen_emit(&cg, "    b.le Lomega_isort_place\n");
        codegen_emit(&cg, "    add w7, w4, #1\n");
        codegen_emit(&cg, "    uxtw x7, w7\n");
        codegen_emit(&cg, "    str w6, [x19, x7, lsl #2]\n");
        codegen_emit(&cg, "    sub w4, w4, #1\n");
        codegen_emit(&cg, "    b Lomega_isort_inner\n");
        codegen_emit(&cg, "Lomega_isort_place:\n");
        codegen_emit(&cg, "    add w5, w4, #1\n");
        codegen_emit(&cg, "    uxtw x5, w5\n");
        codegen_emit(&cg, "    str w3, [x19, x5, lsl #2]\n");
        codegen_emit(&cg, "    add w1, w1, #1\n");
        codegen_emit(&cg, "    b Lomega_isort_outer\n");
        codegen_emit(&cg, "Lomega_isort_done:\n");
        codegen_emit(&cg, "    ldp x19, x20, [sp, #16]\n");
        codegen_emit(&cg, "    ldp x29, x30, [sp], #32\n");
        codegen_emit(&cg, "    ret\n");
    }

    if (fclose(out) != 0) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg), "failed to close asm output '%s': %s", asm_path, strerror(errno));
        status_fail(st, 1u, 1u, msg);
        return false;
    }
    return true;
}
