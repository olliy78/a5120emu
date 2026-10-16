#!/bin/bash
# mc80emu — Starter des MC80 Emulators (Gegenstueck zu run_a5120emu.sh)
# Dasselbe Programm wie der A5120 Emulator, mit dem Programmprofil des MC80 (Modellwahl MC80.30 / .31)
# (app/profil.py): eigene Konfiguration mc80emu.yaml, Bildschirmtastatur MC80.3x.
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

# Den Interpreter des venv DIREKT aufrufen, nicht ueber `source .../activate`.
# `activate` traegt den Pfad des venv ABSOLUT ein (er stammt aus dem Erzeugen);
# ein kopiertes oder mitverschobenes venv stellt damit das Verzeichnis eines
# FREMDEN venv in den PATH, und `python3` ist dann ein anderer Interpreter mit
# anderen Paketen — Fehlerbild: eine installierte Abhaengigkeit gilt als fehlend.
# Der direkt aufgerufene Interpreter findet sein venv ueber den eigenen Pfad.
PY="$VENV_DIR/bin/python3"

# Set library path
export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

# Start GUI
echo "MC80 Emulator"
echo "============="
echo ""
echo "Python: $PY"
echo "PySide6: $("$PY" -c 'import PySide6; print(PySide6.__version__)')"
echo "Library: $BUILD_DIR/libk1520core.so"
echo ""
echo "Starting GUI..."
echo ""

exec "$PY" "$PROJECT_DIR/app/main.py" --machine mc80 "$@"
