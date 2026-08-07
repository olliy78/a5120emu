#!/bin/sh
# ─────────────────────────────────────────────────────────────────────────────
# A5120-Emulator — verteilbares Paket schnüren (Linux/macOS)
# ─────────────────────────────────────────────────────────────────────────────
#
# Baut die Kernbibliothek als Release und legt sie mit GUI, Formatkatalog,
# Beispieldisketten und dem Installer zu einem Archiv zusammen.  Python und Qt
# sind NICHT enthalten — die holt der Installer beim Anwender (§2 des Entwurfs
# doc/design/13_distribution.md).
#
#     packaging/build_payload.sh                 # nach dist/
#     packaging/build_payload.sh --out /tmp/x    # woandershin
#     packaging/build_payload.sh --disks all     # alle Disketten aus disks/
#     packaging/build_payload.sh --refresh-uv    # uv-Pins aktualisieren
set -eu

SELF_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$SELF_DIR/.." && pwd)
. "$SELF_DIR/lib/common.sh"

OUT="$REPO/dist"
BUILD_DIR="$REPO/build_dist"
VERSION=""
DISKS=default
SKIP_BUILD=no
ARCHIVE=yes
RELOCK=no

# Mitgelieferte Beispieldisketten.  Bewusst eine kleine Auswahl: das Paket soll
# klein bleiben, und was hier landet, muss weitergegeben werden dürfen
# (offener Punkt §11 des Entwurfs).
DISKS_DEFAULT="cpa_cpa780_k5601_clock.hfe
cpa_cpa780_k5601_noclock.hfe
cpa_cpa780_combo5zoll_noclock.hfe
cpa_cpa780_combo8zoll_noclock.hfe
scpx17_cpa780_k5601.hfe
udos_boot_k5600_20.hfe"

usage() {
    cat <<EOF
A5120-Emulator — Paket schnüren

  --out DIR        Ausgabeverzeichnis (Vorgabe: $OUT)
  --build-dir DIR  Bauverzeichnis für den Release-Kern (Vorgabe: $BUILD_DIR)
  --version V      Versionsbezeichnung (Vorgabe: aus git describe)
  --disks WAS      default | all | none — welche Beispieldisketten mitkommen
  --skip-build     vorhandene Bibliothek im Bauverzeichnis verwenden
  --relock         packaging/requirements.lock neu auflösen (braucht Netz)
  --no-archive     nur den Baum unter dist/ erzeugen, kein .tar.gz
  --refresh-uv     uv-Pins auf die neueste Fassung setzen und beenden
  -h, --help       diese Hilfe
EOF
}

# ─── uv-Pins auffrischen ─────────────────────────────────────────────────────

refresh_uv_pins() {
    have curl || die "curl wird zum Auffrischen der Pins gebraucht"
    have jq   || die "jq wird zum Auffrischen der Pins gebraucht"
    _ver=$(curl -fsSL https://api.github.com/repos/astral-sh/uv/releases/latest | jq -r .tag_name)
    [ -n "$_ver" ] || die "uv-Version nicht ermittelbar"
    info "uv $_ver"
    _tmp=$(mktemp)
    {
        sed -n '1,/^# Format:/p' "$SELF_DIR/uv_pins.txt"
        printf '\nversion %s\n\n' "$_ver"
        for _t in x86_64-unknown-linux-gnu aarch64-unknown-linux-gnu \
                  x86_64-apple-darwin aarch64-apple-darwin x86_64-pc-windows-msvc; do
            case "$_t" in *windows*) _e=zip ;; *) _e=tar.gz ;; esac
            _s=$(curl -fsSL "https://github.com/astral-sh/uv/releases/download/$_ver/uv-$_t.$_e.sha256" \
                 | awk '{print $1}')
            [ -n "$_s" ] || die "keine Prüfsumme für $_t"
            printf '%-26s %s\n' "$_t" "$_s"
        done
    } > "$_tmp"
    mv "$_tmp" "$SELF_DIR/uv_pins.txt"
    ok "packaging/uv_pins.txt aktualisiert"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --out)         OUT=$2; shift 2 ;;
        --build-dir)   BUILD_DIR=$2; shift 2 ;;
        --version)     VERSION=$2; shift 2 ;;
        --disks)       DISKS=$2; shift 2 ;;
        --skip-build)  SKIP_BUILD=yes; shift ;;
        --relock)      RELOCK=yes; shift ;;
        --no-archive)  ARCHIVE=no; shift ;;
        --refresh-uv)  refresh_uv_pins; exit 0 ;;
        -h|--help)     usage; exit 0 ;;
        *) die "unbekannte Option: $1  (--help zeigt die Liste)" ;;
    esac
