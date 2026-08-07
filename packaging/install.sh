#!/bin/sh
# ─────────────────────────────────────────────────────────────────────────────
# A5120-Emulator — Installation (Linux/macOS, ohne Administratorrechte)
# ─────────────────────────────────────────────────────────────────────────────
#
# Installiert die mitgelieferte Payload (Kernbibliothek + GUI + Daten) in ein
# benutzereigenes Verzeichnis und richtet dort eine eigene Laufzeitumgebung ein:
# uv holt einen Python und legt ein venv mit PySide6 an.  Nichts davon berührt
# das System — kein sudo, kein Systempython, keine Systempakete.
#
# Aufruf (aus dem entpackten Paket heraus):
#
#     ./install.sh                    # nach ~/.local/share/a5120emu
#     ./install.sh --prefix DIR       # woandershin
#     ./install.sh --uninstall        # entfernen (Benutzerdaten bleiben)
#     ./install.sh --uninstall --purge   # samt Disketten und Konfiguration
#
# Entwurf: doc/design/13_distribution.md
set -eu

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SELF_DIR/lib/common.sh"

# ─── Vorgaben ────────────────────────────────────────────────────────────────

# Das Programm liegt unter .local/opt, die Benutzerdaten unter .local/share:
# Arbeitsdisketten werden zurückgeschrieben und dürfen ein Update überleben,
# deshalb bleiben die beiden strikt getrennt (app/paths.py).
PREFIX="$HOME/.local/opt/a5120emu"
BINDIR="$HOME/.local/bin"
APPDIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
ICONDIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
DATADIR="${XDG_DATA_HOME:-$HOME/.local/share}/a5120emu"
CONFDIR="${XDG_CONFIG_HOME:-$HOME/.config}/k1520emu"

MODE=install
SHORTCUTS=yes
KEEP_CACHE=no
PURGE=no
PY_VERSION=$K1520_PY_VERSION

usage() {
    cat <<EOF
A5120-Emulator — Installation

  --prefix DIR     Installationsverzeichnis (Vorgabe: $PREFIX)
  --python X.Y     Python-Fassung der Laufzeitumgebung (Vorgabe: $PY_VERSION)
  --no-shortcut    keinen Startmenü-Eintrag und keinen Starter in ~/.local/bin
  --keep-cache     uv-Download-Cache behalten (schnellere Neuinstallation,
                   belegt einige hundert MB)
  --uninstall      Installation entfernen
  --purge          zusätzlich Disketten und Konfiguration löschen
  -h, --help       diese Hilfe

Erstinstallation lädt einmalig ~120 MB (uv, Python, Qt) und braucht daher
eine Internetverbindung; danach startet der Emulator ohne Netz.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)     PREFIX=$2; shift 2 ;;
        --prefix=*)   PREFIX=${1#*=}; shift ;;
        --python)     PY_VERSION=$2; shift 2 ;;
        --python=*)   PY_VERSION=${1#*=}; shift ;;
        --no-shortcut) SHORTCUTS=no; shift ;;
        --keep-cache) KEEP_CACHE=yes; shift ;;
        --uninstall)  MODE=uninstall; shift ;;
        --purge)      PURGE=yes; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) die "unbekannte Option: $1  (--help zeigt die Liste)" ;;
    esac
done

