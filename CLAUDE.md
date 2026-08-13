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
> `doc/design/13_distribution.md` §6.1. **Offen bleibt die Paketierung** (Inno Setup,
> `install.ps1`, Windows-Job in `release.yml` — §10 Schritt 4).

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

## Verteilbares Paket (`packaging/`)

`packaging/build_payload.sh` schnürt aus dem Baum ein Anwenderpaket (~2 MB:
Kernbibliothek, GUI, `formats.yaml`, Beispieldisketten); `install.sh` darin holt sich mit
**`uv`** Python und Qt in ein venv **innerhalb der Installation** — benutzerlokal, ohne
Administratorrechte. Python/Qt werden bewusst nicht mitverteilt. Bedienung:
`packaging/README.md`, Entwurf und Begründungen: **`doc/design/13_distribution.md`**.
Umgesetzt sind Linux/macOS-Aufbau (Schritt 1+2); Windows (Inno Setup, per-user) steht aus.

Sieben Dinge, die man dabei nicht kaputtmachen darf:

- **`--uninstall` löscht in seinem Ziel, und das Ziel wird ERFRAGT.** Deshalb zwei
  Riegel in `install.sh`: Ziel werden darf nur ein leeres oder bereits von uns belegtes
  Verzeichnis (nie `$HOME`, nie `/`), und gelöscht wird nur, was sich ausweist
  (`.k1520emu-installation`, ersatzweise `VERSION`+`app/paths.py`+`share/k1520emu/`).
  Ohne das löschte die Antwort „`~`" beim Deinstallieren das Heimatverzeichnis — belegt,
  nicht theoretisch. Und gelöscht wird **nur das Inventar aus dem Ausweis** (die Einträge,
  die der Installer anlegte; ein Eintrag ist ein NAME, kein Pfad), nicht die Wurzel als
  Ganzes — fremde Dateien im Ordner überleben, dann bleibt auch der Ordner stehen. Guards:
  `test_installer_verweigert_*`, `test_deinstallieren_loescht_nur_eine_installation`,
  `test_deinstallieren_laesst_eigene_dateien_stehen`,
  `test_deinstallieren_folgt_keinem_pfad_im_ausweis`.
- **Ein Update findet seine Installation selbst** (`vorhandene_installation()` über den
  Starter-Symlink) und schlägt sie als Ziel vor. Ohne das legte ein `install.sh` ohne
  `--prefix` eine ZWEITE Installation am Standardort an und ließe die alte verwaisen.
