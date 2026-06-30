#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PRESET="${PRESET:-macos-arm64-clang-rwdi}"
TARGET="${TARGET:-PoseidonGame}"
JOBS="${JOBS:-8}"
CONTENT_DIR="${CONTENT_DIR:-packages/Combined}"
RENDER="${RENDER:-mtl}"
CONFIGURE="${CONFIGURE:-1}"

usage() {
    cat <<EOF
Usage: $(basename "$0") [--] [game args...]

Builds the default macOS debug game target and launches it with useful
development flags.

Environment overrides:
  PRESET       CMake preset to configure/build (default: $PRESET)
  TARGET       CMake target to build (default: $TARGET)
  JOBS         Parallel build jobs (default: $JOBS)
  CONTENT_DIR  Game content directory passed with -C (default: $CONTENT_DIR)
  RENDER       Renderer passed with --render (default: $RENDER)
  CONFIGURE    Run cmake --preset first, 1 or 0 (default: $CONFIGURE)

Examples:
  ./build-and-run.sh
  ./build-and-run.sh --test-mission "\$HOME/.local/share/Cold War Assault/missions/benchmark.abel"
  RENDER=gl33 ./build-and-run.sh --no-sound
  CONFIGURE=0 JOBS=12 ./build-and-run.sh
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

BUILD_DIR="$ROOT_DIR/build/$PRESET"
GAME_BIN="$BUILD_DIR/apps/cwr/Game/PoseidonGame"

if [[ "$CONFIGURE" != "0" ]]; then
    cmake --preset "$PRESET"
fi

cmake --build "$BUILD_DIR" --target "$TARGET" -j "$JOBS"

exec "$GAME_BIN" \
    --no-splash \
    -C "$CONTENT_DIR" \
    --render "$RENDER" \
    --dev \
    --show-fps \
    "$@"