done

# ─── Version und Zielname ────────────────────────────────────────────────────

if [ -z "$VERSION" ]; then
    VERSION=$(cd "$REPO" && git describe --tags --always --dirty 2>/dev/null || echo 0.0.0)
fi
case "$(uname -s)" in
    Darwin) PLATFORM="macos-$(uname -m)" ;;
    *)      PLATFORM="linux-$(uname -m)" ;;
esac
NAME="a5120emu-$VERSION-$PLATFORM"
STAGE="$OUT/$NAME"

info "A5120-Emulator $VERSION für $PLATFORM"

# ─── 1. Kern bauen ───────────────────────────────────────────────────────────
#
# Eigenes Bauverzeichnis, damit die Entwicklungsstände in build/ und
# build_trace/ unberührt bleiben.  Zwei Einstellungen sind für ein
# verteilbares Paket entscheidend:
#
#   K1520_FORMATS_DEFAULT=""  — sonst steht der absolute Pfad des BAURECHNERS
#                               als Kandidat in der Bibliothek.
#   -static-libstdc++         — die Bibliothek soll auch auf Systemen mit
#   -static-libgcc              älterer/neuerer libstdc++ laden.

LIB=$(core_lib_name)

if [ "$SKIP_BUILD" = no ]; then
    info "Kern als Release bauen ($BUILD_DIR)"
    have cmake || die "cmake nicht gefunden"
    cmake -S "$REPO" -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DLOG_LEVEL=3 \
        -DK1520_FORMATS_DEFAULT= \
        -DBUILD_K1520_TESTS=OFF \
        -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
        >/dev/null || die "cmake-Konfiguration fehlgeschlagen"
    cmake --build "$BUILD_DIR" --target k1520core -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" \
        >/dev/null || die "Bauen der Kernbibliothek fehlgeschlagen"
    ok "$BUILD_DIR/$LIB"
fi
[ -f "$BUILD_DIR/$LIB" ] || die "Kernbibliothek fehlt: $BUILD_DIR/$LIB"

# ─── 2. Payload zusammenstellen ──────────────────────────────────────────────

info "Payload zusammenstellen"
rm -rf "$STAGE"
mkdir -p "$STAGE/payload/bin" "$STAGE/payload/share/a5120emu" \
         "$STAGE/payload/share/disks" "$STAGE/payload/share/icons" "$STAGE/lib"

cp "$BUILD_DIR/$LIB" "$STAGE/payload/bin/$LIB"
strip "$STAGE/payload/bin/$LIB" 2>/dev/null || true

# GUI ohne Bytecode-Reste des Entwicklungsrechners (--exclude MUSS vor dem
# Pfad stehen, sonst wirkt es bei GNU tar nicht)
( cd "$REPO" && tar cf - --exclude='__pycache__' --exclude='*.pyc' app ) \
    | ( cd "$STAGE/payload" && tar xf - ) \
    || die "GUI-Dateien nicht kopierbar"
[ -f "$STAGE/payload/app/main.py" ] || die "app/main.py fehlt in der Payload"

cp "$REPO/data/formats.yaml" "$STAGE/payload/share/a5120emu/formats.yaml"
cp "$SELF_DIR/icon.svg"      "$STAGE/payload/share/icons/a5120emu.svg"

