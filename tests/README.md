# Testsystem

Praktischer Einstieg: ausführen, einen Test hinzufügen, Fehler eingrenzen.
Das *Warum* der Gliederung steht in `doc/design/12_testing.md`, die
Umbaugeschichte in `doc/testsystem_rework.md`.

## Ausführen

Immer über `tools/dev.sh` — es baut zuerst das passende Verzeichnis und
verhindert damit, dass man versehentlich alte Objektdateien testet.

```sh
tools/dev.sh test                    # Regression: alles außer den langsamen (~12 s)
tools/dev.sh test-format             # NUR die langsamen System-Tests (~51 s)
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
| `unit/` | 530 | Eine Klasse isoliert, keine Diskette, kein Boot. Struktur spiegelt `core/`: `primitives/ bus/ cards/ peripherals/ util/` |
| `debugtools/` | 67 | Die header-only Bausteine, aus denen `k1520dbg` und `boot_trace` bestehen (`tools/*.h`) |
| `integration/` | 50 | Ganze Maschine, echter Kaltboot von einer Fixture-Diskette |
| `cli/` | 19 | Die gebauten Werkzeuge als Prozess (Blackbox, Ausgabe geprüft) |
| `system/` | 8 | Originale DDR-Programme unter dem Emulator: FORMAT, CPABCGEN, SCPX INIT/MODF/SYSP, HARDY. **Langsam** (Minuten) |
| `python/` | 7 | pytest: C-ABI (ctypes ↔ `libk1520core.so`) und PySide6-GUI headless |
| `support/` | — | Bibliothek `k1520_testsupport`, keine Tests |
| `fixtures/` | — | Testdisketten (`tests/fixtures/README.md`) |

Die langsamen tragen zusätzlich das Label `format_integration`; darauf filtert
`tools/dev.sh test` (`-LE format_integration`).

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
