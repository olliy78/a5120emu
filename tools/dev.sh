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
#                                     Ausgabe ist KNAPP (nur die Zusammen-
#                                     fassung); -v bzw. --voll oder
#                                     K1520_TEST_VOLL=1 druckt alles.
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
#   tools/dev.sh win   [ctest-args]   Cross-Bau nach Windows (MinGW) + Tests unter
#                                     wine — lokale Vorprüfung der Portierung
#   tools/dev.sh check                build/ + build_trace/ bauen + Frische melden
#   tools/dev.sh rebuild              build/ + build_trace/ von Grund auf neu (rm -rf)
#
# K1520_JOBS=<n> setzt die Testparallelität (Vorgabe: nproc).
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

# Build-Dir → LOG_LEVEL
declare -A LOG_LEVEL=( [build]=3 [build_trace]=5 [build_win]=3 )
# Build-Dir → CMAKE_BUILD_TYPE.  Ohne Typ baut GCC mit -O0 (keine Optimierung) →
# der Z80-Interpreter läuft ~5-8x zu langsam.  build/ = Release (-O3, Normalbetrieb
# + Tests), build_trace/ = RelWithDebInfo (-O2 -g, schnell UND mit Host-Symbolen
# fürs Trace-Werkzeug).
declare -A BUILD_TYPE=( [build]=Release [build_trace]=RelWithDebInfo [build_win]=Release )

# ─── Windows (Git-Bash/MSYS unter GitHub Actions oder auf einem Windows-Rechner) ──
# Zwei Unterschiede, beide zwingend:
#  * Generator: CMake nähme sonst „Visual Studio 17 2022" — einen MEHRKONFIGURATIONS-
#    Generator.  Der legt die Programme nach build/Release/ statt build/, und ctest
#    verlangt ein `-C Release`.  Damit stimmte kein einziger eingespielter Pfad mehr
#    (build/k1520_test_k2526, build/k1520dbg …).  Ninja ist einkonfigurativ und hält
#    das Layout identisch zu Linux.  Die MSVC-Umgebung (vcvars) muss die aufrufende
#    Shell schon gesetzt haben — der Windows-Workflow tut das.
#  * Programmnamen tragen `.exe`.
WINDOWS=0
case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*) WINDOWS=1 ;;
esac
EXE=""; GENERATOR=()
if [ "$WINDOWS" = 1 ]; then EXE=".exe"; GENERATOR=(-G Ninja); fi

# ─── Testparallelität ────────────────────────────────────────────────────────
# ctest startet JEDEN Testfall als eigenen Prozess (gtest_discover_tests macht aus
# jedem TEST einen ctest-Fall).  Serieller Lauf heißt deshalb: 909 Prozessstarts
# nacheinander.  Messung 2026-08-12: 36 s seriell → 14 s mit -j16; unter wine
# (Cross-Bau) sogar 15 min → 15 s, weil dort jeder Start ~0,5 s kostet.
#
# Voraussetzung ist, dass kein Testfall einen FESTEN Dateinamen unter /tmp
# benutzt — sonst greifen zwei gleichzeitig auf dieselbe Datei zu.  Dafür gibt es
# k1520test::tempPath() (tests/support/temp_path.h); unter Linux fiele ein
# Verstoß nicht auf, unter Windows schlägt er als „Sharing violation" zu.
#
# Die langsamen Formatläufe bleiben bewusst außen vor: sie fahren FORMAT.COM über
# ganze Disketten, sind E/A-gebunden, und slow-tests.yml gibt ihnen ohnehin ein
# eigenes -j mit.
#
# Kernzahl: `nproc` ist coreutils und in der Git-Bash nicht garantiert; Windows
# setzt dafür NUMBER_OF_PROCESSORS in der Umgebung.  Ohne diesen Zwischenschritt
# fiele es dort stillschweigend auf 4 zurück — auf dem heutigen Runner zufällig
# richtig, auf einem größeren Rechner die Hälfte verschenkt.
JOBS="${K1520_JOBS:-$(nproc 2>/dev/null || echo "${NUMBER_OF_PROCESSORS:-4}")}"