case "$DISKS" in
    none) ;;
    all)  cp "$REPO"/disks/*.hfe "$REPO"/disks/*.img "$STAGE/payload/share/disks/" 2>/dev/null || true ;;
    default)
        for d in $DISKS_DEFAULT; do
            if [ -f "$REPO/disks/$d" ]; then
                cp "$REPO/disks/$d" "$STAGE/payload/share/disks/"
            else
                warn "Beispieldiskette fehlt, wird ausgelassen: $d"
            fi
        done ;;
    *) die "--disks erwartet default|all|none, nicht '$DISKS'" ;;
esac
cp "$REPO/disks/README.md" "$STAGE/payload/share/disks/README.md" 2>/dev/null || true

# ─── 3. Installer beilegen ───────────────────────────────────────────────────

cp "$SELF_DIR/install.sh"           "$STAGE/install.sh"
cp "$SELF_DIR/launcher.sh"          "$STAGE/launcher.sh"
cp "$SELF_DIR/a5120emu.desktop.in"  "$STAGE/a5120emu.desktop.in"
cp "$SELF_DIR/uv_pins.txt"          "$STAGE/uv_pins.txt"
cp "$SELF_DIR/lib/common.sh"        "$STAGE/lib/common.sh"
cp "$SELF_DIR/paket_readme.md"      "$STAGE/README.md"
chmod +x "$STAGE/install.sh"
if [ -f "$REPO/LICENSE" ]; then cp "$REPO/LICENSE" "$STAGE/LICENSE"; fi

printf '%s (%s, %s, Python %s)\n' \
    "$VERSION" "$PLATFORM" "$(date +%Y-%m-%d)" "$K1520_PY_VERSION" > "$STAGE/VERSION"

# ─── 4. Abhängigkeiten festnageln ────────────────────────────────────────────
#
# packaging/requirements.lock hält Fassungen UND Hashes fest und ist
# eingecheckt: das Schnüren braucht damit kein Netz, jeder Bau liefert
# dasselbe, und eine Änderung an den Abhängigkeiten ist im Diff sichtbar.
# Der Installer installiert mit --require-hashes.  Auffrischen: --relock.

if [ "$RELOCK" = yes ]; then
    info "requirements.lock neu auflösen (uv)"
    UV=$(ensure_uv "$OUT/.tools" "$SELF_DIR/uv_pins.txt")
    UV_CACHE_DIR="$OUT/.cache-uv"; export UV_CACHE_DIR
    "$UV" pip compile "$SELF_DIR/requirements.in" \
        --python-version "$K1520_PY_VERSION" \
        --generate-hashes --quiet --no-header \
        -o "$SELF_DIR/requirements.lock" \
        || die "Abhängigkeiten nicht auflösbar"
    ok "packaging/requirements.lock aktualisiert"
fi

[ -f "$SELF_DIR/requirements.lock" ] \
    || die "packaging/requirements.lock fehlt — einmal mit --relock erzeugen (braucht Netz)"
cp "$SELF_DIR/requirements.lock" "$STAGE/requirements.lock"
ok "$(grep -c '^[a-zA-Z]' "$STAGE/requirements.lock") Pakete festgenagelt"

# ─── 5. Archiv ───────────────────────────────────────────────────────────────

if [ "$ARCHIVE" = yes ]; then
    info "Archiv schnüren"
    ( cd "$OUT" && tar czf "$NAME.tar.gz" "$NAME" )
    ( cd "$OUT" && sha256_of "$NAME.tar.gz" | awk -v n="$NAME.tar.gz" '{print $1"  "n}' > "$NAME.tar.gz.sha256" )
    ok "$OUT/$NAME.tar.gz  ($(du -h "$OUT/$NAME.tar.gz" | awk '{print $1}'))"
    printf "     %s\n" "$(cat "$OUT/$NAME.tar.gz.sha256")"
fi

printf "\n"
info "Fertig.  Probeinstallation:"
printf "     tar xzf %s/%s.tar.gz -C /tmp && /tmp/%s/install.sh --prefix /tmp/a5120emu-test\n" \
    "$OUT" "$NAME" "$NAME"
