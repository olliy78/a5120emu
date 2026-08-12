#!/bin/sh
# ─────────────────────────────────────────────────────────────────────────────
# K1520-Emulator — Installation (Linux/macOS, ohne Administratorrechte)
# ─────────────────────────────────────────────────────────────────────────────
#
# Installiert die mitgelieferte Payload (Kernbibliothek + GUI + Daten) in ein
# benutzereigenes Verzeichnis und richtet dort eine eigene Laufzeitumgebung ein:
# uv holt einen Python und legt ein venv mit PySide6 an.  Nichts davon berührt
# das System — kein sudo, kein Systempython, keine Systempakete.
#
# Aufruf (aus dem entpackten Paket heraus):
#
#     ./install.sh                    # fragt nach dem Zielverzeichnis
#     ./install.sh --prefix DIR       # ohne Rückfrage dorthin
#     ./install.sh -y                 # ohne Rückfrage in den Vorschlag
#     ./install.sh --uninstall        # entfernen (Benutzerdaten bleiben)
#     ./install.sh --uninstall --purge   # samt Disketten und Konfiguration
#
# Entwurf: doc/design/13_distribution.md
set -eu

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SELF_DIR/lib/common.sh"

# ─── Vorgaben ────────────────────────────────────────────────────────────────

# Vorgeschlagenes Ziel: **sichtbar im Heimatverzeichnis**.  Ein Pfad unter
# ~/.local ist zwar der übliche Ort für benutzereigene Software, aber dort
# findet ihn niemand wieder — gefragt wird ohnehin (siehe unten), das hier ist
# nur der Vorschlag.  Die Benutzerdaten liegen davon getrennt im
# Dokumentenordner (~/Dokumente/K1520emu): Arbeitsdisketten werden
# zurückgeschrieben und sollen ein Update überleben (app/paths.py).
PREFIX="$HOME/K1520emu"
BINDIR="$HOME/.local/bin"
APPDIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONDIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
DATADIR=$(benutzerdaten_dir)
CONFDIR="${XDG_CONFIG_HOME:-$HOME/.config}/k1520emu"

# Maschinen dieser Familie, für die Starter und Startmenü-Einträge entstehen.
# Das Projekt heißt nach dem Bus, nicht nach einem Rechner: der A5120 ist die
# erste Maschine, weitere K1520-Rechner bekommen ein eigenes Programm in
# derselben Installation.  Eine neue gehört hier hinein UND braucht eine
# <name>.desktop.in; das Deinstallieren räumt danach von selbst mit auf.
MASCHINEN="a5120emu"

# Werkzeuge der Installation — keine Maschinen, aber ebenfalls mit Starter und
# Startmenue-Eintrag: das k1520DiskTool tauscht Dateien mit Disketten aus
# (doc/design/13_k1520disktool.md).  Wie bei den Maschinen gilt: ein neuer Name
# gehoert hier hinein UND braucht eine <name>.desktop.in.
WERKZEUGE="k1520disktool"

# Alles, was einen Starter/Eintrag bekommt — nur fuer das Aufraeumen gedacht.
STARTER="$MASCHINEN $WERKZEUGE"

# Was der Installer im Zielverzeichnis anlegt — und ausschließlich das entfernt
# das Deinstallieren wieder.  Die Liste wandert beim Installieren in den Ausweis
# (siehe unten), sodass eine ältere Installation nach ihrer EIGENEN Liste
# abgeräumt wird und nicht nach der einer neueren Fassung.
INVENTAR="bin app share venv python tools .cache-uv VERSION requirements.lock LICENSE"

MODE=install
SHORTCUTS=yes
KEEP_CACHE=no
PURGE=no
SLIM=yes
PREFIX_SET=no
ASSUME_YES=no
PY_VERSION=$K1520_PY_VERSION

