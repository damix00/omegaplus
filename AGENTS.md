# CLAUDE.md

This file provides guidance to AI agents when working with code in this repository.

## Project Overview

OmegaPlus is a vibe-coded programming language. OmegaCross is a C11 compiler (`omegac`) for OmegaPlus source files (`.u`) targeting ARM64 Mach-O on Apple Silicon.

## Common Commands

```bash
make              # Build the compiler → bin/omegac
make test         # Run golden test suite (tests/cases/success + tests/cases/fail)
make clean        # Remove compiler binary and build artifacts

# Compile and run an OmegaPlus program
bin/omegac examples/fib_recursive.u -o build/fib_recursive
build/fib_recursive

# Built-in example targets
make run-example          # fib_recursive
make run-float-example    # float_demo
make run-vector-example   # vector_demo
```

## Compiler Architecture

The pipeline is: source file → **Lexer** → **Parser** → **Semantic analysis** → **ARM64 assembly** → Mach-O binary via system linker.

All shared types (AST nodes, tokens, pass structs, helper prototypes) live in `include/omega/compiler.h`. Each compiler pass is a single `.c` file:

| File                      | Responsibility                                                                          |
| ------------------------- | --------------------------------------------------------------------------------------- |
| `src/main.c`              | Driver: reads file, runs all passes, invokes assembler/linker                           |
| `src/frontend/lexer.c`    | Tokenises `.u` source; entry point `lex_all()`                                          |
| `src/frontend/parser.c`   | Recursive-descent parser; entry point `parse_program()`                                 |
| `src/semantic/semantic.c` | Type checking, scope resolution, return-path analysis; entry point `semantic_analyze()` |
| `src/backend/codegen.c`   | ARM64 assembly emission; entry point `generate_program_asm()`                           |
| `src/modules/registry.c`  | Module metadata for `import sti.` / `import vector.`                                    |
| `src/common/support.c`    | Shared helpers: `xcalloc`, `xstrdup`, `read_whole_file`, etc.                           |
| `include/omega/modules.h` | `OmegaBuiltinKind` enum and module declarations                                         |

The central data structure is `ASTNode` (a tagged union in `compiler.h`), carrying `inferred_type` set during semantic analysis and consumed by codegen.

## Test Suite

Tests live in `tests/cases/success/` and `tests/cases/fail/`. Success cases each have a `.u` source and a `.out` golden file. The runner (`tests/run_tests.sh`) rebuilds the compiler from scratch, then compiles and runs each success case comparing stdout, and verifies that fail cases are rejected by the compiler.

To add a test: drop a `.u` file in the appropriate directory; for success cases also add a matching `.out` file.

## Language Notes

- Statements are terminated with `.` (period), not `;`
- Return uses postfix `!` syntax: `value!`
- Namespace calls use `<->`: `sti<->print(x).`
- Vector type syntax: `vector<-int32`
- Literal suffixes: `u` (uint32), `i` (int32), `f` (float32), `b` (boolean)
- No implicit casts; `%` requires integral operands only
- Non-void functions must return on all code paths
