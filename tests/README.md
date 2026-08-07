# Testsystem

Praktischer Einstieg: ausführen, einen Test hinzufügen, Fehler eingrenzen.
Das *Warum* der Gliederung steht in `doc/design/12_testing.md`, die
Umbaugeschichte in `doc/testsystem_rework.md`.

## Ausführen

Immer über `tools/dev.sh` — es baut zuerst das passende Verzeichnis und
verhindert damit, dass man versehentlich alte Objektdateien testet.

```sh
tools/dev.sh test                    # Regression: alles außer den langsamen (~12 s)
tools/dev.sh test-format             # NUR die Boot-Disk-Kette (Label format_integration)
tools/dev.sh test-matrix             # NUR die 88 Format-Matrix-Tests (jedes FORMAT.COM-Menü)
tools/dev.sh test-all                # beides
tools/dev.sh test-python             # nur die pytest-Ebene
tools/dev.sh test-level unit         # eine Ebene: unit|debugtools|integration|cli|system|python
tools/dev.sh test -R K2526           # Namensmuster (ctest-Argumente werden durchgereicht)
```

Einzelnen Fall genauer ansehen:

```sh
./build/k1520_test_k2526 --gtest_filter='*ZVE2*'          # ein Binary direkt
./build/k1520_test_k2526 --gtest_list_tests                # was steckt drin
ctest --test-dir build -R Hardy --output-on-failure        # mit voller Ausgabe
venv/bin/python3 -m pytest tests/python -q -k c_api        # Python-Ebene
```

## Aufbau

Testebene = Verzeichnis = ctest-Label. Quer dazu `fast` / `slow`.

| Verzeichnis | Fälle | Was dort hingehört |
|-------------|------:|--------------------|
| `unit/` | 580 | Eine Klasse isoliert, keine Diskette, kein Boot. Struktur spiegelt `core/`: `primitives/ bus/ cards/ peripherals/ util/` |
| `debugtools/` | 89 | Die header-only Bausteine, aus denen `k1520dbg` und `boot_trace` bestehen (`tools/*.h`) |
| `integration/` | 62 | Ganze Maschine, echter Kaltboot von einer Fixture-Diskette |
| `cli/` | 19 | Die gebauten Werkzeuge als Prozess. Fälle als Daten in `cli/cases/*.cli`, ausgeführt von `cli/run_case.py` |
| `system/` | 104 | Originale DDR-Programme unter dem Emulator: FORMAT, CPABCGEN, SCPX INIT/MODF/SYSP, HARDY, UDOS — plus die 88er Format-Matrix. **Langsam** (Minuten) |
| `python/` | 7 | pytest: C-ABI (ctypes ↔ `libk1520core.so`) und PySide6-GUI headless |
| `support/` | — | Bibliothek `k1520_testsupport`, keine Tests |
| `fixtures/` | — | Testdisketten (`tests/fixtures/README.md`) |

Die langsamen tragen zusätzlich eines von zwei Format-Labeln, auf die
`tools/dev.sh test` filtert (`-LE "format_(integration|matrix)"`):
`format_integration` (16 — die **Tiefe**: je Laufwerkstyp ein Format über die
ganze Diskette) und `format_matrix` (88 — die **Breite**: jeder einzelne
FORMAT.COM-Menüeintrag, Umfang Smoke).  Die Matrix wird beim `cmake` aus
`tests/system/drivers/format_all.py --list-matrix` erzeugt und wächst dort
automatisch mit.

## Einen Test hinzufügen

Quelldatei in die passende Ebene legen, dann **eine Zeile** in deren
`CMakeLists.txt`:

```cmake
k1520_add_test(k5122 SRC cards/test_k5122.cpp LIBS k1520_k5122 LABELS "unit;fast")
```

`k1520_add_test()` (`tests/cmake/K1520AddTest.cmake`) erledigt den Rest: Ziel
heißt `k1520_test_<name>`, das Binary landet in `build/`, `GTest::gtest_main`
und die Include-Wurzel kommen automatisch, ebenso der Fixture-Pfad als
Compile-Definition. Weitere Argumente: `DEFS` (zusätzliche Makros),
`TIMEOUT` (Vorgabe 60 s).

> **Falle:** `gtest_discover_tests(... PROPERTIES LABELS "a;b")` übernimmt nur
> das ERSTE Label — die Liste zerfällt beim Durchreichen. `k1520_add_test()`
> maskiert die Semikola; nicht daran vorbeibauen.

Braucht der Test die Maschine, kommt `k1520_testsupport` in die `LIBS`.

