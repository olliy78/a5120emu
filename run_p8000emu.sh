#!/bin/bash
# p8000emu — Starter des P8000 Emulators (Gegenstück zu run_a5120emu.sh)
# Dasselbe Programm wie der A5120 Emulator, mit dem Programmprofil des P8000
# (app/profil.py): eigene Konfiguration p8000emu.yaml, Terminal statt Bildröhre,
# Winchesterplatte, Funktionstastenleiste.
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

# Den Interpreter des venv DIREKT aufrufen (Begründung: run_k8915emu.sh).
PY="$VENV_DIR/bin/python3"

# Set library path
export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

# Start GUI
echo "P8000 Emulator"
echo "=============="
echo ""
echo "Python: $PY"
echo "PySide6: $("$PY" -c 'import PySide6; print(PySide6.__version__)')"
echo "Library: $BUILD_DIR/libk1520core.so"
echo ""
echo "Starting GUI..."
echo ""

exec "$PY" "$PROJECT_DIR/app/main.py" --machine p8000 "$@"
