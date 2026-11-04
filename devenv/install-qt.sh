#!/usr/bin/env bash
# Install Qt SDK via aqtinstall into the devenv cache (outside the source tree).
# Usage: install-qt.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
# shellcheck disable=SC1091
source "$HERE/paths.sh"
# shellcheck disable=SC1091
source "$REPO_ROOT/ci/linux/versions.env"

QT_ROOT="${DEVENV_CACHE}/qt"
QT_GCC="${QT_ROOT}/${QT_VERSION}/gcc_64"
QT6_DIR="${QT_GCC}/lib/cmake/Qt6"

if [ -d "${QT6_DIR}" ]; then
  echo "==> Qt already present at ${QT_GCC}"
  exit 0
fi

AQT_VENV="${DEVENV_CACHE}/aqt-venv"
echo "==> Installing aqtinstall==${AQTINSTALL_VERSION} into ${AQT_VENV}"
mkdir -p "$(dirname "${AQT_VENV}")"
python3 -m venv "${AQT_VENV}"
# shellcheck disable=SC1091
source "${AQT_VENV}/bin/activate"
python3 -m pip install --quiet "aqtinstall==${AQTINSTALL_VERSION}"

echo "==> Installing Qt ${QT_VERSION} (gcc_64 + qtimageformats + qtpdf) into ${QT_ROOT}"
mkdir -p "${QT_ROOT}"
python3 -m aqt install-qt linux desktop "${QT_VERSION}" gcc_64 \
  --modules qtimageformats qtpdf \
  --outputdir "${QT_ROOT}"
deactivate

if [ ! -d "${QT6_DIR}" ]; then
  echo "ERROR: Qt6 cmake package not found at ${QT6_DIR}" >&2
  exit 1
fi
echo "==> Using Qt6_DIR=${QT6_DIR}"
