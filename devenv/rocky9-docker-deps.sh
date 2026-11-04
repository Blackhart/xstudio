#!/usr/bin/env bash
# Build xSTUDIO vcpkg ports for Rocky 9 inside the toolchain container.
# Host machines often lack kernel-headers; never run Rocky deps on the host.
#
# Usage: bash devenv/rocky9-docker-deps.sh
#
# Env:
#   JOBS                  parallel jobs (default 8)
#   VCPKG_ROOT            default: <repo>/../vcpkg
#   DEPS_IMAGE            default: xstudio-toolchain:rocky9
#   FORCE_TOOLCHAIN_BUILD=1  rebuild the toolchain image even if it exists
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"

if ! command -v docker >/dev/null 2>&1; then
  echo "ERROR: docker not found" >&2
  exit 1
fi

# shellcheck disable=SC1091
source "${REPO_ROOT}/ci/linux/versions.env"

JOBS="${JOBS:-8}"
VCPKG_ROOT="${VCPKG_ROOT:-$(cd "${REPO_ROOT}/.." && pwd)/vcpkg}"
DEPS_IMAGE="${DEPS_IMAGE:-xstudio-toolchain:rocky9}"
DOCKERFILE="${REPO_ROOT}/ci/linux/dockerfiles/rocky9.toolchain.Dockerfile"
CONTEXT="${REPO_ROOT}/ci/linux"

if [ ! -x "${VCPKG_ROOT}/vcpkg" ] && [ ! -d "${VCPKG_ROOT}/.git" ]; then
  echo "==> Bootstrapping host vcpkg clone at ${VCPKG_ROOT}"
  bash "${HERE}/bootstrap-vcpkg.sh"
fi

need_image=0
if [ "${FORCE_TOOLCHAIN_BUILD:-0}" = "1" ]; then
  need_image=1
elif ! docker image inspect "${DEPS_IMAGE}" >/dev/null 2>&1; then
  need_image=1
fi

if [ "${need_image}" -eq 1 ]; then
  echo "==> Building toolchain image ${DEPS_IMAGE}"
  docker build \
    -t "${DEPS_IMAGE}" \
    -f "${DOCKERFILE}" \
    --build-arg "JOBS=${JOBS}" \
    --build-arg "CMAKE_VERSION=${CMAKE_VERSION}" \
    --build-arg "NINJA_VERSION=${NINJA_VERSION}" \
    --build-arg "AQTINSTALL_VERSION=${AQTINSTALL_VERSION}" \
    --build-arg "QT_VERSION=${QT_VERSION}" \
    "${CONTEXT}"
else
  echo "==> Using existing image ${DEPS_IMAGE}"
fi

export DEPS_IMAGE
export DEPS_TAG=rocky9
export VCPKG_ROOT
export JOBS
export SKIP_VCPKG_BOOTSTRAP="${SKIP_VCPKG_BOOTSTRAP:-0}"

echo "==> Installing vcpkg ports in container (x64-xstudio-linux-rocky9)"
bash "${REPO_ROOT}/ci/linux/scripts/run-build-container.sh" deps

MARKER="${REPO_ROOT}/vcpkg_installed/x64-xstudio-linux-rocky9/lib/libssl.so"
if [ ! -e "${MARKER}" ]; then
  echo "ERROR: deps finished but missing ${MARKER}" >&2
  exit 1
fi

echo "==> Rocky 9 deps OK → ${REPO_ROOT}/vcpkg_installed/x64-xstudio-linux-rocky9"
echo "    Next: VS Code task « xstudio: build (Rocky 9) » or F5 Debug xStudio (Rocky 9)"
