#!/usr/bin/env bash
#
# dev.sh — reproduzierbarer Build-&-Test-Workflow für a5120emu.
#
# Motivation: Es gibt ZWEI Build-Verzeichnisse mit GLEICHEN Tool-Namen
#   build/        LOG_LEVEL=3  — Normalbetrieb: Unit-/Integrationstests, GUI, Tools
#   build_trace/  LOG_LEVEL=5  — tiefes TRACE-Logging für boot_trace
# Wer ein Tool/Test aus einem NICHT neu gebauten Dir startet, testet alte
# Objektdateien („stale objects") und wird in die Irre geführt (mehrfach passiert).
#
# GARANTIE dieses Scripts: jeder test/trace/tool-Aufruf baut ZUERST das passende
# Dir (`cmake --build` nutzt CMakes echtes Dependency-Tracking und baut nur
# Geändertes — schnell, wenn nichts zu tun ist) und MELDET, ob etwas neu gebaut
# wurde.  Damit testet man IMMER den aktuellen Quellstand.  Tools/Tests NIEMALS
# direkt aus build*/ aufrufen — immer über dieses Script.
#
# Hinweis: einen verlässlichen READ-ONLY-Frischetest gibt es bei CMake+Make nicht
# (`make -q` ist auf den generierten Makefiles unzuverlässig).  Deshalb prüft
# „sauber?" = „cmake --build ausführen und sehen, ob es etwas zu tun gab".
#
# Benutzung:
#   tools/dev.sh build [trace]        build/ bauen (+ build_trace/ bei 'trace')
#   tools/dev.sh test  [ctest-args]   build/ bauen, dann Regression (ctest)
#                                     OHNE format_integration und format_matrix
#   tools/dev.sh test-all [ctest-args] wie test, ABER inkl. beider Format-Label
#   tools/dev.sh test-format [args]   NUR die langsamen format_integration-Boot-Disk-Tests
#   tools/dev.sh test-matrix [args]   NUR die 88 Format-Matrix-Tests (jedes FORMAT.COM-Menü
#                                     jedes Laufwerkstyps, Leerdiskette, Smoke Spur 0-2)
#   tools/dev.sh test-python [args]   NUR die Python-Ebene (C-ABI + GUI, Label python)
#   tools/dev.sh test-level <ebene>   NUR eine Testebene (unit|debugtools|integration|
#                                     cli|system|python) — entspricht `ctest -L <ebene>`
#   tools/dev.sh trace [boot_trace…]  build_trace/ bauen, dann boot_trace starten
#   tools/dev.sh tool  <name> [args]  build/ bauen, dann build/<name> starten
#                                     (floppy_diag, k1520dbg, kbd_test, boot_trace…)
#   tools/dev.sh check                build/ + build_trace/ bauen + Frische melden
#   tools/dev.sh rebuild              build/ + build_trace/ von Grund auf neu (rm -rf)
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

# Build-Dir → LOG_LEVEL
declare -A LOG_LEVEL=( [build]=3 [build_trace]=5 )
# Build-Dir → CMAKE_BUILD_TYPE.  Ohne Typ baut GCC mit -O0 (keine Optimierung) →
# der Z80-Interpreter läuft ~5-8x zu langsam.  build/ = Release (-O3, Normalbetrieb
# + Tests), build_trace/ = RelWithDebInfo (-O2 -g, schnell UND mit Host-Symbolen
# fürs Trace-Werkzeug).
declare -A BUILD_TYPE=( [build]=Release [build_trace]=RelWithDebInfo )

c_red() { printf '\033[31m%s\033[0m\n' "$*"; }
c_grn() { printf '\033[32m%s\033[0m\n' "$*"; }
c_ylw() { printf '\033[33m%s\033[0m\n' "$*"; }

configure_if_needed() {
    local dir="$1"
    if [ ! -f "$dir/CMakeCache.txt" ]; then
        c_ylw ">> konfiguriere $dir (LOG_LEVEL=${LOG_LEVEL[$dir]}, ${BUILD_TYPE[$dir]})"
        cmake -B "$dir" -DLOG_LEVEL="${LOG_LEVEL[$dir]}" \
              -DCMAKE_BUILD_TYPE="${BUILD_TYPE[$dir]}" >/dev/null
    fi
}

