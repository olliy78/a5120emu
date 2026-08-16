#!/bin/sh
# ─────────────────────────────────────────────────────────────────────────────
# K1520-Emulator — verteilbares Paket schnüren (Linux/macOS)
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
#     packaging/build_payload.sh --refresh-python  # Python-Pins (Windows-Setup)
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
SETUP=no
RELOCK=no

# Mitgelieferte Beispieldisketten.  Bewusst eine kleine Auswahl: je eine
# startfähige Diskette für CP/A, SCPX und UDOS plus die beiden Combo-Disketten,
# damit ein Anwender alle drei Betriebssysteme und die Fremdlaufwerkstypen
# ausprobieren kann, ohne dass das Paket aufgeht.  Alles aus disks/: --disks all.
DISKS_DEFAULT="cpa_cpa780_k5601_clock.hfe
cpa_cpa780_k5601_noclock.hfe
cpa_cpa780_combo5zoll_noclock.hfe
cpa_cpa780_combo8zoll_noclock.hfe
scpx17_cpa780_k5601.hfe
udos_boot_k5600_20.hfe"

usage() {
    cat <<EOF
K1520-Emulator — Paket schnüren

  --out DIR        Ausgabeverzeichnis (Vorgabe: $OUT)
  --build-dir DIR  Bauverzeichnis für den Release-Kern (Vorgabe: $BUILD_DIR)
  --version V      Versionsbezeichnung (Vorgabe: aus git describe)
  --disks WAS      default | all | none — welche Beispieldisketten mitkommen
  --skip-build     vorhandene Bibliothek im Bauverzeichnis verwenden
  --relock         packaging/requirements.lock neu auflösen (braucht Netz)
  --no-archive     nur den Baum unter dist/ erzeugen, kein .tar.gz
  --setup          zusätzlich ein Windows-Installationsprogramm bauen (Inno
                   Setup; nur unter Windows, braucht iscc im PATH)
  --refresh-uv     uv-Pins auf die neueste Fassung setzen und beenden
  --refresh-python Python-Pins (Windows-Setup) auffrischen und beenden
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

# ─── Python-Pins auffrischen ─────────────────────────────────────────────────
#
# Nur fuer das Windows-Setup: dort laedt der Assistent Python selbst, weil uv
# einen Junction anlegt, den der OneDrive-Filtertreiber verweigert (os error
# 448 — Kopf von packaging/python_pins.txt).  Unter Linux bleibt es bei uv.
#
# Die Pruefsummen liegen als SHA256SUMS am Release — es muss also keine 46-MB-
# Datei geladen werden, um zu pinnen.

PBS_REPO=astral-sh/python-build-standalone
PBS_ZIEL=x86_64-pc-windows-msvc

refresh_python_pins() {
    have curl || die "curl wird zum Auffrischen der Pins gebraucht"
    have jq   || die "jq wird zum Auffrischen der Pins gebraucht"
    _rel=$(curl -fsSL "https://api.github.com/repos/$PBS_REPO/releases/latest" | jq -r .tag_name)
    [ -n "$_rel" ] && [ "$_rel" != null ] || die "python-build-standalone: Release nicht ermittelbar"
    _liste=$(curl -fsSL "https://api.github.com/repos/$PBS_REPO/releases/tags/$_rel")

    # Neuester Fehlerstand der Nebenversion, gegen die die Wheels gebaut sind.
    _muster="^cpython-$K1520_PY_VERSION\\.[0-9]+\\+$_rel-$PBS_ZIEL-install_only_stripped\\.tar\\.gz\$"
    _datei=$(printf '%s' "$_liste" \
             | jq -r --arg m "$_muster" '.assets[].name | select(test($m))' \
             | sort -V | tail -1)
    [ -n "$_datei" ] || die "kein $PBS_ZIEL-Abbild für Python $K1520_PY_VERSION in $_rel"
    _ver=$(printf '%s' "$_datei" | sed 's/^cpython-//; s/+.*//')
    _size=$(printf '%s' "$_liste" | jq -r --arg n "$_datei" '.assets[] | select(.name==$n) | .size')
    _sha=$(curl -fsSL "https://github.com/$PBS_REPO/releases/download/$_rel/SHA256SUMS" \
           | awk -v n="$_datei" '$2==n {print $1}')
    [ -n "$_sha" ] || die "keine Prüfsumme für $_datei"

    info "python-build-standalone $_rel — CPython $_ver"
    _tmp=$(mktemp)
    {
        sed -n '1,/^# Format:/p' "$SELF_DIR/python_pins.txt"
        printf '\nversion %s\nrelease %s\n\n' "$_ver" "$_rel"
        printf '%-24s %10s  %s\n' "$PBS_ZIEL" "$_size" "$_sha"
    } > "$_tmp"
    mv "$_tmp" "$SELF_DIR/python_pins.txt"
    ok "packaging/python_pins.txt aktualisiert"
}

# python_pins.txt lesen und PY_VERSION/PY_RELEASE/PY_SIZE/PY_SHA setzen.
lies_python_pins() {
    _pins="$SELF_DIR/python_pins.txt"
    [ -f "$_pins" ] || die "packaging/python_pins.txt fehlt — einmal mit --refresh-python erzeugen"
    PY_VERSION=$(awk '$1=="version" {print $2}' "$_pins")
    PY_RELEASE=$(awk '$1=="release" {print $2}' "$_pins")
    PY_SIZE=$(awk -v z="$PBS_ZIEL" '$1==z {print $2}' "$_pins")
    PY_SHA=$(awk  -v z="$PBS_ZIEL" '$1==z {print $3}' "$_pins")
    [ -n "$PY_VERSION" ] && [ -n "$PY_RELEASE" ] && [ -n "$PY_SIZE" ] && [ -n "$PY_SHA" ] \
        || die "python_pins.txt ist unvollständig (version/release/$PBS_ZIEL)"
    # Die Nebenversion MUSS zu den Wheels aus requirements.lock passen.
    case "$PY_VERSION" in
        "$K1520_PY_VERSION".*) ;;
        *) die "python_pins.txt nennt Python $PY_VERSION, gebaut wird gegen $K1520_PY_VERSION" ;;
    esac
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
        --setup)       SETUP=yes; shift ;;
        --refresh-uv)      refresh_uv_pins; exit 0 ;;
        --refresh-python)  refresh_python_pins; exit 0 ;;
        -h|--help)     usage; exit 0 ;;
        *) die "unbekannte Option: $1  (--help zeigt die Liste)" ;;
    esac
