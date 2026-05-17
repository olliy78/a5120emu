#!/bin/bash
# Quick activation script for venv and environment
# Source this file to prepare your shell for development

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Activate venv
if [ -d "$SCRIPT_DIR/venv" ]; then
    echo "Activating venv..."
    source "$SCRIPT_DIR/venv/bin/activate"
else
    echo "ERROR: venv not found. Create it with:"
    echo "  python3 -m venv venv"
    echo "  source venv/bin/activate"
    echo "  pip install -r requirements.txt"
    return 1
fi

# Set library path
export LD_LIBRARY_PATH="$SCRIPT_DIR/build:$LD_LIBRARY_PATH"

# Verify build exists
if [ ! -f "$SCRIPT_DIR/build/libk1520core.so" ]; then
    echo "WARNING: libk1520core.so not found. Build with:"
    echo "  cd build && cmake .. && make -j4"
else
    echo "✓ libk1520core.so loaded"
fi

echo "✓ Environment ready"
echo ""
echo "Next steps:"
echo "  python3 app/main.py          # Start GUI"
echo "  python3 test_integration.py  # Run tests"
echo "  deactivate                   # Exit venv"
