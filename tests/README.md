# Testsystem

Praktischer Einstieg: ausführen, einen Test hinzufügen, Fehler eingrenzen.
Das *Warum* der Gliederung steht in `doc/design/12_testing.md`, die
Umbaugeschichte in `doc/testsystem_rework.md`.

## Ausführen

Immer über `tools/dev.sh` — es baut zuerst das passende Verzeichnis und
verhindert damit, dass man versehentlich alte Objektdateien testet.

Die Regression läuft **parallel** (`ctest -j`, Vorgabe `nproc`; mit `K1520_JOBS=<n>`
anders einstellbar) — 932 Fälle in ~14 s statt ~36 s. Der Preis dafür: **kein Test darf
geteilten Zustand anfassen**, denn ctest startet jeden Fall als eigenen Prozess. Zwei
Stellen sind daran schon aufgelaufen:

* **Temp-Dateien** brauchen einen eindeutigen Namen — `k1520test::tempPath()`, siehe
  [unten](#vier-windows-fallen-beim-testschreiben).
* **pytests Cache-Verzeichnis** liegt für alle Fälle am selben Ort; die ctest-Aufrufe
  schalten ihn deshalb ab (`-p no:cacheprovider` in `python/CMakeLists.txt`). Ohne das
  brach der Verlierer eines Rennens schon beim Einsammeln ab — *ohne einen einzigen Test
  gefahren zu haben*, was in der Ausgabe wie ein Testfehler aussieht.

```sh
tools/dev.sh test                    # Regression: alles außer den langsamen (~14 s)
tools/dev.sh test-format             # NUR die Boot-Disk-Kette (Label format_integration)
tools/dev.sh test-matrix             # NUR die 88 Format-Matrix-Tests (jedes FORMAT.COM-Menü)
tools/dev.sh test-all                # beides
tools/dev.sh test-python             # nur die pytest-Ebene
tools/dev.sh test-level unit         # eine Ebene: unit|debugtools|integration|cli|system|python
tools/dev.sh test -R K2526           # Namensmuster (ctest-Argumente werden durchgereicht)
```

Jeder Lauf hinterlässt nebenbei `build/Testing/junit.xml`; daraus wird eine lesbare
Seite (Ebenen, Laufzeiten, Fehlschläge mit voller Ausgabe):

```sh
tools/dev.sh test
python3 tools/test_report.py build/Testing/junit.xml -o protokoll.html
```

Dieselbe Seite hängt die CI an jeden Lauf als Artefakt — `doc/ci_pipeline.md` §4.5.

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
| `unit/` | 750 | Eine Klasse isoliert, keine Diskette, kein Boot. Struktur spiegelt `core/`: `primitives/ bus/ cards/ peripherals/ util/` |
| `debugtools/` | 89 | Die header-only Bausteine, aus denen `k1520dbg` und `boot_trace` bestehen (`tools/*.h`) |
| `integration/` | 72 | Ganze Maschine, echter Kaltboot von einer Fixture-Diskette |
| `cli/` | 59 | Die gebauten Werkzeuge als Prozess. Fälle als Daten in `cli/cases/*.cli`, ausgeführt von `cli/run_case.py` |
| `system/` | 106 | Originale DDR-Programme unter dem Emulator: FORMAT, CPABCGEN, SCPX INIT/MODF/SYSP, HARDY, UDOS — plus die 88er Format-Matrix. **Langsam** (Minuten) |
| `python/` | 12 | pytest: C-ABI (ctypes ↔ `libk1520core.so`), PySide6-GUI headless, Pfadauflösung, Testprotokoll |
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

## Vier Windows-Fallen beim Testschreiben

Die Regression läuft seit 2026-08-11 auch auf `windows-latest`
(`.github/workflows/windows-ci.yml`). Von den 29 Fehlschlägen des ersten Laufs war
**keiner** ein Produktfehler — alle vier Ursachen waren Annahmen im Test, und alle
vier kommen wieder, wenn man sie nicht kennt:

1. **Kein festes `"/tmp/…"`, und kein fester Dateiname.** Unter Windows liegt
   `/tmp/…` als `C:\tmp\`, das es nicht gibt; der `ofstream` scheitert **lautlos**
   und der Test sieht eine leere Datei. Richtig ist **`k1520test::tempPath("name")`**
   (`support/temp_path.h`, header-only) bzw. `tmp_path` (pytest) — nie
   `temp_directory_path() / "fester_name"` von Hand.

   Der Dateiname muss eindeutig sein, weil `gtest_discover_tests` **jeden** Testfall
   als eigenen Prozess startet: mit `ctest -j` benutzen dann zwei Testprozesse
   gleichzeitig dieselbe Datei. Unter Linux fällt das nie auf (eine geöffnete Datei
   darf man löschen), unter Windows ist es ein harter Fehler — `remove` scheitert mit
   „Sharing violation", und der Test stirbt an einer Ausnahme, die mit seinem
   Gegenstand nichts zu tun hat. Genau so gefunden (2026-08-12, `tools/dev.sh win`
   mit `-j8`): sechs Tests rot, seriell alle grün.
2. **`remove()` scheitert, solange die Datei offen ist.** Windows kennt kein
   „gelöscht, aber noch benutzt". Ein `std::ifstream`, mit dem der Test die Datei
   gegenliest, gehört in einen eigenen Block **vor** dem `remove()`.
3. **`Path.read_text()` / `text=True` ohne Kodierung** lesen in der
   Gebietsschema-Kodierung — unter Windows cp1252. Der Baum ist UTF-8 und voller
   Umlaute; der erste Umlaut wirft `UnicodeDecodeError` oder liefert Kauderwelsch.
   Immer `encoding="utf-8"` mitgeben (`subprocess.run` genauso).
4. **`std::filesystem::path` → `std::string` ist unter Windows nicht implizit**
   (`value_type` ist dort `wchar_t`). MSVC lehnt es mit C2440 ab; `.string()`
   anhängen.

Was **nicht** Aufgabe des Tests ist: plattformabhängiges Verhalten wegdefinieren.
`~/.config` vs. `%APPDATA%`, `user-dirs.dirs` vs. `Documents`, `install.sh` vs.
`k1520emu.iss` — dort gehört ein `pytest.mark.skipif` mit **Begründung** hin und,
wo es sich lohnt, ein Gegenstück für die andere Plattform (Muster:
`test_paths.py::test_dokumentenordner_unter_windows_ist_documents`).

Lokal vorprüfen, ohne auf die CI zu warten: `tools/dev.sh win` baut mit MinGW-w64
nach Windows und fährt die Tests unter `wine` (~15 s). Findet die Punkte 1–3 sofort,
Punkt 4 nicht (das ist MSVC-eigen).

> **Warum der Cross-Bau `DISCOVERY_MODE PRE_TEST` benutzt:** `gtest_discover_tests`
> startet in der Vorgabe jedes Testprogramm **beim Bauen** einmal, um seine `TEST`s
> aufzuzählen — im Cross-Bau also unter `wine`, und bei `cmake --build -j` dutzendfach
> gleichzeitig. Einer fiel dabei regelmäßig um, und der Bau meldete „Error running test
> executable", obwohl nichts kaputt war. Ein roter Bau, der nicht rot ist, macht das
> Werkzeug unbrauchbar. `PRE_TEST` sammelt erst zur Testzeit ein. Gilt **nur** beim
> Cross-Bau (`CMAKE_CROSSCOMPILING`); nativ bleibt die Vorgabe, die dort schneller ist.

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