**Suitenname:** die geprüfte Komponente, so wie sie im Kern heißt — `Z80PIO`,
`K3526`, `TrackCodec`.  Ein Thema darf eine eigene Suite bekommen
(`BitCodecMarks`, `TrackCodecCrc`), aber **keine willkürliche zweite Suite für
dieselbe Komponente**: sonst übersieht `ctest -R <Suitenname>` stillschweigend
einen Teil.  Genau das war bis 2026-08-07 dreimal der Fall (`PIO` neben
`Z80PIO`, `SIO` neben `Z80SIO`, `CTC` neben `Z80CTC`) — die vier verirrten Tests
sind zusammengeführt.

Eine Ausnahme erzwingt GoogleTest selbst: `TEST` und `TEST_F` dürfen sich keine
Suite teilen.  Braucht ein Teil der Tests eine Fixture und ein anderer eine
eigene Konfiguration, sind zwei Suiten unvermeidlich — so bei `K7024Test`
(Fixture, Standardkonfiguration) und `K7024` (baut Bus und Karte je Test selbst).
`ctest -R K7024` erwischt beide.

Die vorhandenen Namen sind nicht durchgängig — `K5122Test` und `Z80Test` tragen
ein `Test`-Suffix, das andere nicht haben, und Deutsch und Englisch mischen sich.
Das bleibt bewusst so: ein flächendeckendes Umbenennen wäre Kosmetik, würde
`ctest -R`-Gewohnheiten und Verweise in der Werkzeugdokumentation brechen und bei
jedem Merge mit origin/main Reibung erzeugen.  Für **neue** Suiten gilt die Regel
oben.

**CLI-Fall hinzufügen:** eine Datei `cli/cases/<name>.cli` anlegen — sonst
nichts, CMake liest das Verzeichnis ein. Format:

```
tool:   k1520dbg
disk:   cpa_cpa780_k5601_clock.img     # wird nach /tmp kopiert → %DISK%
run:    %DISK%
stdin:
  b 0x0135
  g
  q
expect: bp ZVE1 : ZVE1 PC=0135
```

Erwartungen sind normale Zeichenketten (`expect:`), Regex nur wo nötig
(`expect_re:`); dazu `forbid:`, `exit:`, `setup_run:` für einen Vorlauf,
`file <name>:`/`tmpfile <name>:` für Ein-/Ausgabedateien. Vollständige
Direktivenliste: Kopf von `cli/run_case.py`.

## Gemeinsame Infrastruktur (`support/`)

Namensraum `k1520test`, für Integrations- und Systemtests gedacht:

| Header | Inhalt |
|--------|--------|
| `screen.h` | `vramText()` — Textbildschirm (0xF800) als Zeichenkette; `vramLines()` für lesbare Fehlerausgaben |
| `machine_run.h` | `runCycles()`, `runSmallUntil()`, `runUntilVramContains()`, `runUntilPC()` |
| `keyboard.h` | `typeKey()`, `typeString()`, `typeCtrl()`, `pressKeyUntil()`, `QK_RETURN` |
| `fixtures.h` | `diskPath()`, `readFileBytes()`, `TempDisk` |

Zwei Regeln, die dahinterstecken:

1. **Nie eine committete Diskette direkt mounten** — der Emulator öffnet sie
   schreibend. `TempDisk` macht die Kopie und räumt sie weg, auch wenn der Test
   per `ASSERT_*` abbricht.
2. **Batchgröße ist nicht beliebig.** Sobald Tastatur im Spiel ist, in Schritten
   von 5 000 Takten laufen (`runSmallUntil`, `runCycles`): der K7637 modelliert
   eine 9600-Baud-Strecke, das BIOS holt die Zeichen per Timer-ISR ab. Mit
   groben Batches driftet die CTC-Phase so weit, dass Anschläge verlorengehen.
   Ohne Tastaturbezug ist `runUntilVramContains` (100 000) richtig und schneller.

## Was wo dokumentiert ist

| Frage | Antwort steht in |
|-------|------------------|
| Was prüft ein einzelner Test? | Im Test selbst — jede Datei und jeder `TEST()` trägt einen `@test`/`@brief`-Kommentar. Überblick maschinell: `--gtest_list_tests` |
| Welche Testdiskette ist welche? | `tests/fixtures/README.md` |
| Was deckt die Python-Ebene ab, was nicht? | `tests/python/README.md` |
| Warum ist das System so gegliedert? | `doc/design/12_testing.md` |
| Wie kam es dahin, was ist noch offen? | `doc/testsystem_rework.md` |

## Vor dem Push

`.githooks/pre-push` fährt `tools/dev.sh test` und lehnt den Push bei rotem
Ergebnis ab (einmalig aktivieren: `git config core.hooksPath .githooks`).
Die langsame Runde läuft dort **nicht** mit — `tools/dev.sh test-format` gehört
vor einen Merge nach `main`.
