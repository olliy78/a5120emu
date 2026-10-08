#!/bin/bash
# p8000term — Starter des P8000 Terminals (Arbeitsplatz ohne Rechner, Gegenstück zu run_p8000emu.sh)
# Dasselbe Programm wie p8000emu, mit dem Profil "P8000 Terminal" (app/profil.py, P8000TERM):
# Vorgabe ist nur das Terminal (Typ 2 + Flachtastatur K7673.09) ohne Rechner, eigene Konfiguration
# p8000term.yaml.  Mehrere Arbeitsplätze: run_p8000term.sh --instance platz2
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
echo "P8000 Terminal"
echo "=============="
echo ""
echo "Python: $PY"
echo "PySide6: $("$PY" -c 'import PySide6; print(PySide6.__version__)')"
echo "Library: $BUILD_DIR/libk1520core.so"
echo ""
echo "Starting GUI..."
echo ""

exec "$PY" "$PROJECT_DIR/app/main.py" --machine p8000term "$@"
