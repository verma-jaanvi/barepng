#!/usr/bin/env bash
# Verify reproducible build across two fresh clones
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
BUILD_A="$(mktemp -d)"
BUILD_B="$(mktemp -d)"

cleanup() {
    rm -rf "$BUILD_A" "$BUILD_B"
}
trap cleanup EXIT

echo "=== Reproducible build verification ==="
echo "Source: $REPO_ROOT"
echo "Build A: $BUILD_A"
echo "Build B: $BUILD_B"
echo ""

git clone "$REPO_ROOT" "$BUILD_A/repo" >/dev/null 2>&1
git clone "$REPO_ROOT" "$BUILD_B/repo" >/dev/null 2>&1

echo "Building A..."
( cd "$BUILD_A/repo" && make clean && make all 2>&1 ) | tail -5

echo "Building B..."
( cd "$BUILD_B/repo" && make clean && make all 2>&1 ) | tail -5

BIN_A=$(find "$BUILD_A/repo/build" -name "pngdecoder*" -not -name "*.o" | head -1)
BIN_B=$(find "$BUILD_B/repo/build" -name "pngdecoder*" -not -name "*.o" | head -1)

if [[ -z "$BIN_A" || -z "$BIN_B" ]]; then
    echo "ERROR: binary not found after build"
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    HASH_A=$(sha256sum "$BIN_A" | cut -d' ' -f1)
    HASH_B=$(sha256sum "$BIN_B" | cut -d' ' -f1)
else
    HASH_A=$(shasum -a 256 "$BIN_A" | cut -d' ' -f1)
    HASH_B=$(shasum -a 256 "$BIN_B" | cut -d' ' -f1)
fi

echo ""
echo "build_a: $HASH_A"
echo "build_b: $HASH_B"
echo ""

if [ "$HASH_A" = "$HASH_B" ]; then
    echo "REPRODUCIBLE: byte-identical binary (same hash from two independent builds)"
    exit 0
else
    echo "NOT REPRODUCIBLE: hashes differ"
    exit 1
fi