done

# ─── Version und Zielname ────────────────────────────────────────────────────

if [ -z "$VERSION" ]; then
    VERSION=$(cd "$REPO" && git describe --tags --always --dirty 2>/dev/null || echo 0.0.0)
fi
case "$(uname -s)" in
    Darwin)               PLATFORM="macos-$(uname -m)" ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM="windows-$(uname -m)" ;;
    *)                    PLATFORM="linux-$(uname -m)" ;;
esac
NAME="k1520emu-$VERSION-$PLATFORM"
STAGE="$OUT/$NAME"

info "K1520-Emulator $VERSION für $PLATFORM"

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

# ACHTUNG bei der Namenswahl: `LIB` und `INCLUDE` sind unter MSVC die
# SUCHPFADE des Übersetzers und des Binders.  Diese Variable hiess bis
# 2026-08-12 schlicht `LIB` — unter Linux bedeutungslos, unter Windows
# ueberschrieb sie den Bibliothekssuchpfad, und `link.exe` suchte kernel32.lib
# in einem „Verzeichnis" namens k1520core.dll: `LNK1104: cannot open file
# 'kernel32.lib'`, mitten im Uebersetzertest von cmake.  Deshalb Praefix.
K1520_CORE_LIB=$(core_lib_name)
K1520_DISK_LIB=$(disk_lib_name)
K1520_DISK_CLI=$(disk_cli_name)

# Wie die Laufzeit gebunden wird, ist je System eine ANDERE Frage — beide Male
# lautet die Antwort „die Bibliothek bringt sie mit", der Weg dahin ist anders:
#
#   GCC   -static-libstdc++/-static-libgcc, damit sie auch auf einem System mit
#         anderer libstdc++ lädt.
#   MSVC  -DK1520_MSVC_STATIC_CRT=ON (/MT).  Der Windows-Installer läuft
#         per-user ohne Administratorrechte und kann kein VC-Redist
#         nachinstallieren; ohne /MT startet der Emulator auf einem frischen
#         Windows gar nicht („VCRUNTIME140.dll fehlt").  Geprüft vom Job
#         „Auslieferungsbau" in windows-ci.yml — die DLL hängt danach nur noch
#         an KERNEL32.dll.  Generator Ninja, damit die Ausgabe wie überall
#         direkt in $BUILD_DIR liegt und nicht in einem Konfigurationsordner.
if ist_windows; then
    LAUFZEIT_ARGS='-G Ninja -DK1520_MSVC_STATIC_CRT=ON'
else
    LAUFZEIT_ARGS='-DCMAKE_SHARED_LINKER_FLAGS=-static-libstdc++ -static-libgcc'
fi

