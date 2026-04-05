#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$ROOT_DIR/tests/tmp"
COMPILER="$ROOT_DIR/bin/omegac"
SOURCE_FILES="$(find "$ROOT_DIR/src" -type f -name '*.c' | sort)"

mkdir -p "$TMP_DIR"
rm -f "$TMP_DIR"/*
mkdir -p "$ROOT_DIR/bin"

cc -std=c11 -Wall -Wextra -pedantic -I"$ROOT_DIR/include" $SOURCE_FILES -o "$COMPILER"

pass_count=0
fail_count=0

for src in "$ROOT_DIR"/tests/cases/success/*.u; do
  name="$(basename "$src" .u)"
  expected_file="$ROOT_DIR/tests/cases/success/$name.out"
  bin="$TMP_DIR/$name.bin"
  compile_err="$TMP_DIR/$name.compile.err"
  run_out="$TMP_DIR/$name.run.out"

  if ! "$COMPILER" "$src" -o "$bin" > /dev/null 2> "$compile_err"; then
    echo "[FAIL] success/$name (compiler failed)"
    cat "$compile_err"
    fail_count=$((fail_count + 1))
    continue
  fi

  if ! "$bin" > "$run_out"; then
    echo "[FAIL] success/$name (program crashed)"
    fail_count=$((fail_count + 1))
    continue
  fi

  expected="$(<"$expected_file")"
  actual="$(<"$run_out")"
  if [[ "$expected" == "$actual" ]]; then
    echo "[PASS] success/$name"
    pass_count=$((pass_count + 1))
  else
    echo "[FAIL] success/$name (output mismatch)"
    echo "  expected: $expected"
    echo "  actual:   $actual"
    fail_count=$((fail_count + 1))
  fi
done

for src in "$ROOT_DIR"/tests/cases/fail/*.u; do
  name="$(basename "$src" .u)"
  expected_err_file="$ROOT_DIR/tests/cases/fail/$name.err"
  compile_err="$TMP_DIR/$name.compile.err"
  bin="$TMP_DIR/$name.bin"

  if "$COMPILER" "$src" -o "$bin" > /dev/null 2> "$compile_err"; then
    echo "[FAIL] fail/$name (unexpected compile success)"
    fail_count=$((fail_count + 1))
    continue
  fi

  expected_substring="$(<"$expected_err_file")"
  if grep -Fq "$expected_substring" "$compile_err"; then
    echo "[PASS] fail/$name"
    pass_count=$((pass_count + 1))
  else
    echo "[FAIL] fail/$name (wrong error)"
    echo "  expected substring: $expected_substring"
    echo "  compiler output:"
    cat "$compile_err"
    fail_count=$((fail_count + 1))
  fi
done

echo
echo "Test summary: $pass_count passed, $fail_count failed."
if [[ $fail_count -ne 0 ]]; then
  exit 1
fi
