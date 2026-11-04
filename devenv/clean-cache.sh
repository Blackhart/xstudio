#!/usr/bin/env bash
# Nuclear reset (corrupt tree, etc.). Day-to-day: edit presets/code, hit F5.
#
# Removes:
#   - <repo>/__build/
#   - <repo>/vcpkg_installed/
#   - <repo>/compile_commands.json
#   - legacy ~/.cache/xstudio-devenv/{build,deps}/ if present
# Keeps: Qt SDK + aqt-venv under devenv cache, ../vcpkg
#
# Usage: devenv/clean-cache.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${HERE}/.." && pwd)"
CACHE="${XSTUDIO_DEVENV_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/xstudio-devenv}"

echo "==> Host build clean"

removed=0
for path in \
  "${REPO_ROOT}/__build" \
  "${REPO_ROOT}/vcpkg_installed" \
  "${REPO_ROOT}/compile_commands.json" \
  "${CACHE}/build" \
  "${CACHE}/deps"
do
  if [ -e "${path}" ] || [ -L "${path}" ]; then
    echo "==> Removing ${path}"
    rm -rf "${path}"
    removed=1
  fi
done

if [ "${removed}" -eq 0 ]; then
  echo "==> Already clean"
else
  echo "==> Done — next F5 reconfigures into __build/"
fi
