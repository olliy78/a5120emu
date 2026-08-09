#!/bin/sh
# K1520-Emulator — Starter des A5120.  @ROOT@ wird beim Installieren eingesetzt.
#
# Startet die GUI aus der Laufzeitumgebung der Installation.  Bibliothek,
# Formatkatalog und Disketten findet der Emulator selbst (app/paths.py bzw.
# FormatCatalog über den Modulpfad); hier wird nur die Wurzel bekanntgegeben,
# damit auch ein verschobenes oder über einen Symlink aufgerufenes Verzeichnis
# eindeutig ist.
set -eu

ROOT="@ROOT@"

[ -x "$ROOT/venv/bin/python3" ] || {
    echo "K1520-Emulator: Laufzeitumgebung fehlt in $ROOT/venv" >&2
    echo "Neu einrichten:  ./install.sh --prefix '$ROOT'" >&2
    exit 1
}

K1520_HOME="$ROOT"; export K1520_HOME

exec "$ROOT/venv/bin/python3" "$ROOT/app/main.py" "$@"