if [ "$SKIP_BUILD" = no ]; then
    info "Kern als Release bauen ($BUILD_DIR)"
    have cmake || die "cmake nicht gefunden"
    # shellcheck disable=SC2086  # LAUFZEIT_ARGS soll in Worte zerfallen
    if ist_windows; then
        cmake -S "$REPO" -B "$BUILD_DIR" \
            -DCMAKE_BUILD_TYPE=Release -DLOG_LEVEL=3 \
            -DK1520_FORMATS_DEFAULT= -DBUILD_K1520_TESTS=OFF \
            $LAUFZEIT_ARGS \
            >/dev/null || die "cmake-Konfiguration fehlgeschlagen"
    else
        cmake -S "$REPO" -B "$BUILD_DIR" \
            -DCMAKE_BUILD_TYPE=Release -DLOG_LEVEL=3 \
            -DK1520_FORMATS_DEFAULT= -DBUILD_K1520_TESTS=OFF \
            -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
            >/dev/null || die "cmake-Konfiguration fehlgeschlagen"
    fi
    # Drei Ziele: der Emulatorkern, die DiskTool-Bibliothek fuer dessen
    # Oberflaeche und das Kommandozeilenwerkzeug.  Ohne die letzten beiden
    # laege app/disktool/ zwar im Paket, faende aber keine Bibliothek.
    cmake --build "$BUILD_DIR" --target k1520core k1520disk k1520disktool \
        -j"$(kerne)" \
        >/dev/null || die "Bauen der Bibliotheken/Werkzeuge fehlgeschlagen"
    ok "/, /, $BUILD_DIR/$K1520_DISK_CLI"
fi
[ -f "$BUILD_DIR/$K1520_CORE_LIB" ]     || die "Kernbibliothek fehlt: $BUILD_DIR/$K1520_CORE_LIB"
[ -f "$BUILD_DIR/$K1520_DISK_LIB" ] || die "DiskTool-Bibliothek fehlt: $BUILD_DIR/$K1520_DISK_LIB"
[ -f "$BUILD_DIR/$K1520_DISK_CLI" ] || die "DiskTool-Kommandozeile fehlt: $BUILD_DIR/$K1520_DISK_CLI"

# ─── 2. Payload zusammenstellen ──────────────────────────────────────────────

info "Payload zusammenstellen"
rm -rf "$STAGE"
mkdir -p "$STAGE/payload/bin" "$STAGE/payload/share/k1520emu" \
         "$STAGE/payload/share/disks" "$STAGE/payload/share/icons" "$STAGE/lib"

# `strip` ist ein ELF/Mach-O-Werkzeug; MSVC legt Debugsymbole ohnehin in eine
# eigene .pdb, die hier gar nicht erst mitkommt.  Das `|| true` deckt beides ab.
cp "$BUILD_DIR/$K1520_CORE_LIB" "$STAGE/payload/bin/$K1520_CORE_LIB"
strip "$STAGE/payload/bin/$K1520_CORE_LIB" 2>/dev/null || true

cp "$BUILD_DIR/$K1520_DISK_LIB" "$STAGE/payload/bin/$K1520_DISK_LIB"
strip "$STAGE/payload/bin/$K1520_DISK_LIB" 2>/dev/null || true

# Das Kommandozeilenwerkzeug wird unter -cli abgelegt: `bin/k1520disktool` ist
# der Starter der OBERFLAECHE (wie `bin/a5120emu` beim Emulator), den der
# Installer schreibt.  Zwei Namen sind ehrlicher als ein Programm, das je nach
# Argumenten etwas anderes tut.
# Unter Windows bleibt die Endung hinten: „k1520disktool-cli.exe" — ohne .exe
# startet es niemand.
if ist_windows; then _cli_ziel="k1520disktool-cli.exe"; else _cli_ziel="$K1520_DISK_CLI-cli"; fi
cp "$BUILD_DIR/$K1520_DISK_CLI" "$STAGE/payload/bin/$_cli_ziel"
strip "$STAGE/payload/bin/$_cli_ziel" 2>/dev/null || true

# GUI ohne Bytecode-Reste des Entwicklungsrechners (--exclude MUSS vor dem
# Pfad stehen, sonst wirkt es bei GNU tar nicht)
( cd "$REPO" && tar cf - --exclude='__pycache__' --exclude='*.pyc' app ) \
    | ( cd "$STAGE/payload" && tar xf - ) \
    || die "GUI-Dateien nicht kopierbar"
[ -f "$STAGE/payload/app/main.py" ] || die "app/main.py fehlt in der Payload"
[ -f "$STAGE/payload/app/disktool/main.py" ] \
    || die "app/disktool/main.py fehlt in der Payload"