# Baut <dir> über CMakes echtes Dependency-Tracking und meldet aktuell/neu-gebaut.
# Bricht bei Build-Fehler hart ab (zeigt das Log).
build_dir() {
    local dir="$1" log; log="$(mktemp)"
    configure_if_needed "$dir"
    if ! cmake --build "$dir" -j >"$log" 2>&1; then
        cat "$log"; rm -f "$log"; c_red "BUILD FEHLGESCHLAGEN: $dir"; exit 1
    fi
    if grep -qE "Building |Linking |Generating " "$log"; then
        c_ylw ">> $dir: NEU GEBAUT ($(grep -cE 'Linking ' "$log") Targets gelinkt)"
        grep -E "Building CXX|Linking " "$log" | sed 's/^/     /' | tail -8
    else
        c_grn ">> $dir: aktuell (nichts neu zu bauen)"
    fi
    rm -f "$log"
}

cmd="${1:-}"; shift || true
case "$cmd" in
    build)
        build_dir build
        if [ "${1:-}" = "trace" ] || [ "${1:-}" = "all" ]; then build_dir build_trace; fi ;;
    test)
        build_dir build
        # Standard-Regression: die langsamen Format-Läufe NICHT mit ausführen —
        # LABEL format_integration (Boot-Disk-Kette) und format_matrix (88 Menüs).
        # Für nur diese: test-format bzw. test-matrix; für ALLES: test-all.
        c_ylw ">> ctest (build/) [ohne format_integration/format_matrix]"
        ctest --test-dir build --output-on-failure -LE "format_(integration|matrix)" "$@" ;;
    test-all)
        build_dir build
        c_ylw ">> ctest (build/) ALLE inkl. format_integration + format_matrix"
        ctest --test-dir build --output-on-failure "$@" ;;
    test-format)
        build_dir build
        c_ylw ">> ctest (build/) NUR format_integration (langsam)"
        ctest --test-dir build --output-on-failure -L format_integration "$@" ;;
    test-matrix)
        build_dir build
        c_ylw ">> ctest (build/) NUR format_matrix — 88 FORMAT.COM-Menues auf Leerdisketten"
        ctest --test-dir build --output-on-failure -L format_matrix "$@" ;;
    test-python)
        # pytest-Ebene: C-ABI (ctypes gegen libk1520core.so) + PySide6-GUI.
        # Braucht die gebaute Bibliothek — deshalb erst bauen.
        build_dir build
        c_ylw ">> ctest (build/) NUR Python-Tests (Label python)"
        ctest --test-dir build --output-on-failure -L python "$@" ;;
    test-level)
        # Testebenen (Labels, siehe tests/CMakeLists.txt):
        #   unit debugtools integration cli system python  —  quer dazu: fast slow
        lvl="${1:?Ebene fehlt: unit|debugtools|integration|cli|system|python}"; shift
        build_dir build
        c_ylw ">> ctest (build/) NUR Ebene '$lvl'"
        ctest --test-dir build --output-on-failure -L "^$lvl$" "$@" ;;
    trace)
        build_dir build_trace
        c_ylw ">> build_trace/boot_trace $*"; exec build_trace/boot_trace "$@" ;;
    tool)
        name="${1:?Tool-Name fehlt (z.B. floppy_diag, k1520dbg, kbd_test)}"; shift
        build_dir build
        c_ylw ">> build/$name $*"; exec "build/$name" "$@" ;;
    check)
        for d in build build_trace; do [ -d "$d" ] && build_dir "$d" || c_ylw ">> $d: nicht vorhanden"; done ;;
    rebuild)
        c_ylw ">> rm -rf build build_trace + Neubau von Grund auf"
        rm -rf build build_trace; build_dir build; build_dir build_trace ;;
    ''|-h|--help|help)
        sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^#\{0,1\} \{0,1\}//' ;;
    *)
        c_red "unbekanntes Kommando: $cmd"; echo "siehe: tools/dev.sh --help"; exit 2 ;;
esac
