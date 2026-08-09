# Gemeinsame Shell-Bausteine der Paketierung (Bourne-kompatibel, kein Bashismus).
#
# Wird von packaging/build_payload.sh (Bauen) und packaging/install.sh
# (Installieren beim Anwender) eingebunden.  Entwurf: doc/design/13_distribution.md
#
# Einbinden:  . "$(dirname "$0")/lib/common.sh"

# Python-Fassung der Laufzeitumgebung.  Dieselbe beim Schnüren (requirements.lock
# wird dafür aufgelöst) wie beim Installieren — sonst passen die Wheels nicht.
K1520_PY_VERSION=${K1520_PY_VERSION:-3.12}

# ─── Meldungen ───────────────────────────────────────────────────────────────
# Farben nur, wenn die Ausgabe wirklich ein Terminal ist — in Protokollen und
# Installer-Fenstern stören Steuerzeichen.

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_INFO='\033[1;34m'; C_OK='\033[1;32m'; C_WARN='\033[1;33m'
    C_ERR='\033[1;31m';  C_OFF='\033[0m'
else
    C_INFO=''; C_OK=''; C_WARN=''; C_ERR=''; C_OFF=''
fi

info()  { printf "${C_INFO}==>${C_OFF} %s\n" "$*"; }
ok()    { printf "${C_OK}  ok${C_OFF} %s\n" "$*"; }
warn()  { printf "${C_WARN}Warnung:${C_OFF} %s\n" "$*" >&2; }
die()   { printf "${C_ERR}Fehler:${C_OFF} %s\n" "$*" >&2; exit 1; }

# ─── Plattform ───────────────────────────────────────────────────────────────

# Zieltripel, wie es die uv-Releases benennen.
uv_target() {
    _os=$(uname -s)
    _arch=$(uname -m)
    case "$_arch" in
        x86_64|amd64)  _arch=x86_64 ;;
        aarch64|arm64) _arch=aarch64 ;;
        *) die "nicht unterstützte Architektur: $_arch" ;;
    esac
    case "$_os" in
        Linux)  echo "${_arch}-unknown-linux-gnu" ;;
        Darwin) echo "${_arch}-apple-darwin" ;;
        *) die "nicht unterstütztes System: $_os (Windows: packaging/install.ps1)" ;;
    esac
}

# Dateiname der Kernbibliothek auf diesem System.
core_lib_name() {
    case "$(uname -s)" in
        Darwin) echo "libk1520core.dylib" ;;
        *)      echo "libk1520core.so" ;;
    esac
}

