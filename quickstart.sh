#!/bin/bash
# K1520 Emulator - Quick Start Script
# Builds and runs the K1520 A5120 emulator

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

echo "K1520 A5120 Emulator - Quick Start"
echo "===================================="
echo ""

# Build
echo "1. Building C++ core..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE=Release -DLOG_LEVEL=2 >/dev/null 2>&1
make -j4 >/dev/null 2>&1
cd "$PROJECT_DIR"
echo "   ✓ libk1520core.so built"

# Set library path
export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

# Run tests
echo ""
echo "2. Running integration tests..."
python3 test_integration.py 2>&1 | grep -E "TEST|Tests"

# Run boot test
echo ""
echo "3. Running boot diagnostic..."
python3 test_boot_detailed.py 2>&1 | tail -20

echo ""
echo "===================================="
echo "✓ To start the GUI, run:"
echo "  python3 app/main.py"
echo ""
