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
bash run_gui.sh      # sets LD_LIBRARY_PATH=build and runs app/main.py
```

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
> Doppelschritt, bootfähige Disketten, Diskeditor, Eigenschaften-Dialog, Aufbau der
> Oberfläche, Arbeitsverzeichnisse). Entwurf: `doc/design/13_k1520disktool.md`,
> Bedienung: `tools/k1520disktool.md`. Vier Dinge, die man ohne Nachschlagen wissen muss:
> - **UDOS/ZDOS auf `.img` ist unmöglich** — die Dateiverkettung steht im Gap hinter der
>   Daten-CRC; `rawCompatible()` sperrt es. Bei UDOS1715/NDOS ist `.img` dagegen **erlaubt**
>   (dort trägt die Verkettung in eigenen Zeigersektoren).
> - **`TrackCodec::writeSector` ersetzt ein Datenfeld an Ort und Stelle.** `buildTrack()`
>   taugt zum Schreiben NICHT: es baut die Spur neu und verlöre alles hinter der Daten-CRC.
> - **Stapeloperationen sind Transaktionen** — erst planen und urteilen, dann schreiben; ein
>   Fehler rollt die Momentaufnahme des `DiskMedium` zurück. `list()` liest **immer** frisch.
> - **`filesystems:` in `data/formats.yaml` soll KURZ bleiben**: `CpaDpbRule` rechnet die
>   meisten Profile bitgleich nach, ein neuer Eintrag braucht einen eigenen Grund.

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
> - **Physisch heißt schreibgeschützt, bis jemand widerspricht** — ein Fehler kostet hier
>   nicht eine Kopie, sondern die einzige noch existierende Diskette.

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
