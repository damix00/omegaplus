# Compiler Architecture

OmegaCross (`bin/omegac`) uses a single-pass driver with stage-specific source directories:

- `src/frontend/` for lexing and parsing
- `src/semantic/` for type checking and symbol resolution
- `src/backend/` for ARM64 assembly generation
- `src/modules/` for import and builtin registry data
- `src/common/` for shared utilities
- `src/main.c` for the compiler entrypoint

1. Lexer
- Converts OmegaPlus source into tokens.
- Handles multi-char operators and strict literal suffixes (`u`, `i`, `f`, `b`).
- Recognizes vector declarations such as `vector<-int32`.

2. Parser (Recursive Descent)
- Builds AST from tokens.
- Supports expression precedence, statements, control flow, indexing, and member syntax.
- Implements postfix return syntax (`expr!`).

3. Semantic Analysis
- Resolves symbols and validates strict typing.
- Enforces operator/type compatibility and function call signatures.
- Validates vector members such as `.push_back(...)`, `.pop_back()`, `.clear()`, and `.size`.
- Ensures non-void functions return on all control paths.

4. ARM64 Codegen
- Emits Mach-O compatible assembly.
- Integer/boolean paths use `w` registers.
- Float32 paths use `s` registers and float ALU instructions.

5. Module Registry
- Table-driven module metadata and builtin mapping live in `src/modules/registry.c`.
- Imports are validated semantically against the registry, making module additions straightforward.

6. Toolchain Orchestration
- Runs `as` and `ld` to produce executable outputs.
