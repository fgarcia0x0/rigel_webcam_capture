#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE="Debug"
CLEAN=0
RUN_APP=1

usage() {
    cat <<EOF
Usage: ./run.sh [options]

Builds rigel_webcam_capture and runs rwc_app.

Options:
  -d, --debug        Build in Debug mode (default)
  -r, --release      Build in Release mode
  -c, --clean        Remove the build directory for the selected config before building
      --build-only   Only build, do not run the app afterwards
  -h, --help         Show this help

Environment:
  VCPKG_ROOT         Path to your vcpkg installation (auto-detected if unset)
EOF
}

for arg in "$@"; do
    case "$arg" in
        -d|--debug) BUILD_TYPE="Debug" ;;
        -r|--release) BUILD_TYPE="Release" ;;
        -c|--clean) CLEAN=1 ;;
        --build-only) RUN_APP=0 ;;
        -h|--help) usage; exit 0 ;;
        *)
            echo "error: unknown argument: $arg" >&2
            usage
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_TYPE_LOWER="$(printf '%s' "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="build-${BUILD_TYPE_LOWER}"

if [ -z "${VCPKG_ROOT:-}" ]; then
    for candidate in "$HOME/vcpkg" "$HOME/tools/vcpkg"; do
        if [ -f "$candidate/scripts/buildsystems/vcpkg.cmake" ]; then
            VCPKG_ROOT="$candidate"
            break
        fi
    done
fi

if [ -z "${VCPKG_ROOT:-}" ] || [ ! -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]; then
    echo "error: could not find vcpkg. Set VCPKG_ROOT to your vcpkg installation path." >&2
    exit 1
fi

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi

if [ "$CLEAN" -eq 1 ]; then
    echo "Removing ${BUILD_DIR}"
    rm -rf "$BUILD_DIR"
fi

echo "Configuring (${BUILD_TYPE}) in ${BUILD_DIR} ..."
cmake -S . -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"

echo "Building ..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

if [ "$RUN_APP" -eq 1 ]; then
    echo "Running rwc_app ..."
    exec "$SCRIPT_DIR/bin/${BUILD_TYPE_LOWER}/rwc_app"
fi
