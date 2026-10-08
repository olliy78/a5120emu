# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## One emulator: the modular K1520 core (`core/`)

**K1520 core (`core/`)** — a hardware-accurate, transaction-level emulation of the K1520 bus and its plug-in cards. **No BIOS traps**: real Z80 code (boot ROM, BIOS, OS) runs natively, so any K1520 OS can boot. Builds `libk1520core.so` (a stable C-ABI) consumed by a **Python/PySide6 GUI** (`app/`). `doc/K1520_architecture.md` and `doc/design/*.md` are the authoritative design references.

> **Removed (2026-08-07, branch `rework_testsystem`):** the original monolithic emulator under
> `src/` (CP/M-BIOS-HALT-trap mechanism, targets `a5120emu` / `cparun` / `a5120emu_test`) and its
> hand-rolled test harness `tests/test_main.cpp`. Nothing in `core/`, `tools/` or `app/` depended
> on it. `README.md` still describes that old emulator — its memory map, boot process and disk
> format are CP/M-specific and do **not** apply to the core; treat it as historical until rewritten.
> The standalone CP/M runner in `cparun/` is an independent sub-project with its own copies of
> `z80/memory/cpm_bdos` and is unaffected.

## Build & test

> **ALWAYS go through `tools/dev.sh` — never run a binary straight from `build*/`.**
> There are two build dirs with the SAME tool names (`build/` LOG_LEVEL=3,
> `build_trace/` LOG_LEVEL=5). Running a tool/test from a dir you forgot to rebuild
> tests **stale objects** and has repeatedly sent us down false trails (e.g. a
> "working" `dir` listing that was leftover from a reverted experiment). `dev.sh`
> rebuilds the right dir first (CMake's real dependency tracking — fast when nothing
> changed) and reports `aktuell` vs `NEU GEBAUT`, so you always test current source.
> There is no reliable read-only freshness check on CMake's Makefiles (`make -q` lies);
> "is it clean?" therefore means "run `cmake --build` and see if it had work to do".

```sh
tools/dev.sh test [ctest-args]   # build build/, then ctest (the default; incl. Python layer)
tools/dev.sh test -R K2526       #   one card's tests by name regex
tools/dev.sh build [trace]       # just build build/ (+ build_trace/ with 'trace')
tools/dev.sh trace <boot_trace-args>   # build build_trace/, then run boot_trace
tools/dev.sh tool <name> [args]  # build build/, then run build/<name> (floppy_diag, k1520dbg, kbd_test…)
tools/dev.sh test-python         # only the pytest layer (C-ABI + GUI, label "python")
tools/dev.sh test-level unit     # one test level: unit|debugtools|integration|cli|system|python
tools/dev.sh test-oracle         # Z8000-Kern gegen MAME (build_oracle/, lädt MAME beim Konfigurieren)
tools/dev.sh win [ctest-args]    # Cross-Bau nach WINDOWS (MinGW-w64) + Tests unter wine
tools/dev.sh check               # build both dirs + report freshness
tools/dev.sh rebuild             # rm -rf build build_trace, then build from scratch
```

> **Läufe über ~60 s gehören in den HINTERGRUND** (`Bash` mit `run_in_background: true`),
> nicht in den Vordergrund. Betrifft `test-format` (~60 s), `test-matrix` (~160 s),
> `test-all`, `win` und jeden längeren `boot_trace`-/`format_all.py`-Lauf; `tools/dev.sh
> test` läuft in ~34 s und darf im Vordergrund bleiben. Drei Gründe, und der dritte ist
> der eigentliche: ein Vordergrundlauf **blockiert die Sitzung** für die ganze Dauer; er
> zwingt zu einer Zeitschätzung, die bei einem Fehlschlag zu kurz ist; und die Rückmeldung
> kommt bei einem Hintergrundlauf **von selbst**, sobald der Prozess endet.
> **Nicht pollen** — kein `sleep`+Nachsehen im Sekundentakt, das kostet je Blick einen
> vollen Kontextdurchlauf. Weiterarbeiten und die Benachrichtigung abwarten.
> Ausgabe dabei in eine Datei lenken und nur das Ergebnis lesen; `tools/dev.sh` tut das
> seit 2026-08-19 ohnehin selbst (s. u.).

