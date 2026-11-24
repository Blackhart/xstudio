#!/usr/bin/env bash
# Build and run Mikros PMS unit tests (shotbrowser's data layer).
# Usage: bash devenv/run-shotbrowser-tests.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
cd "$REPO_ROOT"

PMS_SRC="${REPO_ROOT}/src/plugin/data_source/mikros/pms"
BUILD_DIR="${REPO_ROOT}/build/pms-tests"
JOBS="${JOBS:-$(nproc)}"

if [ ! -d "$PMS_SRC" ]; then
  echo "ERROR: missing PMS sources at $PMS_SRC" >&2
  exit 1
fi

PREFIX=""
for candidate in \
  "${REPO_ROOT}/vcpkg_installed/x64-xstudio-linux-ubuntu2404" \
  "${REPO_ROOT}/vcpkg_installed/x64-xstudio-linux-rocky9" \
  "${REPO_ROOT}/vcpkg_installed/x64-linux"
do
  if [ -d "$candidate" ]; then
    PREFIX="$candidate"
    break
  fi
done

if [ -z "$PREFIX" ]; then
  echo "ERROR: no vcpkg_installed triplet found under ${REPO_ROOT}/vcpkg_installed" >&2
  echo "  Run a host build first (task: xstudio: build (Ubuntu 24.04))." >&2
  exit 1
fi

echo "==> Configure PMS tests (${BUILD_DIR})"
cmake -S "$PMS_SRC" -B "$BUILD_DIR" \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$PREFIX"

echo "==> Build pms_tests (-j${JOBS})"
cmake --build "$BUILD_DIR" -j"${JOBS}"

echo "==> Run ctest"
ctest --test-dir "$BUILD_DIR" --output-on-failure -j"${JOBS}"
