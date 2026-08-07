# Testsystem: Bestandsaufnahme und Umbauvorschlag

**Stand:** 2026-08-07, Branch `rework_testsystem` (Basis `main` @ 983fc1d)
**Umgesetzt:** Schritte 0–10 (alle bis auf 11 „Kür" und 12 „Befunde") — §6
**Zweck:** Ist-Landschaft der Tests vollständig erfassen, Schwachstellen benennen,
eine Zielstruktur und einen schrittweisen Migrationsweg vorschlagen.

Alle Zahlen sind gemessen (`ctest -N`, `ctest -j8`, `wc -l`), nicht geschätzt.

---

## 1. Inventar — was heute existiert

Ursprünglich sechs unabhängige „Testpakete“ mit vier Ausführungswegen; nach dem Entfernen des
Legacy-Pakets (§6) sind es **fünf Pakete unter einem einzigen Ausführungsweg (ctest)**:

| # | Paket | Ort | Framework | Fälle | Registriert in | Laufzeit |
|---|-------|-----|-----------|-------|----------------|----------|
| 1 | ~~**Legacy-Harness** (Z80/Memory/Floppy des alten `src/`-Emulators)~~ | ~~`tests/test_main.cpp` (929 Z.)~~ | ~~eigenes `TEST`/`END_TEST`-Makro-Framework~~ | ~~58~~ | **entfernt** (§6) | — |
| 2 | **Core-Unit-Tests** (Primitives, Bus, Karten, Floppy-Stack, Util) | `tests/cpp/test_*.cpp` (23 Dateien) | GoogleTest | ~570 | `gtest_discover_tests` in `CMakeLists.txt` | < 1 s |
| 3 | **Debug-Werkzeug-Unit-Tests** | `tests/cpp/test_{expr_eval,until_cond,event_bp,mem_watch,dbg_commands,coverage_diff,callstack_tracker,prn_listing}.cpp` | GoogleTest, testen **Header aus `tools/`** | 62 | wie 2 | < 1 s |
| 4 | **Maschinen-Integrationstests** | `tests/cpp/test_{boot_integration,machine_snapshot,a5120_disk_api}.cpp` | GoogleTest, echter Kaltboot | 32 | wie 2 | 0,2–1,7 s je Fall |
| 5 | **CLI-/Blackbox-Tests** der Werkzeuge | **direkt als Shell-Einzeiler in `CMakeLists.txt`** (Z. 500–597) | `add_test` + `PASS_REGULAR_EXPRESSION` | 19 | `CMakeLists.txt` | je < 1 s |
| 6 | **System-/Format-Integration** (langsam) | `tests/system/test_scpx_init.cpp`, `test_hardy.cpp` + `tests/system/drivers/make_bootdisk.py` | GoogleTest bzw. Python-Treiber | 8 | Label `format_integration` | Minuten (Timeout 600 s je Boot-Disk-Test) |

**Summen (nach Entfernen des Legacy-Pakets):** 669 ctest-Fälle — 661 in der Schnellrunde,
8 mit Label `format_integration`. Alles läuft jetzt unter ctest.
Schnellrunde gemessen: **1,94 s wall** (`-j8`), 100 % grün, 1 Test deaktiviert.

### 1.1 Nicht registrierter Test-Wildwuchs

Diese Dateien sind **eingecheckt, heißen `test_*`, werden aber von nichts referenziert**
(weder CMake noch `dev.sh` noch ein Skript):

| Datei | Größe | Status |
|-------|-------|--------|
| `test_boot.py` | 4,0 KB | Root; lädt `libk1520core.so` via ctypes, Boot-Smoke |
| `test_boot_detailed.py` | 4,4 KB | Root; Boot-Diagnose |
| `test_integration.py` | 5,4 KB | Root; C-API-Integrationstest via ctypes |
| `test_qt.py` | 2,3 KB | Root; **importiert PyQt5** — die GUI nutzt PySide6 ⇒ toter Code |
| `test_c_api.c` | 2,4 KB | Root; `dlopen`-Smoke der C-API, kein Build-Target |
| `test_reti.cpp` | 7,9 KB | Root; RETI-Erkennung über CTC/PIO/SIO, kein Build-Target |

Dazu im Root: `disk_b.img` (800 KB, eingecheckt), 40+ `k1520_*.log` (ignoriert, u. a. eine
115-MB-Datei), `analyze_vram.py`, `analyze_eprom.py`, `disasm_k2526.py`.

### 1.2 Manuelle Testtreiber (kein ctest, aber Testzweck)

| Werkzeug | Ort | Zweck |
|----------|-----|-------|
| `format_all.py` | `tools/` | formatiert + verifiziert alle K5601-Formate über `format_driver` |
| `make_bootdisk.py` | `tests/system/drivers/` | leere Disk → FORMAT → CPABCGEN → Kaltboot-Verify (6 Presets, 5 davon als ctest registriert) |
| `capture_format_menus.py` | `tools/` | Menü-Screenshots von FORMAT.COM |
| `kbd_test`, `floppy_diag`, `bench_run` | `tools/*.cpp` | Smoke-/Diagnose-Läufe, nirgends als Test registriert |
| `disasm_difftest.py` | `tools/` | Kreuzvergleich Disassembler ↔ `z80dis`-Paket — ein echter Test, nicht registriert |

### 1.3 Wo Testdaten liegen (fünf Orte)

| Ort | Inhalt | Größe |
|-----|--------|-------|
| `tests/fixtures/` | `cpa_mini.img`, `cpa_mini.hfe` | 28 KB |
| `disks/` | 15 Boot-/Leer-/SCPX-Images + `.prn`-Listings + `bootsec.bin` | **23 MB** |
| `boot_disk/` | CP/A-Programme (`format.com`, `cpabcgen.com`, `hardy.com`, …) | 248 KB |
| `tests/system/drivers/` | `format.com`, `cpabcgen.com` (Duplikate von `boot_disk/`) | 27 KB |
| `docs/` | `cpabcgen.com`, `hardy.com` (nochmals), `format.md` | 168 KB |
| `tests/python/fixtures/` | **leer** | 0 |

`A5120_TEST_DISK_DIR` zeigt auf `${CMAKE_SOURCE_DIR}/disks` — Integrationstests laden also
aus `disks/`, Unit-Tests aus `tests/fixtures/`, die CLI-Tests aus `disks/…clock_noautoexec.img`.

---

## 2. Befunde

### B1 — ~~Vier Ausführungswege, keine einheitliche Ergebnisquelle~~ ✅ ERLEDIGT
`ctest` allein war **unvollständig**: der Legacy-Harness (58 Fälle) lief nur, weil `dev.sh`
danach noch `./build/a5120emu_test` startete. Wer `ctest` direkt aufrief (IDE, CI, Agent),
bekam ein grünes Ergebnis ohne diese 58 Fälle.
**Behoben durch Entfernen des Legacy-Emulators** (§6) — `ctest` ist jetzt die einzige und
vollständige Ergebnisquelle.

### B2 — ~~`CMakeLists.txt` ist überwiegend Testregistrierung~~ ✅ ERLEDIGT (§6, 2026-08-07)
Ursprünglich 609 Zeilen (nach Entfernen des Legacy-Teils 554), davon Z. 217–440 (Unit-Test-Targets) und Z. 500–607 (CLI-/Boot-Disk-Tests)
= **332 Zeilen Test-Wiring im Root-CMake**. Es gibt **kein** `tests/CMakeLists.txt` (obwohl
`doc/design/12_testing.md` eines beschreibt). Pro Test-Binary werden 3–5 Zeilen Boilerplate
wiederholt (31×), plus eine separate `gtest_discover_tests`-Zeile (31×) an anderer Stelle —
zwei Listen, die auseinanderlaufen können.
**Behoben:** Root-CMake auf 232 Zeilen, Registrierung in `tests/` mit `k1520_add_test()` (§6).

### B3 — ~~CLI-Tests als escapete Shell-Einzeiler im CMake~~ ✅ ERLEDIGT (§6, 2026-08-07)
Beispiel (gekürzt):
```cmake
add_cli_test(cli_dbg_breakpoint "bp ZVE1 : ZVE1 PC=0135"
  "D=$(mktemp); cp ${_bbdisk} \$D; printf 'b 0x0135\\ng\\nq\\n' | $<TARGET_FILE:k1520dbg> \$D; rm -f \$D")
```
Im Code steht dazu der Kommentar *„NB: regex uses '.' for literal []()*+ — escaped specials
don't survive the macro."* Das ist eine dokumentierte Wartungsfalle: die Regexes können
Sonderzeichen nicht ausdrücken, das `mktemp; cp; …; rm`-Ritual ist **15×** kopiert, und ein
Abbruch lässt Temp-Dateien liegen.