> **Die Testausgabe ist KNAPP — das ist Absicht.** Ein grüner Volllauf meldet fünf Zeilen
> statt 2183 (204 740 B → 265 B; die 1083 „Start"- und 1083 „Passed"-Zeilen sind
> Fortschrittsanzeige, keine Information). Bei ROT kommt die volle Ausgabe der roten Fälle
> samt `FAILED`-Liste — ein Fehlschlag verliert nichts. Volltext immer in
> `<builddir>/Testing/ctest.log`, Maschinenfassung in `junit.xml`; die alte Ausgabe über
> `-v`/`--voll` bzw. `K1520_TEST_VOLL=1`. **Nicht rückgängig machen und nicht umgehen**
> (kein `ctest` von Hand, um „mehr zu sehen"): die 205 KB entsprechen rund 51 000
> Modell-Token, die anschliessend bei JEDER weiteren Anfrage derselben Sitzung erneut
> gelesen werden. Wer mehr sehen will, nimmt `-v` **mit `-R <Name>`** — einen Fall, nicht
> 1083.

> **Windows-Portierung (2026-08-11).** Der Kern baut mit MSVC und fährt dort die volle
> Regression (`.github/workflows/windows-ci.yml`). Vier Dinge tragen das:
> **`core/api/k1520_export.h`** (`K1520_API` vor jeder C-ABI-Funktion — ohne das
> exportiert eine MSVC-DLL **gar nichts** und `ctypes` findet keine Funktion),
> der MSVC-Zweig im `CMakeLists.txt` (`/utf-8` — die Quellen sind voller Umlaute —,
> `/permissive-`, `/bigobj`, statische CRT nur hinter `-DK1520_MSVC_STATIC_CRT=ON`,
> weil GoogleTest die dynamische erwartet), **`core/util/os_compat.h`** (die vier
> POSIX-Reste `getpid`/`isatty`/`setenv`/`unsetenv` an EINER Stelle) und
> **`.gitattributes` mit `* -text`** (Git unter Windows wandelt sonst LF→CRLF auch in
> Dateien, die es fälschlich für Text hält — ein 0x0D in einer `.hfe` verschiebt eine
> ganze Spur und sieht wie ein Emulatorfehler aus).
> `tools/dev.sh win` ist die **lokale Vorprüfung** (MinGW-w64 + wine,
> `cmake/toolchain-mingw64.cmake`); sie ersetzt den CI-Lauf nicht — MinGW ist GCC und
> exportiert wie unter Linux per Vorgabe alles. Voller Stand: `doc/ci_pipeline.md` §4.4,
> `doc/design/13_distribution.md` §6.1. Die **Paketierung** steht ebenfalls: das
> Inno-Setup installiert selbst (s. u.), `release.yml` hat einen Windows-Job.

**The test system is documented in `tests/README.md`** (run it, add a test, shared helpers),
`doc/design/12_testing.md` (why it is cut this way) and `tests/fixtures/README.md` (which test
disk is which). The essentials for editing here:

- Registration lives entirely in `tests/`, one `CMakeLists.txt` per level; a test is one line via
  `k1520_add_test()` (`tests/cmake/K1520AddTest.cmake`). Binaries stay in `build/`, so
  `./build/k1520_test_k2526 --gtest_filter=…` keeps working.
- Level = directory = ctest label: `unit/{primitives,bus,cards,peripherals,util}`, `debugtools/`
  (the header-only `tools/*.h` pieces), `integration/`, `cli/`, `system/`, `python/`; crosswise
  `fast`/`slow`. The slow ones keep the historical label `format_integration` that `dev.sh` filters on.
- Integration/system tests use `tests/support/` (`k1520test::`): `vramText()`, `runSmallUntil()`,
  `typeString()`, `TempDisk`, … **Never mount a committed disk directly** — the emulator opens it
  r/w; `TempDisk` makes the copy. And keep the batch size: 5 000 cycles whenever the keyboard is
  involved (K7637 = 9600 baud + timer ISR), 100 000 otherwise.

> **Trap when adding a test:** `gtest_discover_tests(... PROPERTIES LABELS "a;b")` silently keeps
> only the FIRST label — the list is flattened while being passed through. `k1520_add_test()`
> escapes the semicolons for you; do not bypass it.

**Two safety nets: a `pre-push` hook locally, GitHub Actions on the server.**
`.githooks/pre-push` runs `tools/dev.sh test` and refuses the push if anything is red (~12 s).
Activate it once per working copy with `git config core.hooksPath .githooks`; bypass a single
push with `git push --no-verify`. The slow `format_integration` round is deliberately NOT in the
hook — run `tools/dev.sh test-format` before merging to main.

The workflows in `.github/workflows/` **drive the same `tools/dev.sh` commands** — never raw
cmake/ctest, otherwise CI tests something else than the developer does. **Everything is
triggered by hand** (`workflow_dispatch`) — nothing runs on push, on a PR, or on a schedule; the
one exception is a pushed `v*` tag, which builds the release package. Bedienung, nötige
GitHub-Einstellungen und Fehlersuche: **`doc/ci_pipeline.md`**.

| Workflow | Auslöser | Was |
|----------|----------|-----|
| `ci.yml` | nur von Hand | `tools/dev.sh test` auf `ubuntu-latest` (inkl. Python-Ebene; `venv/` wird angelegt, damit CMake sie registriert) |
| `slow-tests.yml` | nur von Hand | `test-format` und/oder `test-matrix` |
| `release.yml` | von Hand **oder** Tag `v*` | `packaging/build_payload.sh` auf **ubuntu-22.04** (glibc-Baseline, §7 des Verteilungsentwurfs), Rauchtest (Bibliothek laden, `k1520_version`, `--paths`, kein Baurechner-Pfad im Binärabbild), Asset am Release-**Entwurf** |
| `windows-ci.yml` | nur von Hand | **Windows-Gegenprobe**: MSVC auf `windows-latest`, dieselbe `tools/dev.sh test`-Runde wie Linux (vcvars über `vswhere` → `$GITHUB_ENV`, Generator **Ninja** statt des mehrkonfigurativen VS-Generators), danach `dumpbin /exports` auf beide DLLs |

Anstoßen: `gh workflow run ci.yml --ref main` (oder Actions → Workflow → *Run workflow*).

After any experiment that touched build dirs (sanitizer builds, `-DLOG_LEVEL=…`,
interrupted builds), run `tools/dev.sh rebuild` to be certain. Raw commands still work
(`cmake -B build && cmake --build build -j`; `ctest --test-dir build`;
`./build/k1520_test_k2526 --gtest_filter='*ZVE2*'`) but only `dev.sh` guarantees no stale binary.

`LOG_LEVEL` is the compile-time **ceiling** (0=off … 5=trace) baked in via `add_compile_definitions`; call sites above it are removed (zero overhead), so build with `-DLOG_LEVEL=5` (the `build_trace/` dir) to make every site available. **The actual output level is now runtime-controlled and gated** (`core/logger.{h,cpp}`): a runtime *base level* (`Logger::setBaseLevel`, default = ceiling), plus dynamic *gates* that raise the effective level only inside a PC range (`addPCGate`) or cycle window (`addCycleGate`), plus a RAII scoped boost (`K1520_LOG_BOOST(Level::TRACE)` at a function top). The emit macros check `Logger::shouldLog()` before formatting, so disabled logs cost ~one atomic load. The run loop (`a5120.cpp`) calls `Logger::update(cycle, zve1pc, zve2pc)` per instruction (cheap early-out when no gates). This replaces "build at level 5 → multi-GB log → grep" with "run quietly, boost only the window of interest". GoogleTest is fetched on first configure (network required). The `formating-disks` working branch is fully green (583/583 ctest, 2026-07-05); the tests that were formerly known-failing (FormatParser CPA780 / K3526 / K7024 / CTC) now pass here. If you branch off an older baseline and see those red, confirm against the baseline before treating them as a regression.

## Python GUI

The GUI loads `libk1520core.so` through `ctypes`. **All paths are resolved in one place —
`app/paths.py`** (`core_library()`, `formats_file()`, `bundled_disks_dir()`,
`user_disks_dir()`, `config_dir()`, `describe()`), in the order **environment variable
(`K1520_HOME`/`K1520_LIB`/`K1520_FORMATS`/`K1520_DISKS`) → installation layout
(`<root>/{bin,share}`) → source tree (`<repo>/{build,data,disks}`)**. Don't hard-wire repo
paths in GUI code, and don't reintroduce `<repo>/build` lookups — that breaks a packaged
installation. `python3 app/main.py --paths` prints the whole resolution (works without Qt
and without the core lib; first thing to ask when something isn't found). Needs the shared
lib built:

```sh
python3 -m venv venv && source venv/bin/activate && pip install -r requirements.txt
bash run_a5120emu.sh      # sets LD_LIBRARY_PATH=build and runs app/main.py
bash run_k8915emu.sh      # the same with --machine k8915 (K8915 Emulator)
```

> **Vier Programme, eine Oberfläche** (2026-09-30, AP-UI1; drittes Programm 2026-10-02, AP-P5d, viertes 2026-10-04, AP-5a,
> `doc/design/18_k8915emu_oberflaeche.md`, `doc/design/11_python_app.md` §10.9):
> **A5120 Emulator** (`a5120emu`), **K8915 Emulator** (`k8915emu`, `app/main.py
> --machine k8915`) und **PRG710 Emulator** (`prg710emu`, `--machine prg710`, Modellwahl PRG 710 /
> 710-1 über `general.model`, `prg710emu.yaml`, `data/default_config_prg710.yaml`) und **PC1715 Emulator** (`pc1715emu`, `--machine pc1715`,
> Modellwahl = Bildschirm K7222/K7221 über `general.model`, **PC 1715W** als drittes Modell (3,9936 MHz über
> `Programmprofil.modell_takte`, kein K7221 dort), `pc1715emu.yaml`, `data/default_config_pc1715.yaml`, Bildschirmtastatur
> `app/ui/keyboard_pc1715.py`: physische Matrixtasten, SHIFT/CTRL/REP gemerkt und vor der Taste gedrückt,
> LOCK/SI/SO rasten im ROM).  Alles Maschinenspezifische steht im **Programmprofil
> `app/profil.py`** (Titel, Konfig-/Vorgabedatei, Takt, Tastatur K7637/K7672,
> Frontplatte, eigene Aktionen wie `nmi` via `actions.NUR_FUER`) — kein
> `if machine == …` in der Oberfläche.  Konfiguration je Programm im selben
> Ordner: **`a5120emu.yaml`** / **`k8915emu.yaml`**, Vorgaben
> **`data/default_config_a5120.yaml`** / **`default_config_k8915.yaml`**; eine alte
> `config.yaml` zieht der A5120 beim Start EINMAL nach `a5120emu.yaml` um
> (`config_io.konfig_umziehen`).  Die Starter der Installation sind EINE Vorlage, der
> NAME (`k8915emu*`) wählt das Profil.  Wächter: `py_k8915emu_gui`.

> **Die Oberfläche des Emulators ist wie die des DiskTool geschnitten**
> (2026-09-13, `doc/design/11_python_app.md` §10): **jede Bedienung ist eine
> `QAction` in `app/ui/actions.py`** (Menü und Leiste zeigen dieselbe), die
> **Symbolleiste wird aus einer Namensliste gebaut** (`window.toolbar` in der
> Konfiguration, einrichtbar über `app/ui/toolbar_config.py`), die
> **Statuszeile zeigt Zustand statt Zähler** (`app/ui/status_bar.py`:
> EINGESTELLTER Takt aus `app/takt.py` — der gemessene schwankt und steht nur im
> Tooltip —, Laufwerksleuchte, Abbildname, R/O ↔ R/W; Cycles/FPS sind weg), und das **Handbuch**
> liegt als `app/help/handbuch.md` (Fenster: `app/ui_help.py`, von beiden
> Programmen benutzt, ebenso `app/ui_icons.py`).  Vier Dinge, die man nicht
> aufweichen darf:
> - **Kein Tastenkürzel ohne `Strg+Umschalt`** (Ausnahme `F11`): Qt wertet
>   Kürzel VOR dem Widget aus, und `^S`/`^P`/`^C`/F-Tasten gehören dem
>   emulierten Rechner.  Wächter `test_no_shortcut_steals_a_key_from_the_emulated_machine`.
> - **Die Kürzeltabelle des Handbuchs ist ein Vertrag** — zwei Wächter prüfen
>   beide Richtungen.
> - **`QToolBar.clear()` gibt die Hülle eines `toggleViewAction()` frei**
>   (C++-Objekt überlebt).  Deshalb einzeln `removeAction()` und die
>   Kastenschalter in `_aktion()` FRISCH beim Kasten holen — sonst stirbt der
>   zweite Leistenaufbau, also genau der aus einer gespeicherten Konfiguration.
> - **Ein unbekannter Leisten-Name wird übergangen**, nicht als Fehler behandelt
>   (ältere Konfigurationen).
>
> **Das Diskettenformat wird ERFRAGT, nicht eingestellt** (2026-09-13).  Das
> dauerhafte Auswahlfeld im Laufwerkskasten ist weg; an seiner Stelle steht hinter
> *Format:* das **erkannte** Format (`k1520_disk_detected_format` →
> `A5120Machine::detectedFormatName`, Architektur §8.6.9) oder `unbekannt`.
> Gefragt wird nur noch bei `.img` (`app/ui/format_dialog.py`) — und dort auch beim
> Anlegen.  Vier Dinge dazu:
> - **`.hfe`/`.dmk` bekommen NIE einen Formatdialog**: die Geometrie steht in der
>   Datei.  Der Kern verlangt beim Mounten trotzdem einen gültigen Katalognamen und
>   benutzt ihn als Platzhalter — die Oberfläche gibt dafür den Laufwerksstandard.
> - **`unbekannt` sperrt den `.img`-Export.** Zweiter Grund neben
>   `rawCompatible()`; ohne Befund wäre die Sektorreihenfolge geraten, und ein
>   geratenes Abbild sieht heil aus und ist es nicht.
> - **Gleich gute Treffer mit demselben Sektorraum sind KEINE Mehrdeutigkeit**
>   (`cpa640` ≡ `k5601_16x256`) — sonst hiesse jede 16×256-Diskette „unbekannt".
>   Verglichen wird die aufgelöste Spurbelegung, nicht die Bereichsliste.
> - **Die Statuszeilen-Leuchte folgt dem ZUGRIFF, nicht dem Inhalt**: ein
>   angesprochenes LEERES Laufwerk leuchtet rot, wie am echten Gerät.  Wächter
>   `test_an_access_to_an_empty_drive_turns_the_lamp_red`.
>
> Der Knopf heisst **„Leere Diskette"** (nicht „Neue"): was entsteht, ist
> unformatiert und muss vom Gastsystem erst formatiert werden.
>
> **Beide Oberflächen starten einander — Menü „Werkzeuge"** (2026-09-14,
> `doc/design/11_python_app.md` §10.8).  Das Wie steht an EINER Stelle
> (`app/programme.py`), die Pfade der Konsolenwerkzeuge wie alles andere in
> `app/paths.py` (`tools_dir`/`debugger`/`disktool_cli`/`doc_file`).  Gestartet
> wird der **eigene Interpreter mit dem Skript des anderen Programms**, nicht der
> Starter aus `bin/` (den gibt es nur in einer Installation), abgekoppelt und im
> Diskettenordner.  Der Emulator öffnet zusätzlich eine **Werkzeugkonsole**: eine
> bei jedem Öffnen neu erzeugte Startdatei in der Benutzerkonfiguration
> (`werkzeugkonsole.sh`/`.cmd`), die `PATH`/`K1520_HOME`/`K1520_FORMATS` setzt,
> einen abtippbaren Beispielaufruf druckt und auf einer interaktiven Shell endet.
> Sie bleibt unter Windows **ASCII** (`cmd.exe` liest Batchdateien in der
> Kodepage).  Kein Tastenkürzel für die drei Einträge — die Kürzeltabelle des
> Handbuchs ist ein Vertrag.  Wächter: `py_programme`.
>
> **Der Auslieferungszustand ist eine DATEI, kein Programmtext** (2026-09-14,
> `doc/design/11_python_app.md` §10.7): `data/default_config_a5120.yaml` bzw.
> `…_k8915.yaml` (in der Installation `share/k1520emu/`) hat denselben Aufbau wie
> die `a5120emu.yaml`/`k8915emu.yaml` des Anwenders und wird an zwei Stellen
> gebraucht — beim ERSTEN Start, solange es noch keine Konfiguration gibt, und bei *Ansicht ▸ Standard zurücksetzen*, das
> sie nach Rückfrage anwendet und **sofort** zurückschreibt.  Aufgelöst in
> `app/paths.py::default_config_file()`, gelesen in
> `app/config_io.py::standard_konfiguration()`.  Vier Dinge dazu:
> - **Ein FEHLENDER Abschnitt heisst „nicht anfassen", ein leerer „leeren"** —
>   `_apply_config` mountet nur bei vorhandenem `disks`.  Die Vorgabe trägt
>   keins: Diskettenpfade sind rechnerspezifisch, und das Zurücksetzen der
>   *Ansicht* räumt die Maschine nicht leer.
> - **Kein `window.geometry` in der Vorgabe** (das ist die Bildschirmposition
>   des Baurechners); `width`/`height`/`dock_state` reichen.
> - **Der Benutzerordner ist KEIN Fundort** — anders als beim Formatkatalog;
>   sonst setzte man auf das zurück, was gerade überschrieben werden soll.
>   Umbiegbar über `K1520_DEFAULT_CONFIG`.
> - **Ohne die Datei läuft alles weiter** (`{}` → eingebaute Vorgaben); sie ist
>   eine Beigabe, keine Voraussetzung.

**Tests for this side live in `tests/python/`** (pytest, registered with ctest under label
`python`, one ctest case per module: `py_c_api`, `py_binding`, `py_boot_smoke`, …). They cover
the two things C++ tests cannot reach: the **C-ABI** (`core/api/k1520_api.h` ↔ `libk1520core.so`
↔ the ctypes declarations in `app/core_binding/k1520.py` — a signature change breaks *silently*
otherwise; `test_c_api.py` compares all three mechanically) and the **GUI** (PySide6 headless via
`QT_QPA_PLATFORM=offscreen`; widget wiring only — a `QOpenGLWidget` has no FBO offscreen, so
pixels are not testable there). Install the test deps with
`venv/bin/python3 -m pip install -r requirements-dev.txt`; without them CMake skips registering
the layer and says so. Details + limits: `tests/python/README.md`.