# abs_path <pfad> — Tilde und Relativangaben auflösen.
#
# Die Tilde muss dabei GESCHÜTZT werden: unquotiert dehnt die Shell sie in der
# Musterangabe selbst zu $HOME aus, das Muster passt dann nie, und im Ergebnis
# bliebe ein Verzeichnis namens „~" stehen.
abs_path() {
    _p=$1
    case "$_p" in
        "~")   _p="$HOME" ;;
        "~/"*) _p="$HOME/${_p#"~/"}" ;;
    esac
    case "$_p" in
        /*) ;;
        *)  _p="$(pwd)/$_p" ;;
    esac
    echo "$_p"
}

# Dateiname des Merkmals, an dem eine Installation zu erkennen ist.
INSTALL_MARKER=".k1520emu-installation"

# ist_installation <verzeichnis> — trägt das Verzeichnis unsere Installation?
#
# Das Deinstallieren LÖSCHT sein Ziel, und seit das Ziel erfragt wird, kann dort
# alles stehen — im schlimmsten Fall das Heimatverzeichnis.  Gelöscht wird
# deshalb nur, was sich ausweisen kann.  Installationen aus der Zeit vor dem
# Merkmal werden am Inventar erkannt, damit ein Update über sie hinweg geht.
ist_installation() {
    [ -f "$1/$INSTALL_MARKER" ] && return 0
    [ -f "$1/VERSION" ] && [ -f "$1/app/paths.py" ] && [ -d "$1/share/k1520emu" ]
}

# ersetze_root <vorlage> <wurzel> — @ROOT@ einsetzen und das Ergebnis ausgeben.
#
# Bewusst ohne `sed`: der Installationspfad kommt seit der Zielabfrage vom
# Anwender und darf jedes Zeichen enthalten — „|" wäre dort das Trennzeichen des
# Ausdrucks, „&" und „\" in der Ersetzung wieder etwas anderes.  Je Zeile ein
# Platzhalter genügt (beide Vorlagen halten sich daran).
ersetze_root() {
    _vorlage=$1; _wurzel=$2
    while IFS= read -r _zeile || [ -n "$_zeile" ]; do
        case "$_zeile" in
            *@ROOT@*) printf '%s%s%s\n' "${_zeile%%@ROOT@*}" "$_wurzel" "${_zeile#*@ROOT@}" ;;
            *)        printf '%s\n' "$_zeile" ;;
        esac
    done < "$_vorlage"
}

# dokumente_dir — Dokumentenordner des Anwenders ausgeben, sonst mit 1 enden.
#
# Sein Name ist sprachabhängig (~/Dokumente, ~/Documents, ~/Documenti);
# verbindlich ist XDG_DOCUMENTS_DIR aus ~/.config/user-dirs.dirs, die die
# Desktop-Umgebung selbst pflegt.  Gelesen wird die Datei direkt, nicht über das
# Programm `xdg-user-dir` — das fehlt auf schlanken Systemen.
#
# ACHTUNG: dieselbe Regel steht in app/paths.py (`documents_dir`).  Beide
# müssen dasselbe liefern, sonst räumt `--purge` woanders auf, als der Emulator
# schreibt; Guard: `test_dokumentenordner_shell_und_python_stimmen_ueberein`.
dokumente_dir() {
    if [ -n "${XDG_DOCUMENTS_DIR:-}" ]; then
        echo "$XDG_DOCUMENTS_DIR"
        return 0
    fi
    _ud="${XDG_CONFIG_HOME:-$HOME/.config}/user-dirs.dirs"
    if [ -f "$_ud" ]; then
        _wert=$(sed -n 's/^[[:space:]]*XDG_DOCUMENTS_DIR=//p' "$_ud" | tail -n 1)
        _wert=$(printf '%s' "$_wert" | tr -d "\"'")
        # In der Datei steht „$HOME/Dokumente" — die Variable gehört zum Format,
        # eine Shell hat sie nie gesehen.  Ersetzt wird sie ohne `sed`, damit ein
        # Sonderzeichen im Heimatpfad nicht zum Ausdruck wird.
        case "$_wert" in
            '$HOME')   _wert=$HOME ;;
            '$HOME/'*) _wert="$HOME/${_wert#\$HOME/}" ;;
        esac
        if [ -n "$_wert" ]; then
            echo "$_wert"
            return 0
        fi
    fi
    for _n in Documents Dokumente; do
        if [ -d "$HOME/$_n" ]; then echo "$HOME/$_n"; return 0; fi
    done
    return 1
}

# benutzerdaten_dir — wohin der Emulator die Anwenderdaten legt (Disketten …).
benutzerdaten_dir() {
    if _docs=$(dokumente_dir); then
        echo "$_docs/K1520emu"
    else
        echo "${XDG_DATA_HOME:-$HOME/.local/share}/K1520emu"
    fi
}

# ist_leer <verzeichnis> — Verzeichnis nicht vorhanden oder ohne Inhalt?
ist_leer() {
    [ -d "$1" ] || return 0
    [ -z "$(ls -A "$1" 2>/dev/null)" ]
}

# ─── Werkzeuge ───────────────────────────────────────────────────────────────

have() { command -v "$1" >/dev/null 2>&1; }

# fetch <url> <ziel> — lädt eine Datei (curl oder wget, je nachdem was da ist).
fetch() {
    if have curl; then
        curl -fsSL --retry 3 --connect-timeout 20 -o "$2" "$1" \
            || die "Download fehlgeschlagen: $1"
    elif have wget; then
        wget -q --tries=3 --timeout=20 -O "$2" "$1" \
            || die "Download fehlgeschlagen: $1"
    else
        die "weder curl noch wget gefunden — eines von beiden wird zum Nachladen gebraucht"
    fi
}

# sha256_of <datei> — Prüfsumme, egal ob sha256sum (Linux) oder shasum (macOS).
sha256_of() {
    if have sha256sum; then sha256sum "$1" | awk '{print $1}'
    elif have shasum;   then shasum -a 256 "$1" | awk '{print $1}'
    else die "kein sha256sum/shasum gefunden — Prüfsummen nicht überprüfbar"
    fi
}

# ─── uv ──────────────────────────────────────────────────────────────────────

# uv_pin <pins-datei> <schlüssel> — Wert aus packaging/uv_pins.txt lesen.
uv_pin() {
    awk -v key="$2" '$1 == key { print $2; exit }' "$1"
}

# ensure_uv <zielverzeichnis> <pins-datei>
#
# Legt <zielverzeichnis>/uv an und gibt den Pfad aus.  Ist dort schon ein uv,
# wird es benutzt.  Sonst: gepinnte Fassung laden, Prüfsumme gegen die
# MITGELIEFERTE Erwartung halten, entpacken.
#
# Ein systemweit vorhandenes uv wird bewusst NICHT verwendet: die Installation
# soll auf jedem Rechner dieselbe, geprüfte Fassung benutzen und beim
# Deinstallieren restlos verschwinden.
ensure_uv() {
    _dir=$1
    _pins=$2
    _uv="$_dir/uv"
    [ -x "$_uv" ] && { echo "$_uv"; return 0; }

    [ -f "$_pins" ] || die "uv-Pins nicht gefunden: $_pins"
    _ver=$(uv_pin "$_pins" version)
    _target=$(uv_target)
    _want=$(uv_pin "$_pins" "$_target")
    [ -n "$_ver" ] || die "keine uv-Version in $_pins"
    [ -n "$_want" ] || die "keine uv-Prüfsumme für $_target in $_pins"

    _tmp=$(mktemp -d) || die "kein temporäres Verzeichnis anlegbar"
    _archive="$_tmp/uv.tar.gz"
    _url="https://github.com/astral-sh/uv/releases/download/$_ver/uv-$_target.tar.gz"

    info "uv $_ver für $_target laden (~15 MB)" >&2
    fetch "$_url" "$_archive"

    _got=$(sha256_of "$_archive")
    [ "$_got" = "$_want" ] || {
        rm -rf "$_tmp"
        die "Prüfsumme von uv stimmt nicht:
  erwartet $_want
  erhalten $_got
Abbruch — die heruntergeladene Datei wird nicht ausgeführt."
    }

    ( cd "$_tmp" && tar xzf uv.tar.gz ) || { rm -rf "$_tmp"; die "uv-Archiv nicht entpackbar"; }
    mkdir -p "$_dir"
    # Das Archiv enthält ein Verzeichnis uv-<tripel>/ mit uv und uvx.
    cp "$_tmp/uv-$_target/uv" "$_uv" || { rm -rf "$_tmp"; die "uv nicht auffindbar im Archiv"; }
    chmod +x "$_uv"
    rm -rf "$_tmp"

    # Virenscanner und Sicherheitsrichtlinien greifen sich gelegentlich frisch
    # heruntergeladene Binärdateien — das hier ist die verständliche Meldung.
    [ -x "$_uv" ] || die "uv wurde nach dem Entpacken entfernt (Virenscanner? Richtlinie?)"
    echo "$_uv"
}
