#!/usr/bin/env bash
# F5 preLaunch: ensure vcpkg + Qt + host RelWithDebInfo build.
# Usage: debug-prelaunch.sh ubuntu24|rocky9
#
# Build: <repo>/__build/<tag>/
# Qt:    ${XDG_CACHE_HOME:-$HOME/.cache}/xstudio-devenv/qt/
# vcpkg: sibling ../vcpkg (override with VCPKG_ROOT)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
# shellcheck disable=SC1091
source "$HERE/paths.sh"
DISTRO="${1:-}"

case "$DISTRO" in
  ubuntu24|rocky9) ;;
  *)
    echo "ERROR: distro must be ubuntu24 or rocky9 (got '${DISTRO:-}')" >&2
    exit 1
    ;;
esac

case "$DISTRO" in
  ubuntu24) TAG="ubuntu2404" ;;
  rocky9)   TAG="rocky9" ;;
esac

export JOBS="${JOBS:-8}"

missing=()
for cmd in cmake ninja g++ gdb python3 git; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    missing+=("$cmd")
  fi
done
if [ "${#missing[@]}" -gt 0 ]; then
  echo "ERROR: missing host tools: ${missing[*]}" >&2
  echo "  See: $HERE/host-packages.txt" >&2
  exit 1
fi

bash "$HERE/bootstrap-vcpkg.sh"
bash "$HERE/install-qt.sh"
bash "$HERE/configure-host.sh" "$DISTRO" --build

BIN="${REPO_ROOT}/__build/${TAG}/bin/xstudio.bin"
if [ ! -x "$BIN" ]; then
  echo "ERROR: missing $BIN" >&2
  exit 1
fi

echo "==> Ready for gdb host ($BIN)"