cp "$REPO/data/formats.yaml" "$STAGE/payload/share/k1520emu/formats.yaml"
cp "$SELF_DIR/icon.svg"      "$STAGE/payload/share/icons/a5120emu.svg"
# Windows braucht ein .ico (Startmenue, Deinstallationseintrag, Setup selbst).
# Es liegt eingecheckt daneben, weil der Windows-Laeufer weder Qt noch
# tools/svg_to_ico.py hat; auffrischen nach einer Aenderung am SVG:
#   QT_QPA_PLATFORM=offscreen venv/bin/python3 tools/svg_to_ico.py \
#       packaging/icon.svg packaging/icon.ico
cp "$SELF_DIR/icon.ico"      "$STAGE/payload/share/icons/a5120emu.ico"

# Beispieldisketten werden GEPACKT abgelegt.  Ein Diskettenabbild besteht zum
# größten Teil aus Füllmuster (11 MB schrumpfen auf gut 1 MB), und gebraucht
# wird es genau einmal: beim ersten Start entpackt `paths.seed_user_disks()` es
# in das Arbeitsverzeichnis des Anwenders.  Ungepackt lägen die Abbilder danach
# doppelt auf der Platte — einmal in der Installation, einmal beim Anwender.
lege_diskette_ab() {
    gzip -9 -c "$1" > "$STAGE/payload/share/disks/$(basename "$1").gz" \
        || die "Beispieldiskette nicht packbar: $1"
}

