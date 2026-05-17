#!/bin/bash
# K1520 A5120 Emulator - GUI Launcher
# Automatically activates venv and starts the GUI

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$PROJECT_DIR/venv"
BUILD_DIR="$PROJECT_DIR/build"

# Check if venv exists
if [ ! -d "$VENV_DIR" ]; then
    echo "ERROR: Virtual environment not found at $VENV_DIR"
    echo ""
    echo "To create it, run:"
    echo "  python3 -m venv venv"
    echo "  source venv/bin/activate"
    echo "  pip install -r requirements.txt"
    exit 1
fi

# Activate venv
source "$VENV_DIR/bin/activate"

# Set library path
export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

# Start GUI
echo "K1520 A5120 Emulator GUI"
echo "========================"
echo ""
echo "Python: $(which python3)"
echo "PySide6: $(python3 -c 'import PySide6; print(PySide6.__version__)')"
echo "Library: $BUILD_DIR/libk1520core.so"
echo ""
echo "Starting GUI..."
echo ""

python3 "$PROJECT_DIR/app/main.py"
