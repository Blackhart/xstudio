#!/usr/bin/env bash
# Clone/checkout vcpkg at the manifest baseline (sibling of the repo).
# Usage: bootstrap-vcpkg.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
VCPKG_ROOT="${VCPKG_ROOT:-$(cd "${REPO_ROOT}/.." && pwd)/vcpkg}"

VCPKG_BASELINE="$(
  python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['builtin-baseline'])" \
    "${REPO_ROOT}/vcpkg.json"
)"

echo "==> vcpkg root: ${VCPKG_ROOT} (baseline ${VCPKG_BASELINE})"
if [ ! -d "${VCPKG_ROOT}/.git" ]; then
  git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
else
  git -C "${VCPKG_ROOT}" fetch origin
fi
git -C "${VCPKG_ROOT}" checkout --force "${VCPKG_BASELINE}"
"${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
echo "==> vcpkg OK"
