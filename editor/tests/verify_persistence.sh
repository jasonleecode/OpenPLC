#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${TIZI_TEST_BUILD_DIR:-${TMPDIR:-/tmp}/tizi-persistence-build}"
QT_PREFIX="${QT_ROOT:-}"
if [[ -z "$QT_PREFIX" ]] && command -v qmake >/dev/null 2>&1; then
    QT_PREFIX="$(qmake -query QT_INSTALL_PREFIX)"
fi
cmake_args=(-DTIZI_BUILD_TESTS=ON)
if [[ -n "$QT_PREFIX" ]]; then
    cmake_args+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX")
fi
cmake -S "$ROOT_DIR/editor" -B "$BUILD_DIR" "${cmake_args[@]}"
cmake --build "$BUILD_DIR" --target TiZiPersistenceTests --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R '^editor_persistence$'