- **`slim.py` strippt Bibliotheken, aber NIE Programme.** Der Interpreter von
  python-build-standalone überlebt `strip` in keiner Variante („allocated section `.dynstr'
  not in segment" → „undefined symbol: , version"); gearbeitet wird auf einer Kopie, und
  eine Warnung von `strip` verwirft sie. Ebenso: die `ldd`-Zeile wird an der **Ladeadresse
  am Ende** getrennt, nicht am ersten Leerzeichen — sonst kippt bei einem Installationspfad
  mit Leerzeichen die ganze Qt-Hülle in den Sicherheitsrückfall (223 statt 146 MB).
  Begründungen: `doc/design/13_distribution.md` §8.1.
- **`FormatCatalog` findet seine `formats.yaml` über den Pfad des *eigenen Moduls***
  (`dladdr` / `GetModuleHandleEx`, `format_catalog.cpp: moduleDir()`), nicht über
  `/proc/self/exe` — sonst sucht die per `ctypes` geladene Bibliothek neben dem
  venv-Python. Guard: `py_paths`.
- **Release-Bauten setzen `-DK1520_FORMATS_DEFAULT=`** — sonst trägt jede ausgelieferte
  Bibliothek den absoluten Pfad des Baurechners als Suchkandidaten. Guard: `py_packaging`.
- **Arbeitsdisketten liegen außerhalb der Installation**, im **Dokumentenordner**
  (`<Dokumente>/K1520emu/Disketten`), weil der Autosave in die gemountete Datei
  zurückschreibt. Der Ordnername ist sprachabhängig — maßgeblich ist `XDG_DOCUMENTS_DIR`
  aus `~/.config/user-dirs.dirs`, und die Regel steht ZWEIMAL (`paths.documents_dir()` und
  `dokumente_dir()` in `lib/common.sh`, damit `--purge` dort aufräumt, wo der Emulator
  schreibt; Guard: `test_dokumentenordner_shell_und_python_stimmen_ueberein`). Im Paket
  liegen die Abbilder **gepackt** (`*.hfe.gz`), ausgepackt wird beim ersten Start
  (`paths.seed_user_disks()`).
- **Produkt = `k1520emu`, Programm = `a5120emu`.** Installation, Paketname, `share/k1520emu/`,
  Datenordner und Marker tragen den FAMILIENnamen (der Bus, nicht der Rechner); Starter,
  Symbol und `.desktop` heißen nach der Maschine. Weitere K1520-Rechner bekommen ein eigenes
  Programm in derselben Installation: eigener Block beim Starterschreiben + `<name>.desktop.in`
  + Eintrag in `MASCHINEN` (`install.sh`), woran das Deinstallieren die Verknüpfungen findet.

Tests: `py_paths` + `py_packaging` (schnell, ohne Netz, in der Standardregression). Der
vollständige Installationslauf (lädt ~120 MB) liegt hinter
`K1520_PACKAGING_FULL=1 venv/bin/python3 -m pytest tests/python/test_packaging.py`.

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

The full CP/A cold boot works (boot ROM → SYL loader → secondary loader → CP/A boot system →
`@OS.COM` → running OS at the interactive prompt); the ZVE1↔ZVE2 DMA handshake is **solved**.
This section is the reusable debug/trace toolkit for the boot path (still the trickiest code to
poke at); `doc/analyse_zre_rom_boot.md` + `doc/K1520_architecture.md` §14 hold the analysis.

> **Start here: `tools/how_to_debug_and_trace.md`** is the task-oriented guide for the two
> debug/trace tools (which one when, with worked scenarios). Full references:
> `tools/k1520dbg.md` and `tools/boot_trace.md`. Two tools, complementary:
> - **`boot_trace`** — non-interactive: run a boot to a cycle limit / `--until <cond>`,
>   get a report (milestones, `[03F8]` done-flag, PC histograms, VRAM banner). **Locates**
>   *where* the DMA/boot hangs; also `--coverage`/`--diff`/`--csv` exports, `--fold`
>   (PC-period loop-collapse — crushes even register-varying hot loops), `--itrace`
>   (accepted-INT/NMI CSV), and a K5122 read-attempt log via `--log-level info|debug`.
> - **`k1520dbg`** — interactive gdb-style: breakpoints (incl. conditional / `bint`/`bnmi`/
>   `breti` and floppy `bbusrq`/`bxfer` event BPs), step into/over/out, `rs` reverse-step +
>   `rc` reverse-continue + `snap`/`snap diff`/`savestate`, watch mem/io, `logpoint`,
>   `itrace`, `x` examine, exact history `bt`, `where` (both CPUs at a glance), `hist`
>   (PC hotspots of both CPUs), `disk verify` (medium CRC health), `vars -f` (loadable
>   dashboard), `dev ctc/pio/sio` chip state, `help floppy`/`help dualcpu`. **Dissects** a
>   located problem. Full command list + the Dual-CPU/Floppy-read recipe:
>   `tools/how_to_debug_and_trace.md` §0b.
>
> **Run efficiently (this matters for the agent):**
> - **Invoke via `tools/dev.sh`** (rebuilds first → never a stale binary, see Build & test):
>   `tools/dev.sh trace <boot_trace-args>` and `tools/dev.sh tool k1520dbg <args>`. The bare
>   tool names in the examples below stand for these wrappers.
> - ✅ **Disk safety is now the default (Copy-on-Write).** Both tools copy the disk to a
>   temp file and mount that, so a committed fixture can't be corrupted — no more
>   `mktemp; cp DISK $D; … $D; rm $D` ritual; just pass the disk directly. Use `--rw` only
>   when a write must persist (e.g. FORMAT tests), then work on your own temp copy.
> - boot_trace: `-L /dev/null` discards the verbose emulator log; **`--quiet --json`** gives
>   exactly one machine-readable result line (instead of ~880) + a meaningful exit code
>   (`--until`: 0 met / 2 not met). Prefer **`--until <cond>`** over guessing cycle counts.
> - k1520dbg: drive it in one shot via a pipe (`printf 'b 0x0437\ng\nrj\nq\n' | k1520dbg $D`)
>   or `-x script.dbg`; `rj` prints registers as JSON. The interactive REPL/readline is for
>   humans — the agent uses batch mode.
> - **Boot once, resume often:** `--save-state`/`--load-state` (boot_trace) and
>   `savestate`/`loadstate` (k1520dbg) persist RAM+CPU+ROM-mapping to a file, so the ~2 s
>   boot is a one-time cost. Load `-l <bios.prn>` to see commented source instead of raw disasm.

Supporting tools (`tools/`):

- `tools/z80_disasm2.py` — the canonical generic Z80 disassembler (configurable `--org`, repeatable `--entry`/`--label`). The other two disassemblers are format.com-specific.
- **`k1520dbg`** (`tools/k1520dbg.md`) — the interactive debugger; expression-conditioned breakpoints, reverse-step, save-state, and `.prn`/symbol annotation make hand-disassembling RAM dumps mostly unnecessary. Delegate heavy log/trace reads to the `log-trace-analyzer` subagent.
- **`.prn`-Listing-Annotation (`-l`, both `k1520dbg` and `boot_trace`)** — instead of hand-disassembling RAM dumps, load the commented MACRO-80 source listing of the running code (e.g. `-l ~/projects/CPA_Workbench/build/bios.prn`) and every disassembly/trace line + PC-histogram entry whose address is in the listing gets the **original label+mnemonic+comment** appended. Repeatable (multiple listings cover different ranges); `@OFFSET` (signed, `0x..`/`..h`/dec) relocates a listing's addresses to the runtime load address. Only absolute addresses — a BIOS listing covers ~`0xD200+` (and BIOS pieces mapped low, e.g. the CONIN keyboard poll at `0x041C–0x042B`). Parser: header-only `tools/prn_listing.h` (tests `tests/debugtools/test_prn_listing.cpp`, gtest suite `PrnListing`). See `tools/k1520dbg.md` §6 / `tools/boot_trace.md` §4.
- **Fremdquellen `.MAC`/`.ASM` (`-l quelle.mac[@auto]`)** — für Fremd-OS (UDOS, SCPX …) gibt es kein `.prn`, nur reinen Quelltext ohne Adressspalte. `tools/mac_listing.h` **assembliert** ihn (Opcode-Tabelle wird zur Laufzeit aus `z80dis_min.h` *rückwärts* erzeugt; `ORG`/`EQU`/`DB`/`DW`/`DS`, `Mxxxx`-Adressanker mit Selbstkorrektur) und liefert dieselbe Adresse→Quellzeile-Tabelle. **`@auto`** bestimmt den Ladeversatz selbst (Objektbytes im RAM suchen, alle Kandidaten bewerten) und urteilt über die Passung (*identischer Build* … *anderer Build*). Ergänzend `verify <datei> @<adr>` (Datei↔RAM-Abgleich) und `dump <adr> <len> <datei>`. Tests `tests/debugtools/test_mac_listing.cpp` (`MacListing`). Doku: `tools/k1520dbg.md` §6.1, `tools/how_to_debug_and_trace.md` §0d.
- **break-before-execute (Debugger-Halt)** — `Z80::abortBeforeExecute` (nur ausgewertet, wenn ein `traceCallback` installiert ist → im Produktivlauf gratis): fordert der Trace-Callback einen Halt an, kehrt `step()` mit **0 Takten** zurück und die Instruktion läuft NICHT. `A5120Machine::run` behandelt `used==0` als Laufende. Damit zeigen Haltezeile und jede Folgeabfrage (`r`/`rj`/`where`/`snap`/`savestate`) denselben Zustand — vorher lief die Instruktion noch zu Ende (Haltezeile 0135, `r` 0136). `k1520dbg` überspringt beim Fortsetzen einmalig die Halteprüfung auf der aktuellen Adresse (sonst hielte `g` sofort wieder), und `s` zählt so, dass N Instruktionen ausgeführt werden und VOR der (N+1)-ten gehalten wird (gdb-Semantik). Guards: `Z80Test.AbortBeforeExecute_*`, `MachineRunControl.*`, `cli_dbg_stop_is_before_instruction`, `cli_dbg_resume_past_breakpoint`, `cli_dbg_step_shows_next_instruction`.
- **Debugger-Regressionsnetz** — `ctest -R cli_dbg_` sichert `k1520dbg` ab: `cli_dbg_all_commands_smoke` fährt über `tests/cli/scripts/all_commands_smoke.dbg` **jedes** Kommando einmal an (schlägt fehl, sobald eines aus der Dispatch-Kette fällt), dazu ~30 gezielte Tests auf den Meldungs-Wortlaut. Neue Kommandos gehören in beide. `MacListing.RoundTripsEveryDecodableInstruction` prüft Assembler↔Disassembler über den ganzen Befehlssatz.
- **Interrupt-Diagnose** — `k1520dbg ivt` zeigt die IM-2-Vektortabelle (Vektor → Tabelleneintrag → Gerät → Status; findet die scharfe Quelle ohne Tabelleneintrag), `dev pio [all|bs|k5122ctrl|k5122data]` jetzt inkl. **beider K5122-PIOs**, `bint`/`--itrace` melden Vektor **und Quellbaustein** und unterscheiden `SPURIOUS` (kein Gerät hat quittiert) vom Vektor 0xFF. **Lauf-Budgets (`g N`) zählen die Maschinenuhr (beide CPUs)** — `clock zve1` schaltet zurück; Ctrl-C bricht einen langen Lauf ab.
- `tools/disasm_difftest.py` — cross-checks the disassembler against the `z80dis` pip package (in `venv`); run it before changing the disassembler engine.
- `tools/boot_trace.cpp` (`boot_trace` target) — traces **both** ZVE1 and ZVE2 per instruction and reports where the DMA freezes. Use `-L <file>` to divert the emulator log so the summary stays readable. A separate `build_trace/` build dir is conventionally configured with `-DLOG_LEVEL=5` (the compile ceiling). **Default base level is now ERROR — the run is quiet & fast.** Raise it with `--log-level <off|error|warn|info|debug|trace>`, or far better, boost only where it matters: `--log-pc LO:HI[:level]` (effective level while either CPU PC is in the range) and `--log-cycle FROM:TO[:level]` (while the cycle counter is in the window). **Gotcha:** a `--log-pc` gate on a *spin-loop* address fires for as long as the CPU parks there (can be tens of millions of cycles → multi-GB log) — pair it with a tight `--log-cycle`, or just use a cycle window. Reference: `boot_trace --log-level info …` (≈11 KB / 8 s for a full @OS.COM run) gives the K5122 `>>> READ` summaries; add a `--log-cycle` window for full TRACE only there.

### Boot chain — SOLVED (don't regress these invariants)

The full chained boot (ROM `0x01DD` → SYL loader `0x0437` → secondary loader `0x062E` → CP/A
boot system `0x1800` → `@OS.COM`) runs end-to-end into the running OS. The read path was
refactored to the format-agnostic TrackImage stack (§ "K1520 core architecture" and
`doc/K1520_architecture.md` §8.5/§14.5); the load-bearing boot invariants a future editor
must **not** break:

1. **Concurrent ZVE1/ZVE2 stepping during DMA** (`A5120Machine::run`): while `/BUSRQ` is held
   and ZVE2 active, step ZVE2 **and fall through to also step ZVE1** (parallel on real HW). ZVE1
   must finish `CALL 0194` (tail writes `[0x03F8]=0`) and reach its poll loop `0x0168` before ZVE2
   writes `[0x03F8]=3`, else the late `=0` clobbers the `=3` and boot hangs.
2. **Transition-based completion watch** on `[0x03F8]` (0=running / 1=timeout / 3=done): the run
   loop arms only after seeing `[0x03F8]!=3` (ZVE1 cleared it) and *then* treats `→3` as completion
   — level detection fires on the stale `3` left from the previous round.
3. **ZVE2 start-from-reset** (`K2526::zve2StartFromReset`): the 3rd stage poises ZVE2 via
   `[0x0000]=JP 0x1F7D` + `OUT(04)=0x00` and restores `[0x0000]` immediately (no explicit bit0=1
   start), so `run()` starts ZVE2 from PC=0 when `/BUSRQ` asserts while ZVE2 is in reset. `OUT(04H)`
   bit0=1 also restarts ZVE2 from PC=0 every DMA round (reloads its IDAM regs).
4. **Faithful read stream + MK/MK1 resync** (`K5122`): `startReadTransfer()` streams
   `buildFaithfulReadTrack` (4×A1 sync — serves boot ROM *and* SYL loader); MK (ctrl Port A bit1) and
   **MK1 (bit4)** re-sync edges call `resyncToNextMark` (IDAM→DATA→next IDAM). The **MK1 resync was
   the final `@OS.COM` fix** — without it a data `0xA1` was mistaken for the A1 address-mark sync.
   Standard IBM-CCITT CRC throughout.
5. **Head-select = ctrl Port A bit2 (/FR)**, latched only at the `/STR` edge (bit5 is step DIRECTION
   only, toggles with MK/MK1). **Track-end `/BUSRQ` release** on `OUT(13H),03H` during a 128-B read
   (ZVE1 takes over before ZVE2's idle loop `L0696` corrupts `[07F8..07FC]`).
6. **Asymmetric mixed geometry** — now declared in **`data/formats.yaml`** (`cpa780`:
   `{0,0,0,1,26,128}` + `{1,1,0,0,26,128}` + `{1,1,1,1,5,1024}` + `{2,79,0,1,5,1024}`, all MFM);
   index period `≈490000` cycles. Guarded by `test_format_catalog`
   (`BootKritischeGeometrien_Unveraendert`). **Do not declare the 128-B system tracks as
   `encoding: fm`** — the disks are plain IBM-MFM and the ROM's FM→MFM trial-and-error depends on
   it (§14.5).
7. **/WR (BS-PIO Port A, A5) ist ein Strobe, kein Dauerpegel** (`K2526::pulseWriteStrobe`, pro
   ZVE1-Schreibzyklus gepulst; A0 `/M1` und A6 `/RDY` bleiben dauernd aktiv). Die
   Speicher-Ausbaumessung des Lade-ROMs (`0040H–005AH`) schärft Port A mit Maske `9FH`
   (A5 AND A6, aktiv-LOW), gibt `EI` und will den Interrupt **durch** das Testschreiben —
   ihre ISR (`007AH`) prüft, ob das Byte ankam. Dauerpegel ⇒ Interrupt schon beim `EI` ⇒
   die ISR sieht den ALTEN Speicherinhalt: bei frischem DRAM (0xFF) unauffällig, bei
   **Reset/Power-Cycle aus dem laufenden Betrieb** meldet sie „kein Speicher" und der
   Neustart entgleist. Dazu gehört, dass ein Interruptsteuerwort mit IE=0 eine anstehende
   Anforderung **verwirft** (`Z80PIO::writeCtrl`) — sonst bleibt die vom Stack-Push der
   Interruptannahme neu gesetzte Anforderung liegen. Guards: `test_k2526`
   (`K2526WriteStrobe.*`), `test_hardy` (MEMDI-RDY-Test nutzt dieselbe Maske).
8. **Reset ist ein SYSTEMWEITER /RESET, nicht nur die CPU** (`A5120Machine::resetHardware()`,
   von `reset()` **und** `powerOn()` benutzt). Die /RESET-Leitung des Backplane räumt alle
   Bausteine ab: `Z80CTC::reset` / `Z80PIO::reset` / `Z80SIO::reset` (neu),
   `K2526::powerOn` (Q302-CTC + BS-PIO), `K5122::reset` (Transfer abbrechen, /BUSRQ frei,
   PIOs; Disketten/Kopfposition bleiben), `K8025::reset`, `K7637::reset`, dazu
   NMI/INT/WAIT lösen + `markIntDirty()`. **Ohne das** zählte nach einem Reset aus dem
   laufenden Betrieb der System-CTC mit der IM2-Vektorbasis des alten OS (`vecBase=F8`,
   INT frei) weiter → der erste Timer-Interrupt nach dem `EI` des Lade-ROMs landet auf
   einem Fantasie-Vektor aus der ROM-Seite 0 → Boot-Kette entgleist (genau der Fall
   „nach der Uhrzeit-Eingabe am `A>` geht weder Reset noch Power ON"). `powerOn()` löscht
   zusätzlich das DRAM (`ops_.fill(0xFF)`) — Netz-Aus verliert den Inhalt, `reset()` nicht.
   Guards: `test_boot_integration` (`RestartFromInteractivePromptRebootsFromRom`,
   `ResetFromRunningSystemRebootsFromRom`, `PowerCycleFromRunningOsRebootsFromRom`).

Handshake RAM: `[0x03F8]` done-flag, `[0x03F7]` index counter, `[0x03FD]` path byte (`0x87`),
`[0x07F2]` target sector count, `[0x03F0]` load address. Key addresses: ZVE1 wait `0x0168`,
ZVE2-start `0x0194`, ZVE2 entry `0x01DD`, index ISR `0x01C7`, SYL sig check `0x01B6`, loaded code
`0x0437`, secondary loader `0x062E`, 3rd stage `0x1800`/read `0x1F7D`. Guard tests:
`test_boot_integration` (`Stage3_FullyLoadsAndJumpsToOs`), `test_k5122`
(`Continuous1024_MK1ResyncJumpsToNextAddressMark`), `test_k2526` (`K2526ZVE2FloppyChain`). Full
analysis: `doc/analyse_zre_rom_boot.md`, `doc/analyse_bootloader.md`, `doc/K1520_architecture.md`
§14.5/§14.5b/§14.5c.

`boot_trace` post-boot tracing: `-p <cycles>` continues past `0x0437`; the summary then
adds an I/O-port read/write histogram, VRAM write count + range, a loaded-code PC
histogram, and an 80-col text dump of VRAM (`0xF800`) so the screen banner is visible.

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

**Parallelität — Bau-Kollision beachten:** Mehrere Agenten, die gleichzeitig `build/` (oder
`build_trace/`) anfassen, kollidieren beim `cmake --build` (Race/kaputte Binaries). Daher: build-/
test-berührende Delegationen **sequenziell** laufen lassen ODER dem Agenten ein eigenes Worktree
geben (`isolation: "worktree"`). Read-only-Agenten (`code-explorer`, `log-trace-analyzer`,
`boot-disasm-analyst` gegen ein bereits gebautes `./build/…`) parallelisieren gefahrlos. Hintergrund-
Agenten sind langsam (ein voller Boot unter `k1520dbg`/`boot_trace` ~2 s bis Minuten je Aufgabe) —
nicht mit „hängt" verwechseln; sie melden sich bei Abschluss selbst.

## k1520DiskTool — Dateiaustausch mit Disketten (`core/filesystem/`, `app/disktool/`)

Zweites Anwenderprogramm neben dem Emulator: holt Dateien von CP/A-, SCPX- und
**UDOS**-Disketten und schreibt sie zurück (`.img`/`.hfe`/`.dmk`).  Es teilt sich mit dem
Emulator die Container-/Medium-Schicht, hat aber **eine eigene Bibliothek**
(`libk1520disk.so`) ohne Z80 und Karten.  Voller Entwurf: `doc/design/13_k1520disktool.md`,
Bedienung: `tools/k1520disktool.md`.

```
core/filesystem/   SectorSpace (physisch + linear) · GeometryProbe (Erkennung Stufe 1)
                   FsProfile/FsCatalog · CpmFileSystem · UdosFileSystem · DiskVolume
core/api/k1520_disk_api.*   C-ABI  →  libk1520disk.so
tools/k1520disktool.cpp     CLI    →  tools/dev.sh tool k1520disktool ls <abbild>
app/disktool/               PySide6-Oberfläche  →  bash run_disktool.sh
```

Was beim Weiterarbeiten zu wissen ist:

- **Bootfähige Disketten (2026-08-12, `doc/design/13_k1520disktool.md` §13a).**  Das
  Werkzeug legt Disketten mit **Bootabbild** an: `create --fs NAME --boot datei.bin`
  (GUI: Rückfrage + Dateiauswahl bei „Neue Diskette", Gegenstück „Bootabbild sichern…"
  = `boot-get`).  Das Abbild ist ein **rohes Byteband** über die Systemspuren, deren
  Umriss je Familie feststeht: CP/M = alles vor `data_cyl`/`data_head` (cpa780: 15104 B),
  UDOS = Spuren 0–2 **plus Bootspur 21** (13312 B je Seite — ohne die Bootspur bricht
  der UDOS-Kaltstart mit `ERROR: 45` ab).  **Geprüft wird VOR dem Formatieren**, sonst
  bliebe bei einem zu grossen Abbild eine halbe Diskette liegen; kürzer ist erlaubt.
  Fertige Abbilder: `disks/boot_{cpa780,scpx640,scpx798,udos43}.bin`.  Wächter
  `test_disktool_bootdiskette` — baut die Diskette mit dem Werkzeug und **bootet sie**
  (CP/A bis `A>`, SCPX in beiden Geometrien, UDOS bis `%`).
- **UDOS-Dateien tragen mehr als ihre Bytes (2026-08-12, `doc/udos_diskettenformat.md`
  §6/§14).**  Der Kopfsektor steuert, wie UDOS eine Datei **lädt**; am Ende (Offset
  122/124/126) stehen **LOW ADDRESS / HIGH ADDRESS / STACK SIZE** — genau das, was
  `EXTRACT` im laufenden System meldet.  Der Lader trägt LOW/HIGH nach `(1275H)/(1277H)`
  und lässt sie vom Speicherverwalter (`1009H`) zuteilen; stehen dort `FFFF`, bricht er
  mit **`MEMORY PROTECT VIOLATION`** ab (Fehler `43H`, Meldungstabelle `13C6H`/`12B2H`,
  Index = A−40H).  Ebenso maßgeblich: **Offset 17** ist NICHT immer die Kopie der
  Satzlänge (bei 256/512 = 0) — mit dem falschen Wert startet ein neu geschriebener
  Nukleus (`OS`) nicht mehr.  Der Kopfsektor ist damit lückenlos zugeordnet; berechnet
  werden beim Schreiben nur Zeiger (6–11), Satzanzahl (13) und Bytes im letzten Satz (22).
  Alles andere führt das Werkzeug mit: `WriteOptions::udos_*` / `UdosAttrs` →
  CLI `put --type/--props/--entry/--record-len/--block-len/--segment/--mem/--extra/
  --created/--date`, `attr` zeigt und ändert sie an einer vorhandenen Datei, und ein
  **Beiblatt** `udos-dateiangaben.txt` (Schlüssel=Wert) trägt sie durch `get`→`put`.
  C-ABI: `k1520d_entry_*` + `k1520d_set_udos_attrs`.
- **UDOS-Bootdisketten laufen (2026-08-13).**  `get` → `create --boot` → `put` ergibt
  eine Diskette, die den Selbststart fährt (`OS.INIT`: Banner, `DATE`) und **Befehle
  ausführt** (`CAT`, `STATUS`, `PRINT`).  Der letzte Stolperstein war: **das
  Speicherabbild einer Programmdatei reicht über ihr logisches Dateiende hinaus** —
  `OS` ist 5504 Byte lang (`bytes_in_last`), sein Abbild 5632 (11 volle Sätze à 512),
  und in den 128 Byte dahinter steht Nukleus-Code, in den er selbst springt (`2580H`).
  Wer auf `length()` kürzt, bekommt eine Diskette, die bootet und beim ersten Befehl in
  den Monitor fällt (`BREAK 4150`).  Deshalb liefert `UdosFileSystem::readChain` **volle
  Sätze**, sobald `segment_len > length()`, und `bytes_in_last` wird mitgeführt
  (`rest=` im Beiblatt) statt ausgerechnet.  Kleinste bootfähige Diskette:
  Systemspuren + `OS` + `ZDOS` (Urlader sucht beide über das VERZEICHNIS).  Wächter:
  `DiskToolBootdiskette.GebauteUdosDisketteBootetUndFuehrtBefehleAus`.
- **Dateiangaben sehen und ändern (2026-08-13, `doc/design/13_k1520disktool.md` §13c).**
  Rechtsklick/Doppelklick auf eine Datei → **Eigenschaften-Dialog**
  (`app/disktool/ui/properties_dialog.py`): UDOS-Kopfsektor voll editierbar, CP/M
  Nutzerbereich + R/O/SYS/ARC.  Dafür kam **`CpmAttrs` als zweite Überladung** von
  `FileSystem::setAttributes` (nicht eine gemeinsame Struktur — die Familien haben
  fachlich nichts gemeinsam), C-ABI `k1520d_set_cpm_attrs` + `k1520d_entry_bytes_in_last`,
  CLI `attr --ro/--sys/--arc/--user`.  Drei Festlegungen: **(1)** Der Nutzerbereich ist
  IDENTITÄT, kein Attribut — `--user` verschiebt nach `3:NAME.TYP` und wird abgelehnt,
  wenn dort schon eine gleichnamige Datei liegt; geändert werden **alle Extents**.
  **(2) Satzlänge und „Bytes im letzten Satz“ sind nicht änderbar** (sie bestimmen die
  Sektorlage; Weg dahin ist `get` + `put --record-len`) — der Dialog fasst den *Inhalt*
  einer Datei nie an.  **(3)** Geschrieben wird nur, was sich unterscheidet
  (`aenderungen()`), sonst bewegte ein blosses Ansehen das Änderungsdatum.
  Dazu ein **CP/M-Beiblatt `cpm-dateiangaben.txt`** analog zum UDOS-Beiblatt (ohne es
  ging der Nutzerbereich beim Rundlauf `extractAll`→`insertAll` verloren; `zielName()`
  benutzen **`checkFit` und `insertAll` gemeinsam**, sonst urteilt die Platzprüfung über
  einen anderen Namen als die Ausführung).  Das **Archiv** druckt seitdem alle Angaben
  als zweite Tabelle „DATEIANGABEN IM EINZELNEN“ — für die Wiederherstellung von Hand;
  maschinell reichen die Beiblätter im selben Archiv.  Wächter: `CpmFileSystemAttrs.*`,
  `DiskVolume.CpmBeiblatt*`, `py_disktool_gui`.
- **`data/formats.yaml` hat jetzt ZWEI Sektionen.**  `formats:` (Physik, liest der
  Emulator) und `filesystems:` (logische Ebene, liest nur das DiskTool).  `data_start`
  ist dort eine **Spur**, kein Byte-Offset — bei gemischter Geometrie (cpa780: drei
  128-B-Seiten, dann 1024 B) wäre er als Spurzahl gar nicht ausdrückbar; cpmtools trägt
  deshalb `offset 15104` ein, was der `SectorSpace` aus `data_start c2h0` ausrechnet.
  Mehrere Dateisysteme je Geometrie sind möglich (26×128 trägt UDOS *und* CP/M), aber
  selten — die Sektion soll **kurz bleiben** (s. u.).  Neue Formatnamen gehören in die
  Erwartungsliste von
  `FormatCatalog.Formatnamen_SindEinStabilerVertrag` bzw. `FsCatalog.ProfilnamenSind…`.
- **Ein fehlendes Dateisystemprofil ist KEIN Hindernis mehr — `CpaDpbRule` rechnet.**
  `core/filesystem/cpm/cpa_dpb.{h,cpp}` bildet die Formaterkennung des CP/A-BIOS nach
  (`biosdsk.mac`/`drdfrm`, Tabellen `dtrsl0..3`; Analyse: `doc/cpa_format_detection.md`):
  aus Sektorlängencode der Datenspur (**Zylinder 3, Kopf 0** — `dlgint`, einseitig
  adressiert), Spurzahl, ein-/beidseitig und dem Inhalt der Spur 0 entstehen
  Systemspuren, Blockgröße, Verzeichnisplätze und Sektorversatz.  Ein benanntes
  Katalogprofil **gewinnt immer**; die Ableitung ist der Rückfall und heißt `cpa_auto`
  (`--fs cpa_auto` erzwingt sie).  Damit sind **104 von 117** erzeugten Abbildern
  mountbar (vorher 12).  Die Regel reproduziert `cpa780`/`scpx798` exakt und korrigierte
  dabei einen geratenen Wert: **`cpa800` hat 192 Verzeichnisplätze, nicht 128** — am
  laufenden CP/A nachgewiesen (`DiskToolNeueDisketten.CpaFindetDateiJenseitsVonPlatz128`).
  Beim Ändern der Tabellen: `test_cpa_dpb` hält sie gegen `biosdsk.mac`.
- **Doppelschritt (`step: 2`) ist umgesetzt** (2026-08-11, war
  `doc/feature_requests/doppelschritt_disketten.md`).  `tracks:` bleibt **logisch**,
  `DiskFormat::physicalCylinder()` rechnet um; die Spurnummer im **ID-Feld ist die
  logische** (physisch c4h0 meldet `cyl=2`) — sonst verwirft der Gast-Treiber jeden
  Sektor.  Berührt `SectorSpace` (Slot kennt beide Nummern), `ImgCodec` (`.img` ist
  logisch), `DiskImage::create` (ungerade Zylinder bleiben unformatiert), `GeometryProbe`
  (die Lücken sind ein **positives** Kriterium, sonst würde eine gewöhnliche
  40-Spur-Diskette verwechselt) und `formatFitsDrive` (physische Ausdehnung).
  Guards: `ctest -R Doppelschritt` + `DiskToolNeueDisketten.CpaLiestDoppelschrittDiskette`.
- **Ein fehlender `formats:`-Eintrag ist auch kein Hindernis mehr.** Passt keine
  Katalogsgeometrie, baut `GeometryProbe::synthesize()` eine aus der Messung
  (`detection().format == "(gemessen)"`) — Spurbereiche als echte **Rechtecke** (erst
  Zylinder mit gleichem Kopf-Muster, dann die Köpfe; sonst bekäme cpa780 einen Bereich,
  den es nicht gibt), Lückenmuster als `step: 2`. **Ein so gelesener Datenträger ist
  unaufhebbar schreibgeschützt** (`setReadOnly(false)` verweigert,
  `readOnlyForced()`) — die Geometrie ist geraten, nicht belegt. Abgewiesen wird
  weiterhin, was keinen zusammenhängenden Sektorraum ergibt (Loch mitten im
  beschriebenen Bereich, uneinheitliche Sektorgrößen INNERHALB einer Spur).
  Dabei fiel eine alte Schwäche auf: „zu wenige Sektoren" war ein Schaden **ohne
  Obergrenze**, sodass 7×512 als „k5601_ss40_9x512 mit 40 defekten Spuren" durchging —
  jetzt ist mehr als ein Viertel abweichender Spuren ein anderes Format (Regel 4b).
- **`filesystems:` soll KURZ bleiben.** Vier der fünf CP/M-Profile rechnet `CpaDpbRule`
  bitgleich nach; sie stehen nur noch da, weil `create --fs NAME` einen Namen braucht und
  „cpa780" die bessere Auskunft ist als „cpa_auto". Ein neuer Eintrag braucht einen
  eigenen Grund. `cpa640` (Dateisystem ab Spur 0) wurde 2026-08-11 **entfernt**: CP/A
  kann so eine Diskette nicht erzeugen (`dtrsl1` hat ein FESTES Offset von 4 log.
  Spuren), der Eintrag machte nur jede 16×256-Diskette „nicht eindeutig". Guard:
  `FsCatalog.SechzehnMalZweihundertsechsundfuenfzigHatNurEinProfilAbZylinderZwei`.
- **Wächter „alle Formate sind mountbar"**:
  `DiskVolume.JedesKatalogformatLaesstSichAnlegenUndWiederOeffnen` legt JEDES
  `formats:`-Format an, öffnet es ohne `--fs` und prüft die Wiedererkennung.  Ein neuer
  Katalogeintrag, den die Erkennung nicht wiederfindet, fällt sofort auf.
- **`TrackCodec::writeSector`** ersetzt ein Datenfeld an Ort und Stelle und rechnet die
  CRC neu.  `buildTrack()` taugt zum Schreiben **nicht**: es baut die Spur neu und
  verlöre die Bytes hinter der Daten-CRC — bei UDOS die gesamte Dateiverkettung.
- **UDOS: jede Seite ist ein eigenes Dateisystem**, für den Anwender aber EINE Diskette.
  `DiskVolume` führt beide als `Side0`/`Side1`; `extractAll` legt die Unterverzeichnisse
  an, `insertAll` verlangt sie.  UDOS auf `.img` ist unmöglich (Kontrollblock hinter der
  Daten-CRC) und wird abgelehnt.
- **Stapeloperationen sind Transaktionen**: erst planen und urteilen, dann schreiben;
  ein Fehler rollt die Momentaufnahme des `DiskMedium` zurück.  `list()` liest **immer**
  frisch aus dem Medium — es gibt keinen zwischengespeicherten Verzeichnisstand.
- **Kreuzproben statt Selbstbestätigung** (`ctest -R DiskTool.*Roundtrip`, Label
  `format_integration`): geschrieben wird mit dem Werkzeug, gelesen vom **laufenden
  CP/A** (`TYPE`/`DIR`) bzw. **UDOS** (`CAT`/`PRINT`/`STATUS`).  Der CP/M-Lesepfad ist
  zusätzlich byteweise gegen `cpmtools` verifiziert (nicht als Abhängigkeit — die
  Prüfsummen im Test frieren das Ergebnis ein).

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
  — `tools/dev.sh test-matrix` (~200 s wall bei `-j8`). Die Matrix wird beim `cmake` aus
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