# ─── Testprotokoll (JUnit-XML) ───────────────────────────────────────────────
# Jeder ctest-Lauf legt zusätzlich eine Maschinenfassung seines Ergebnisses ab;
# daraus baut `tools/test_report.py` die HTML-Seite, die die CI als Artefakt
# anhängt.  Kostet nichts (ctest schreibt sie beim Laufen mit) und ist auch
# lokal nützlich: nach jedem `tools/dev.sh test` liegt der letzte Lauf da.
#
#     tools/dev.sh test
#     python3 tools/test_report.py build/Testing/junit.xml -o protokoll.html
#
# ACHTUNG, der Pfad ist RELATIV ZUM BUILD-VERZEICHNIS, nicht zum Arbeits-
# verzeichnis — `--output-junit build/Testing/junit.xml` schriebe nach
# `build/build/Testing/…`.  Deshalb steht er hier EINMAL und relativ; damit
# stimmt er für build/ wie für build_win/ (Cross-Bau) ohne Fallunterscheidung.
JUNIT=(--output-junit Testing/junit.xml)

c_red() { printf '\033[31m%s\033[0m\n' "$*"; }
c_grn() { printf '\033[32m%s\033[0m\n' "$*"; }
c_ylw() { printf '\033[33m%s\033[0m\n' "$*"; }

configure_if_needed() {
    local dir="$1" extra=()
    # build_win/ ist der Cross-Bau nach Windows (MinGW + wine, siehe `win`).
    # Die Python-Ebene bleibt dort AUS: sie lädt die Bibliothek per ctypes aus
    # einem LINUX-Python — eine .dll kann sie nicht laden.
    if [ "$dir" = build_win ]; then
        extra=(-DCMAKE_TOOLCHAIN_FILE="$PROJECT_DIR/cmake/toolchain-mingw64.cmake"
               -DBUILD_PYTHON_TESTS=OFF)
    fi
    if [ ! -f "$dir/CMakeCache.txt" ]; then
        c_ylw ">> konfiguriere $dir (LOG_LEVEL=${LOG_LEVEL[$dir]}, ${BUILD_TYPE[$dir]})"
        cmake -B "$dir" "${GENERATOR[@]}" "${extra[@]}" -DLOG_LEVEL="${LOG_LEVEL[$dir]}" \
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
        # `|| true` ist hier NICHT Zierrat: läuft der Bau NUR neu durch CMakes
        # Generate-Schritt (Log enthält „Generating", aber keine einzige
        # „Building CXX"/„Linking"-Zeile), findet dieses grep nichts und liefert
        # 1 — und mit `set -euo pipefail` (Zeile 42) reisst das die ganze
        # Funktion mit, BEVOR ctest überhaupt startet.  Nach aussen sieht das
        # aus wie eine rote Regression: der pre-push-Hook bricht ab und meldet
        # „die Regressionstests sind nicht grün", obwohl kein Test gelaufen ist.
        # Genau so am 2026-08-16 einen Push abgelehnt.  Ausgelöst wird der Fall
        # von jedem `file(GLOB … CONFIGURE_DEPENDS)` (tests/python, tests/cli),
        # sobald dort eine Datei dazu- oder wegkommt: der nächste Bau ist dann
        # ein reiner Konfigurationslauf.
        grep -E "Building CXX|Linking " "$log" | sed 's/^/     /' | tail -8 || true
    else
        c_grn ">> $dir: aktuell (nichts neu zu bauen)"
    fi
    rm -f "$log"
}