### B4 — ~~Golden-Werte im Build-System statt im Testcode~~ ✅ ERLEDIGT (§6, 2026-08-07)
```cmake
add_cli_test(cli_dbg_json "\"pc\":\"0x1E52\".*\"hl\":\"0x231A\"" …)
```
Ein Registerabbild bei exakt 5 000 000 Zyklen, hart im `CMakeLists.txt`. Ändert sich das
Floppy-Timing, schlägt ein Test fehl, dessen Erwartungswert niemand im Testverzeichnis findet.
**Behoben:** steht jetzt in `tests/cli/cases/dbg_json.cli`, mit der Begründung daneben (§6).

### B5 — ~~Keine gemeinsame Test-Infrastruktur~~ ✅ ERLEDIGT (§6, 2026-08-07)
„Boote bis zum Prompt“, „warte auf Text im VRAM“, „tippe ein Kommando“, „kopiere die Disk
nach /tmp“ ist in `test_boot_integration.cpp` (937 Z.), `test_scpx_init.cpp` (346 Z.),
`test_hardy.cpp` (146 Z.) und den 19 CLI-Tests jeweils **neu implementiert**. Es gibt weder
eine Test-Support-Bibliothek noch GoogleTest-Fixtures dafür.
**Behoben:** `tests/support/` (Bibliothek `k1520_testsupport`) — eine Fassung je Helfer (§6).

### B6 — ~~Testcode liegt in `tools/`~~ ✅ ERLEDIGT (§6, 2026-08-07)
Zwei verschiedene Fälle, die man auseinanderhalten muss:
- **Legitim:** `tools/{expr_eval,until_cond,event_bp,mem_watch,dbg_commands,coverage_diff,callstack_tracker,prn_listing}.h`
  sind header-only *Produktivbausteine* des Debuggers; dass Tests sie inkludieren, ist in Ordnung —
  nur ist `tools/` dafür der falsche Ort (es ist eine Bibliothek, kein Werkzeug).
- **Falsch platziert:** `tests/system/drivers/format_all.py`, `tests/system/drivers/make_bootdisk.py`,
  `tools/capture_format_menus.py`, `tools/disasm_difftest.py` sind **Testtreiber**. `make_bootdisk.py`
  ist sogar direkt als 5 ctest-Fälle registriert — ein Test, der in `tools/` wohnt.
  **Behoben:** `format_all.py` und `make_bootdisk.py` liegen jetzt in
  `tests/system/drivers/` (§6). `capture_format_menus.py` und `disasm_difftest.py` bleiben
  bewusst in `tools/` — sie sind manuelle Werkzeuge, kein registrierter Test.

### B7 — ~~Keine Python-/GUI-Testebene~~ ✅ ERLEDIGT (§6, 2026-08-07)
`app/` hat 12 Python-Module (Fenster, Bildschirm-Widget mit GLSL-Shader, Tastatur,
Laufwerks-Widget, Konfig-I/O) und **null** Tests. Die C-API — die einzige Schnittstelle
zwischen Kern und GUI — wird nur indirekt über C++-Tests abgedeckt. `requirements.txt`
kennt kein `pytest`. Der Design-Entwurf `doc/design/12_testing.md` sieht genau diese Ebene
seit Beginn vor; sie wurde nie gebaut (`tests/python/` war leer).
**Behoben:** `tests/python/` mit 80 pytest-Fällen in 7 Modulen, als ctest-Fälle mit Label
`python` registriert (§6).

### B8 — Laufzeitklassen nur grob getrennt
Es gibt genau **ein** Label (`format_integration`, 8 Fälle). Alles andere läuft in einem Topf.
Das ist heute erträglich (1,94 s), aber die Klassen sind inhaltlich verschieden: reine
Unit-Tests (µs), Kaltboot-Integration (0,2–1,7 s), Prozess-startende CLI-Tests. Auffällig:
`ScpxIntegration.*` (3 Fälle, bis 1,7 s, volle Boots) sitzt in der Schnellrunde, während
`ScpxInit.*` aus derselben Domäne als `format_integration` markiert ist — die Zuordnung ist
historisch gewachsen, nicht systematisch.

### B9 — Ein dauerhaft deaktivierter Test
`TEST(KeyboardIntegration, DISABLED_TypeCommandAtCcpEchoesAndProcesses)` in
`test_boot_integration.cpp:678` — deaktiviert, ohne Ablaufdatum oder Ticket. Ausgerechnet
Tastatureingabe ins laufende System, also der Pfad, den die GUI benutzt.

### B10 — ~~Dreifache, teils falsche Testdokumentation~~ ✅ ERLEDIGT (§6, 2026-08-07)
| Dokument | Umfang | Problem |
|----------|--------|---------|
| `doc/design/12_testing.md` | 288 Z. | beschreibt `tests/cpp/CMakeLists.txt` und eine pytest-Ebene — **beides existiert nicht**; nennt `test_floppy.cpp`/`test_format_parser.cpp`, die es nicht (mehr) gibt |
| `doc/cpp_testsyste.md` | 1349 Z. | nennt Pfade `./build/tests/test_k2526` und `./build/test_main` — real sind es `./build/k1520_test_k2526` und `./build/a5120emu_test`; Tippfehler schon im Dateinamen |
| `CLAUDE.md` | ~40 Z. | einzige aktuelle Quelle (dev.sh-Workflow, Labels) |

**Behoben:** `doc/cpp_testsyste.md` gelöscht, `doc/design/12_testing.md` neu geschrieben,
`tests/README.md` als praktischer Einstieg ergänzt, CLAUDE.md auf Verweise gekürzt (§6).

### B11 — Uneinheitliche Namen
Quelle `tests/unit/cards/test_k5122.cpp` → Target `k1520_test_k5122` → Suiten `K5122Test`,
`K5122FormatStream`. Andere Karten ohne `Test`-Suffix (`K2526`, `K3526`). Deutsch und
Englisch gemischt bis in Testnamen (`Mount_BereichsüberschreitungAbgelehnt` neben
`Motor_SpinupBisAufDrehzahl` neben `HXCHFEV3_Signatur_gibtNullptr`), Umlaute in ctest-Namen.
Ein Tippfehler ist bereits im Suitennamen zementiert: `Sekorgroessen/MfmRoundtrip`
(neben korrektem `Sektorgroessen/MfmBitCodecRoundtrip`).

### B12 — Fixtures: 23 MB Binärdaten im Git, teils generierbar
`disks/` enthält 15 Images à 0,8–2 MB. Seit `DiskImage::create` gültig formatierte
Leerdisketten erzeugen kann, sind mindestens `empty_cpa780.hfe` und `cpa_leer800k.hfe`
zur Laufzeit herstellbar. Zusätzlich liegen dieselben CP/A-Programme in drei Verzeichnissen
(`boot_disk/`, `tests/system/drivers/`, `docs/`).

### B13 — ~~Keine CI~~ ✅ ENTSCHIEDEN (§6, 2026-08-07)
Kein `.github/workflows/`, kein anderer CI-Konfigurationsort. Die Testqualität hing daran,
dass jemand lokal `tools/dev.sh test` aufruft.
**Entscheidung: bewusst keine CI** — das Projekt wird lokal entwickelt. Stattdessen erzwingt
`.githooks/pre-push` die grüne Schnellrunde vor jedem Push (§6).

---

## 3. Zielbild

**Leitgedanken**

1. **Eine Ausführungsquelle:** alles läuft über `ctest`. `dev.sh` bleibt der bequeme
   Frontend-Wrapper, ist aber nicht mehr die einzige Stelle, die die volle Testmenge kennt.
2. **Gliederung nach Testebene**, nicht nach Framework — Verzeichnisstruktur = Testpyramide.
3. **Laufzeit ist ein Label, keine Verzeichniseigenschaft** (`fast`/`slow`), zusätzlich zum
   Ebenen-Label. Auswahl über `ctest -L`, nicht über handgepflegte Ausschlusslisten.
