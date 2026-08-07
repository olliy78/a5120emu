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