> **Die Python-Ebene muss OHNE `greaseweazle` laufen** (2026-08-18). Das Paket ist eine
> freiwillige Abhängigkeit und liegt nicht auf PyPI — **in der CI ist es nie installiert**,
> auf dem Entwicklungsrechner meistens schon. Genau daran lagen drei rote Tests auf `main`,
> die lokal grün waren (`py_gw_gui` lief 300 s in den Zeitüberlauf, `py_gw_physical` und
> `py_physical_cli` fielen um). Drei Festlegungen halten das jetzt zusammen:
> **(1) `bitarray` steht in `requirements-dev.txt`** — `app/gw` rechnet damit, und ohne
> `greaseweazle` käme es sonst nicht mit; fehlt es, stirbt der Arbeitsfaden still und die
> wartenden Leser laufen in ihre Frist. **(2) Die Verfügbarkeitsprüfung gehört an die
> BEDIENWEGE und an `PhysicalSession.start`, nicht dazwischen** — `MainWindow.open_physical`
> und `physical_cli.main()` prüfen nicht mehr selbst (sie werden mit einer Ersatzsitzung
> gerufen); wer eine Ersatzsitzung einsetzt, ersetzt auch die Verfügbarkeit
> (`hosttools_gelten_als_vorhanden`). **(3) Ein unerwartetes modales Fenster scheitert
> sofort** statt zu blockieren (`kein_unerwartetes_meldungsfenster` in `conftest.py`) —
> sonst wird aus einem Fehlschlag ein Hänger ohne Begründung.
> **Das Paket in der CI mitzuinstallieren ist NICHT die Lösung**: es brächte keinen
> zusätzlichen Testfall (hinter `verfuegbar()` steht in der zweiten Zeile
> `util.usb_open` — alles dahinter braucht Hardware; mit und ohne Paket bleiben
> dieselben 8 Fälle übersprungen) und verdeckte die Lage des Anwenders. Wächter ist
> stattdessen `tests/python/test_gw_ohne_paket.py`: er **blendet den Import aus** und
> schlägt deshalb auch auf einem Entwicklungsrechner an, auf dem das Paket liegt.

## Verteilbares Paket (`packaging/`)

`packaging/build_payload.sh` schnürt aus dem Baum ein Anwenderpaket (~2 MB); das `install.sh`
darin holt sich Python und Qt in ein venv **innerhalb der Installation** — benutzerlokal, ohne
Administratorrechte. Unter Windows installiert das Inno-Setup (`packaging/k1520emu.iss`)
selbst; ein `install.ps1` gibt es nicht mehr. Bedienung: `packaging/README.md`, Entwurf und
Begründungen: **`doc/design/13_distribution.md`**.

> **Bevor du hier etwas änderst: `doc/merkposten/paketierung.md` lesen.** Dort stehen die
> elf Festlegungen, die man nicht aufweichen darf, jeweils mit dem Wächter, der sie hält.
> Die vier, bei denen es am teuersten wird:
> - **`--uninstall` löscht in einem ERFRAGTEN Ziel.** Zwei Riegel in `install.sh`: Ziel darf
>   nur leer oder bereits von uns belegt sein, und gelöscht wird nur das Inventar aus dem
>   Ausweis (`.k1520emu-installation`). Ohne das löschte die Antwort „`~`" beim
>   Deinstallieren das Heimatverzeichnis — belegt, nicht theoretisch.
> - **Alles Nachladbare läuft im Assistenten in `PrepareToInstall`, also VOR dem Kopieren**
>   (eine Ausnahme in `ssPostInstall` räumt nichts zurück und hinterlässt eine halbe
>   Installation) — und **kein PowerShell** im Installationsweg.
> - **Release-Bauten setzen `-DK1520_FORMATS_DEFAULT=`**, sonst trägt jede ausgelieferte
>   Bibliothek den absoluten Pfad des Baurechners als Suchkandidaten (Wächter `py_packaging`).
> - **Produkt = `k1520emu`, Programm = `a5120emu`**; Arbeitsdisketten liegen im
>   **Dokumentenordner**, nicht in der Installation (der Autosave schreibt in die gemountete
>   Datei zurück).

## K1520 core architecture (the part that needs multiple files to grasp)

Strict layering — each layer only knows the one below (see `doc/K1520_architecture.md` §5):

```
machines/a5120  →  wires cards onto the bus, drives the run loop, exposes the machine API
cards/          →  K2526 (ZRE/CPU), K3526 (RAM), K7024 (screen), K8025 (serial), K5122 (floppy)
primitives/     →  Z80, Z80PIO, Z80CTC, Z80SIO, EPROM/RAM devices  (generic chips)
bus/            →  K1520Bus (memory/IO dispatch, INT daisy-chain, BUSRQ, NMI, MEMDI) + Koppelbus (signal router)
```

