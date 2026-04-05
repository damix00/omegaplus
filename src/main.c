#include "omega/compiler.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = NULL;
    bool emit_asm_only = false;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing argument after -o\n");
                return 1;
            }
            output_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--emit-asm-only") == 0) {
            emit_asm_only = true;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
        input_path = argv[i];
    }

    if (input_path == NULL) {
        fprintf(stderr, "missing input file\n");
        return 1;
    }

    char *owned_output = NULL;
    if (output_path == NULL) {
        owned_output = strip_ext(input_path);
        output_path = owned_output;
    }

    char *source = read_whole_file(input_path);
    if (source == NULL) {
        fprintf(stderr, "failed to read input file: %s\n", input_path);
        free(owned_output);
        return 1;
    }

    Lexer lx;
    memset(&lx, 0, sizeof(lx));
    lx.src = source;
    lx.len = strlen(source);
    lx.line = 1u;
    lx.column = 1u;
    if (!lex_all(&lx)) {
        fprintf(stderr, "lex error at %u:%u: %s\n", lx.st.line, lx.st.column, lx.st.message);
        free(source);
        free(owned_output);
        return 1;
    }

    Parser p;
    memset(&p, 0, sizeof(p));
    p.tokens = lx.tokens.data;
    p.count = lx.tokens.len;
    p.st.ok = true;
    ASTNode *program = parse_program(&p);
    if (program == NULL || !p.st.ok) {
        fprintf(stderr, "parse error at %u:%u: %s\n", p.st.line, p.st.column, p.st.message);
        free(source);
        free(owned_output);
        return 1;
    }

    Semantic sem;
    memset(&sem, 0, sizeof(sem));
    if (!semantic_analyze(&sem, program)) {
        fprintf(stderr, "semantic error at %u:%u: %s\n", sem.st.line, sem.st.column, sem.st.message);
        free(source);
        free(owned_output);
        return 1;
    }

    size_t out_len = strlen(output_path);
    char *asm_path = (char *)xcalloc(out_len + 3u, 1u);
    char *obj_path = (char *)xcalloc(out_len + 3u, 1u);
    (void)snprintf(asm_path, out_len + 3u, "%s.s", output_path);
    (void)snprintf(obj_path, out_len + 3u, "%s.o", output_path);

    Status cg_st;
    memset(&cg_st, 0, sizeof(cg_st));
    cg_st.ok = true;
    if (!generate_program_asm(program, asm_path, &cg_st)) {
        fprintf(stderr, "codegen error: %s\n", cg_st.message);
        free(obj_path);
        free(asm_path);
        free(source);
        free(owned_output);
        return 1;
    }

    /* Collect file imports */
    char *c_files[16];
    size_t c_file_count = 0;
    for (size_t i = 0; i < program->as.program.decl_count && c_file_count < 16u; i++) {
        ASTNode *d = program->as.program.decls[i];
        if (d->kind == AST_IMPORT && d->as.import_stmt.is_file_import) {
            c_files[c_file_count++] = d->as.import_stmt.file_path;
        }
    }

    if (!emit_asm_only) {
        if (!run_macho_toolchain(asm_path, obj_path, output_path, c_files, c_file_count)) {
            free(obj_path);
            free(asm_path);
            free(source);
            free(owned_output);
            return 1;
        }
    }

    printf("OmegaPlus compile succeeded: %s\n", output_path);
    printf("  asm: %s\n", asm_path);
    if (!emit_asm_only) {
        printf("  obj: %s\n", obj_path);
    }

    free(obj_path);
    free(asm_path);
    free(source);
    free(owned_output);
    return 0;
}
