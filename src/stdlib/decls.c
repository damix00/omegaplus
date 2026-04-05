/*
 * OmegaPlus standard library module declarations.
 *
 * To add a new stdlib module:
 *   1. Create  stdlib/<module>.c  with functions named  <module>_<funcname>
 *   2. Add entries to k_stdlib_decls below.
 *   3. Run `make` — the new module is immediately available in every .u file.
 *
 * No `import` statement is needed for stdlib modules.
 */

#include "omega/compiler.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *module_name;
    const char *func_name;
    OmegaType   return_type;
    size_t      param_count;
    struct { OmegaType type; const char *name; } params[8];
} StdlibDecl;

/* ── Add stdlib module functions here ──────────────────────────────────── */
static const StdlibDecl k_stdlib_decls[] = {
    /* math module */
    { "math", "pow",   OMEGA_TYPE_INT32, 2, { {OMEGA_TYPE_INT32,"base"}, {OMEGA_TYPE_INT32,"exp"} } },
    { "math", "sqrt",  OMEGA_TYPE_INT32, 1, { {OMEGA_TYPE_INT32,"n"} } },
    { "math", "abs",   OMEGA_TYPE_INT32, 1, { {OMEGA_TYPE_INT32,"n"} } },
    { "math", "min",   OMEGA_TYPE_INT32, 2, { {OMEGA_TYPE_INT32,"a"}, {OMEGA_TYPE_INT32,"b"} } },
    { "math", "max",   OMEGA_TYPE_INT32, 2, { {OMEGA_TYPE_INT32,"a"}, {OMEGA_TYPE_INT32,"b"} } },
    { "math", "clamp", OMEGA_TYPE_INT32, 3, { {OMEGA_TYPE_INT32,"val"}, {OMEGA_TYPE_INT32,"lo"}, {OMEGA_TYPE_INT32,"hi"} } },
    { "math", "gcd",   OMEGA_TYPE_INT32, 2, { {OMEGA_TYPE_INT32,"a"}, {OMEGA_TYPE_INT32,"b"} } },
};
/* ────────────────────────────────────────────────────────────────────────── */

void omega_register_stdlib(Semantic *sem) {
    size_t n = sizeof(k_stdlib_decls) / sizeof(k_stdlib_decls[0]);
    for (size_t i = 0; i < n; i++) {
        const StdlibDecl *d = &k_stdlib_decls[i];
        FunctionSymbol sym;
        memset(&sym, 0, sizeof(sym));
        sym.name        = xstrdup_local(d->func_name);
        sym.module_name = xstrdup_local(d->module_name);
        sym.return_type = d->return_type;
        sym.param_count = d->param_count;
        sym.is_extern   = true;
        if (d->param_count > 0u) {
            sym.params = (Param *)xcalloc(d->param_count, sizeof(Param));
            for (size_t p = 0; p < d->param_count; p++) {
                sym.params[p].type = d->params[p].type;
                sym.params[p].name = xstrdup_local(d->params[p].name);
            }
        }
        semantic_register_module_func(sem, sym);
    }
}