4. **Testcode wohnt in `tests/`** — auch Python-Treiber, auch Fixtures, auch Golden-Dateien.
5. **Gemeinsame Infrastruktur statt Copy-Paste:** eine `testsupport`-Bibliothek für Boot,
   Bildschirm, Tastatur, Temp-Disk.

### 3.1 Vorgeschlagene Verzeichnisstruktur

```
tests/
├── CMakeLists.txt                # einziger Ort der Testregistrierung (add_subdirectory)
├── README.md                     # Kurzanleitung: was wie ausführen
├── cmake/
│   └── K1520AddTest.cmake        # k1520_add_test() / k1520_add_cli_test() / k1520_add_py_test()
│
├── support/                      # ── lib k1520_testsupport (C++) ──────────────────
│   ├── machine_fixture.h/.cpp    # BootedMachine: Kaltboot bis Prompt, run_until(...)
│   ├── screen.h/.cpp             # VRAM→Text, find(), wait_for_text()   (heute 3× dupliziert)
│   ├── keyboard.h/.cpp           # type("DIR\r"), Echo abwarten
│   ├── temp_disk.h/.cpp          # RAII-CoW-Kopie einer Fixture-Disk    (heute 15× als Shell)
│   └── golden.h/.cpp             # Golden-Datei laden/vergleichen/aktualisieren
│
├── unit/                         # ── keine Disk, kein Boot, µs–ms ────────────────
│   ├── primitives/  test_z80.cpp test_pio.cpp test_ctc.cpp test_sio.cpp
│   ├── bus/         test_bus.cpp
│   ├── cards/       test_k2526.cpp test_k3526.cpp test_k5122.cpp
│   │                test_k7024.cpp test_k7637.cpp test_k8025.cpp
│   ├── floppy/      test_track_codec.cpp test_bit_codec.cpp test_track_image.cpp
│   │                test_disk_image_raw.cpp test_hfe_image.cpp
│   │                test_floppy_drive2.cpp test_drive_profile.cpp test_format_catalog.cpp
│   └── util/        test_yaml_lite.cpp
│
├── debugtools/                   # ── Unit-Tests der Debugger-Bausteine ───────────
│   └── test_expr_eval.cpp test_until_cond.cpp test_event_bp.cpp test_mem_watch.cpp
│       test_dbg_commands.cpp test_coverage_diff.cpp test_callstack_tracker.cpp
│       test_prn_listing.cpp
│
├── integration/                  # ── ganze Maschine, echter Boot, 0,2–2 s ────────
│   ├── test_boot_chain.cpp       # ROM→SYL→2nd→CP/A→@OS.COM (die Boot-Invarianten)
│   ├── test_boot_drives.cpp      # Boot von B:/C:, .img vs .hfe
│   ├── test_reset_power.cpp      # Reset / Power-Cycle aus dem laufenden System
│   ├── test_keyboard.cpp         # inkl. Reaktivierung des heute DISABLED-Tests
│   ├── test_disk_api.cpp         # A5120Machine-Diskettenschnittstelle
│   └── test_machine_snapshot.cpp
│
├── cli/                          # ── Blackbox der Werkzeuge, datengetrieben ──────
│   ├── cases/                    # je Fall eine .cli-Datei: Kommando, stdin, Erwartung
│   │   ├── dbg_breakpoint.cli
│   │   └── bt_until.cli …
│   ├── run_case.sh               # Temp-Disk, Ausführung, Vergleich (einmal, nicht 19×)
│   └── golden/                   # erwartete Ausgaben (statt Regex im CMake)
│
├── system/                       # ── langsam: Label slow ─────────────────────────
│   ├── test_scpx_init.cpp  test_hardy.cpp
│   ├── bootdisk/                 # aus tests/system/drivers/ hierher
│   │   ├── make_bootdisk.py  presets.yaml
│   └── format/                   # aus tests/system/drivers/format_all.py hierher
│       └── format_all.py
│
├── python/                       # ── pytest: C-API + GUI ─────────────────────────
│   ├── conftest.py               # lädt libk1520core.so, Fixture-Pfade
│   ├── test_c_api.py             # aus test_integration.py / test_c_api.c übernommen
│   ├── test_boot_smoke.py        # aus test_boot.py übernommen
│   └── test_gui_smoke.py         # PySide6 offscreen (QT_QPA_PLATFORM=offscreen)
│
└── fixtures/                     # ── alle Testdaten an EINEM Ort ─────────────────
    ├── disks/                    # aus disks/ + tests/fixtures/
    ├── programs/                 # format.com, cpabcgen.com, hardy.com (heute 3× dupliziert)
    └── listings/                 # *.prn
```

Parallel dazu zwei kleine Verschiebungen **außerhalb** von `tests/`:

- `tools/{expr_eval,until_cond,event_bp,mem_watch,dbg_commands,coverage_diff,callstack_tracker,prn_listing,z80dis_min}.h`
  → `tools/dbglib/` (oder `core/debug/`). Sie sind Bibliothek, nicht Werkzeug; die Tests in
  `tests/debugtools/` inkludieren dann `tools/dbglib/…`.
- Root aufräumen: die sechs verwaisten `test_*`-Dateien migrieren oder löschen,
  `disk_b.img` nach `tests/fixtures/disks/`, `k1520_*.log` in ein ignoriertes `logs/`.

### 3.2 CMake-Umbau

Ein Helper ersetzt die 31× wiederholte Boilerplate **und** die zweite `gtest_discover_tests`-Liste:

```cmake
# tests/cmake/K1520AddTest.cmake
function(k1520_add_test name)
  cmake_parse_arguments(T "" "TIMEOUT" "SRC;LIBS;LABELS;DEFS" ${ARGN})
  add_executable(k1520_test_${name} ${T_SRC})
  target_link_libraries(k1520_test_${name} PRIVATE ${T_LIBS} k1520_testsupport GTest::gtest_main)
  target_include_directories(k1520_test_${name} PRIVATE ${CMAKE_SOURCE_DIR})
  target_compile_definitions(k1520_test_${name} PRIVATE
      K1520_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures" ${T_DEFS})
  gtest_discover_tests(k1520_test_${name}
      PROPERTIES TIMEOUT ${T_TIMEOUT} LABELS "${T_LABELS}")
endfunction()
```

Aufruf dann einzeilig pro Test:

```cmake
k1520_add_test(k5122   SRC test_k5122.cpp   LIBS k1520_k5122          LABELS "unit;fast")
k1520_add_test(boot    SRC test_boot_chain.cpp LIBS k1520_a5120       LABELS "integration;fast" TIMEOUT 60)
k1520_add_test(hardy   SRC test_hardy.cpp   LIBS k1520_a5120          LABELS "system;slow"      TIMEOUT 180)
```

Erwartete Wirkung: Root-`CMakeLists.txt` von 609 → ca. **250 Zeilen** (nur noch Bibliotheken,
Werkzeuge, `add_subdirectory(tests)`); die Testregistrierung liegt vollständig bei den Tests.

### 3.3 Labels und `dev.sh`

| Label | Bedeutung | Fälle heute |
|-------|-----------|-------------|
| `unit` | isolierte Klasse, keine Disk | ~570 |
| `debugtools` | header-only Debugger-Bausteine | 62 |
| `integration` | ganze Maschine, echter Boot | ~35 |
| `cli` | Werkzeug als Prozess (Blackbox) | 19 |
| `system` | FORMAT/CPABCGEN/HARDY/SCPX, Minuten | 8 |
| `python` | pytest über C-API/GUI | neu |
| `fast` / `slow` | quer zu allen obigen, Grenze ~5 s | |

```sh
tools/dev.sh test              # -L fast          (Standard-Regression, heute ~2 s)
tools/dev.sh test unit         # -L unit
tools/dev.sh test-slow         # -L slow
tools/dev.sh test-all          # alles
```

`ctest` allein sagt seit dem Entfernen des Legacy-Harness wieder die volle Wahrheit (B1).

---

## 4. Migrationsplan

Jeder Schritt ist eigenständig, hinterlässt einen grünen Baum und ist einzeln committebar.

