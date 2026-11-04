#!/usr/bin/env bash
# Host configure/build via CMake presets + vcpkg.
# Usage: configure-host.sh ubuntu24|rocky9 [--build]
#
# binaryDir: <repo>/__build/<tag> (CMakeUserPresets.json).
# Qt SDK stays in XSTUDIO_DEVENV_CACHE (outside the source tree).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
# shellcheck disable=SC1091
source "$HERE/paths.sh"

DISTRO="${1:-}"
DO_BUILD=0
if [ "${2:-}" = "--build" ]; then
  DO_BUILD=1
elif [ "${1:-}" = "--build" ]; then
  echo "ERROR: usage: configure-host.sh ubuntu24|rocky9 [--build]" >&2
  exit 1
fi

case "$DISTRO" in
  ubuntu24)
    TAG="ubuntu2404"
    TRIPLET="x64-xstudio-linux-ubuntu2404"
    PRESET="HostUbuntu2404NinjaRelWithDebInfo"
    ;;
  rocky9)
    TAG="rocky9"
    TRIPLET="x64-xstudio-linux-rocky9"
    PRESET="HostRocky9NinjaRelWithDebInfo"
    echo "WARNING: Rocky triplet on a non-Rocky host is an ABI compromise." >&2
    ;;
  *)
    echo "ERROR: distro must be ubuntu24 or rocky9 (got '${DISTRO:-}')" >&2
    exit 1
    ;;
esac

# shellcheck disable=SC1091
source "$REPO_ROOT/ci/linux/versions.env"

VCPKG_ROOT="${VCPKG_ROOT:-$(cd "${REPO_ROOT}/.." && pwd)/vcpkg}"
QT_ROOT="${DEVENV_CACHE}/qt"
QT_GCC="${QT_ROOT}/${QT_VERSION}/gcc_64"
QT6_DIR="${QT_GCC}/lib/cmake/Qt6"
BUILD_DIR="${REPO_ROOT}/__build/${TAG}"
JOBS="${JOBS:-8}"

if [ ! -x "${VCPKG_ROOT}/vcpkg" ]; then
  echo "ERROR: vcpkg not found at ${VCPKG_ROOT}" >&2
  echo "  Run: bash devenv/bootstrap-vcpkg.sh" >&2
  exit 1
fi
if [ ! -d "${QT6_DIR}" ]; then
  echo "ERROR: Qt missing at ${QT6_DIR}" >&2
  echo "  Run: bash devenv/install-qt.sh" >&2
  exit 1
fi
if ! command -v ninja >/dev/null 2>&1; then
  echo "ERROR: host ninja not found (apt install ninja-build)" >&2
  exit 1
fi

export VCPKG_ROOT
export VCPKG_TARGET_TRIPLET="${TRIPLET}"
export XSTUDIO_SRC="${REPO_ROOT}"
export JOBS

echo "==> Installing vcpkg ports (${TRIPLET})"
bash "${REPO_ROOT}/ci/linux/scripts/build-deps.sh"

mkdir -p "${BUILD_DIR}"

echo "==> cmake --preset ${PRESET} → ${BUILD_DIR}"
cmake --preset "${PRESET}" \
  -DQt6_DIR="${QT6_DIR}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DCMAKE_CXX_FLAGS="-Wno-error=maybe-uninitialized" \
  -DVCPKG_MANIFEST_INSTALL=OFF \
  -DVCPKG_INSTALLED_DIR="${REPO_ROOT}/vcpkg_installed"

ln -sfn "${BUILD_DIR}/compile_commands.json" "${REPO_ROOT}/compile_commands.json"

if [ "${DO_BUILD}" -eq 1 ]; then
  echo "==> cmake --build --preset ${PRESET} -j${JOBS}"
  cmake --build --preset "${PRESET}" -j "${JOBS}"
fi

BIN="${BUILD_DIR}/bin/xstudio.bin"
if [ "${DO_BUILD}" -eq 1 ] && [ ! -x "${BIN}" ]; then
  echo "ERROR: missing ${BIN}" >&2
  exit 1
fi

VCPKG_INSTALLED="${REPO_ROOT}/vcpkg_installed/${TRIPLET}"
export LD_LIBRARY_PATH="${VCPKG_INSTALLED}/lib:${QT_GCC}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${QT_GCC}/plugins"
export QML2_IMPORT_PATH="${QT_GCC}/qml"

echo "==> OK (${PRESET})"
echo "    binary: ${BIN:-${BUILD_DIR}/bin/xstudio.bin}"
