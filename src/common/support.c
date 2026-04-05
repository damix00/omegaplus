#include "omega/compiler.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

void status_fail(Status *st, uint32_t line, uint32_t column, const char *message) {
    if (!st->ok) {
        return;
    }
    st->ok = false;
    st->line = line;
    st->column = column;
    (void)snprintf(st->message, sizeof(st->message), "%s", message);
}

void *xcalloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if (p == NULL) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (p == NULL) {
        fprintf(stderr, "fatal: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

char *xstrndup_local(const char *s, size_t n) {
    char *out = (char *)xcalloc(n + 1u, 1u);
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

char *xstrdup_local(const char *s) {
    return xstrndup_local(s, strlen(s));
}

void token_vec_push(TokenVec *v, Token t) {
    if (v->len == v->cap) {
        size_t next = (v->cap == 0u) ? 16u : v->cap * 2u;
        v->data = (Token *)xrealloc(v->data, next * sizeof(Token));
        v->cap = next;
    }
    v->data[v->len++] = t;
}

void node_vec_push(NodeVec *v, ASTNode *n) {
    if (v->len == v->cap) {
        size_t next = (v->cap == 0u) ? 8u : v->cap * 2u;
        v->data = (ASTNode **)xrealloc(v->data, next * sizeof(ASTNode *));
        v->cap = next;
    }
    v->data[v->len++] = n;
}

void param_vec_push(ParamVec *v, Param p) {
    if (v->len == v->cap) {
        size_t next = (v->cap == 0u) ? 8u : v->cap * 2u;
        v->data = (Param *)xrealloc(v->data, next * sizeof(Param));
        v->cap = next;
    }
    v->data[v->len++] = p;
}

ASTNode *new_node(ASTNodeKind kind, uint32_t line, uint32_t column) {
    ASTNode *n = (ASTNode *)xcalloc(1u, sizeof(*n));
    n->kind = kind;
    n->line = line;
    n->column = column;
    n->inferred_type = OMEGA_TYPE_INVALID;
    return n;
}

const char *type_name(OmegaType t) {
    switch (t) {
        case OMEGA_TYPE_UINT32:
            return "uint32";
        case OMEGA_TYPE_INT32:
            return "int32";
        case OMEGA_TYPE_FLOAT32:
            return "float32";
        case OMEGA_TYPE_BOOLEAN:
            return "boolean";
        case OMEGA_TYPE_STRING:
            return "string";
        case OMEGA_TYPE_VECTOR:
            return "vector<-int32";
        case OMEGA_TYPE_VOID:
            return "void";
        default:
            return "invalid";
    }
}

bool type_is_numeric(OmegaType t) {
    return t == OMEGA_TYPE_UINT32 || t == OMEGA_TYPE_INT32 || t == OMEGA_TYPE_FLOAT32;
}

bool type_is_integral(OmegaType t) {
    return t == OMEGA_TYPE_UINT32 || t == OMEGA_TYPE_INT32;
}

bool type_is_pointer_like(OmegaType t) {
    return t == OMEGA_TYPE_STRING || t == OMEGA_TYPE_VECTOR;
}

bool vector_type_allows_element(OmegaType t) {
    return t == OMEGA_TYPE_INT32;
}

int align16(int n) {
    int rem = n % 16;
    if (rem == 0) {
        return n;
    }
    return n + (16 - rem);
}

char *read_whole_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "failed to open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0L) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)xcalloc((size_t)sz + 1u, 1u);
    size_t n = fread(buf, 1u, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[sz] = '\0';
    return buf;
}

char *strip_ext(const char *path) {
    const char *last_slash = strrchr(path, '/');
    const char *base = (last_slash == NULL) ? path : (last_slash + 1);
    const char *dot = strrchr(base, '.');
    size_t base_len = (dot == NULL) ? strlen(base) : (size_t)(dot - base);

    size_t prefix_len = (size_t)(base - path);
    char *out = (char *)xcalloc(prefix_len + base_len + 1u, 1u);
    memcpy(out, path, prefix_len);
    memcpy(out + prefix_len, base, base_len);
    out[prefix_len + base_len] = '\0';
    return out;
}

void print_usage(const char *argv0) {
    fprintf(stderr, "Usage: %s <input.u> [-o output] [--emit-asm-only]\n", argv0);
}

static int run_command_checked(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1) {
        fprintf(stderr, "failed to start command: %s\n", cmd);
        return 1;
    }
    if (WIFEXITED(rc)) {
        int code = WEXITSTATUS(rc);
        if (code != 0) {
            fprintf(stderr, "command failed (%d): %s\n", code, cmd);
        }
        return code;
    }
    fprintf(stderr, "command terminated abnormally: %s\n", cmd);
    return 1;
}

bool run_macho_toolchain(const char *asm_path, const char *obj_path, const char *exe_path) {
    char cmd1[1024];
    char cmd2[2048];

    (void)snprintf(cmd1, sizeof(cmd1), "as -arch arm64 \"%s\" -o \"%s\"", asm_path, obj_path);
    if (run_command_checked(cmd1) != 0) {
        return false;
    }

    (void)snprintf(cmd2, sizeof(cmd2),
                   "ld -arch arm64 -lSystem -syslibroot \"$(xcrun --sdk macosx --show-sdk-path)\" -e _main -o \"%s\" \"%s\"",
                   exe_path, obj_path);
    if (run_command_checked(cmd2) != 0) {
        return false;
    }
    return true;
}