| Schritt | Inhalt | Risiko | Aufwand |
|---------|--------|--------|---------|
| ~~**0**~~ | ~~Root aufräumen~~ ✅ erledigt (§6) — 6 verwaiste `test_*`-Dateien, 3 Analyseskripte nach `tools/`, `disk_b.img` nach `disks/` | — | — |
| ~~**1**~~ | ~~Legacy-Harness in ctest~~ → stattdessen **Legacy komplett entfernt**, ✅ erledigt (§6) | — | — |
| ~~**2**~~ | ~~`tests/CMakeLists.txt` + `k1520_add_test()`~~ ✅ erledigt (§6) | — | — |
| ~~**3**~~ | ~~Verzeichnisumzug `tests/cpp/*`~~ ✅ erledigt (§6) | — | — |
| ~~**4**~~ | ~~`tests/support/`-Bibliothek~~ ✅ erledigt (§6) | — | — |
| ~~**5**~~ | ~~CLI-Tests datengetrieben~~ ✅ erledigt (§6) | — | — |
| **6a** | ~~Fixtures unter `tests/fixtures/disks/` konsolidieren~~ ✅ erledigt (§6) | — | — |
| **6b** | Rest: `.com`-Dateien entdoppeln (`boot_disk/` + `tests/system/drivers/` + `docs/` halten dieselben `format.com`/`cpabcgen.com`/`hardy.com`), `leer_cpa780.hfe` zur Testzeit erzeugen statt committen | gering | 2 h |
| ~~**7**~~ | ~~`tests/python/` mit pytest~~ ✅ erledigt (§6) — 80 Fälle, 7 Module, Label `python` | — | — |
| ~~**8**~~ | ~~Testtreiber nach `tests/system/`~~ ✅ erledigt (§6); Presets bleiben Python-Dicts (Begründung dort) | — | — |
| ~~**9**~~ | ~~Testdoku vereinheitlichen~~ ✅ erledigt (§6) | — | — |
| ~~**10**~~ | ~~GitHub-Actions-Workflow~~ → stattdessen **`pre-push`-Hook**, ✅ erledigt (§6) | — | — |
| **11** | Kür: `DISABLED_TypeCommandAtCcpEchoesAndProcesses` reaktivieren, Suiten-Namensschema vereinheitlichen, Tippfehler `Sekorgroessen` beheben | gering | 3 h |
| **12** | Befunde §7 (5×1024-Generierung erzeugt defekte Disk) und §8 (IEO ignoriert IUS) bewerten und entweder beheben oder als bewusste Grenze festschreiben | offen | unbekannt |

**Stand 2026-08-07:** erledigt sind **0–10**.  Offen: **11** (Kür: deaktivierten Test
reaktivieren, Namensschema, Tippfehler) und **12** (die zwei Befunde §7/§8).

---

## 6. Änderungsprotokoll

### 2026-08-07 — Legacy-Emulator vollständig entfernt

Entscheidung des Projektinhabers zu offener Frage 1: nicht einfrieren, sondern entfernen.

**Gelöscht**
- `src/` komplett (17 Dateien, ~380 KB): `a5120emu.cpp`, `cpa_bios.{cpp,h}`, `cparun.cpp`,
  `cpm_bdos.{cpp,h}`, `floppy.{cpp,h}`, `memory.{cpp,h}`, `terminal_ansi.cpp`,
  `terminal_sdl.cpp`, `terminal.h`, `font8x8.h`, `z80.{cpp,h}`
- `tests/test_main.cpp` (929 Z., 58 Fälle, eigenes Makro-Framework)

**Angepasst**
- `CMakeLists.txt`: Targets `a5120emu`, `cparun`, `a5120emu_test`, die Option `USE_SDL`
  samt SDL2-Erkennung und `install(TARGETS …)` entfernt → 609 → 554 Zeilen
- `tools/dev.sh`: der separate `./build/a5120emu_test`-Aufruf in `test` und `test-all` entfällt
- `CLAUDE.md`: Abschnitt „Two emulators live in this repo" → „One emulator"; Verweise auf
  `a5120emu_test` und die 58 Legacy-Fälle bereinigt
- `.claude/agents/{test-runner,code-explorer,cpp-coder}.md`: Legacy-Hinweise entfernt,
  test-runner auf `tools/dev.sh test` umgestellt

**Nicht betroffen**
- `cparun/` — eigenständiges Unterprojekt mit eigenem `CMakeLists.txt` und eigenen Kopien von
  `z80/memory/cpm_bdos`; baut unverändert
- `core/`, `tools/`, `app/` — keine einzige Abhängigkeit auf `src/` (geprüft per Include-Suche)

**Verifikation:** `build/` und `build_trace/` gelöscht und von Grund auf neu gebaut,
anschließend `tools/dev.sh test`: **660/660 grün**, 1 Test deaktiviert (der bekannte
`KeyboardIntegration.DISABLED_…`), Laufzeit 8,95 s inkl. Erstkonfiguration.

### 2026-08-07 — Testdisketten als Fixtures konsolidiert und umbenannt

Entscheidung zu offener Frage 2: `disks/` bleibt **Arbeitsverzeichnis**, die Tests bekommen
unter `tests/fixtures/disks/` eine eigene Kopie ausschließlich der wirklich gebrauchten
Dateien, mit einheitlichem Namensschema.