# ─── Testausgabe: knapp statt vollstaendig ───────────────────────────────────
# Ein GRUENER Volllauf schreibt 2183 Zeilen / 205 KB — je Testfall eine
# „Start"- und eine „Passed"-Zeile.  Am Terminal ist das Fortschrittsanzeige;
# in einem Protokoll, einer CI-Ausgabe oder im Kontext eines Agenten, der die
# Ausgabe LIEST, ist es reines Rauschen: die Information ist „1081/1081 gruen"
# und passt in eine Zeile.  Gemessen 2026-08-18: 205 KB entsprechen rund
# 51 000 Modell-Token, die anschliessend bei JEDER weiteren Anfrage derselben
# Sitzung erneut gelesen werden — der Kurzmodus spart davon rund 99,9 %.
#
# Deshalb laeuft ctest in eine Logdatei und es wird nur die Zusammenfassung
# gedruckt.  Bei einem Fehlschlag kommt die volle Ausgabe der ROTEN Faelle
# dazu (gefiltert werden ausschliesslich die beiden Rauschzeilenarten) — ein
# roter Lauf verliert also nichts, was zur Fehlersuche taugt.
#
#   -v | --voll   (oder K1520_TEST_VOLL=1)   → alte, vollstaendige Ausgabe
#
# Der Volltext liegt IMMER in <builddir>/Testing/ctest.log, die Maschinen-
# fassung unveraendert daneben in junit.xml.
VOLL="${K1520_TEST_VOLL:-0}"

# Zwei Zeilenarten sind das Rauschen: der Startvermerk und der gruene Fall.
# Alles andere (Fehlerausgaben, die FAILED-Liste, die Zusammenfassung) bleibt.
RAUSCHEN='^ *Start +[0-9]+:|Passed +[0-9.]+ sec *$'

run_ctest() {
    local dir="$1"; shift
    local log="$dir/Testing/ctest.log" rc=0
    if [ "$VOLL" = 1 ]; then
        ctest --test-dir "$dir" --output-on-failure "${JUNIT[@]}" "$@"
        return $?
    fi
    mkdir -p "$dir/Testing"
    ctest --test-dir "$dir" --output-on-failure "${JUNIT[@]}" "$@" >"$log" 2>&1 || rc=$?
    # Eine LEERE Auswahl (`-R` ohne Treffer) laesst ctest mit 0 enden und druckt
    # nur „No tests were found!!!".  Ohne diese Pruefung meldete der Kurzmodus
    # dafuer „gruen" — ein Fehlgruen, das genau dann zuschlaegt, wenn man sich
    # im Testnamen vertippt hat.  Kein Zusammenfassungssatz ⇒ kein gruener Lauf.
    if [ "$rc" = 0 ] && grep -qE '^[0-9]+% tests passed' "$log"; then
        grep -E '^[0-9]+% tests passed|^Total Test time' "$log" | sed 's/^/   /'
        c_grn ">> gruen  (Volltext: $log)"
    else
        # Bei Rot: alles ausser dem Rauschen — gedeckelt, damit ein Lauf, in dem
        # ALLES rot ist, nicht doch wieder das ganze Log ausschuettet.
        grep -vE "$RAUSCHEN" "$log" | head -250 | sed 's/^/   /' || true
        c_red ">> ROT (Volltext: $log) — einzelner Fall: tools/dev.sh test -R <Name> -v"
        [ "$rc" = 0 ] && rc=1
    fi
    return $rc
}

cmd="${1:-}"; shift || true

# `-v`/`--voll` gilt NUR fuer die Testkommandos — bei `trace`/`tool` waere es
# ein Argument des aufgerufenen Programms und darf nicht verschluckt werden.
case "$cmd" in
    test*)
        _args=()
        for _a in "$@"; do
            case "$_a" in
                -v|--voll) VOLL=1 ;;
                *)         _args+=("$_a") ;;
            esac
        done
        set -- ${_args[@]+"${_args[@]}"} ;;