# Relativangaben in absolute wandeln — der Starter braucht einen festen Pfad.
case "$PREFIX" in
    /*) ;;
    *)  PREFIX="$(pwd)/$PREFIX" ;;
esac

# ─── Deinstallieren ──────────────────────────────────────────────────────────

if [ "$MODE" = uninstall ]; then
    info "Installation entfernen: $PREFIX"
    if [ -d "$PREFIX" ]; then
        rm -rf "$PREFIX"
        ok "Verzeichnis gelöscht"
    else
        warn "nichts zu löschen — $PREFIX gibt es nicht"
    fi
    rm -f "$BINDIR/a5120emu" "$APPDIR/a5120emu.desktop" "$ICONDIR/a5120emu.svg"
    ok "Starter und Startmenü-Eintrag entfernt"
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
info "A5120-Emulator $VERSION"
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
# Eine ältere Installation wird ersetzt, venv/, python/ und tools/ bleiben
# stehen — genau das macht ein Update billig.
for d in bin app share; do
    rm -rf "$PREFIX/$d"
    cp -R "$SELF_DIR/payload/$d" "$PREFIX/$d"
done
cp "$SELF_DIR/VERSION" "$SELF_DIR/requirements.lock" "$PREFIX/"
if [ -f "$SELF_DIR/LICENSE" ]; then cp "$SELF_DIR/LICENSE" "$PREFIX/"; fi
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

info "Laufzeitumgebung anlegen"
"$UV" venv --python "$PY_VERSION" --python-preference only-managed "$PREFIX/venv" >/dev/null \
    || die "venv konnte nicht angelegt werden"

info "Qt und Abhängigkeiten installieren (~70 MB, dauert einen Moment)"
"$UV" pip install --python "$PREFIX/venv" --require-hashes -r "$PREFIX/requirements.lock" \
    || die "Abhängigkeiten konnten nicht installiert werden (kein Netz? Proxy?)"
ok "Laufzeitumgebung fertig"

if [ "$KEEP_CACHE" = no ]; then
    rm -rf "$PREFIX/.cache-uv"
fi

# ─── 3. Starter ──────────────────────────────────────────────────────────────

info "Starter schreiben"
mkdir -p "$PREFIX/bin"
sed "s|@ROOT@|$PREFIX|g" "$SELF_DIR/launcher.sh" > "$PREFIX/bin/a5120emu"
chmod +x "$PREFIX/bin/a5120emu"

if [ "$SHORTCUTS" = yes ]; then
    mkdir -p "$BINDIR" "$APPDIR" "$ICONDIR"
    ln -sf "$PREFIX/bin/a5120emu" "$BINDIR/a5120emu"
    cp "$PREFIX/share/icons/a5120emu.svg" "$ICONDIR/a5120emu.svg" 2>/dev/null || true
    sed "s|@ROOT@|$PREFIX|g" "$SELF_DIR/a5120emu.desktop.in" > "$APPDIR/a5120emu.desktop"
    if have update-desktop-database; then
        update-desktop-database "$APPDIR" >/dev/null 2>&1 || true
    fi
    ok "Startmenü-Eintrag und $BINDIR/a5120emu"
    case ":$PATH:" in
        *":$BINDIR:"*) ;;
        *) warn "$BINDIR liegt nicht im PATH — der Emulator startet trotzdem über das Startmenü" ;;
    esac
fi

# ─── 4. Rauchtest ────────────────────────────────────────────────────────────
#
# Ein kaputter Startmenü-Eintrag ist schlimmer als eine abgebrochene
# Installation: hier wird geladen, was der Emulator beim Start lädt.

# Das `cd` ist nicht kosmetisch: bei einem Skript aus der Standardeingabe steht
# das ARBEITSVERZEICHNIS vorn in sys.path.  Ohne den Wechsel prüfte ein aus dem
# Quellbaum heraus gestarteter Installer dessen app/ statt der Installation —
# und meldete „läuft", obwohl er nie angefasst hatte, was er ausliefert.
info "Rauchtest"
cd "$PREFIX"
PYTHONPATH="$PREFIX" "$PREFIX/venv/bin/python3" - "$PREFIX" <<'PYEOF' || die "Rauchtest fehlgeschlagen — die Installation ist unbrauchbar"
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
PYEOF
ok "läuft"

printf "\n"
info "Fertig."
printf "     Starten:      %s\n" "$PREFIX/bin/a5120emu"
if [ "$SHORTCUTS" = yes ]; then
    printf "     oder einfach: a5120emu   (bzw. über das Startmenü)\n"
fi
printf "     Entfernen:    %s --uninstall\n" "$0"
