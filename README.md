# OmegaCross

OmegaCross is a C11 compiler (`omegac`) for OmegaPlus source (`.u`) targeting ARM64 Mach-O on Apple Silicon.

## Repository Layout

```text
.
├── src/
│   ├── backend/         # ARM64 code generation
│   ├── common/          # Shared helpers
│   ├── frontend/        # Lexer + parser
│   ├── modules/         # Module registry
│   ├── semantic/        # Semantic analysis
│   └── main.c           # Compiler driver
├── include/omega/       # Shared compiler headers
├── examples/            # Example OmegaPlus programs
├── tests/               # Golden success/fail test suite
├── bin/                 # Built compiler binary
├── build/               # Built example executables and artifacts
├── docs/                # Project docs
└── legacy/              # Historical/reference files
```

## Language Support

- Types: `uint32`, `int32`, `float32`, `string`, `boolean`, `vector<-int32>`
- Literal suffixes: `u`, `i`, `f`, `b`
- Control flow: `if`, `else`, `while`
- Statements: declarations, assignment (`x = expr.`), expression statements, postfix return (`value!`)
- Built-ins: `print(expr).`, `input(identifier).`, plus namespace forms `sti<->print(...)` and `sti<->input(...)`
- Modules:
  - `import sti.` for standard I/O
  - `import vector.` for vectors (`example_vector.push_back(x).`, `example_vector.pop_back()`, `example_vector.clear()`, `example_vector.size`, `example_vector[index]`)
- Operators:
  - Arithmetic: `+`, `-`, `*`, `/`, `%`
  - Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
  - Logic: `~`, `\/`, `/\`

## Build

```bash
make
```

Compiler output path:

```text
bin/omegac
```

## Compile a Program

```bash
bin/omegac examples/fib_recursive.u -o build/fib_recursive
build/fib_recursive
```

## Included Example Targets

```bash
make run-example
make run-float-example
make run-vector-example
```

## Test Suite

```bash
make test
```

This runs golden tests under `tests/cases/success` and `tests/cases/fail`.

## Strict Typing Rules

- No implicit casts.
- Binary operators require type-compatible operands.
- `%` requires integral operands (`uint32`/`int32`).
- Non-void functions must return on all code paths.
