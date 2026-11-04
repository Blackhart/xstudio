# Shared paths for host F5 (sourced by other devenv scripts).
# Qt SDK lives outside the source tree (aqt). CMake binaryDir is <repo>/__build/.
#
# Override: XSTUDIO_DEVENV_CACHE=/elsewhere
: "${XSTUDIO_DEVENV_CACHE:=${XDG_CACHE_HOME:-$HOME/.cache}/xstudio-devenv}"
DEVENV_CACHE="$(cd / && mkdir -p "$XSTUDIO_DEVENV_CACHE" && cd "$XSTUDIO_DEVENV_CACHE" && pwd)"
export XSTUDIO_DEVENV_CACHE DEVENV_CACHE
