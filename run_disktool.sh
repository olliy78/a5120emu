#!/bin/bash
# k1520DiskTool — Starter (Gegenstück zu run_gui.sh)
# Aktiviert das venv, setzt den Bibliothekspfad und startet die Oberfläche.

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$PROJECT_DIR/venv"
BUILD_DIR="$PROJECT_DIR/build"

if [ ! -d "$VENV_DIR" ]; then
    echo "FEHLER: kein venv unter $VENV_DIR"
    echo ""
    echo "Anlegen mit:"
    echo "  python3 -m venv venv"
    echo "  source venv/bin/activate"
    echo "  pip install -r requirements.txt"
    exit 1
fi

if [ ! -f "$BUILD_DIR/libk1520disk.so" ]; then
    echo "FEHLER: $BUILD_DIR/libk1520disk.so fehlt — vorher bauen:"
    echo "  tools/dev.sh build"
    exit 1
fi

# Den Interpreter des venv DIREKT aufrufen, nicht über `source .../activate`.
# `activate` trägt den Pfad des venv ABSOLUT ein (er stammt aus dem Erzeugen);
# ein kopiertes oder mitverschobenes venv stellt damit das Verzeichnis eines
# FREMDEN venv in den PATH, und `python3` ist dann ein anderer Interpreter mit
# anderen Paketen — Fehlerbild: eine installierte Abhängigkeit gilt als fehlend.
# Der direkt aufgerufene Interpreter findet sein venv über den eigenen Pfad.
export LD_LIBRARY_PATH="$BUILD_DIR:$LD_LIBRARY_PATH"

exec "$VENV_DIR/bin/python3" "$PROJECT_DIR/app/disktool/main.py" "$@"
