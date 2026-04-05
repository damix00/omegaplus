#ifndef OMEGA_MODULES_H
#define OMEGA_MODULES_H

#include <stdbool.h>

typedef enum OmegaBuiltinKind {
    OMEGA_BUILTIN_NONE = 0,
    OMEGA_BUILTIN_PRINT,
    OMEGA_BUILTIN_INPUT,
    OMEGA_BUILTIN_VECTOR_METHOD_PUSH_BACK,
    OMEGA_BUILTIN_VECTOR_METHOD_POP_BACK,
    OMEGA_BUILTIN_VECTOR_METHOD_CLEAR,
    OMEGA_BUILTIN_VECTOR_PROPERTY_SIZE
} OmegaBuiltinKind;

typedef struct OmegaBuiltinSpec {
    OmegaBuiltinKind kind;
    const char *module_name;
    const char *name;
    bool allow_plain_calls;
    bool requires_import;
} OmegaBuiltinSpec;

bool omega_module_exists(const char *module_name);
const OmegaBuiltinSpec *omega_find_namespaced_builtin(const char *module_name, const char *function_name);
const OmegaBuiltinSpec *omega_find_plain_builtin(const char *function_name);
const OmegaBuiltinSpec *omega_find_vector_method_builtin(const char *method_name);
const OmegaBuiltinSpec *omega_find_vector_property_builtin(const char *property_name);

#endif
