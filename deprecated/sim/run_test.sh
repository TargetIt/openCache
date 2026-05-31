#!/bin/bash
# openCache test runner
# Usage: ./sim/run_test.sh [debug|release]

set -e

BUILD_TYPE="${1:-release}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/$BUILD_TYPE"

echo "=== openCache Test Runner ==="
echo "Build type: $BUILD_TYPE"
echo "Build dir:  $BUILD_DIR"

# Configure
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ "$BUILD_TYPE" = "debug" ]; then
    cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Debug
else
    cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Release
fi

# Build
cmake --build . --config "${BUILD_TYPE^}"

# Run tests
echo ""
echo "=== Running Tests ==="
./test_openCache

echo ""
echo "=== Done ==="
