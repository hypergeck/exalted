#!/usr/bin/env bash
# Builds and runs the platform-independent tests with whatever C++20 compiler
# is on PATH. Works on macOS/Linux without CMake; CMake drives the same tests
# on Windows.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${CXX:-clang++}"
OUT="${OUT:-build/portable-tests}"
mkdir -p "$OUT"
status=0
for src in tests/test_*.cpp; do
  name="$(basename "$src" .cpp)"
  "$CXX" -std=c++20 -Wall -Wextra -Werror -O1 -Icore/include -Itests -pthread "$src" -o "$OUT/$name"
  "$OUT/$name" || status=1
done
exit $status