esac
case "$cmd" in
    build)
        build_dir build
        if [ "${1:-}" = "trace" ] || [ "${1:-}" = "all" ]; then build_dir build_trace; fi ;;
    test)
        build_dir build
        # Standard-Regression: die langsamen Format-Läufe NICHT mit ausführen —
        # LABEL format_integration (Boot-Disk-Kette) und format_matrix (88 Menüs).
        # Für nur diese: test-format bzw. test-matrix; für ALLES: test-all.
        c_ylw ">> ctest (build/, -j$JOBS) [ohne format_integration/format_matrix]"
        run_ctest build -LE "format_(integration|matrix)" \
              -j"$JOBS" "$@" ;;
    test-all)
        build_dir build
        c_ylw ">> ctest (build/) ALLE inkl. format_integration + format_matrix"
        run_ctest build "$@" ;;
    test-format)
        build_dir build
        c_ylw ">> ctest (build/) NUR format_integration (langsam)"
        run_ctest build -L format_integration "$@" ;;
    test-matrix)
        build_dir build
        c_ylw ">> ctest (build/) NUR format_matrix — 88 FORMAT.COM-Menues auf Leerdisketten"
        run_ctest build -L format_matrix "$@" ;;
    test-python)
        # pytest-Ebene: C-ABI (ctypes gegen libk1520core.so) + PySide6-GUI.
        # Braucht die gebaute Bibliothek — deshalb erst bauen.
        build_dir build
        c_ylw ">> ctest (build/, -j$JOBS) NUR Python-Tests (Label python)"
        run_ctest build -L python -j"$JOBS" "$@" ;;
    test-level)
        # Testebenen (Labels, siehe tests/CMakeLists.txt):
        #   unit debugtools integration cli system python  —  quer dazu: fast slow
        lvl="${1:?Ebene fehlt: unit|debugtools|integration|cli|system|python}"; shift
        build_dir build
        c_ylw ">> ctest (build/, -j$JOBS) NUR Ebene '$lvl'"
        run_ctest build -L "^$lvl$" -j"$JOBS" "$@" ;;
    trace)
        build_dir build_trace
        c_ylw ">> build_trace/boot_trace$EXE $*"; exec "build_trace/boot_trace$EXE" "$@" ;;
    tool)
        name="${1:?Tool-Name fehlt (z.B. floppy_diag, k1520dbg, kbd_test)}"; shift
        build_dir build
        c_ylw ">> build/$name$EXE $*"; exec "build/$name$EXE" "$@" ;;
    win)
        # LOKALE Windows-Vorprüfung: nach Windows cross-übersetzen (MinGW-w64) und
        # die Tests unter wine laufen lassen.  Findet Portierungsfehler in Sekunden
        # statt in 10-Minuten-CI-Runden — ersetzt sie aber NICHT: MinGW ist GCC,
        # die verbindliche Prüfung ist .github/workflows/windows-ci.yml
        # (Begründung im Kopf von cmake/toolchain-mingw64.cmake).
        build_dir build_win
        # Unter wine ist -j der Unterschied zwischen 15 Minuten und 15 Sekunden
        # (jeder Prozessstart ~0,5 s) — Begründung oben bei JOBS.
        c_ylw ">> ctest (build_win/, unter wine, -j$JOBS) [ohne format_integration/format_matrix]"
        WINEDEBUG="${WINEDEBUG:--all}" \
        run_ctest build_win -LE "format_(integration|matrix)" \
              -j"$JOBS" "$@" ;;
    check)
        for d in build build_trace; do [ -d "$d" ] && build_dir "$d" || c_ylw ">> $d: nicht vorhanden"; done ;;
    rebuild)
        c_ylw ">> rm -rf build build_trace + Neubau von Grund auf"
        rm -rf build build_trace build_win; build_dir build; build_dir build_trace ;;
    ''|-h|--help|help)
        sed -n '2,42p' "${BASH_SOURCE[0]}" | sed 's/^#\{0,1\} \{0,1\}//' ;;
    *)
        c_red "unbekanntes Kommando: $cmd"; echo "siehe: tools/dev.sh --help"; exit 2 ;;
esac