**Namensschema** `<system>_<diskformat>_<laufwerkskonfiguration>_<merkmale>.<ext>`
(Variante „kompakt": `combo5zoll`/`combo8zoll` statt ausgeschriebener Laufwerkstypen).
`autofs` und `noautoexec` entfallen — bei allen CP/A-Disketten gleich, dokumentiert in
`tests/fixtures/README.md`.

| alt | neu |
|-----|-----|
| `cpadisk_autofs_clock_noautoexec.*` | `cpa_cpa780_k5601_clock.*` |
| `cpadisk_autofs_noclk_noautoexec.*` | `cpa_cpa780_k5601_noclock.*` |
| `cpadisk_autofs_noclock_5inchCombo.*` | `cpa_cpa780_combo5zoll_noclock.*` |
| `cpadisk_autofs_noclock_8inchCombo.*` | `cpa_cpa780_combo8zoll_noclock.*` |
| `scpx_boot.hfe` | `scpx17_cpa780_k5601.hfe` |
| `scpx_5x1024_hardy.hfe` | `scpx17_5x1024_k5601_hardy.hfe` |
| `empty_cpa780.hfe` | `leer_cpa780.hfe` (nur noch als Fixture) |
| `bootsec.bin` | `bootsec_cpa780.bin` |

**Neu:** `tests/fixtures/disks/` mit 12 Dateien (11 MB) + `tests/fixtures/README.md`
(Schema, Zweck jeder Datei, wer sie benutzt). `cpa_mini.{img,hfe}` von `tests/fixtures/`
nach `tests/fixtures/disks/` verschoben.

**Angepasst:** `A5120_TEST_DISK_DIR` und `FIXTURE_DIR` zeigen auf `tests/fixtures/disks`;
`_bbdisk` der CLI-Tests ebenso; `make_bootdisk.py` und `format_all.py` bilden denselben Pfad.
`disks/README.md` neu geschrieben (Arbeitsverzeichnis, Verweis auf die Fixtures).

**Aus `disks/` entfernt:** `empty_cpa780.hfe`, `cpa_leer800k.hfe` — Leerdisketten erzeugt
`DiskImage::create` inzwischen selbst (echte IDAM/DATA/CRC); die einzige committete
Leerdiskette ist die Formatiervorlage `tests/fixtures/disks/leer_cpa780.hfe`.
Die übrigen Dateien in `disks/` tragen jetzt dieselben Namen wie die Fixtures, damit
`.img`/`.hfe`/`.prn` zusammenpassen; alle Referenzen in `tools/`, `doc/`, `docs/`, `app/`
wurden mitgezogen.

**Verifikation:** `tools/dev.sh test` 660/660 grün, `tools/dev.sh test-format` 8/8 grün.

---

### 2026-08-07 — Python-Testebene aufgebaut, Projektwurzel aufgeräumt

Entscheidung zu offener Frage 3: Variante a (volle Ebene, C-ABI **und** GUI).

**Neu: `tests/python/`** — 80 pytest-Fälle in 7 Modulen, Laufzeit 2,8 s:

| Modul | Fälle | Inhalt |
|-------|-------|--------|
| `test_c_api.py` | 15 | Header ↔ `.so` ↔ ctypes-Bindung mechanisch verglichen; Handle-Lebenszyklus; Framebuffer-Geometrie; Fehlerpfade; Formatkatalog |
| `test_binding.py` | 11 | Semantik der `K1520Emulator`-Methoden (Mount, Schreibschutz, `create_disk`, Laufwerksstatus, Motor) |
| `test_boot_smoke.py` | 6 | Kaltboot **durch die Bindung** bis Bootloader/CP/A-Banner/Uhrzeit-Abfrage, `.img` + `.hfe`, Reset, Tastatureingabe |
| `test_drive_types.py` | 9 | Laufwerkskatalog, `normalize()`, Migration alter Profilnamen |
| `test_config_io.py` | 7 | YAML-Rundlauf, CRT-Parameter, Toleranz gegen fremde Schlüssel |
| `test_keyboard_map.py` | 25 | `qt_event_to_core_key`: Zeichen, Sondertasten, F1–F8, Ctrl, Modifikatoren |
| `test_gui_smoke.py` | 7 | Hauptfenster headless (`QT_QPA_PLATFORM=offscreen`): Panels, Emulator, Laufwerksleiste, Konfiguration |

Registrierung: je Modul ein ctest-Fall (`py_c_api`, `py_binding`, …) mit **Label `python`**,
Interpreter bevorzugt `venv/bin/python3`; fehlen pytest/PySide6/PyYAML, wird die Ebene nicht
registriert und CMake nennt den Installationsbefehl. Neu: `requirements-dev.txt`,
`tools/dev.sh test-python`, `tests/python/README.md`.

**Dabei geschlossene Lücke — 10 nicht deklarierte C-API-Funktionen.** `k1520_stop`,
`k1520_version`, `k1520_mem_read`, `k1520_mem_write`, `k1520_io_read`,
`k1520_set_console_mode`, `k1520_console_poll`, `k1520_console_key`, `k1520_serial_send`,
`k1520_serial_set_rx_cb` waren in `app/core_binding/k1520.py` **ohne `argtypes`**. ctypes
konvertiert dann still nach `int` — der 64-Bit-Handle-Zeiger wird abgeschnitten, der Aufruf
stürzt ab oder liefert Unsinn. Alle zehn sind jetzt deklariert; `test_c_api.py` erzwingt, dass
das so bleibt. Dazu kamen die Wrapper `mem_read`/`mem_write`/`io_read`/`version` und
`screen_text()` (liest das K7024-VRAM ab `0xF800` als 24×80-Text — die robuste Art,
Bildschirminhalte zu prüfen).

**Projektwurzel aufgeräumt** (Schritt 0 des Plans):

| Datei | Verbleib |
|-------|----------|
| `test_boot.py`, `test_boot_detailed.py`, `test_integration.py`, `test_c_api.c` | gelöscht — Inhalt in `tests/python/test_boot_smoke.py` + `test_c_api.py` aufgegangen |
| `test_qt.py` | gelöscht (PyQt5, die GUI nutzt PySide6) |
| `test_reti.cpp` | nach GoogleTest überführt → `tests/unit/primitives/test_reti.cpp`, Target `k1520_test_reti`, 5 Fälle (§8) |
| `analyze_eprom.py`, `analyze_vram.py`, `disasm_k2526.py` | nach `tools/` verschoben, Doku-Pfade nachgezogen |
| `disk_b.img` | nach `disks/unbekannt_daten_b.img` (keine Bootdiskette; nur in einem Docstring erwähnt) |

**Verifikation:** `tools/dev.sh test` **672/672 grün**, `tools/dev.sh test-format` **8/8 grün**.

### 2026-08-07 — `pre-push`-Hook statt CI

Entscheidung zu offener Frage 4: Variante c. Das Projekt wird lokal entwickelt; eine CI würde
nur eine zweite Umgebung pflegen wollen. Die Schnellrunde dauert ~12 s — bezahlbar vor jedem Push.

`.githooks/pre-push` (versioniert, im Gegensatz zu `.git/hooks/`) führt `tools/dev.sh test` aus
und bricht den Push bei rotem Ergebnis mit Hinweis auf `--no-verify` ab. Reine Ref-Löschungen
(lokale SHA = lauter Nullen) überspringen den Lauf. Die langsame `format_integration`-Runde ist
absichtlich **nicht** enthalten — sie gehört vor einen Merge nach main.

Aktivierung je Arbeitskopie: `git config core.hooksPath .githooks` (hier bereits gesetzt).

**Dabei gefunden:** `core.hooksPath` stand in `.git/config` auf
`/home/olliy/projects/a5120emu/.git/hooks` — dem Hook-Verzeichnis eines **anderen** Projekts
(Rest einer kopierten Arbeitskopie). Dort lagen nur Samples, es lief also nichts; ein in
`.git/hooks` installierter Hook wäre aber stillschweigend ignoriert worden. Derselbe Kopier-Rest
steckt in `venv/bin/pip` (Shebang auf das fremde venv) — dort hilft `venv/bin/python3 -m pip`.

Verifiziert: grüner Lauf lässt durch (Exit 0), simulierter roter Lauf blockiert (Exit 1),
reine Löschung wird übersprungen (Exit 0), und der Hook greift auch aus einem
Unterverzeichnis heraus (`git hook run pre-push` aus `core/`).

### 2026-08-07 — Arbeitskopie ortsunabhängig gemacht

Anlass: der `core.hooksPath`-Fund. Die Prüfung des ganzen versionierten Baums förderte
weitere hartkodierte Pfade zutage — **alle** auf `/home/olliy/projects/a5120emu`, also das
andere Projekt; in dieser Arbeitskopie waren sie damit ohnehin kaputt.

| Datei | vorher | jetzt |
|-------|--------|-------|
| `.git/config` (`core.hooksPath`) | `/home/olliy/projects/a5120emu/.git/hooks` | `.githooks` (relativ, von Git zur Arbeitsbaumwurzel aufgelöst) |
| `SETUP.md` | `cd /home/olliy/projects/a5120emu` | `cd <Projektverzeichnis>` + Hinweis, dass alle Pfade relativ sind |
| `tools/analyze_eprom.py`, `tools/disasm_k2526.py`, `tools/analyze_vram.py` | absoluter Pfad zu EPROM/Quellen | `ROOT = Path(__file__).resolve().parents[1]` |
| `tools/z80_disasm.py`, `tools/z80_disasm3.py` | absoluter Vorgabepfad zu `format.com` | `ROOT`-relativ, zusätzlich per Argument überschreibbar |
| `tools/bootsec/bootsec_analyze.py` | absoluter Pfad | `ROOT`-relativ + `sys.argv[1]`-Override |
| `.vscode/settings.json` | `autoApprove`-Regex mit vier absoluten Pfaden (u. a. auf das entfernte `src/`) | Eintrag entfernt |

Zusätzlich in `SETUP.md` korrigiert: der Testabschnitt rief noch die gelöschten Root-Skripte
(`test_integration.py`, `test_boot.py`, `gcc … test_c_api.c`) auf — jetzt `tools/dev.sh test`
und Verwandte; dazu die Hook-Aktivierung als eigener Einrichtungsschritt.

`tools/bootsec/bootsec_analyze.py` scheiterte danach mit `IndexError`: es erwartet die drei
Systemspuren (9984 Byte), die committete `bootsec_cpa780.bin` ist aber nur der erste Sektor
(512 Byte). Statt des kryptischen Absturzes meldet es das jetzt im Klartext und nimmt einen
Pfad als Argument.

Gegenprobe: alle Werkzeuge aus `/tmp` heraus aufgerufen — laufen. Im versionierten Baum
(außerhalb der Analysetexte unter `doc/`/`docs/`) steht kein absoluter Pfad mehr.

### 2026-08-07 — Testregistrierung nach `tests/`, Gliederung nach Testebene (Schritte 2+3)

**Root-`CMakeLists.txt`: 612 → 232 Zeilen.** Sie enthält jetzt nur noch Projektaufbau,
Bibliotheken, Werkzeugziele und `add_subdirectory(tests)`.

**Neu: `tests/cmake/K1520AddTest.cmake`** mit `k1520_add_test()` — ein Test = eine Zeile:

```cmake
k1520_add_test(k5122 SRC cards/test_k5122.cpp LIBS k1520_k5122 LABELS "unit;fast")
```

Ersetzt den bisherigen Vierzeiler-Block **und** die zweite, getrennt gepflegte
`gtest_discover_tests`-Liste. Zusagen des Helfers: Zielname bleibt `k1520_test_<name>`, das
Binary landet weiterhin in `build/` (nicht `build/tests/…`, damit `./build/k1520_test_k2526
--gtest_filter=…` aus der Dokumentation gültig bleibt), Include-Wurzel und Fixture-Pfad kommen
automatisch.

**Verzeichnisstruktur — Testebene = Verzeichnis = ctest-Label:**

```
tests/
├── CMakeLists.txt          GoogleTest-Fetch, Helfer, Fixture-Pfad, add_subdirectory je Ebene
├── cmake/K1520AddTest.cmake
├── unit/          530 Fälle   primitives/ bus/ cards/ peripherals/ util/   (spiegelt core/)
├── debugtools/     67         die header-only Bausteine aus tools/*.h
├── integration/    50         ganze Maschine, echter Kaltboot
├── cli/            19         Werkzeuge als Prozess (Blackbox)
├── system/          8         FORMAT/CPABCGEN/SCPX/HARDY (langsam)
└── python/          7         pytest: C-ABI + GUI
```

`tests/cpp/` existiert nicht mehr. Labels: Ebenenname plus `fast`/`slow`; die langsamen tragen
zusätzlich das historische `format_integration`, auf das `dev.sh` und die Doku verweisen.
Neu: `tools/dev.sh test-level <ebene>`.

**Dabei gefunden — eine stille Falle in CMake:**
`gtest_discover_tests(... PROPERTIES LABELS "unit;fast")` setzt **nur das erste Label**; die
Liste zerfällt beim Durchreichen in zwei Argumente, der Rest verschwindet ohne Warnung. Folge im
ersten Anlauf: die drei GoogleTest-Systemtests (`ScpxInit`, `Hardy`) verloren ihr
`format_integration` und liefen in der schnellen Runde mit (+14 s). `k1520_add_test()` maskiert
die Semikola jetzt (`string(REPLACE ";" "\\;" …)`); der Kommentar an der Stelle hält den Grund
fest. Zweite Falle derselben Art: `enable_testing()` muss im **Wurzel**-CMakeLists stehen — nur
in `tests/` aufgerufen, meldet ctest „No tests were found!!!".

**Gegenprobe nach dem Umbau:** `build/` gelöscht, von Grund auf neu konfiguriert und gebaut;
`tools/dev.sh test` **672/672 grün**, `tools/dev.sh test-format` **8/8 grün** — identische
Fallzahlen wie vor dem Umzug. Label-Zählung: unit 530, debugtools 67, integration 50, cli 19,
system 8, python 7, fast 673, slow 8.

### 2026-08-07 — gemeinsame Test-Infrastruktur `tests/support/` (Schritt 4)

Die Helfer „warte auf Text im Bildschirm", „tippe ein Kommando", „lass N Takte laufen",
„kopiere die Diskette nach /tmp" existierten in `test_boot_integration.cpp`,
`test_scpx_init.cpp` und `test_hardy.cpp` **je einmal eigenständig** — identischer Code,
dreifach gepflegt.  Jetzt gibt es die Bibliothek `k1520_testsupport` (Namensraum `k1520test`):

| Datei | Inhalt |
|-------|--------|
| `screen.{h,cpp}` | `vramText()` (Text-VRAM 0xF800–0xFFFF als ASCII), `vramLines()` (24×80 mit Umbrüchen für Fehlermeldungen) |
| `machine_run.{h,cpp}` | `runCycles()`, `runSmallUntil()`, `runUntilVramContains()`, `runUntilPC()` — mit den Batchgrößen als **dokumentiertem Vertrag** (5 000 Takte bei Tastaturbezug, sonst 100 000) |
| `keyboard.{h,cpp}` | `typeKey()`, `typeString()`, `typeCtrl()`, `pressKeyUntil()`, `QK_RETURN` samt der Wartezeiten (9600-Baud-Strecke des K7637) |
| `fixtures.{h,cpp}` | `diskPath()`, `readFileBytes()` und **`TempDisk`** — RAII-Kopie einer Fixture bzw. freier Zielpfad |

`TempDisk` ersetzt das bisher **sechsfach** kopierte `temp_directory_path()/copy_file/remove`-
Ritual.  Nebenwirkung, die vorher fehlte: der Destruktor räumt auch dann auf, wenn ein Test
per `ASSERT_*` abbricht — die manuellen `remove`-Zeilen am Testende wurden in dem Fall nie
erreicht und ließen Disketten-Kopien in `/tmp` liegen.  Zwei weitere lokale Pfad-Helfer
(`tmpImg()` in `test_boot_integration.cpp`, `tmpPath()` in `test_a5120_disk_api.cpp`) sind
darin aufgegangen.

Bewusst **nicht** in die Bibliothek gewandert: `runUntilDmaComplete()` samt der Adressen
`[0x03F8]`/`0x0400` — das ist Wissen über den Bootvorgang, nicht allgemeine Infrastruktur, und
bleibt bei dem Test, der es prüft.

Die Bibliothek wird **nicht** automatisch an jeden Test gelinkt: die Unit-Tests kommen ohne
`A5120Machine` aus und sollen die Maschinenbibliothek nicht mitziehen.  Integration und System
listen sie in ihrem `LIBS`.

Zeilenbilanz: die drei Testdateien 1432 → 1260 Zeilen; die Bibliothek 335 Zeilen (gut die
Hälfte davon Erläuterung, u. a. warum die Batchgrößen so sind).  Netto also kaum weniger Code —
aber statt drei Fassungen gibt es eine, und die Begründungen stehen an einer Stelle.

**Verifikation:** `tools/dev.sh test` 672/672 grün, `tools/dev.sh test-format` 8/8 grün,
Laufzeiten unverändert.  Gegenprobe per Suche: keine lokale Definition von `vramText`,
`runSmallUntil`, `runCycles`, `typeKey`, `typeString`, `typeCtrl`, `diskPath`, `pressKeyUntil`,
`runUntilPC` oder `runUntilVramContains` mehr in `tests/`.

### 2026-08-07 — Testdokumentation vereinheitlicht (Schritt 9)

Statt drei teils falscher Dokumente jetzt drei Dokumente mit klarer Aufgabenteilung:

| Dokument | Aufgabe |
|----------|---------|
| **`tests/README.md`** (neu) | Praktischer Einstieg: ausführen, Test hinzufügen, gemeinsame Infrastruktur, wo was dokumentiert ist |
| **`doc/design/12_testing.md`** (neu geschrieben) | Begründung der Gliederung: Leitgedanken, Testpyramide, was in welche Ebene gehört, Labels, bewusste Auslassungen |
| **`doc/testsystem_rework.md`** (dieses) | Bestandsaufnahme, Plan, Protokoll — die Geschichte, nicht der Zustand |

**Gelöscht: `doc/cpp_testsyste.md`** (1349 Zeilen). Ein Abgleich Stichprobe gegen Quelle zeigte:
die Datei war eine deutsche Nacherzählung der `@test`/`@brief`-Kommentare, die ohnehin in den
Testquellen stehen — mit allen Nachteilen einer Kopie. Sie beschrieb zuletzt `test_floppy.cpp`
und `test_format_parser.cpp` (existieren nicht), nannte Binärpfade `./build/tests/test_k2526`
und `./build/test_main` (heißen anders bzw. gibt es nicht) und trug den Tippfehler schon im
Dateinamen. Die maßgebliche Beschreibung eines Tests steht im Test; maschinellen Überblick
liefern `ctest -N` und `--gtest_list_tests`. Der neue §7 von `12_testing.md` hält das fest.

`doc/design/12_testing.md` beschrieb bis dahin einen **Entwurf**, der nie so gebaut wurde
(`tests/cpp/CMakeLists.txt`, eine pytest-Ebene, ein CI-Workflow). Die Neufassung beschreibt den
Ist-Zustand und begründet ihn — einschließlich der bewussten Auslassungen (keine CI, keine
Coverage-Messung, keine Pixelprüfung, ein dauerhaft deaktivierter Test).

`CLAUDE.md` verweist jetzt auf diese Dokumente statt sie zu wiederholen; stehen bleibt nur, was
beim Bearbeiten unmittelbar gebraucht wird (Registrierung, Labels, Support-Bibliothek und die
gtest_discover_tests-Falle). Der Abschnitt 13 von `doc/K1520_architecture.md` ist ebenso auf
einen Verweis zusammengezogen, sein Verzeichnisbaum auf die neue Struktur aktualisiert.

**Dabei gefunden — eine tote und kaputte Build-Datei:** `core/CMakeLists.txt` (204 Zeilen)
war von nichts eingebunden, verwies auf `tests/cpp/test_main.cpp` und ist nicht einmal
gültiges CMake — der Dateikopf steht in C-Kommentaren (`/** … */`), sodass `cmake -P` mit
„Parse error" abbricht. Gelöscht. Sie hätte jeden in die Irre geführt, der versucht, `core/`
eigenständig zu konfigurieren.

Außerdem: alle Verweise auf die alten Pfade `tests/cpp/test_*.cpp` in 15 Dateien (Analysetexte,
Werkzeugdoku, zwei Testquellen) auf die neue Struktur gezogen — keine toten Pfade mehr.

**Verifikation:** `build/` gelöscht und neu gebaut, `tools/dev.sh test` 672/672 grün,
`tools/dev.sh test-format` 8/8 grün.

### 2026-08-07 — CLI-Tests datengetrieben (Schritt 5)

Die 19 Blackbox-Fälle stehen jetzt als **Daten** in `tests/cli/cases/*.cli`; `run_case.py`
führt sie aus, `tests/cli/CMakeLists.txt` liest das Verzeichnis ein (je Datei ein ctest-Fall).
Einen Fall hinzufügen heißt: eine Datei anlegen.

Eine Falldatei sieht so aus:

```
tool:   k1520dbg
disk:   cpa_cpa780_k5601_clock.img     # Kopie nach /tmp → %DISK%
run:    %DISK%
stdin:
  b 0x0135
  g
  q
expect: bp ZVE1 : ZVE1 PC=0135
```

Was das behebt:

- **Erwartungen sind wieder lesbar.** Vorher mussten sie als CMake-Regex durch zwei Ebenen
  Escaping; Klammern überlebten das nicht, weshalb im Build-System der Hinweis stand: „regex
  uses '.' for literal []()*+" — aus `WR [6005]=99` wurde `WR .6005.=99`.  Jetzt ist die
  Erwartung eine normale Zeichenkette; Regex ist die Ausnahme (`expect_re:`).
- **Das mktemp/cp/rm-Ritual existiert einmal**, nicht 15-mal — und räumt in einem `finally`
  auf, auch wenn ein Lauf abbricht (vorher blieben Diskettenkopien in `/tmp` liegen).
- **Golden-Werte im Testverzeichnis.** Der Registerabbild-Vergleich bei 5 000 000 Takten
  steht in `dbg_json.cli`, mit der Begründung daneben, statt im `CMakeLists.txt`.
- **Brauchbare Fehlermeldung.** Statt CTests „required regular expression not found" nennt der
  Runner die fehlende Erwartung, das ausgeführte Kommando und die letzten 40 Ausgabezeilen.
- **Zusammengesetzte Erwartungen ohne Regex-Akrobatik.** `"pc":"0x1E52".*"hl":"0x231A"` sind
  jetzt zwei `expect:`-Zeilen.

Direktiven: `tool`, `disk`/`disk_path`, `run`, `setup_run` (Vorlauf mit verworfener Ausgabe —
für den Save-/Load-State-Fall), `stdin`, `file <n>:`/`tmpfile <n>:` (Ein-/Ausgabedateien, z. B.
die beiden CSVs des `--diff`-Falls), `expect`/`expect_re`/`forbid`/`forbid_re`, `exit`,
`timeout`.  Argumentzerlegung per `shlex`, damit Anführungszeichen erhalten bleiben
(`--until 'screen ~ "Bootloader"'`).

Bewusst **keine Golden-Volldateien**: die Ausgaben enthalten Taktzahlen, ein Vollvergleich wäre
bei jeder Timing-Änderung rot.  Die Erwartung ist die inhaltliche Aussage.

**Verifikation:** 19/19 grün, dazu zwei Gegenproben — eine verfälschte `expect:`-Zeile und ein
verfälschter `exit:`-Wert lassen den jeweiligen Fall mit der erwarteten Meldung fehlschlagen
(ein Runner, der stillschweigend alles durchwinkt, wäre sonst nicht zu erkennen).
Anschließend `build/` gelöscht: `tools/dev.sh test` 672/672, `test-format` 8/8 grün.

### 2026-08-07 — Duplikate aufgelöst, Testtreiber nach `tests/` (Schritte 6b + 8)

**Testtreiber (Schritt 8).** `format_all.py` und `make_bootdisk.py` sind Testcode — letzterer
ist direkt als fünf ctest-Fälle registriert — und lagen in `tools/`.  Sie liegen jetzt in
`tests/system/drivers/`, ihre Pfadableitung entsprechend angepasst.  `tools/cpa_tools/` ist
damit leer und entfernt.

Die Presets bleiben **Python-Dicts statt YAML** (im Plan war YAML angedacht): sie enthalten
Geometrie-Tupel und Verweise auf Funktionen desselben Moduls, die Auslagerung hätte nur eine
Indirektion ohne Nutzen ergeben — es gibt keinen zweiten Leser.

`capture_format_menus.py` und `disasm_difftest.py` bleiben bewusst in `tools/`: manuelle
Werkzeuge, kein registrierter Test.

**Doppelte Programme (Schritt 6b).** Drei CP/A-Programme lagen mehrfach im Baum
(md5-identisch):

| Datei | lag in | jetzt |
|-------|--------|-------|
| `cpabcgen.com` | `boot_disk/`, `docs/`, `tools/cpa_tools/` | nur `boot_disk/` |
| `format.com` | `boot_disk/`, `tools/cpa_tools/` | nur `boot_disk/` |
| `hardy.com` | `boot_disk/`, `docs/` | nur `boot_disk/` |

Die Kopien unter `tools/cpa_tools/` wurden von **nichts** gelesen: die Boot-Disk-Pipeline
startet CPABCGEN und FORMAT aus dem gemounteten Diskettenabbild, nicht aus einer Datei
(`dump cpabcgen` im Treiberskript ist ein Bildschirmabzug mit dieser Beschriftung, kein
Ladebefehl).  Die `docs/`-Kopien waren Analysegegenstände; die betreffenden Texte verweisen
jetzt auf `boot_disk/`.

**Leerdiskette (Schritt 6b).** `leer_cpa780.hfe` (1,9 MB) ist nicht mehr eingecheckt.  Die
Boot-Disk-Pipeline erzeugt ihre Vorlage zur Laufzeit über `DiskImage::create` (C-API,
`gen_named_template()`).  `mk_disk_template` schied dafür aus — es schreibt nur einseitige
Abbilder, cpa780 ist doppelseitig; es um Seiten zu erweitern hieße, das HFE-Seiteninterleave
anzufassen, mit dem Risiko subtil kaputter Vorlagen für kein Testergebnis.  Der Weg über
`create_disk` benutzt dagegen Code, den `A5120DiskApi`, `CreateDiskDefault` und
`tests/python/test_binding.py` bereits abdecken.

Gegenprobe der erzeugten Vorlage vor dem Umstellen: `k1520dbg` → `disk verify` meldet
„160 Spuren, 863 Sektoren, 0 CRC-Fehler, 0 Problem-Spuren" — und `bootdisk_cpa780` läuft
damit grün durch.

**Ersparnis:** 1,9 MB (Leerdiskette) + 52 KB (Programmkopien) weniger im Git.

**Verifikation:** `build/` gelöscht und neu gebaut, `tools/dev.sh test` 672/672 grün,
`tools/dev.sh test-format` 8/8 grün, `format_all.py --list` funktioniert am neuen Ort.

### 2026-08-07 — origin/main eingeholt (33 Commits, zwei Stufen)

Der Branch lag 33 Commits hinter origin/main.  Gemerged in zwei Stufen entlang der
Feature-Grenzen von origin (`94bcffb` boot_udos, `a24e4e8` udos_diskformats), jede Stufe
einzeln aufgelöst und vollständig getestet.

**Was Git selbst konnte:** die Rename-Erkennung hat origins Änderungen an den von uns
verschobenen Testdateien korrekt zugeordnet — `test_boot_integration`, `test_z80`,
`test_machine_snapshot`, `test_prn_listing`, `test_k5122` landeten ohne Konflikt in
`tests/{integration,unit,debugtools}/`.  Nur bei den NEUEN Dateien versagte die Heuristik
(„directory rename split": `tests/cpp` wurde auf mehrere Ziele aufgeteilt, keines mit
Mehrheit) — die mussten von Hand einsortiert werden.

**Wiederkehrendes Konfliktmuster.** In beiden Stufen kollidierte `CMakeLists.txt`: origin
ergänzt dort Testregistrierung, wir haben sie nach `tests/` verlagert.  Auflösung jeweils:
prüfen, ob origins Änderung **nur** Registrierung ist (Stufe A: ja) oder auch Bibliotheken
betrifft (Stufe B: ja — `k1520_floppy2` bekam die neuen Codec-Quellen), dann unsere Fassung
plus die Bibliotheksänderung übernehmen und die Registrierungen in `tests/` nachziehen.

**Stufe A (boot_udos).** UDOS 4.3, drei Emulator-Fixes, Debugger-Regressionsnetz,
Fremdquellen-Annotation.  Nachgezogen: `test_mac_listing` → `tests/debugtools/`,
`all_commands_smoke.dbg` → `tests/cli/scripts/`, **27 neue CLI-Fälle ins Datenformat**
(46 statt 19).  Dabei drei Runner-Erweiterungen (`capture_file:`, Lauf im Temp-Verzeichnis,
`%CLI_DIR%`) und **ein echter Fehler im Runner**: Platzhalter wurden nur in `run:` ersetzt,
nicht in der Standardeingabe.

**Stufe B (udos_diskformats).** Der Medium-Umbau: `DiskMedium` als internes Abbild plus
Container-Codecs `.img`/`.hfe`/`.dmk`; `hfe_image`/`raw_sector_image` sind weg,
`test_hfe_image` und `test_disk_image_raw` durch `test_hfe_codec`/`test_img_codec` ersetzt.
Fünf neue Unit-Tests nach `tests/unit/peripherals/`, `test_udos_format` nach `tests/system/`,
dazu die **88er Format-Matrix** (neues Label `format_matrix`, beim `cmake` aus
`format_all.py --list-matrix` erzeugt) und `dev.sh test-matrix`.

**Vier Dateien habe ich bewusst in origins Fassung übernommen** statt zu verschmelzen —
`make_bootdisk.py`, `format_all.py`, `test_a5120_disk_api.cpp`, `test_boot_integration.cpp`:
origin hat sie um die neue Medium-Semantik herum neu geschrieben, und deren Sachwissen wiegt
schwerer als unsere Änderungen daran.  Anschließend nur unsere **strukturellen** Anpassungen
erneut aufgetragen (Pfade, Fixture-Namen, `TempDisk`, Support-Bibliothek).

**Damit überholt:** `gen_named_template()` aus Schritt 6b.  Es beruhte darauf, dass
`DiskImage::create` für `.hfe` eine *formatierte* Diskette anlegt (Stand bis 2026-07-06).
Seit dem Medium-Umbau heißt ein leerer Formatname „echte Leerdiskette", und origins
Boot-Disk-Pipeline formatiert sie selbst mit FORMAT.COM — der Gap-Blank-Hänger ist im Kern
behoben (`doc/analyse_format_leerspur.md`).  Die eingecheckte Leerdiskette bleibt damit
zu Recht entfernt, nur auf einem besseren Weg als von uns gebaut.

**Verifikation nach vollständigem Neubau:** `tools/dev.sh test` **783/783**,
`test-format` **16/16**, `test-matrix` **88/88** grün.  Ebenen jetzt: unit 580,
debugtools 89, integration 62, cli 46, system 104, python 7.

---

## 7. Nebenbefund: 5×1024-System als Quelle erzeugt defekte Systemdiskette

Beim Versuch, die beiden SCPX-Fixtures zu **einer** zusammenzulegen (die HARDY-Diskette sollte
`scpx17_cpa780_k5601.hfe` mit ersetzen), kam heraus: die beiden Disketten tragen **verschiedene
SYSP-Generierungen** — 16×256 gegenüber 5×1024 — und sind nicht austauschbar.

Reproduzierbarer Befund dabei:

> `ScpxInit.Builds5x1024SystemViaInitModfSyspAndBoots` erzeugt per INIT (Option 3) + MODF +
> SYSP + `PIP B:=A:*.*` eine 5×1024-Systemdiskette und bootet sie.
> - Quelle = **16×256**-System (`scpx17_cpa780_k5601.hfe`): erzeugte Diskette ist sauber,
>   bootet, `STAT` läuft. ✅
> - Quelle = **5×1024**-System (`scpx17_5x1024_k5601_hardy.hfe`): die erzeugte Diskette hat
>   auf **jeder** Datenspur genau einen defekten Sektor (`k1520dbg` → `disk verify`:
>   „C n H m: 5 Sekt, 1 CRC-Fehler", durchgehend C1…C79 beide Köpfe). Sie bootet und `DIR`
>   listet alle Dateien, aber kein `.COM` lässt sich mehr laden (`SCPX ERR ON A: BAD SECTOR`).
>
> Beide committeten Quelldisketten sind selbst fehlerfrei (`disk verify` sauber).

Offen, ob das eine echte Lücke im Schreibpfad des K5122 ist oder eine reale SCPX-Einschränkung
(ein 5×1024-System generiert kein zweites 5×1024-System). Kein Test hängt daran — der
Regressionswächter läuft weiter auf der 16×256-Quelle. Aufwand für die Klärung: unbekannt,
Einstieg wäre ein `--log-level info`-Lauf des Schreibpfads über eine Datenspur.

---

**Offener Folgepunkt:** `README.md` beschreibt weiterhin den entfernten Emulator
(Speicherkarte, Bootprozess, Diskettenformat — alles CP/M-spezifisch und für den Kern
ungültig). Es braucht eine Neufassung für den K1520-Kern.

---

## 5. Offene Entscheidungen

1. ~~**Legacy-Emulator `src/`**~~ — **entschieden am 2026-08-07: vollständig entfernt** (§6).
2. **Disk-Fixtures (23 MB)** — im Git belassen, auf git-lfs umstellen, oder soweit möglich zur
   Testzeit generieren?
3. **Python-Testebene** — gewünscht? Sie schließt die einzige komplett ungetestete Schicht
   (GUI + C-API-Bindung), kostet aber pytest+PySide6 als Testabhängigkeit.
4. **CI** — GitHub Actions gewünscht, oder bleibt es bei lokalen Läufen?

---

## 8. Nebenbefund: IEO sperrt nur bei anstehendem, nicht bei laufendem Interrupt

Beim Überführen des losen `test_reti.cpp` nach GoogleTest stellte sich heraus, dass seine
Erwartung nicht dem implementierten Verhalten entspricht — die Datei war nie gebaut worden,
also hatte es nie jemand bemerkt.

Alle drei Interruptbausteine implementieren `getIEO()` als **„IEI && kein Interrupt
ANSTEHEND"**:

| Baustein | Stelle |
|----------|--------|
| `Z80CTC::getIEO` | `core/primitives/z80_ctc.cpp:381` — `return iei_ && !anyPending();` |
| `Z80PIO::getIEO` | `core/primitives/z80_pio.cpp:382` |
| `Z80SIO::getIEO` | `core/primitives/z80_sio.cpp:192` |

Nach der Quittung ist `pending` gelöscht und `ius` gesetzt — IEO gibt also wieder frei, obwohl
die ISR noch läuft. Auf echter Hardware bliebe IEO gesperrt, bis `RETI` das IUS zurücknimmt;
nachrangige Bausteine könnten die laufende ISR nicht unterbrechen. Der Doxygen-Kommentar an
`Z80CTC::getVector` behauptet genau das („This blocks lower-priority channels until RETI is
executed") — die Implementierung tut es nicht.

Der Schutz greift eine Ebene tiefer: `getVector()` verweigert die Quittung, solange IUS steht.
Die Verschachtelung wird also **je Baustein** verhindert, nicht über die Kette. Für den A5120
reicht das offenbar (voller CP/A-Boot und alle Integrationstests grün).

Bewusst **nicht geändert**: Interrupts sind für die Bootkette load-bearing (CLAUDE.md,
Invariante 7 zum PIO-Interruptsteuerwort), und eine Verhaltensänderung gehört nicht in einen
Testsystem-Umbau. Stattdessen hält `RetiChain.IeoBlocksOnPendingNotOnService` den IST-Zustand
fest — schlägt er fehl, wurde das Verhalten geändert und der Kommentar dort gehört korrigiert.

Nebenbei fiel auf: `Z80SIO::getVector()` liefert bei „nichts quittierbar" die **reine
Vektorbasis** statt `0xFF` wie CTC und PIO (`core/primitives/z80_sio.cpp:333`). Ohne Folgen,
solange der Vektor nur nach einer echten INT-Anforderung gelesen wird.