usage() {
    cat <<EOF
K1520-Emulator — Installation

  --prefix DIR     Installationsverzeichnis (ohne Angabe wird gefragt,
                   Vorschlag: $PREFIX)
  -y, --yes        nicht fragen, Vorschlag verwenden
  --python X.Y     Python-Fassung der Laufzeitumgebung (Vorgabe: $PY_VERSION)
  --no-shortcut    keinen Startmenü-Eintrag und keinen Starter in ~/.local/bin
  --no-slim        Python und Qt vollständig lassen (~400 statt ~146 MB)
  --keep-cache     uv und seinen Download-Cache behalten (schnellere
                   Neuinstallation, belegt einige hundert MB mehr)
  --uninstall      Installation entfernen
  --purge          zusätzlich Disketten und Konfiguration löschen
  -h, --help       diese Hilfe

Erstinstallation lädt einmalig ~120 MB (uv, Python, Qt) und braucht daher
eine Internetverbindung; danach startet der Emulator ohne Netz.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)     PREFIX=$2; PREFIX_SET=yes; shift 2 ;;
        --prefix=*)   PREFIX=${1#*=}; PREFIX_SET=yes; shift ;;
        -y|--yes)     ASSUME_YES=yes; shift ;;
        --python)     PY_VERSION=$2; shift 2 ;;
        --python=*)   PY_VERSION=${1#*=}; shift ;;
        --no-shortcut) SHORTCUTS=no; shift ;;
        --no-slim)    SLIM=no; shift ;;
        --keep-cache) KEEP_CACHE=yes; shift ;;
        --uninstall)  MODE=uninstall; shift ;;
        --purge)      PURGE=yes; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) die "unbekannte Option: $1  (--help zeigt die Liste)" ;;
    esac
done

# vorhandene_installation — Wurzel einer bereits eingerichteten Installation.
#
# Der Starter in ~/.local/bin zeigt darauf; es genügt irgendeiner der
# Maschinen-Starter, sie zeigen alle in dieselbe Installation.  Zwei Aufgaben
# hängen daran: das Deinstallieren ohne --prefix und — wichtiger — der
# Vorschlag beim Aktualisieren.  Ohne ihn schlüge der Installer beim Update den
# Standardort vor; wer damals woanders installiert hat und Enter drückt, bekäme
# eine ZWEITE Installation, während die alte verwaist liegen bliebe.
vorhandene_installation() {
    for _m in $MASCHINEN; do
        [ -L "$BINDIR/$_m" ] || continue
        _ziel=$(readlink -f "$BINDIR/$_m" 2>/dev/null || true)
        case "$_ziel" in
            */bin/"$_m") _wurzel=${_ziel%/bin/"$_m"} ;;
            *) continue ;;
        esac
        [ -d "$_wurzel" ] || continue      # toter Verweis einer alten Installation
        echo "$_wurzel"
        return 0
    done
    return 1
}

GEFUNDEN=no
if [ "$PREFIX_SET" = no ] && _vorhanden=$(vorhandene_installation); then
    PREFIX=$_vorhanden
    GEFUNDEN=yes
fi

# ─── Ziel erfragen ───────────────────────────────────────────────────────────
#
# Ohne --prefix wird gefragt, sofern ein Terminal da ist.  Läuft der Installer
# unbeaufsichtigt (Skript, CI, Pipe), gilt kommentarlos der Vorschlag.

if [ "$MODE" = install ] && [ "$PREFIX_SET" = no ] && [ "$ASSUME_YES" = no ] \
   && [ -r /dev/tty ] && [ -t 1 ]; then
    if [ "$GEFUNDEN" = yes ]; then
        printf "\nEs gibt bereits eine Installation — Enter aktualisiert sie.\n"
    else
        printf "\nWohin soll der K1520-Emulator installiert werden?\n"
        printf "  (Enter übernimmt den Vorschlag; das Verzeichnis wird angelegt)\n"
    fi
    printf "Ziel [%s]: " "$PREFIX"
    if IFS= read -r _antwort < /dev/tty; then
        [ -n "$_antwort" ] && PREFIX=$_antwort
    fi
    printf "\n"
fi

# Der Starter braucht einen festen Pfad — „~/…" und Relativangaben auflösen.
PREFIX=$(abs_path "$PREFIX")

