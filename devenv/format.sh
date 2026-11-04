#!/usr/bin/env bash
# Format all tracked C/C++ sources with clang-format (same excludes as CI).
# Usage: bash devenv/format.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
cd "$REPO_ROOT"

CF="$(command -v clang-format-11 || command -v clang-format || true)"
if [ -z "$CF" ]; then
  echo "ERROR: clang-format not found (apt install clang-format-11)" >&2
  exit 1
fi

echo "Using $CF ($("$CF" --version | head -1))"

git ls-files -z -- \
  '*.c' '*.cc' '*.cpp' '*.cxx' \
  '*.h' '*.hh' '*.hpp' '*.hxx' \
  | grep -zvE '^(extern/|src/ui/qml/quickfuture/)' \
  | xargs -0 -r "$CF" -i

echo "Format OK"