> **Floppy controller — single formatagnostic K5122.** Slot 2 (`core/cards/k5122/`)
> is a formatagnostic *read-head-over-rotating-track* controller on the `core/peripherals/floppy_drive/`
> stack (TrackImage / TrackCodec / BitCodec / **DiskMedium + ImageCodec** / DiskImage / FloppyDriveV2 /
> DriveProfile).  It **boots CP/A fully** from the **real standard-IBM-MFM disks** (all
> `test_boot_integration` stages green, incl. boot from drives B:/C:) and reads/writes
> **`.img`, HFE v1 and DMK**.  The controller is encoding-faithful: FM vs MFM is a property of the
> drive+medium (`DriveProfile::default_read_encoding`, overridable by the OS via the read-mark control
> word 0x85=MFM / 0x87=FM).  For the boot read path `startReadTransfer()` streams
> `TrackCodec::buildFaithfulReadTrack` — the real sync/mark/CRC structure, but with **4×A1 sync** per
> MFM field, which is the one sync length both the boot-ROM read routine (1 discard + 3 reads, FE at
> buf[4]) and the SYL loader (skip-A1-until-FE) accept; the MK/MK1 resync lands on the first A1
> (`romReadResyncTarget`, markPos-4 MFM / markPos-1 FM).  CRC is the standard IBM-CCITT
> (`TrackCodec::crc16`) — the A5120 disks are plain standard IBM-MFM.  Boot itself is the real FM/MFM trial-and-error: the ROM starts in FM, finds no IDAM on the
> MFM disk → index timeout → toggles MK to MFM → reads.  Full model: `doc/design/07_k5122_afs.md`,
> `doc/K1520_architecture.md` §8.5.  The boot invariants that must not regress are listed below.
>
> **Fremd beschriebene Disketten: zwei Dialekte, die der Lesepfad kennen muss**
> (2026-08-17, `doc/design/14_physische_diskette.md` §8.1a–c; Fixture
> `udos_ds77_k5601_fremdsync.hfe`).  Eine an einem ANDEREN K1520-Rechner beschriebene
> UDOS-Diskette war unlesbar, lief aber in ihrem Rechner — drei Festlegungen daraus:
> **(1) Die Marke ist das erste Byte der Sync-Gruppe, das kein 0xA1 ist**, nicht „das
> Byte nach dem Sync": jener Controller schreibt die Datenfeld-Gruppe als
> `A1(Sync) A1(Sync) A1(regulär 0x44A9) FB`, teils mit nur EINER echten Sync-Marke.  Wer
> das reguläre A1 als Marken-Byte verbraucht, findet für jeden von diesem Rechner
> **geschriebenen** Sektor kein Datenfeld (Symptom: „Sektor 1 auf Spur 23 ist kuerzer als
> 128 B").  Einzelne Sync-Marken gelten nur im ZWEITEN `BitCodec::decode`-Durchlauf und
> nur bei einem Sektorkopf ohne Datenfeld — pauschal geöffnet werden Reste alter
> Formatierung zu Sektoren („82 Spuren mit Daten", `ScpxIntegration.*` rot).
> **(2) `TrackCodec::mfmFieldCrcOk()` akzeptiert BEIDE CRC-Sitten**: mit A1-Präambel
> (Standard-IBM ≡ Init 0xCDB4 ab der Marke) und ohne (Init 0xFFFF ab der Marke = die
> FM-Rechnung).  Auf jener Diskette folgen alle 4004 ID-Felder dem Dialekt, alle 4004
> Datenfelder dem Standard — die ID-CRC entsteht beim Formatieren (vorab gerechnet, ohne
> Präambel), die Daten-CRC in der Hardware ab dem Sync.  Geschrieben wird weiter
> Standard.  **(3) Der Schnitt der Spur gehört VOR EINEN SEKTORKOPF**, nicht stur an den Index
> (`app/gw/device.py::naht_vor_sektorkopf`): sonst wird der Sektor über der Index-Naht zerhackt,
> und bei UDOS reisst damit die Zeigerkette (12 statt 46 Dateien).  Wächter:
> `BitCodecFremdeSyncgruppe.*`, `TrackCodecCrcDialekt.*`,
> `DiskVolume.LiestEineDisketteMitFremderSyncSitte`, `test_naht_*`.
>
> **Internal disk medium (2026-08-05, `doc/K1520_architecture.md` §8.7 + `doc/design/09_floppy_drive.md`).**
> A mounted disk lives **entirely in memory** as a `DiskMedium` (every track a `TrackImage`);
> `.img`/`.hfe`/`.dmk` are pure **container codecs** in front of it (`ImageCodec`), no file-bound
> backend classes any more.  `DiskImage` = medium + file binding; changed tracks are written back
> **delayed** (`autoFlush` waits for a *write pause* of ≈0.5 s machine time — tracked via
> `DiskMedium::revision()`, so a FORMAT/COPY burst re-encodes the file once, not dozens of
> times — driven from `A5120Machine::run`), and `saveAs()`
> writes into any container **and re-binds**.  `createDisk` with an EMPTY format name now creates a
> genuinely **blank, unformatted** disk in the *drive's* geometry (guest can format it, incl. UDOS);
> `.img` is refused for that.  `rawCompatible()` is the flag that blocks `.img` as a target as soon
> as a track is unformatted or a sector carries data behind the data CRC (UDOS sector control block).
> Guards: `test_disk_medium`, `test_img_codec`, `test_hfe_codec`, `test_dmk_codec`, `test_disk_image`,
> `UdosFormat.FormatsBrandNewBlankDiskette`, `UdosFormat.BuildsBootableSystemDiskAndBootsFromIt`
> (blank disk → `.dmk` → format both sides → boot from it), `BootIntegrationCpa02.DmkBootsIntoRunningCpaOs`.
>
> **Spurdichte: die Diskette muss nicht zum Laufwerk passen** (2026-08-12,
> `doc/design/09_floppy_drive.md` §7.2).  5,25″ kennt 48 tpi (40 Spuren) und 96 tpi (80).
> Passt die Diskette nicht zum `DriveProfile`, wird sie **nicht mehr abgewiesen, sondern
> übersetzt** — `FloppyDriveV2::mount()` legt einmalig `TrackPitch` und `side0Only` fest,
> ausgerechnet wird an EINER Stelle (`mediumCylinder()`), durch die jeder Spurzugriff geht
> (`track`/`mutableTrack`/`markTrackDirty`/**`writeTrackAt`** — auch das bekommt eine
> *Kopfposition*).  40 Spuren im 80er-Laufwerk → `DoubleStep` (Position 2n = Spur n),
> 80 Spuren im 40er → `HalfStep` (Position n = Spur 2n), zweiseitig im einseitigen →
> Kopf 1 fehlt.  Je Einschränkung ein Satz in `notices()` → `A5120Machine::diskNotice` →
> `k1520_disk_notice` → Laufwerkskasten der GUI (**kein** Meldungsfenster — die Diskette
> ist benutzbar).  Drei Festlegungen, die man nicht aufweichen darf:
> **(1)** Die *Spurzahl* entscheidet (48 tpi = 35–45 Zylinder, 96 tpi = ab 70), nicht das
> Katalogformat — „nur die äußere Hälfte beschrieben" gibt es nicht; wer eine halb
> beschriebene 96-tpi-Diskette abbilden will, braucht ein 80-Zylinder-Abbild mit
> unformatierter Innenhälfte (`.hfe`/`.dmk`, nicht `.img`).
> **(2)** Schreibzugriffe auf eine Position ohne Spur werden **verworfen** (Log-Warnung),
> nicht auf die Nachbarspur umgeleitet.
> **(3)** Beide Darstellungen derselben Diskette — 80 Zylinder mit formatierten geraden
> (`step: 2`) und 40 Zylinder — müssen unter dem Kopf **byteweise gleich** sein; das ist
> der Wächter `FloppyDriveV2.Doppelschritt_IstDieselbeDisketteWieEinDoppelschrittAbbild`.
> Am laufenden CP/A gegengeprüft: Geometrie U (40 Spuren Doppelschritt) formatiert ein
> 40-Zylinder-Abbild im K5601 voll und liest es fehlerfrei zurück.
>
> **UDOS-Laufwerkstypen (`SET DISKCON`) — Matrix in `doc/udos_diskettenformat.md` §12.3.**
> Bootfähig herstellbar sind `41` (K5600.20), `31` (K5600.10, 40 Spuren) und `41` auf dem
> 8″-MF6400 — Guards `Einseitig/UdosLaufwerkstypen.BautBootfaehigeSystemdiskette/*`
> (`test-format`, je ~16 s).  **Die vier Fehlschläge sind GASTVERHALTEN, nicht suchen:**
> Sektorlänge ≠ 128 (`x2`/`x4`) — FORMAT.COM benutzt nur das Typ-Nibble (`4052: AND F0H`)
> und formatiert fest 26×128 (`5F85: LD B,80H`), während nur der Nukleus-Treiber
> (`0794: AND 0FH`) die Sektorgröße in die Lese-Koroutine patcht (`0AAE`/`0AB7`);
> 8″-Typen `11`/`21` — UDOS schreibt das Datenfeld ohne den 4-Byte-Sektorkontrollblock
> (`buf=148, tail=2` statt `152/6`); Typ `61` — FORMAT schreibt einfachschrittig, der
> Treiber liest schrittverdoppelt.  Kontrollkreuz: gleiche 5,25″-HW + `21` scheitert,
> 8″-HW + `41` bootet ⇒ es hängt am Typ-Nibble, nicht am Laufwerk.
>
> **Drive select (8212, port 18H): the HIGH nibble is /SE, the low nibble /LCK (motor)** —
> both active-low, `drive_selected_[d] = !(data>>(4+d) & 1)`.  Not readable off the usual
> select byte (`LD A,77H / RLCA (drv+1)×` → `0xEE`/`0xDD`/… drops one bit of *each* nibble);
> decided by the callers that mask exactly one nibble — CP/A's drive-detect `LD A,0F7H`
> "ohne lock" and UDOS' `OR 0FH` / `AND 0F0H`.  Swapping them silently redirects **every**
> foreign-OS access to drive 1…3 onto drive 0.  `doc/design/07_k5122_afs.md` §8, guard
> `K5122Test.DriveSelect_HighNibbleIstSelect`.

- **Registration model**: cards register memory ranges and I/O port ranges on `K1520Bus`; the CPU's read/write/port callbacks route through the bus, which dispatches to the owning device. Interrupt priority is a daisy chain set via `bus.setInterruptChain(...)`; the Koppelbus models the A5120 backplane's hand-wired signal links (CTC clock cascades, second IEI/IEO chain).
- **Dual Z80 on the K2526 (`core/cards/k2526/`)** — non-obvious and central to the boot path:
  - **ZVE1** is the main CPU. Its memory accesses pass through the **Q240 protection logic** (`MemIOProtect`); a violation raises NMI.
  - **ZVE2** is a second Z80 acting as the **DMA processor** for loading boot sectors. It shares the same bus (no Q240 filtering). The run loop in `a5120.cpp` steps **ZVE2 only while `/BUSRQ` is asserted**, otherwise it steps ZVE1; both CPUs coordinate purely through shared RAM variables. ZVE2 is held in reset (port `04H`) and stalled by `/WAIT-ZVE2` (BS-PIO B3) until ZVE1 releases it.
  - The **boot ROM** is mapped at `0x0000–0x03FF` at power-on and unmapped by writing BS-PIO Port B bit0 (`/LD-ROM`); after that the low addresses are plain RAM shared by both CPUs.
- **C-API boundary** (`core/api/k1520_api.{h,cpp}`): the only surface the Python side sees; keep it `extern "C"` and ABI-stable. `A5120Machine` (`core/machines/a5120/a5120.{h,cpp}`) is the integration point exposing `run()`, disk mounting, framebuffer, keyboard, and debug accessors.
- **EPROM/charset data** are committed as generated C arrays (`*_data.h`, `chargen_*.h`) produced from binaries by `tools/eprom_to_h.py`; they are not loaded at runtime. The K7024 character generator is the two-EPROM Latin set (`chargen_zg1.h` = pixel rows 0–7 / v171, `chargen_zg2.h` = rows 8–11 / v172); binaries under `doc/EPROMS/K7024/`.

## Variante A5120.16 (Erweiterungsmodul EM064/EM256 mit U8001)

Ein A5120 mit `A5120Machine::Config::em` (`core/cards/em/`, CPU-Primitive
`core/primitives/z8000.{h,cpp}`, Werkzeuge `tools/z8000/` + `z8kasm`); C-ABI
`k1520_create_with_em`/`k1520_em_*`, Python `K1520Emulator(em="em256")`, Debugger
`k1520dbg --em em256` + `cpu u8000` (`tools/k1520dbg.md` §12), `boot_trace --em`. Plan
und Stand: `doc/design/17_a5120_16.md`. Beim Zusammenführen mit dem Zweig K8915
(2026-10-02) festgelegt:
- **Bus-/MEMDI ist ein JE ZUGRIFF getriebenes Signal** (`K1520Bus::MemdiDriver`), es gibt
  kein `setMEMDI` mehr; die ZRE 045-8762 des K8915 führt /MEMDI nur als Kartenzustand.
- **Save-State v7** = SIO-Block mit Break/Ext-Latch (K8915-Zweig) + EM-Block; beide Zweige
  hatten unabhängig „v6“ vergeben, ältere Stände laden deshalb ohne Geräteteil.
- **Prüfprogramme `tools/em256/`** (em256adr, em16abl, em256ful; seit 2026-10-02 hier,
  nicht mehr in der CPA-Workbench): Quelle + eingecheckte `.com`, Bau
  `tools/em256/build.py` (U8001-Firmware mit `z8kasm`, gemeinsamer Teil
  `tools/cpm_bau.py` wie SERTEST/ROMREAD). Sie liegen — wie SERTEST und ROMREAD — auf
  den A5120-Bootdisketten in `disks/` (`tools/disketten_beigaben.py`, Wächter
  `cli_beigaben_auf_den_disketten`, prüft auch die Kopien in `tests/fixtures/cpm/`).
- **Ein EM gibt es nur am A5120**: Modellwahl nur im Programmprofil mit `modellwahl`
  (`app/profil.py`), `K1520Emulator(machine="k8915", em=…)` → `ValueError`.
  Wächter `test_only_the_a5120_offers_the_a5120_16_model`.

## Zweite Maschine: K8915

Robotron K8915 (5¼″, V3) neben dem A5120, eigener Zweig unter `core/machines/k8915/`:
Karten `zre8762` (CPU + 128 KB RAM, Speicherumschaltung Port A8H), `k7028` (ATS,
2×SIO+2×CTC), Peripherie `k7672` (Tastatur); wiederverwendet `K7024` und `K5122`
(zweite Betriebsart `/WAIT`, **ohne** ZVE2 — eigener Zweig, rührt den `/BUSRQ`-Weg/die
A5120-Boot-Invarianten nicht an), gemeinsamer Baustein `Laufwerke`. **In `libk1520core.so`** (`k1520_create(K1520_MACHINE_K8915)`,
Python `K1520Emulator(machine="k8915")`; Bild dort nur über `k1520_screen_char`, nie
`mem_read`). **`boot_trace`/`k1520dbg` mit `--machine k8915`** (AP-E4d: eine CPU,
A8H-Speicherbild, `map`/`bank`, Ereignisprotokoll K5122/61H/A8H/Interrupts, Abbruch am
Prompt; ZVE2/`bbusrq`/Snapshots/Savestates melden „nicht vorhanden“ —
`tools/k1520dbg.md` §11, `tools/boot_trace.md` §7). **Stand 2026-09-30:** eigenes Programm **k8915emu** (AP-UI1: Frontplatte in der
Statuszeile, NMI-Taster `k1520_nmi`, Bildschirmtastatur K7672 mit Matrixpositionen —
Vorsatz 2AH/1DH statt `E0`).  Etappen 1–3 fertig (SCPX 8915 V5.3 bootet bis zum Prompt — beide BIOS-Fassungen,
drei Systemdisketten als Fixtures, AP-B2), AP-E4a/E4b/**E4c**/E4d/E4e/E4f fertig (C-ABI,
Werkzeuge, **FORMAT.COM + DISGEN.COM laufen**: Leerdiskette → FORMAT → DISGEN → Kaltstart,
Wächter `K8915Format.*` in `test-format`; **Drucker (SIO1-B) + DFÜ (SIO2-A, vorläufig) nach
außen** über `K1520Machine::setPrinterCallback`/`printerSend` bzw.
`setDFUECallback`/`dfueSend`, `Z80SIO` liefert bei leerem Empfänger jetzt das zuletzt
empfangene Byte statt FFH — Datenblatt-Korrektur, für den A5120 folgenlos); **DiskTool
(Etappe 5): AP-E5a/E5b/E5c fertig** — BIOS-DPB, `.img`-Export, **bootfähige K8915-Disketten**
(`create --fs scpx8915 --boot` / `boot-put --fs scpx8915`, Ladekopf wird vor dem Schreiben
geprüft, A5120-Abbild abgewiesen); Rest von Etappe 4 offen, Arbeitspakete in
`doc/design/16_k8915.md` §8a. Im `/WAIT`-Zweig liefert das Lesen die Spur **so, wie sie auf
der Scheibe liegt** (FORMAT.COM prüft Byte für Byte nach), `.img`-Spuren mit Normlücken —
nicht den nachgebauten 4×A1-Strom des A5120-Wegs.

**Varianten (Zweig `K8915Varianten`, `doc/design/24_k8915_varianten.md`):** `K8915Machine::Config::generation`
= `V3` (Vorgabe) | `Gen2` = Gerät **V2** (ZRE K2521 + RAM-Karte K3528, 64 KB; Vorgabe-ROM = 175/176 + **repariertes 177**
(0A33H = 00H, F9) — der Selbsttest läuft durch; der Abzug selbst (Selbsttestfehler „ROM C“) bleibt
unverändert und dient nur Tests über `Config::gen2_rom`). C-ABI `k1520_create_k8915(gen, …)`/`k1520_k8915_generation`, Python
`machine="k8915-g2"`, Werkzeuge `--machine k8915-g2`, im `k8915emu` Modellwahl V3/V2
(`general.model`, fehlend = V3, V1 ausgegraut über `gesperrte_modelle`; EM nie).
V1 (K7634) ist gesperrt (F11); `app/ui/keyboard_k7634.py` liegt bereit, nicht eingehängt.
**Vor Arbeiten daran: `doc/merkposten/k8915_varianten.md` lesen** (Abzug 177 nie flicken — die Reparatur ist eine eigene Datei, A8H
an der K3528, `zre()` nur am V3).

**Vor Arbeiten daran: `doc/merkposten/k8915.md` lesen** — die Festlegungen mit ihrem
Wächter (A8H-Brückenfeld, `/WAIT`-Zweig der K5122 samt Spur-wie-sie-liegt und MK = nur
Markenerkennung, `Z80PIO`/`Z80SIO`-Korrekturen, K7672 SCP/DCP, vorläufige Prüfstecker-Vorgabe,
`TempDisk`, FORMAT.COM-Bedienung). Plan: `doc/design/16_k8915.md`.

## Dritte Maschine: PRG 710 / PRG 710-1

Numerik-Programmiergeräte (K2521-ZRE, 1 CPU, `/WAIT`-K5122, UDOS 4.3 und SCPX 1526), eine
Klasse `Prg710Machine` unter `core/machines/prg710/` (Variante 710 | 710-1 wählt ROM,
Tastaturweg — 8279/K7609 bzw. K7672 an K8025 A32-B —, K8025-Belegung und Marken-FF-Polarität);
Karten `k2521`, `prg710_speicher` (Seitenregister E8H–EBH), `atp590068`, `ass590069` (Fernschreiber → `SerialHub`, Text). **In
`libk1520core.so`** (`k1520_create_prg710(variante, …)`, Python
`K1520Emulator(machine="prg710"|"prg710-1")`; Bild nur über `k1520_screen_char`, nie
`mem_read`). **`boot_trace`/`k1520dbg` mit `--machine prg710[-1]`** (`--keys`, `map` mit
den Seitenregistern), eigenes Programm **prg710emu** (AP-P5d), dritter Starter im Paket
(AP-P5g), DiskTool `boot-scpx`/`--prg` (AP-P6). **Stand 2026-10-02:** Etappen 1–6 fertig
(beide Varianten booten UDOS bis `%` und SCPX bis `A>`, FORMAT läuft); offen
[Anwender]-Fragen (Kartenbefund, Tastenbild, Disketten), AP-P3b (leere Spur = Rauschen im K5122-`/WAIT`-Weg, nur halb belegt — Merkposten). **EPROMmer (AP-P7, 2026-10-03):** virtueller Sockel der ATP 590068 (`eprommer590068.*`, D0H–D4H + ZRE-PIO 84H Bit 0, Befund `doc/prg710/eprommer.md`), `k1520_eprom_*`, Kasten in `prg710emu`; nur Belegtes wirkt (Brennen nur 1→0, Impulsbreite nur protokolliert). **Lochband + Fernschreiber (AP-P8, 2026-10-03):** K6022 (seit Entwurf 23 Option, s. u.) und 590069, unter UDOS **`F=A`** — Merkposten.

**Vor Arbeiten daran: `doc/merkposten/prg710.md` lesen** — die Festlegungen mit Wächter
(Speicherverwaltung als Arbeitsmodell, Marken-FF low-aktiv am 710 + `setMkeJedesSyncByte`
samt Berichtigung, `keyRelease` Pflicht am 710-1, K8025-Belegung mit `taktquelle = 1`, ISO-646-
Zeichensatz, Fixtures/beschädigte Abzüge, Bedienung von UDOS/SCPX/FORMAT im Test). Plan:
`doc/design/20_prg710.md`.

## Vierte Maschine: PC 1715 / PC 1715W

Bürocomputer robotron PC 1715 (Z80, 64 KB, 8275-Bild aus dem Haupt-RAM, eigener Tastatur-U880
mit S600, Floppy in K5122-Bauart `/WAIT`) und PC 1715W (256 KB mit Bankregister, U8272 + Z80-DMA,
Bild-RAM + ladbarer Zeichensatz; SCP 3.0 bootet, `K1520Emulator(machine="pc1715w")`, in `pc1715emu` wählbar, Lampen/Motor aus MOS 28H/U8272). Eine Klasse `Pc1715Machine` unter
`core/machines/pc1715/` (Variante `PC1715` | `PC1715W`); Karten `pc1715_zre` (ROM-Overlay,
CTC/SIO, 8275, BWS 34H), Primitive `i8275`, `z80_dma`, `upd765`, `core/peripherals/tastatur1715/`;
wiederverwendet `K5122` mit `Portlage::Pc1715` (Vorgabe unverändert). **In `libk1520core.so`**
(`K1520_MACHINE_PC1715 = 3`, `k1520_create_pc1715(variante, …)`, Python
`K1520Emulator(machine="pc1715")`; Bild nur über `k1520_screen_char`, nie `mem_read`).
**`boot_trace --machine pc1715`** (Grundform). **Stand 2026-10-03:** Etappen 1–3 fertig (SCP 1715,
CP/A 1715, CP/Z 2.2, UDOS 1715 booten bis zum Prompt; `dir`/`STAT`/`cat` über die Tastatur);
Etappe 4 (Schnittstellen/V.24-Boot) und AP-5a (Programm `pc1715emu`, Framebuffer 640 × 300 mit CP/A-Statuszeile) stehen; Paket (AP-5c) und 1715W (AP-W3/W4: Kern, Programm, Lieferdiskette) stehen; offen DiskTool, RAM-Disk-Prüfung, [Anwender]-Fragen.

**Vor Arbeiten daran: `doc/merkposten/pc1715.md` lesen** — die Festlegungen mit Wächter
(ROM-Overlay lesen ROM/schreiben RAM, BWS 34H = Adresse >> 10, SIO-Adressierung AB0 = Kanal,
MO-Register 21H = DB4–7, Index als Interrupt, 8275-Sondercodes F0–F3, Tastatur gedrückt = 1 und
Shift/CTRL einzeln vorweg, Return = 9EH, Berichtigungen zum 1715W, Testhilfe `pc1715_input.h`).
Plan: `doc/design/21_pc1715.md`.

## Fünfte Maschine: P8000 (Branch `P8000`, Stand 2026-10-07)

Robotron P8000 mit 8-Bit-Teil (U880, UDOS) und 16-Bit-Teil (UB8001 + 3×UB8010-MMU, WEGA/UNIX),
Winchester-Controller WDC mit Original-Firmware. **Kein K1520-Rechner** — eigener Bus; nur Bausteine
werden wiederverwendet. Eine Klasse `P8000Machine` unter `core/machines/p8000/`, Karten unter
`core/cards/p8000/` (`speicher8`, `karte8`, `floppy8`, `karte16`, `mmu_logik16`, `dram16`, `kopplung`,
`wdc`), neue Primitive `z8010` (UB8010) und ein vollständig überarbeiteter `z80_dma`/`z8000`,
Terminal `core/peripherals/p8000_terminal/` (ADM31/VT100 im Kern), Platte `core/peripherals/winchester/`.
C-ABI `K1520_MACHINE_P8000 = 4`, `k1520_create_p8000(konfig)`; `boot_trace`/`k1520dbg --machine p8000|p8000-16`.
**Stand:** M1 (UDOS bootet) und M2 (`x` → U8000-Monitor, `O U` → `boot`) erreicht, WDC läuft mit Firmware 4.2,
`sa.format` (M3) in Arbeit; DiskTool-WEGA-Dateisystem und Paket offen.
**Oberfläche `p8000emu` (AP P16, 2026-10-07; `--machine p8000`, `run_p8000emu.sh`, `p8000emu.yaml`,
`data/default_config_p8000.yaml`, `doc/design/26_p8000emu_oberflaeche.md`):** fünftes Programmprofil im gemeinsamen
Hauptfenster — `profil.terminal` ersetzt die Bildröhre durch `TerminalTabs` (`app/ui/p8000_terminal.py`: je Kern-Terminal
ein Reiter, **Zeichensatz aus den EPROM-Abzügen** `P8TEZS`/`P8TDZS` → `app/ui/p8000_zeichensatz.py`, erzeugt von
`tools/p8000/zeichensatz_zu_py.py`), Funktionstastenleiste statt Bildschirmtastatur, Plattenkasten unter den Disketten
(`app/ui/platten_widget.py`; **seit P21 keine Standardplatte mehr** — das Vollgerät startet ohne Platte, angelegt wird nur über *Neue Platte…*),
Modellwahl Vollgerät/ohne Winchester/nur 8-Bit, ROM-Fassung und Platinenindex als `hardware`-Wahl
(`Programmprofil.kern_parameter` → `p8000={…}`), Frontplatte Run/16-Bit/Platte/Power (aktiv high), Zwischenstand
(*Maschine ▸ Zwischenstand*, ohne Kürzel). Zusätzliche C-ABI (additiv): `k1520_term_snapshot/_flags/_bell_count`,
`k1520_state_save/_load/_error`; Tasten ohne Qt-Gegenstück gehen als `0x02000000 + TerminalTaste` an
`P8000Machine::keyPress`. Wächter `py_p8000emu_gui`. Offen: UDOS-Laufwerksnamen 0/1.
**Varianten und Mehrplatz (AP P21, 2026-10-08; Entwurf 26 §8, Handbuch „Das Originalterminal und der Mehrplatzbetrieb"):**
`general.model` = `p8000`/`p8000-16`/`p8000-8` (Kern-Terminal) · `p8000-ot`/`-16-ot`/`-8-ot` (**P8000 + P8000 Terminal**: Originalterminal
Typ 2 + Flachtastatur K7673.09 an tty1) · `p8000-term` (**P8000 Terminal**: Arbeitsplatz ohne Rechner, Kernmaschine `p8000-terminal`,
Leitung per Hub). Das Profil entscheidet (`modell_arten`/`modell_art`/`modell_maschine`), nie die Oberfläche. Zweites Programm
**`p8000term`** (`run_p8000term.sh`, `--machine p8000term`, `p8000term.yaml`, `data/default_config_p8000term.yaml`) = dasselbe
Profil, Terminal vorn; `maschine` bleibt `p8000`. Bild: `app/ui/p8000_original.py` (Pixelbild 640 × 312, Pause bei unverändertem
`term_frame_count`, Skalierung ganzzahlig/glatt, Farbe), Tastatur: `app/ui/k7673_layout.py` (Matrix gleich `term_matrix_scancode`
des Kerns — Wächter) + `keyboard_k7673.py`; Wirtstasten über `NORMAL_Tab`/`SHIFT_Tab`, ein selbst gedrücktes SHIFT bekommt
150 ms Vorlauf. Verbindung: `app/ui/verbindung_dialog.py` (*Maschine ▸ Verbindung zum Rechner…*, kein Kürzel). Mehrinstanz:
`--instance NAME`/`--config DATEI` (`app/instanz.py`), Plattensperre `<abbild>.lock`. Wächter `py_p8000_original_gui`.
**Paket und Werkzeugmenü (AP P18, 2026-10-08; `doc/design/13_distribution.md` §10b):** `p8000emu` und `p8000term` sind
fünfter/sechster Starter im Linux-Paket und im Inno-Setup (Vorlage `launcher.*`, `p8000emu.desktop.in`/`p8000term.desktop.in`,
`MASCHINEN`, `{#Programm5}`/`{#Programm6}`, beide `default_config_p8000*.yaml`, Rauchtest `k1520_create(4)`/`(5)`); **keine
P8000-Disketten im Paket**. Menü *Werkzeuge*: `p8000term` ist überall erreichbar, `Aktionen.gibt_es(name, maschine, programm)`
trennt `p8000emu`/`p8000term` (gleiche Maschine). Wächter `py_packaging`, `py_programme`, `py_pc1715emu_gui`.
**Mehrplatz-Abnahme (AP P22, 2026-10-08; `doc/p8000/mehrplatz_abnahme.md`, Merkposten 40–43):** WEGA von der Platte,
Konsole am Originalterminal + drei Arbeitsplätze (tty0/tty2 über die Koppelsoftware, tty4) per Loopback-Telnet
(`P8000WegaMehrplatz.*`, `tools/dev.sh test-wega`).  **Der U880 der 8-Bit-Karte nimmt nach `EI` erst nach dem folgenden
Befehl an** (sonst wuchs unter Last der Stapel der Koppelsoftware in den Code), **der Wandler blickt höchstens 1/16 der
9600-Zeichenzeit voraus** (sonst MON16-Fehler 61) — beides nicht aufweichen.
**Grundsatz:** Chips werden vollständig + systematisch getestet, Debugger unterstützen sie vollständig
(Plan §10.11a). **Vor Arbeiten daran: `doc/merkposten/p8000.md` lesen**; Plan und Stand
`doc/design/25_p8000.md` (§9/§9a), Quellen/Referenzen `doc/p8000/` (EPROM-Abzüge in `doc/p8000/eproms/`).

## RAM-Floppy RAF 128/512/2M (alle Maschinen; PC 1715 nur mechanisch, s. Merkposten pc1715)

Steckbare K-1520-RAM-Floppy des ZWG der AdW auf **E/A 88H/89H** (fest), Karte
`core/cards/raf/`, gesteckt über `K1520Machine::installRaf` **nach dem Anlegen, vor dem
ersten Lauf** (C-ABI `k1520_raf_*`, Python `K1520Emulator(raf="raf512")`, Oberfläche
*Einstellungen ▸ Allgemein ▸ RAM-Disk* mit Stand-by-Ablage `raf_<programm>.bin`,
`k1520dbg`/`boot_trace --raf`).  Gast: `RAFCPM.COM` (M:) / `RAF512.COM` (P:) unter CP/A,
SCPX 8915 und SCPX 1.7, dazu die CP/A-Diskette mit eingebautem Treiber
`disks/cpa_cpa780_k5601_noclock-raf.hfe`.  Kern der Sache: A8–A15 (Register B) tragen
Sektor bzw. Bytezeiger, Sperrbits und Spiegelung sind **so** nachgebildet, RESET erhält den
Inhalt.  **Vor Arbeiten daran: `doc/merkposten/raf.md` lesen**; Entwurf
`doc/design/22_raf512.md`, Originale `doc/raf512/`.

## Lochstreifen K6022/SIF1000 (alle Maschinen; PC 1715 nur mechanisch)

ADA K6022 (Leser daro 1210, Stanzer daro 1215) als steckbare Option auf **E/A E0H–E7H**
(fest), gesteckt über `K1520Machine::installK6022` **nach dem Anlegen, vor dem ersten Lauf**
(C-ABI `k1520_ptape_*`, Python `K1520Emulator(ptape=True)`, `--ptape` in
`boot_trace`/`k1520dbg`), Vorgabe überall aus (`general.ptape`).  Bandformate Roh/Intel HEX
(Adresse = Bandposition)/ASCII-Art (Lochbild maßgeblich) im Kern, Kennungen 0/1/2 sind
Vertrag; der Stanzer wird an seine Datei **gebunden** und schreibt nach 0,5 s Stanzpause
Maschinenzeit zurück.  Kasten „Lochstreifen“ nur mit Karte, kein Kürzel, nicht im Save-State.
**Vor Arbeiten daran: `doc/merkposten/lochstreifen.md` lesen**; Entwurf `doc/design/23_lochstreifen.md`.

## Boot-ROM debugging workflow

Der volle CP/A-Kaltstart läuft (Boot-ROM → SYL-Lader → Zweitlader → CP/A-Bootsystem →
`@OS.COM` → laufendes OS am Prompt); der ZVE1↔ZVE2-DMA-Handschlag ist **gelöst**. Zwei
Werkzeuge, ergänzend zueinander: **`boot_trace`** (nicht-interaktiv — *lokalisiert*, wo es
hängt) und **`k1520dbg`** (interaktiv, gdb-artig — *seziert* das Lokalisierte). Beide über
`tools/dev.sh trace …` bzw. `tools/dev.sh tool k1520dbg …` aufrufen, nie direkt aus `build*/`.

Einstieg ist **`tools/how_to_debug_and_trace.md`** (aufgabenorientiert, mit durchgerechneten
Szenarien); Referenzen: `tools/k1520dbg.md`, `tools/boot_trace.md`. Der volle Merkposten —
alle Werkzeuge, der Stapelbetrieb für den Agenten (COW-Mount, `--quiet --json`, Save-State,
`-x script.dbg`), `.prn`/`.MAC`-Annotation, Interrupt-Diagnose — liegt in
**`doc/merkposten/boot_debugging.md`**.

> **Die acht Boot-Invarianten dürfen nicht zurückfallen.** Volltext mit Begründung und
> Wächtern in `doc/merkposten/boot_debugging.md` (Abschnitt „Boot chain"), Analyse in
> `doc/K1520_architecture.md` §14.5 und `doc/analyse_zre_rom_boot.md`:
> 1. Während der DMA ZVE2 **und** ZVE1 schrittweise fahren (auf echter HW parallel).
> 2. Die Fertigmeldung `[0x03F8]` **flankengetriggert** beobachten, nicht pegelbasiert.
> 3. ZVE2 startet aus dem Reset bei PC=0, sobald `/BUSRQ` anzieht.
> 4. Treuer Lesestrom (`buildFaithfulReadTrack`, **4×A1-Sync**) samt **MK1-Resync**.
> 5. Kopfwahl = Steuerport A Bit2 (`/FR`), übernommen nur an der `/STR`-Flanke.
> 6. Gemischte Geometrie steht in `data/formats.yaml`; die 128-B-Systemspuren sind
>    **MFM**, nicht FM — die FM→MFM-Probiererei des ROMs hängt daran.
> 7. `/WR` (BS-PIO Port A, A5) ist ein **Strobe**, kein Dauerpegel.
> 8. Reset ist ein **systemweiter `/RESET`**, nicht nur die CPU.
>
> Handschlag-RAM: `[0x03F8]` Fertigmeldung, `[0x03F7]` Indexzähler, `[0x03FD]` Pfadbyte,
> `[0x07F2]` Sektorzahl, `[0x03F0]` Ladeadresse. Wächter: `test_boot_integration`,
> `test_k5122`, `test_k2526`.

## Subagenten / Delegation an günstigere Modelle

Projektspezifische Subagenten liegen in `.claude/agents/`. **Standing rule: soweit sinnvoll,
abgrenzbare Teilaufgaben an Agenten auf Basis GÜNSTIGERER Modelle delegieren, statt sie selbst
(Opus) zu erledigen** — das spart Kosten (Haiku/Sonnet statt Opus) und hält den Hauptkontext frei.
Faustregel: kontextarme, gut umrissene Arbeit auslagern; eng mit dem laufenden Arbeitsstand
verwobene Arbeit selbst behalten. Opus bleibt für Orchestrierung, Entwurf und Entscheidungen.

| Agent | Modell | Wofür |
|-------|--------|-------|
| `log-trace-analyzer` | haiku | Auswertung großer `boot_trace`-/Emulator-Logs, Trace-/ctest-Ausgaben, VRAM-/Port-Histogramme — liefert nur die Schlussfolgerung. |
| `code-explorer`      | haiku | Read-only Code-/Symbol-/Fundstellen-Suche über `core/` + `tests/` + `tools/` + `app/`. |
| `test-runner`        | haiku | Bauen + `ctest` ausführen, Pass/Fail knapp berichten, gegen bekannte pre-existing Failures abgrenzen. |
| `cpp-coder`          | sonnet | Umrissene C++-Implementierungen in `core/` + zugehörige GoogleTests, im Stil der Umgebung. |
| `boot-disasm-analyst`| sonnet | Z80-Disassembly + ZRE-Boot-ROM/ZVE1↔ZVE2-DMA-Analyse mit den `tools/`-Werkzeugen. |

Konkret heißt das u.a.: breite Suchen → `code-explorer`; Log-/Trace-Auswertung → `log-trace-analyzer`;
Build-&-Test-Durchläufe → `test-runner`; umrissene C++-Teile → `cpp-coder`; Boot-Disasm/RE →
`boot-disasm-analyst`.

**Stehende Erlaubnis — dafür ist NICHT jedes Mal zu fragen.** Manche Claude-Code-Fassungen
tragen die Grundregel „keine Subagenten starten, solange der Anwender es nicht verlangt".
Dieser Absatz IST das Verlangen, ein für alle Mal, für genau diese drei Fälle:

- **`test-runner`** — jeder Bau-&-Test-Durchlauf, dessen Ergebnis „grün/rot + welche Fälle" ist.
- **`log-trace-analyzer`** — jede Auswertung eines `boot_trace`-/Emulator-Logs, einer
  Trace-Datei, eines VRAM-/Port-Histogramms oder eines roten `ctest.log`.
- **`code-explorer`** — jede breite Suche, deren Ergebnis eine Fundstellenliste ist.

Für alles andere (`cpp-coder`, `boot-disasm-analyst`, Worktrees) bleibt es beim Fragen.

**Warum es sich rechnet — und wann nicht.** Der Gewinn ist nicht der Modellpreis allein,
sondern dass die **Ausgabe im Kontext des Agenten lebt und mit ihm stirbt**: gemessen am
2026-08-18 liest dieses Projekt im Schnitt ~530 000 Token Kontext je Tool-Aufruf wieder ein,
also kostet jedes Byte, das einmal im Hauptkontext landet, bei jeder weiteren Anfrage erneut.
Ein 51-KB-Log, das der Agent auf drei Sätze eindampft, wird damit hundertfach nicht bezahlt.
**Es rechnet sich NICHT** bei einer Frage, die in einem Handgriff beantwortet ist (der Agent
startet kalt und liest diese Datei erst einmal ganz), und nicht bei Arbeit, die eng am
laufenden Stand hängt — die Weitergabe des Zwischenstands wäre teurer als die Arbeit selbst.

**Parallelität — Bau-Kollision beachten:** Mehrere Agenten, die gleichzeitig `build/` (oder
`build_trace/`) anfassen, kollidieren beim `cmake --build` (Race/kaputte Binaries). Daher: build-/
test-berührende Delegationen **sequenziell** laufen lassen ODER dem Agenten ein eigenes Worktree
geben (`isolation: "worktree"`). Read-only-Agenten (`code-explorer`, `log-trace-analyzer`,
`boot-disasm-analyst` gegen ein bereits gebautes `./build/…`) parallelisieren gefahrlos. Hintergrund-
Agenten sind langsam (ein voller Boot unter `k1520dbg`/`boot_trace` ~2 s bis Minuten je Aufgabe) —
nicht mit „hängt" verwechseln; sie melden sich bei Abschluss selbst.

## k1520DiskTool — Dateiaustausch mit Disketten (`core/filesystem/`, `app/disktool/`)

Zweites Anwenderprogramm neben dem Emulator: holt Dateien von CP/A-, SCPX-, **UDOS**-,
**UDOS1715**- und **SCP1700**-Disketten (CP/M-86, A7100) und schreibt sie zurück
(`.img`/`.hfe`/`.dmk`). Es teilt sich mit dem Emulator die Container-/Medium-Schicht, hat
aber **eine eigene Bibliothek** (`libk1520disk.so`) ohne Z80 und Karten.

```
core/filesystem/   SectorSpace (physisch + linear) · GeometryProbe · FsProfile/FsCatalog ·
                   CpmFileSystem · UdosFileSystem · Udos1715FileSystem · DiskVolume
core/api/k1520_disk_api.*   C-ABI  →  libk1520disk.so
tools/k1520disktool.cpp     CLI    →  tools/dev.sh tool k1520disktool ls <abbild>
app/disktool/               PySide6-Oberfläche  →  bash run_disktool.sh
```

> **Bevor du hier arbeitest: `doc/merkposten/disktool.md` lesen** — rund zwanzig
> Festlegungen mit ihren Wächtern (die Dateisysteme SCP1700/UDOS1715/P8000, `CpaDpbRule`,
> Doppelschritt, bootfähige Disketten, Diskeditor, Eigenschaften-Dialog,
> Dateisystemprüfung, Aufbau der Oberfläche, Arbeitsverzeichnisse). Entwurf:
> `doc/design/13_k1520disktool.md`, Prüfung: `doc/design/15_dateisystempruefung.md`,
> Bedienung: `tools/k1520disktool.md`. Fünf Dinge, die man ohne Nachschlagen wissen muss:
> - **UDOS/ZDOS auf `.img` ist unmöglich** — die Dateiverkettung steht im Gap hinter der
>   Daten-CRC; `rawCompatible()` sperrt es. Bei UDOS1715/NDOS ist `.img` dagegen **erlaubt**
>   (dort trägt die Verkettung in eigenen Zeigersektoren).
> - **Erzeugte Spuren = Normlücken + genau eine Umdrehung** (AP-F1, `doc/design/16_k8915.md`):
>   Lücke 2 = 22 × 4E (das CP/A-Bootsystem liest nach der ID-CRC 25 Bytes, bevor es MK1
>   scharf macht), und die Zellenzahl einer `.hfe`-Spur IST ihre Umdrehungszeit
>   (`gw write` streckt sie). Der A5120-Lesestrom übernimmt Lücke 2 vom Medium (nur MFM); eine
>   knappe Diskette scheitert im Emulator wie am Gerät (`BootIntegrationLuecke2.*`).
> - **`TrackCodec::writeSector` ersetzt ein Datenfeld an Ort und Stelle.** `buildTrack()`
>   taugt zum Schreiben NICHT: es baut die Spur neu und verlöre alles hinter der Daten-CRC.
> - **Stapeloperationen sind Transaktionen** — erst planen und urteilen, dann schreiben; ein
>   Fehler rollt die Momentaufnahme des `DiskMedium` zurück. `list()` liest **immer** frisch.
> - **`filesystems:` in `data/formats.yaml` soll KURZ bleiben**: `CpaDpbRule` rechnet die
>   meisten Profile bitgleich nach, ein neuer Eintrag braucht einen eigenen Grund.
> - **Teure Läufe an einer physischen Diskette werden VORGELADEN, nicht gesperrt.**
>   Prüf- und Suchdialog rufen `MainWindow._abbild_vervollstaendigen` — fehlende Spuren
>   kommen mit dem Fortschrittsdialog der Formaterkennung herein („X von Y Spuren
>   geladen", abbrechbar). Die Sperre in den drei Dialogen fragt seitdem
>   `abbild_vollstaendig` statt `tool.path`: eine physische Diskette, die schon ganz
>   im `DiskMedium` liegt, war vorher grundlos gesperrt.
> - **Die Dateisystemprüfung (`fsck`, `core/filesystem/check/`) schreibt NIE** — sonst wäre
>   die Automatik beim Öffnen ein Schreibzugriff auf eine schreibgeschützt geöffnete
>   Diskette; Reparaturen gehen ausschliesslich über `FileSystem::repair`, gebündelt in
>   `DiskVolume::applyRepairs` (EINE Transaktion, Rangfolge Verzeichnis→Ketten→Plan→Zähler,
>   danach automatisch neu prüfen). Beim Öffnen läuft nur die
>   SCHNELLprüfung (kein zusätzlicher Spurzugriff!), die Befundkennungen
>   (`"cpm.block.doppelt"`) sind ein VERTRAG, und **Falschmeldungen sind schlimmer als
>   fehlende** (Wächter `FsCheckKeineFalschmeldungen.*`).
> - **Vorausgewählt wird nur, was keine Daten verwirft** — in der CLI (`fsck --repair`
>   nimmt nur `empfohlen && !datenverlust`, `=alle` auch die übrigen) wie im Dialog
>   (`app/disktool/ui/fsck_dialog.py`, Strg+F). Und ein CP/M-Blockzeiger wird nie
>   mitten aus der Liste gestrichen, sondern **ab dort abgeschnitten**: ein Loch
>   verschöbe jeden folgenden Satz, der Extent lieferte danach falsche Daten aus.
> - **Die Gegenprobe der Alternativprofile** (§11a): bei mehrdeutiger Erkennung öffnet
>   `DiskVolume::gegenprobe` dieselbe Datei mit jedem Alternativprofil und meldet
>   `erkennung.alternative` (Info), wenn eines **weniger Befunde ODER mehr sichtbare
>   Dateien** liefert — beides zählt, denn ein zu KLEINER Verzeichnisbereich versteckt
>   Dateien, ohne einen einzigen Befund zu erzeugen (ein zu großer fliegt dagegen schon
>   bei `cpmVerzeichnisPlausibel` raus). **Sie ändert die Wahl nie**, sonst sähe der
>   Anwender bei jedem Öffnen ein anderes Dateisystem. Prüfbar nur über den
>   test-eigenen Katalog `tests/fixtures/formats_mehrdeutig.yaml` — `data/formats.yaml`
>   wird bewusst eindeutig gehalten (`cpa640` wurde genau deshalb entfernt), Wächter
>   `FsCheckGegenprobe.DerAusgelieferteKatalogIstEindeutig`.
> - **„Nichts erkannt" ist selbst ein Befund** (Ebene 0, §11): die Ablehnungsgründe
>   der Erkennung werden eingesammelt (`DiskVolume::merkeAblehnung`) statt im
>   `continue` verworfen und bei `hasFileSystem() == false` als `erkennung.abgelehnt`
>   auf der Ebene `FsLayer::Erkennung` ausgegeben. `check`/`fsck` öffnen dafür **roh**
>   (Rückgabewert 2), die Oberfläche schreibt sie ins Protokoll, der Prüfdialog geht
>   auch ohne Dateisystem auf. Auf einer ERKANNTEN Diskette darf `erkennung.*` nie
>   vorkommen (Wächter `…EineErkannteDisketteHatKeineBefundeDerEbene0`).
> - **Retten geht vor Wiederherstellen** (`recover`, `check/cpm_recover.cpp`,
>   `check/udos_recover.cpp`, `check/udos1715_recover.cpp`,
>   `app/disktool/ui/recover_dialog.py`): der Suchlauf ändert nichts, *in den Ordner
>   holen* geht immer und auch schreibgeschützt. **Auf der Diskette eintragen gibt es
>   nur bei CP/M** — dort ist es ein Byte und der Name stimmt; erlaubt, wenn kein Block
>   inzwischen einer lebenden Datei gehört (sonst entstünde der `cpm.block.doppelt`, den
>   die Prüfung als Gefahr meldet), nachgeprüft unmittelbar vor dem Schreiben. Bei CP/M
>   überlebt der Name, **nicht der Nutzerbereich** — er stand in ebendem Byte, das 0xE5
>   wurde; die **billige Suchtiefe fasst dort keine Datenspur an**.
> - **Bei UDOS/NDOS ist die Rettung rein lesend** (§13.3b). Gelöscht wird der
>   VERZEICHNISEINTRAG, nicht ein Byte: der Kopfsektor überlebt mit Typ, Eigenschaften,
>   ENTRY, Satzlänge und allen Segmenten — verloren ist **allein der Name**. Der Weg
>   zurück heißt *retten → benennen → `put`*; `recoverExtract` legt dafür das Beiblatt
>   `udos-dateiangaben.txt` an. Drei Fallen: die **Systemspuren werden nicht
>   übersprungen** (`NOTE.TO.SD` der Referenzdiskette hat ihren Kopfsektor auf Spur 21 —
>   ausgeschlossen wird nur, was die Karte im Bootbereich als belegt führt), bei **NDOS
>   trägt die Bytesignatur nicht** (die `FF 00`-Marken sind A5120-Sitte; getragen wird
>   die Erkennung über `FIRSTBL` → Zeigersektor → erste Eintragung = der Descriptor), und
>   **beide Suchtiefen fassen die Datenspuren an** — nach dem Löschen steht im
>   Verzeichnis nichts Gesuchtes mehr (an einer physischen Diskette kostet das **einmal**
>   die ganze Scheibe, danach liegt sie im `DiskMedium`).
> - **Erst ansehen, dann handeln**: jeder Fund führt seine **ganze Sektorliste** in
>   Lesereihenfolge (`FsRecoverFind::orte`, `k1520d_recover_part*`, `RecoverFind.parts`),
>   und der Rettungsdialog schlägt jeden Eintrag im Diskeditor auf. Kein Komfort, sondern
>   die einzige Handhabe: die Sätze einer UDOS-Datei liegen verkettet und physisch
>   verschränkt (`NOTE.TO.SD`: Sektor 6, 7, 12, 23, 1, 8, …) — den zweiten fände von Hand
>   niemand, und einen Namen zum Wiedererkennen gibt es dort auch nicht.

## Serielle Schnittstellen nach außen (`core/serial/`, `app/ui/serial_widget.py`)

Die seriellen Kanäle der K8025 (A5120: DFÜ/V.24, DFÜ/IFSS, Drucker) und der K7028 (K8915:
Drucker/IFSS1 X3, V.24 X4, DFÜ/IFSS2 X5 — Namen nach der Gerätebeschriftung, AP-S12) gehen über **Telnet** oder **RFC 2217** (Client/Server) oder in eine
**Datei** nach außen; die Tastatur bleibt fest verdrahtet. Je Maschine ein `SerialHub`
(`K1520Machine::serialHub()`, I/O-Faden) mit je Schnittstelle einem `Wandler`; die Karten
liefern nur einen `SerialAnschluss`. C-ABI `k1520_serial_*`, Python `K1520Emulator.serial_*`,
Reiter „Schnittstellen" im Einstellungen-Kasten (seit AP-S10, kein eigener Dock). Entwurf: `doc/design/19_serielle_schnittstellen.md`.

> **Vor Arbeiten daran: `doc/merkposten/serielle_schnittstellen.md` lesen** (Festlegungen
> mit Wächtern, wie man einen Gast im Test senden lässt, Gegenstellen). Die teuersten Regeln:
> - **Maschinenzeit, nicht Uhr:** der Wandler taktet in Maschinentakten nach der vom Gast
>   programmierten Baud; **verlustfrei durch Rückstau**, nie ein SIO-Überlauf.
> - **Der Gast ist maßgeblich:** eine RFC-2217-Anfrage ändert Baud/Format nie, sie wird mit
>   dem Gastwert beantwortet und nur als `baud_abweichend` angezeigt.
> - **Kein Netz im Emulationsfaden, die Karte kennt kein Netz**; Sperrreihenfolge Hub → Wandler.
> - **RTS-Halt erst nach dem ersten gesetzten RTS** (CP/A/SCPX setzen es nie); danach sofort.
>   Nullmodem-Kreuzung nur als Server.
> - **Loop ⇔ keine Verbindung**; K8915 startet mit Loop an. **Datei meldet VERBUNDEN.**
> - **Tests: nie feste Ports** (Port 0 bzw. freier Port, nur Loopback). Wächter u. a.
>   `SerialWandler.*`, `SerialHub.*`, `SerielleKopplung.*` (64 KiB-Fassung in `test-format`),
>   `py_serial_api`, `py_serial_gui`, `py_serial_pyserial`.
> - **Prüfprogramm `SERTEST.COM`** (`tools/sertest/`, Entwurf 19 §14; `.com` eingecheckt): Wächter
>   `Sertest.*`/`SertestKopplung.*` (lange Kopplungsfälle in `test-format`); Geräteprüfung offen.

## Physische Diskette am Greaseweazle (`core/peripherals/floppy_drive/track_sync.*`, `app/gw/`)

Neben der Datei (`.img`/`.hfe`/`.dmk`) gibt es eine **zweite Art von Bindung** des internen
Mediums: ein echtes Laufwerk an einem
[Greaseweazle](https://github.com/keirf/greaseweazle)-Adapter. Der Unterschied ist nicht das
Medium, sondern die **Körnung** — gelesen und geschrieben wird **spurweise nach Bedarf**, der
Zwischenschritt „ganze Diskette in eine Datei" entfällt. An echter Hardware nachgewiesen
(0,5–0,8 s je Spur, Emulator-Kaltstart von der eingelegten Diskette, Schreiben mit
Prüf-Lesen). Entwurf: **`doc/design/14_physische_diskette.md`**.

> **Vor Arbeiten daran: `doc/merkposten/physische_diskette.md` lesen** (Prioritäten des
> Arbeitsfadens, Leseausrutscher-Wiederholung, Oberflächen, `--physical` in der
> Kommandozeile, Hardware-Tests). Die fünf Regeln, die den Aufbau tragen:
> - **Der Kern kennt Greaseweazle NICHT.** Kein USB, kein Import, kein Rückruf in die
>   Anwendung. Ein fremder Arbeitsfaden holt Aufträge ab und liefert **HFE-Bitzellen**
>   zurück, die durch denselben `BitCodec::decode` laufen wie eine `.hfe`-Datei. Ein anderer
>   Adapter wäre ein anderes `device` in `app/gw/`, keine Kernänderung.
> - **Je Spur ein Zustand** statt eines Dirty-Bits: `Unknown` / `Clean` / `Dirty`.
>   `Unknown` ist **nicht** „unformatiert" — letzteres ist eine belegte Aussage über die
>   Diskette, ersteres gar keine.
> - **Nachgeladen wird in `DiskMedium::track()` — und NUR dort.** Medienweite Reihenläufe
>   benutzen `peek()` und laden nie nach, sonst zieht eine Statusabfrage die ganze Diskette ein.
> - **Geschrieben gilt erst nach dem ZURÜCKLESEN** (Vergleich auf Sektorebene, beide CRCs).
>   Das Zurückgelesene wird **nie** ins Abbild übernommen.
> - **Zwei Schlösser, und beide müssen sichtbar sein** (2026-09-14): die **Sitzung**
>   darf auf die Scheibe schreiben oder nicht, das **Laufwerk** („Write-Protect" im
>   Laufwerkskasten) lässt die Maschine es versuchen oder nicht.  Im **DiskTool** heisst
>   physisch weiter *schreibgeschützt, bis jemand widerspricht* (Öffnen ist ein
>   Lesevorgang; ein Fehler kostet dort die einzige noch existierende Diskette), im
>   **Emulator** kommt der Haken gesetzt — dort wird die Diskette benutzt.  Was der Kern
>   sperrt, muss der Kasten zeigen.

## Diskettenformatierung (FORMAT.COM) — Scope

Scriptgesteuerte Formatier-Pipeline: `tests/system/drivers/format_all.py` (Runner) + `tools/format_driver`
formatieren mit **FORMAT.COM (V19.05.89)** die K5601-Formate nach Laufwerk B: und verifizieren
(§3 80-Spur-DS: .hfe 13/15, .img 14/15; §3.4-Geometrien S/V/W als .hfe+.img, T/U als .hfe).
Über **Combo-Boot-Disketten** (B:/C: als Fremdtypen) sind auch die 5,25″-SS- und 8″-FM/MFM-
Formate testbar (Laufwerkstyp = reine BIOS-Software). `tests/system/drivers/make_bootdisk.py` fährt
zusätzlich die ganze Kette *Leerdiskette → FORMAT.COM → CPABCGEN → bootfähige Disk → Kaltstart*
(6 Presets, als langsame `format_integration`-Tests registriert — via `tools/dev.sh test-format`).

**Zwei Test-Label, zwei Blickrichtungen** (beide aus der Standard-Regression ausgeschlossen):
- `format_integration` — die **Tiefe**: je Laufwerkstyp EIN Format über die ganze Diskette
  (5 Boot-Disk-Ketten + 2 Voll-Läufe Leerdiskette/160 Spuren) — `tools/dev.sh test-format`.
- `format_matrix` — die **Breite**: **88 Tests, jeder einzelne FORMAT.COM-Menüeintrag**
  (§3 K5601 80-DS, §3.4-Geometrien S/W/U/V/T, native Menüs von K5600.10/K5600.20/MF3200/
  MF6400), jeweils Leerdiskette + Vergleichs-Lesen, Umfang **Smoke (Spur 0–2, ~9 s je Format)**
  — `tools/dev.sh test-matrix` (~160 s wall bei `-j16`; `run_ctest` setzt die
  Parallelität selbst, ein eigenes `-j` gewinnt — die CI gibt `-j4` mit). Die Matrix wird beim `cmake` aus
  `tests/system/drivers/format_all.py --list-matrix` erzeugt: neue Formate dort in die Tabellen eintragen,
  der Testsatz wächst automatisch mit. **Voll-Läufe bleiben manuell**
  (`python3 tests/system/drivers/format_all.py --all --full`) — dort sind K5601 `7` (`Fehler 'S'`) und `5`
  als `.hfe` bekannt rot (doc/format.md §8.2), im Smoke fallen sie nicht an.
> **Ausgangszustand aller CP/A-Formatier-Tests ist seit 2026-08-07 eine ECHTE LEERDISKETTE**
> (`createB`/`FD_DISKC_FMT` = *leerer* Formatname → unformatiertes Medium in der Geometrie des
> Laufwerks). Das ist der Anwenderfall und die schärfere Prüfung; die früher nötigen Vorlagen
> (`mk_disk_template`, `disks/empty_cpa780.hfe`, Template-Kopie in `format_all.py`) werden von
> der Pipeline nicht mehr benutzt. **Einzige Ausnahme `--type img`**: ein rohes Sektorimage hat
> keinen Zustand „unformatiert" (`createDisk` lehnt den leeren Formatnamen für `.img` ab), dieser
> Pfad legt weiter vorformatiert an. Verifiziert: 88/88 Formate über K5601 + alle §3.4-Geometrien
> + K5600.10/K5600.20/MF3200/MF6400.

`DiskImage::create` legt
**gültig formatierte** Leerdisketten an (echte IDAM/DATA/CRC, Daten 0xE5): `.hfe` je Spur per
`TrackCodec::buildTrack`→`BitCodec::encode`, `.img` als 0xE5 in Format-Geometrie. Ein `DiskFormat`
(Geometrie) ist dafür Pflicht — `A5120Machine::createDisk` mit **gesetztem** Formatnamen ist der
vorformatierte Weg; **leerer** Formatname legt seit dem Medium-Umbau eine echte Leerdiskette an
(§8.7). `defaultFormatName(drive)` liefert weiterhin das laufwerkstyp-spezifische Standardformat
(K5601→cpa800, K5600.10→200K, K5600.20→400K, MF3200→308K/FM, MF6400→616K) — die Formatier-Pipeline
übergibt es explizit. C-API: `k1520_create_disk`, `k1520_save_disk_as`,
`k1520_disk_raw_compatible`. Voller Stand + offene Punkte: `doc/format.md` §8–§11.

> **Gap-Blank-`.hfe`-Hänger — GELÖST (2026-07-06), Ursache seit 2026-08-05 im Controller behoben:**
> `K5122::startReadTransfer()` streamt für eine **unformatierte** Spur markenlosen Gap-Flux, sodass
> die Leseroutine über den Index-Timeout terminiert statt in der ZVE2-Lese-Koroutine `0x1D0F` zu
> verklemmen. Damit ist eine gap-leere Diskette ein **gültiger, gewollter** Zustand; die frühere
> Ablehnung markenloser Images beim Öffnen (`hasFormattedData`) ist entfallen, und
> `DiskImage::createBlank` legt genau so eine Leerdiskette an. `DiskImage::create` (mit Format)
> erzeugt weiterhin eine voll formatierte Diskette.
>
> **`Fehler 'U' SPUR DEFEKT` beim Formatieren einer Leerdiskette — GELÖST (2026-08-06):**
> Ein Lese-`/STR`-Strobe aus **ZVE1**-Kontext committet jetzt einen noch anstehenden
> Vollspur-FORMAT-Schreibstrom, BEVOR er den Lesetransfer armiert
> (`K5122::handleCtrlPortAWrite`). Vorher löschte `startReadTransfer()` nur `write_mode_`,
> der fertige Strom blieb verwaist in `write_buf_` liegen (die Schreib-Idle-Erkennung in
> `update()` läuft nur im `write_mode_`) und die frisch formatierte Spur galt bis zum
> *nächsten* Schreib-Strobe als unformatiert — traf FORMAT.COMs Vergleichs-Lesen dieses
> Fenster, lief es in den BIOS-Index-Timeout (`fl.to1`, `'U'`). Auf echter HW gibt es das
> Fenster nicht: geschriebene Bytes liegen sofort auf der Scheibe. Guards:
> `K5122Test.FormatWrite_LeseStrobeCommittetSpurSofort` und die beiden
> `format_blank_disk_with_verify`-Läufe (Anlaufphase 80 **und** 78 — Phase 80 allein lief
> auch mit dem Fehler durch). Analyse: `doc/analyse_format_leerspur.md`.

## Conventions

- Code comments and many log strings are in German; match the surrounding language of the file you edit.
- Card classes encode DIP switches / backplane bridges as compile-time config structs (e.g. `K2526::A5120Config`), not runtime settings.
- `cparun/` is an independent sub-project (own `CMakeLists.txt`) and is kept unchanged.