# ─── Ziel prüfen ─────────────────────────────────────────────────────────────
#
# Das Deinstallieren löscht sein Ziel mit `rm -rf`.  Solange das Ziel fest
# verdrahtet war, war das harmlos; seit danach GEFRAGT wird, ist ein Tippfehler
# („~", „~/Dokumente") eine Katastrophe.  Deshalb zwei Riegel: hier darf nur ein
# leeres oder bereits von uns belegtes Verzeichnis Ziel werden, und beim Löschen
# muss sich das Verzeichnis ausweisen (ist_installation).

case "$PREFIX" in
    /|"$HOME"|"$HOME"/) die "„$PREFIX\" darf kein Installationsverzeichnis sein — bitte einen eigenen Ordner angeben (z. B. $HOME/K1520emu)" ;;
esac

if [ "$MODE" = install ] && ! ist_leer "$PREFIX" && ! ist_installation "$PREFIX"; then
    die "$PREFIX ist nicht leer und enthält keine Installation des Emulators.
     Bitte einen eigenen Ordner angeben — beim Deinstallieren würde dieser
     hier samt Inhalt gelöscht."
fi

# ─── Deinstallieren ──────────────────────────────────────────────────────────

# entferne_installation <wurzel> — nur das wegräumen, was der Installer anlegte.
#
# Ein `rm -rf` auf die Wurzel wäre einfacher, nähme dem Anwender aber alles mit,
# was er DANEBEN abgelegt hat — eigene Disketten, Notizen, ein Skript.  Gelöscht
# wird deshalb nach dem Inventar aus dem Ausweis (eine ältere Installation nach
# ihrer eigenen Liste, nicht nach der dieser Fassung); bleibt danach etwas
# übrig, bleibt auch das Verzeichnis stehen und der Anwender erfährt es.
entferne_installation() {
    _wurzel=$1
    _liste=$(sed -n 's/^eintrag //p' "$_wurzel/$INSTALL_MARKER" 2>/dev/null || true)
    [ -n "$_liste" ] || _liste=$INVENTAR

    for _e in $_liste; do
        # Ein Eintrag ist ein NAME, kein Pfad — sonst zeigte ein verfälschter
        # Ausweis („../..") aus der Installation heraus.
        case "$_e" in
            */*|..|.|"") warn "übergangen: fragwürdiger Eintrag im Ausweis ($_e)"; continue ;;
        esac
        rm -rf "$_wurzel/$_e"
    done
    # Protokolle des Emulators — sie entstehen im ARBEITSVERZEICHNIS (seit
    # e6db12c in dessen Unterverzeichnis `logs/`, siehe k1520_api.cpp), also
    # hier, sobald jemand aus der Installation heraus startet.  Die ältere
    # Schreibweise direkt im Verzeichnis bleibt berücksichtigt, damit auch eine
    # damit angelegte Installation sauber verschwindet.
    rm -rf "$_wurzel/logs"
    rm -f "$_wurzel"/k1520_*.log
    rm -f "$_wurzel/$INSTALL_MARKER"

    if rmdir "$_wurzel" 2>/dev/null; then
        ok "Verzeichnis gelöscht"
    else
        ok "Programmdateien entfernt"
        printf "     %s bleibt stehen — darin liegt, was nicht vom Installer stammt:\n" "$_wurzel"
        ls -A "$_wurzel" 2>/dev/null | sed 's/^/       /'
    fi
}

if [ "$MODE" = uninstall ]; then
    info "Installation entfernen: $PREFIX"
    if [ ! -d "$PREFIX" ]; then
        warn "nichts zu löschen — $PREFIX gibt es nicht"
    elif ist_installation "$PREFIX"; then
        entferne_installation "$PREFIX"
    else
        warn "$PREFIX sieht nicht nach einer Installation aus — es bleibt unangetastet"
    fi
    for _m in $STARTER; do
        rm -f "$BINDIR/$_m" "$APPDIR/$_m.desktop" "$ICONDIR/$_m.svg"
    done
    ok "Starter und Startmenü-Einträge entfernt"
    if [ "$PURGE" = yes ]; then
        rm -rf "$DATADIR" "$CONFDIR"
        ok "Disketten und Konfiguration gelöscht"
    else
        printf "     Behalten: %s (Disketten), %s (Konfiguration)\n" "$DATADIR" "$CONFDIR"
        printf "     Mit --purge löschen.\n"
    fi
    exit 0
fi

# ─── Vorprüfungen ────────────────────────────────────────────────────────────

[ -d "$SELF_DIR/payload" ] || die "payload/ fehlt neben $0 — ist das Paket vollständig entpackt?"
[ -f "$SELF_DIR/requirements.lock" ] || die "requirements.lock fehlt im Paket"

VERSION=$(cat "$SELF_DIR/VERSION" 2>/dev/null || echo unbekannt)
info "K1520-Emulator $VERSION"
printf "     Ziel:   %s\n" "$PREFIX"
printf "     Python: %s (eigene Laufzeitumgebung, kein Systempython)\n" "$PY_VERSION"

mkdir -p "$PREFIX" 2>/dev/null || die "Zielverzeichnis nicht anlegbar: $PREFIX"
[ -w "$PREFIX" ] || die "Zielverzeichnis nicht beschreibbar: $PREFIX"

# Grob 500 MB werden gebraucht (Python + Qt + Payload).
if have df; then
    _free=$(df -Pk "$PREFIX" | awk 'NR==2 {print int($4/1024)}')
    if [ "${_free:-999999}" -lt 500 ]; then
        warn "nur ${_free} MB frei in $PREFIX — gebraucht werden ~500 MB"
    fi
fi

# ─── 1. Payload kopieren ─────────────────────────────────────────────────────

info "Programmdateien kopieren"
# Eine ältere Installation wird ersetzt; python/ (der geladene Interpreter)
# bleibt stehen, die Laufzeitumgebung wird weiter unten neu aufgebaut.
for d in bin app share; do
    rm -rf "$PREFIX/$d"
    cp -R "$SELF_DIR/payload/$d" "$PREFIX/$d"
done
cp "$SELF_DIR/VERSION" "$SELF_DIR/requirements.lock" "$PREFIX/"
if [ -f "$SELF_DIR/LICENSE" ]; then cp "$SELF_DIR/LICENSE" "$PREFIX/"; fi

# Ausweis für das Deinstallieren.  Er beantwortet zwei Fragen: ob dieses
# Verzeichnis überhaupt uns gehört (sonst wird nichts gelöscht) und WAS darin
# uns gehört — nur diese Einträge werden entfernt, alles andere bleibt liegen.
# Die Liste reist mit der Installation, damit eine ältere Fassung nach ihrem
# eigenen Inventar abgeräumt wird und nicht nach dem einer neueren.
{
    printf 'k1520emu %s\n' "$VERSION"
    printf '# Vom Installer angelegt — nur das entfernt --uninstall wieder.\n'
    for d in $INVENTAR; do printf 'eintrag %s\n' "$d"; done
} > "$PREFIX/$INSTALL_MARKER"
ok "Kern, GUI und Daten in $PREFIX"

# ─── 2. Laufzeitumgebung ─────────────────────────────────────────────────────

UV=$(ensure_uv "$PREFIX/tools" "$SELF_DIR/uv_pins.txt")

# Alles, was uv anlegt, landet INNERHALB der Installation: der Python unter
# python/, der Cache unter .cache-uv/.  Deinstallieren = Verzeichnis löschen.
UV_PYTHON_INSTALL_DIR="$PREFIX/python"; export UV_PYTHON_INSTALL_DIR
UV_CACHE_DIR="$PREFIX/.cache-uv";       export UV_CACHE_DIR
UV_NO_MODIFY_PATH=1;                    export UV_NO_MODIFY_PATH

# --no-bin: uv legt sonst ~/.local/bin/python3.x an — ein Symlink im PATH des
# Anwenders, der unser Deinstallieren überlebte und dann ins Leere zeigte.
# Der Emulator ruft seinen Python ohnehin über den vollen Pfad auf.
info "Python $PY_VERSION bereitstellen"
"$UV" python install --no-bin "$PY_VERSION" >/dev/null 2>&1 \
    || "$UV" python install --no-bin "$PY_VERSION" \
    || die "Python $PY_VERSION konnte nicht installiert werden (kein Netz? Proxy?)"
ok "Python bereit"

# Eine vorhandene Laufzeitumgebung wird ERNEUERT, nicht weiterbenutzt: `uv venv`
# verweigert sonst schlicht den Dienst, und weiterbenutzen wäre auch nicht
# richtig — das Schlankmachen einer älteren Fassung kann etwas entfernt haben,
# das die neue braucht, und nachträglich wiederherstellen kann uv das nicht.
# Der heruntergeladene Python unter python/ bleibt dabei liegen.
info "Laufzeitumgebung anlegen"
VENV_CLEAR=
if [ -d "$PREFIX/venv" ]; then
    VENV_CLEAR=--clear
    printf "     (bestehende Laufzeitumgebung wird erneuert)\n"
fi
# shellcheck disable=SC2086  # VENV_CLEAR ist leer oder genau ein Wort
"$UV" venv $VENV_CLEAR --python "$PY_VERSION" --python-preference only-managed \
    "$PREFIX/venv" >/dev/null \
    || die "venv konnte nicht angelegt werden"

info "Qt und Abhängigkeiten installieren (~70 MB, dauert einen Moment)"
"$UV" pip install --python "$PREFIX/venv" --require-hashes -r "$PREFIX/requirements.lock" \
    || die "Abhängigkeiten konnten nicht installiert werden (kein Netz? Proxy?)"
ok "Laufzeitumgebung fertig"

if [ "$KEEP_CACHE" = no ]; then
    rm -rf "$PREFIX/.cache-uv"
fi

# ─── 3. Schlankmachen ────────────────────────────────────────────────────────
#
# Python und Qt bringen ~400 MB mit, keine 15 davon gehören dem Emulator.  Der
# Rest ist QML/Quick, Qt-Entwicklungswerkzeug, ungenutzte Bindungen, CPythons
# Testsuite und Tcl/Tk.  slim.py schneidet das heraus — welche Qt-Bibliotheken
# bleiben, bestimmt dabei `ldd` und keine Vermutung.  Der Rauchtest gleich
# danach ist die Gegenprobe.

if [ "$SLIM" = yes ]; then
    info "Überflüssiges entfernen"
    if [ "$KEEP_CACHE" = yes ]; then
        "$PREFIX/venv/bin/python3" "$SELF_DIR/slim.py" "$PREFIX" --keep-tools \
            || warn "Schlankmachen fehlgeschlagen — die Installation bleibt vollständig"
    else
        "$PREFIX/venv/bin/python3" "$SELF_DIR/slim.py" "$PREFIX" \
            || warn "Schlankmachen fehlgeschlagen — die Installation bleibt vollständig"
    fi
fi

# ─── 4. Starter ──────────────────────────────────────────────────────────────

# Angelegt wird je Maschine EINZELN und nicht über $MASCHINEN in einer Schleife:
# jede braucht ihren eigenen Einstiegspunkt, eine Schleife über dieselbe Vorlage
# schriebe lauter gleiche Starter unter verschiedenen Namen.  Kommt der nächste
# K1520-Rechner, entsteht hier ein zweiter Block — und sein Name gehört zusätzlich
# in $MASCHINEN, damit das Deinstallieren ihn findet.
info "Starter schreiben"
mkdir -p "$PREFIX/bin"
ersetze_root "$SELF_DIR/launcher.sh" "$PREFIX" > "$PREFIX/bin/a5120emu"
chmod +x "$PREFIX/bin/a5120emu"

# Das Diskettenwerkzeug ist ein eigenes Programm mit eigenem Starter.  Die
# Kommandozeile liegt bereits als bin/k1520disktool-cli in der Payload; hier
# entsteht der Starter der Oberflaeche.
ersetze_root "$SELF_DIR/disktool_launcher.sh" "$PREFIX" > "$PREFIX/bin/k1520disktool"
chmod +x "$PREFIX/bin/k1520disktool"
[ -f "$PREFIX/bin/k1520disktool-cli" ] && chmod +x "$PREFIX/bin/k1520disktool-cli"

if [ "$SHORTCUTS" = yes ]; then
    mkdir -p "$BINDIR" "$APPDIR" "$ICONDIR"
    ln -sf "$PREFIX/bin/a5120emu" "$BINDIR/a5120emu"
    cp "$PREFIX/share/icons/a5120emu.svg" "$ICONDIR/a5120emu.svg" 2>/dev/null || true
    ersetze_root "$SELF_DIR/a5120emu.desktop.in" "$PREFIX" > "$APPDIR/a5120emu.desktop"

    ln -sf "$PREFIX/bin/k1520disktool" "$BINDIR/k1520disktool"
    ln -sf "$PREFIX/bin/k1520disktool-cli" "$BINDIR/k1520disktool-cli" 2>/dev/null || true
    ersetze_root "$SELF_DIR/k1520disktool.desktop.in" "$PREFIX" \
        > "$APPDIR/k1520disktool.desktop"

    if have update-desktop-database; then
        update-desktop-database "$APPDIR" >/dev/null 2>&1 || true
    fi
    ok "Startmenü-Einträge und $BINDIR/{a5120emu,k1520disktool}"
    case ":$PATH:" in
        *":$BINDIR:"*) ;;
        *) warn "$BINDIR liegt nicht im PATH — der Emulator startet trotzdem über das Startmenü" ;;
    esac
fi

# ─── 5. Rauchtest ────────────────────────────────────────────────────────────
#
# Ein kaputter Startmenü-Eintrag ist schlimmer als eine abgebrochene
# Installation: hier wird geladen, was der Emulator beim Start lädt — Kern,
# Formatkatalog, Qt samt Plattform-Plugin und das Hauptfenster.  Letzteres ist
# zugleich die Gegenprobe zum Schlankmachen: fehlte ein Qt-Plugin, käme man
# hier nicht durch.

# Das `cd` ist nicht kosmetisch: bei einem Skript aus der Standardeingabe steht
# das ARBEITSVERZEICHNIS vorn in sys.path.  Ohne den Wechsel prüfte ein aus dem
# Quellbaum heraus gestarteter Installer dessen app/ statt der Installation —
# und meldete „läuft", obwohl er nie angefasst hatte, was er ausliefert.
info "Rauchtest"
cd "$PREFIX"
QT_QPA_PLATFORM=offscreen PYTHONPATH="$PREFIX" "$PREFIX/venv/bin/python3" - "$PREFIX" <<'PYEOF' || die "Rauchtest fehlgeschlagen — die Installation ist unbrauchbar"
import ctypes, sys
from app import paths

lib = ctypes.CDLL(str(paths.core_library()))
lib.k1520_version.restype = ctypes.c_char_p
print("     Kern:      ", lib.k1520_version().decode())

import PySide6
print("     PySide6:   ", PySide6.__version__)

fmt = paths.formats_file()
if fmt is None:
    sys.exit("formats.yaml nicht gefunden:\n" + paths.describe())
print("     Katalog:   ", fmt)
print("     Disketten: ", paths.seed_user_disks(), "kopiert nach", paths.user_disks_dir())

# Das Fenster wirklich bauen (unsichtbar) und sofort wieder abräumen.
from PySide6.QtWidgets import QApplication
from app.ui.main_window import MainWindow
qt = QApplication([])
fenster = MainWindow()
fenster.close()
print("     Oberfläche: baut auf")
PYEOF
# Der Kern legt beim Erzeugen einer Maschine ein Protokoll unter `logs/` im
# ARBEITSVERZEICHNIS an (k1520_api.cpp) — das ist hier die frische Installation.
# Eine frische Installation soll aber nur enthalten, was hineingehört.
rm -rf "$PREFIX/logs"
rm -f "$PREFIX"/k1520_*.log
ok "läuft"

printf "\n"
info "Fertig."
printf "     Installiert:  %s (%s)\n" "$PREFIX" \
    "$(du -sh "$PREFIX" 2>/dev/null | awk '{print $1}')"
printf "     Starten:      %s\n" "$PREFIX/bin/a5120emu"
printf "     Diskettenwerkzeug: %s  (Kommandozeile: %s)\n" \
    "$PREFIX/bin/k1520disktool" "$PREFIX/bin/k1520disktool-cli"
if [ "$SHORTCUTS" = yes ]; then
    printf "     oder einfach: a5120emu   (bzw. über das Startmenü)\n"
fi
printf "     Entfernen:    %s --uninstall\n" "$0"
