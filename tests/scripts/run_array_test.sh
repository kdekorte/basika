#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT_DIR"

echo "Building..."
make -s

echo "Running array test"
OUT=$(./basika tests/arrays/array_test.bas 2>&1)
printf "%s\n" "$OUT"

EXP=$(cat tests/arrays/array_test.expected)

if [ "$OUT" = "$EXP" ]; then
  echo "Array Test PASS"
  exit 0
else
  echo "Array Test FAIL"
  echo "--- expected ---"
  printf "%s\n" "$EXP"
  echo "--- actual ---"
  printf "%s\n" "$OUT"
  exit 2
fi
