#include "omega/modules.h"

#include <stddef.h>
#include <string.h>

typedef struct OmegaModuleInfo {
    const char *name;
} OmegaModuleInfo;

static const OmegaModuleInfo k_modules[] = {
    {"sti"},
    {"vector"},
};

static const OmegaBuiltinSpec k_function_builtins[] = {
    {
        .kind = OMEGA_BUILTIN_PRINT,
        .module_name = "sti",
        .name = "print",
        .allow_plain_calls = true,
        .requires_import = false,
    },
    {
        .kind = OMEGA_BUILTIN_INPUT,
        .module_name = "sti",
        .name = "input",
        .allow_plain_calls = true,
        .requires_import = false,
    },
};

static const OmegaBuiltinSpec k_vector_method_builtins[] = {
    {
        .kind = OMEGA_BUILTIN_VECTOR_METHOD_PUSH_BACK,
        .module_name = "vector",
        .name = "push_back",
        .allow_plain_calls = false,
        .requires_import = true,
    },
    {
        .kind = OMEGA_BUILTIN_VECTOR_METHOD_POP_BACK,
        .module_name = "vector",
        .name = "pop_back",
        .allow_plain_calls = false,
        .requires_import = true,
    },
    {
        .kind = OMEGA_BUILTIN_VECTOR_METHOD_CLEAR,
        .module_name = "vector",
        .name = "clear",
        .allow_plain_calls = false,
        .requires_import = true,
    },
    {
        .kind = OMEGA_BUILTIN_VECTOR_METHOD_SORT,
        .module_name = "vector",
        .name = "sort",
        .allow_plain_calls = false,
        .requires_import = true,
    },
    {
        .kind = OMEGA_BUILTIN_VECTOR_METHOD_FREE,
        .module_name = "vector",
        .name = "free",
        .allow_plain_calls = false,
        .requires_import = true,
    },
};

static const OmegaBuiltinSpec k_vector_property_builtins[] = {
    {
        .kind = OMEGA_BUILTIN_VECTOR_PROPERTY_SIZE,
        .module_name = "vector",
        .name = "size",
        .allow_plain_calls = false,
        .requires_import = true,
    },
};

bool omega_module_exists(const char *module_name) {
    if (module_name == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(k_modules) / sizeof(k_modules[0]); i++) {
        if (strcmp(k_modules[i].name, module_name) == 0) {
            return true;
        }
    }
    return false;
}

const OmegaBuiltinSpec *omega_find_namespaced_builtin(const char *module_name, const char *function_name) {
    if (module_name == NULL || function_name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(k_function_builtins) / sizeof(k_function_builtins[0]); i++) {
        const OmegaBuiltinSpec *spec = &k_function_builtins[i];
        if (strcmp(spec->module_name, module_name) == 0 && strcmp(spec->name, function_name) == 0) {
            return spec;
        }
    }
    return NULL;
}

const OmegaBuiltinSpec *omega_find_plain_builtin(const char *function_name) {
    if (function_name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(k_function_builtins) / sizeof(k_function_builtins[0]); i++) {
        const OmegaBuiltinSpec *spec = &k_function_builtins[i];
        if (!spec->allow_plain_calls) {
            continue;
        }
        if (strcmp(spec->name, function_name) == 0) {
            return spec;
        }
    }
    return NULL;
}

const OmegaBuiltinSpec *omega_find_vector_method_builtin(const char *method_name) {
    if (method_name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(k_vector_method_builtins) / sizeof(k_vector_method_builtins[0]); i++) {
        const OmegaBuiltinSpec *spec = &k_vector_method_builtins[i];
        if (strcmp(spec->name, method_name) == 0) {
            return spec;
        }
    }
    return NULL;
}

const OmegaBuiltinSpec *omega_find_vector_property_builtin(const char *property_name) {
    if (property_name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(k_vector_property_builtins) / sizeof(k_vector_property_builtins[0]); i++) {
        const OmegaBuiltinSpec *spec = &k_vector_property_builtins[i];
        if (strcmp(spec->name, property_name) == 0) {
            return spec;
        }
    }
    return NULL;
}
