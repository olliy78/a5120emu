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

# Arbeitsverzeichnis = Benutzerdaten.  Der Kern legt sein Protokoll unter
# `logs/` im ARBEITSVERZEICHNIS an (k1520_api.cpp); ohne diesen Wechsel
# entstünde es dort, wo der Anwender gerade zufällig steht — beim Start über das
# Startmenü also im Heimatverzeichnis.  Der Emulator nimmt selbst keine
# Dateiargumente entgegen, ein Wechsel bricht daher nichts.
#
# Gefragt wird die Pfadauflösung des Emulators (app/paths.py), statt die Regel
# für den Dokumentenordner hier ein drittes Mal aufzuschreiben: sie kostet einen
# Interpreterstart, bleibt dafür aber richtig, wenn der Anwender seinen
# Dokumentenordner später umbenennt.  Scheitert sie, bleibt es beim
# Arbeitsverzeichnis — das ist eine Frage des Protokollorts, kein Startgrund.
DATEN=$("$ROOT/venv/bin/python3" -c \
    'import sys; sys.path.insert(0, sys.argv[1]); from app import paths; print(paths.user_data_dir())' \
    "$ROOT" 2>/dev/null) || DATEN=
if [ -n "$DATEN" ] && mkdir -p "$DATEN" 2>/dev/null; then
    cd "$DATEN" || true
fi

exec "$ROOT/venv/bin/python3" "$ROOT/app/main.py" "$@"
