#!/bin/sh
# k1520DiskTool — Dateiaustausch mit K1520-Disketten.  @ROOT@ setzt der Installer.
#
# Startet die Oberflaeche aus der Laufzeitumgebung der Installation.  Bibliothek
# und Formatkatalog findet das Werkzeug selbst (app/paths.py bzw. FsCatalog ueber
# den Modulpfad); hier wird nur die Wurzel bekanntgegeben.
#
# Die KOMMANDOZEILE liegt daneben als `bin/k1520disktool-cli` — zwei Namen sind
# ehrlicher als ein Programm, das je nach Argumenten etwas anderes tut.
set -eu

ROOT="@ROOT@"

[ -x "$ROOT/venv/bin/python3" ] || {
    echo "k1520DiskTool: Laufzeitumgebung fehlt in $ROOT/venv" >&2
    echo "Neu einrichten:  ./install.sh --prefix '$ROOT'" >&2
    exit 1
}

K1520_HOME="$ROOT"; export K1520_HOME

# Arbeitsverzeichnis = Benutzerdaten, damit der Dateidialog dort aufgeht, wo die
# Disketten liegen.  Dieselbe Auflösung wie beim Emulator-Starter.
DATEN=$("$ROOT/venv/bin/python3" -c \
    'import sys; sys.path.insert(0, sys.argv[1]); from app import paths; print(paths.user_data_dir())' \
    "$ROOT" 2>/dev/null) || DATEN=
if [ -n "$DATEN" ] && mkdir -p "$DATEN" 2>/dev/null; then
    cd "$DATEN" || true
fi

exec "$ROOT/venv/bin/python3" "$ROOT/app/disktool/main.py" "$@"
