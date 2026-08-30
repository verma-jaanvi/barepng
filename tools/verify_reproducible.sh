#!/usr/bin/env bash
# verify_reproducible.sh - checks that two independent builds of the same
# source tree produce a byte-identical binary.
#
# Usage: bash tools/verify_reproducible.sh
# Requires: git, make, sha256sum (or shasum on macOS), gcc
#
# Scope: reproducibility is claimed within the same toolchain and OS.
# Cross-compiler or cross-platform reproducibility is not tested.
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

# Clone the repo twice (includes only tracked files)
git clone "$REPO_ROOT" "$BUILD_A/repo" >/dev/null 2>&1
git clone "$REPO_ROOT" "$BUILD_B/repo" >/dev/null 2>&1

# Build both, capturing warnings
echo "Building A..."
( cd "$BUILD_A/repo" && make clean && make all 2>&1 ) | tail -5

echo "Building B..."
( cd "$BUILD_B/repo" && make clean && make all 2>&1 ) | tail -5

# Find the built binary (Linux or Windows .exe)
BIN_A=$(find "$BUILD_A/repo/build" -name "pngdecoder*" -not -name "*.o" | head -1)
BIN_B=$(find "$BUILD_B/repo/build" -name "pngdecoder*" -not -name "*.o" | head -1)

if [[ -z "$BIN_A" || -z "$BIN_B" ]]; then
    echo "ERROR: binary not found after build"
    exit 1
fi

# Hash both binaries
if command -v sha256sum >/dev/null 2>&1; then
    HASH_A=$(sha256sum "$BIN_A" | cut -d' ' -f1)
    HASH_B=$(sha256sum "$BIN_B" | cut -d' ' -f1)
else
    # macOS fallback
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
    echo "  Compiler: $(gcc --version | head -1)"
    echo "  OS: $(uname -sm 2>/dev/null || echo 'Windows')"
    exit 1
fi