case "$DISKS" in
    none) ;;
    all)
        for d in "$REPO"/disks/*.hfe "$REPO"/disks/*.img "$REPO"/disks/*.dmk; do
            if [ -f "$d" ]; then lege_diskette_ab "$d"; fi
        done ;;
    default)
        for d in $DISKS_DEFAULT; do
            if [ -f "$REPO/disks/$d" ]; then
                lege_diskette_ab "$REPO/disks/$d"
            else
                warn "Beispieldiskette fehlt, wird ausgelassen: $d"
            fi
        done ;;
    *) die "--disks erwartet default|all|none, nicht '$DISKS'" ;;
esac
cp "$REPO/disks/README.md" "$STAGE/payload/share/disks/README.md" 2>/dev/null || true

# ─── 3. Installer beilegen ───────────────────────────────────────────────────

# Beigelegt wird NUR der Installer des Zielsystems.  Ein Windows-Anwender soll
# keine .sh-Datei sehen (er hätte keine Shell dafür), ein Linux-Anwender keine
# .cmd.  Die .desktop-Vorlagen bleiben im Windows-Paket weg — `slim.py` dagegen
# kommt MIT: es kann seit 2026-08-12 auch Windows (es liest die
# PE-Importtabelle selbst, weil der Anwender kein dumpbin hat).
#
# Unter Windows liegt gar KEIN Installationsskript mehr bei: seit 2026-08-14
# installiert der Assistent selbst (packaging/k1520emu.iss), und das
# Verzeichnis hier ist nur noch sein Rohstoff — die .cmd-Vorlagen und slim.py
# wandern IN das Setup.  `uv_pins.txt` bleibt Sache der Unix-Installer.
cp "$SELF_DIR/paket_readme.md" "$STAGE/README.md"
cp "$SELF_DIR/slim.py"         "$STAGE/slim.py"

if ist_windows; then
    cp "$SELF_DIR/launcher.cmd"           "$STAGE/launcher.cmd"
    cp "$SELF_DIR/disktool_launcher.cmd"  "$STAGE/disktool_launcher.cmd"
    rmdir "$STAGE/lib" 2>/dev/null || true    # lib/common.sh ist Shell-Sache
else
    cp "$SELF_DIR/uv_pins.txt"              "$STAGE/uv_pins.txt"
    cp "$SELF_DIR/install.sh"               "$STAGE/install.sh"
    cp "$SELF_DIR/launcher.sh"              "$STAGE/launcher.sh"
    cp "$SELF_DIR/disktool_launcher.sh"     "$STAGE/disktool_launcher.sh"
    cp "$SELF_DIR/k1520disktool.desktop.in" "$STAGE/k1520disktool.desktop.in"
    cp "$SELF_DIR/a5120emu.desktop.in"      "$STAGE/a5120emu.desktop.in"
    cp "$SELF_DIR/lib/common.sh"            "$STAGE/lib/common.sh"
    chmod +x "$STAGE/install.sh"
fi
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

ARCHIV=""
if [ "$ARCHIVE" = yes ]; then
    info "Archiv schnüren"
    if ist_windows; then
        # .zip statt .tar.gz: der Explorer packt es mit einem Doppelklick aus.
        # Gepackt wird von packaging/zip_ordner.ps1 — die Git-Bash bringt kein
        # `zip` mit, ihr GNU-tar kann kein zip, und die eingebauten
        # PowerShell-Wege schreiben unter .NET Framework BACKSLASHES in die
        # Eintragsnamen (Begründung ausführlich im Kopf jener Datei).  Sie
        # prüft ihr Ergebnis selbst.
        #
        # Eigene Datei statt einer `-Command`-Zeile: das Escaping durch zwei
        # Ebenen (Bourne-Shell → PowerShell) überlebt kein `-replace '\\','/'`.
        ARCHIV="$NAME.zip"
        powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass \
            -File "$(cygpath -w "$SELF_DIR/zip_ordner.ps1")" \
            -Quelle "$(cygpath -w "$OUT/$NAME")" \
            -Ziel   "$(cygpath -w "$OUT/$ARCHIV")" \
            || die "Archiv nicht erzeugbar"
    else
        ARCHIV="$NAME.tar.gz"
        ( cd "$OUT" && tar czf "$ARCHIV" "$NAME" )
    fi
    ( cd "$OUT" && sha256_of "$ARCHIV" | awk -v n="$ARCHIV" '{print $1"  "n}' > "$ARCHIV.sha256" )
    ok "$OUT/$ARCHIV  ($(du -h "$OUT/$ARCHIV" | awk '{print $1}'))"
    printf "     %s\n" "$(cat "$OUT/$ARCHIV.sha256")"
fi

# ─── 6. Windows-Installationsprogramm ────────────────────────────────────────
#
# Das Setup installiert SELBST — Nachladen, Auspacken, Laufzeitumgebung,
# Schlankmachen, Starter, Rauchtest, Deinstallieren.  Ein PowerShell-Skript ist
# daran nicht mehr beteiligt (Begründung im Kopf von k1520emu.iss).  Das
# Verzeichnis unter dist/ ist nur noch sein Rohstoff.

if [ "$SETUP" = yes ]; then
    if ! ist_windows; then
        die "--setup geht nur unter Windows (Inno Setup)"
    fi
    if ! have iscc; then
        die "iscc nicht gefunden — Inno Setup 6.5 oder neuer installieren (choco install innosetup)"
    fi
    lies_python_pins
    info "Windows-Installationsprogramm bauen (Python $PY_VERSION aus $PY_RELEASE)"
    # Version ohne die git-Zusätze: Inno will etwas, das wie eine Version
    # aussieht, `1.2.3-4-gabc1234-dirty` lehnt es ab.
    _iss_version=$(printf '%s' "$VERSION" | sed 's/^v//; s/[^0-9.].*$//')
    [ -n "$_iss_version" ] || _iss_version=0.0.0
    # Die Adresse des Python-Archivs setzt die .iss aus Fassung und Release
    # selbst zusammen — sie darf hier gar nicht erst als Argument auftauchen:
    # die MSYS-Shell rechnet Argumente, die wie Pfade aussehen, in
    # Windows-Pfade um und macht aus `https://…` `https:\…`.
    iscc //Qp \
         "//DVersion=$_iss_version" \
         "//DPaket=$(cygpath -w "$STAGE")" \
         "//DPyVersion=$PY_VERSION" \
         "//DPyRelease=$PY_RELEASE" \
         "//DPySha256=$PY_SHA" \
         "//DPySize=$PY_SIZE" \
         "//O$(cygpath -w "$OUT")" \
         "$(cygpath -w "$SELF_DIR/k1520emu.iss")" \
        || die "Inno Setup ist fehlgeschlagen"
    _exe=$(ls -1 "$OUT"/*setup.exe 2>/dev/null | head -1)
    [ -n "$_exe" ] || die "Setup wurde nicht erzeugt"
    ( cd "$OUT" && sha256_of "$(basename "$_exe")" \
        | awk -v n="$(basename "$_exe")" '{print $1"  "n}' > "$(basename "$_exe").sha256" )
    ok "$_exe  ($(du -h "$_exe" | awk '{print $1}'))"
fi

printf "\n"
info "Fertig.  Probeinstallation:"
if ist_windows; then
    printf "     %s\n" "$(ls -1 "$OUT"/*setup.exe 2>/dev/null | head -1)"
    printf "     (still:  /VERYSILENT /DIR=... /Daten=... /LOG=setup.log)\n"
else
    printf "     tar xzf %s/%s.tar.gz -C /tmp && /tmp/%s/install.sh --prefix /tmp/k1520emu-test\n" \
        "$OUT" "$NAME" "$NAME"
fi
