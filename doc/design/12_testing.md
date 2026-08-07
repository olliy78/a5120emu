# Testkonzept

**Modul:** `tests/`
**Stand:** 2026-08-07 (Umbau in `doc/testsystem_rework.md` protokolliert)

Dieses Dokument begründet die Gliederung. Wie man die Tests ausführt und einen
neuen hinzufügt, steht in `tests/README.md`.

---

## 1. Leitgedanken

1. **Eine Ausführungsquelle.** Alles läuft unter `ctest` — auch die
   Python-Ebene und die Blackbox-Tests der Werkzeuge. `tools/dev.sh` ist ein
   bequemer Aufsatz, aber nicht die einzige Stelle, die die volle Testmenge
   kennt. (Bis 2026-08-07 lief eine eigene Harness-Suite mit 58 Fällen
   *außerhalb* von ctest — ein `ctest`-Lauf meldete grün, ohne sie ausgeführt zu
   haben.)
2. **Gliederung nach Testebene, nicht nach Framework.** Verzeichnis = Ebene =
   ctest-Label.
3. **Laufzeit ist ein Label, keine Verzeichniseigenschaft** (`fast`/`slow`).
   Auswahl über `ctest -L`, nicht über handgepflegte Ausschlusslisten.
4. **Testcode wohnt in `tests/`** — auch Python-Treiber, Fixtures und
   Golden-Dateien.
5. **Gemeinsame Infrastruktur statt Kopien.** Was mehr als ein Test braucht,
   gehört nach `tests/support/`.

---

## 2. Testpyramide

```
        ┌──────────────────────────────────────────┐
        │  system/    104  originale DDR-Programme │  Minuten
        │               FORMAT · CPABCGEN · SCPX   │
        │               INIT/MODF/SYSP · HARDY ·   │
        │               UDOS + 88er Format-Matrix  │
        ├──────────────────────────────────────────┤
        │  python/      7  C-ABI + GUI (pytest)    │  Sekunden
        │  cli/        46  Werkzeuge als Prozess   │
        │  integration/62  ganze Maschine, Kaltboot│
        ├──────────────────────────────────────────┤
        │  debugtools/ 89  tools/*.h isoliert      │  Millisekunden
        │  unit/      580  eine Klasse isoliert    │
        └──────────────────────────────────────────┘
```

Die Zahlen sind der Stand vom 2026-08-07 (nach dem Merge von origin/main);
maßgeblich ist immer `ctest -N`.

---

## 3. Was in welche Ebene gehört

### `unit/` — eine Klasse, isoliert

Keine Diskette, kein Boot, keine Datei. Struktur spiegelt `core/`
(`primitives/ bus/ cards/ peripherals/ util/`), damit die Frage „wo teste ich
das?" nicht diskutiert werden muss: dort, wo im Kern auch der Code liegt.

Ein Test gehört *nicht* hierher, sobald er eine `A5120Machine` braucht — das ist
die Grenze zur Integrationsebene. Sichtbar auch im Build: `unit/` linkt die
Maschinenbibliothek nicht.

### `debugtools/` — die Bausteine der Werkzeuge

`k1520dbg` und `boot_trace` sind aus header-only Bausteinen zusammengesetzt
(`tools/expr_eval.h`, `until_cond.h`, `event_bp.h`, `mem_watch.h`,
`dbg_commands.h`, `coverage_diff.h`, `callstack_tracker.h`, `prn_listing.h`).
Die sind Produktivcode, kein Testcode — sie werden hier wie jede andere
Bibliothek geprüft, nur eben ohne Emulator.

### `integration/` — die ganze Maschine

Echter Kaltboot von einer Fixture-Diskette; prüft, was nur im Zusammenspiel
sichtbar wird: die ZVE1↔ZVE2-DMA-Kette, Reset/Power-Cycle, das Zusammenspiel
von K5122-Lesepfad und laufendem OS. Laufzeit 0,2–2 s je Fall, deshalb `fast`.

Kriterium für einen Fall hier: Es gibt einen **benennbaren Meilenstein** (ein
Banner, ein Prompt, ein Sprungziel), an dem sich eine Regression festmachen
lässt. „Bootet irgendwie" ist kein Test.

### `cli/` — die Werkzeuge als Prozess

Blackbox: Werkzeug starten, Sitzung per stdin einspielen, Ausgabe prüfen. Deckt
die Schicht ab, die die Header-Tests nicht erreichen — Kommandozerlegung,
Argumentbehandlung, Exit-Codes, Zusammenbau der Maschine.

Die Fälle stehen als **Daten** in `tests/cli/cases/*.cli` (Werkzeug, Diskette,
Standardeingabe, Erwartungen) und werden von `tests/cli/run_case.py` ausgeführt
— je Datei ein ctest-Fall. Einen Fall hinzufügen heißt: eine Datei anlegen.

Erwartungen sind normale Zeichenketten; ein regulärer Ausdruck ist die Ausnahme
(`expect_re:`) und nicht mehr die Regel. Bis 2026-08-07 steckten die Fälle als
escapte Shell-Einzeiler mit CMake-Regex im Build-System — Sonderzeichen
überlebten das doppelte Escaping nicht, weshalb dort der Hinweis stand: „regex
uses '.' for literal []()*+".

### `system/` — originale Programme

FORMAT.COM, CPABCGEN.COM, SCPX INIT/MODF/SYSP, HARDY: vollständige
Anwenderabläufe, tastaturgesteuert, je Fall ein oder mehrere Kaltstarts.
Minuten statt Millisekunden — daher `slow` und aus der Standardrunde
ausgeschlossen.

Diese Ebene ist der einzige Ort, an dem echtes Zeitverhalten der Peripherie
geprüft wird (Index-Interrupt, CTC-Phase, Tastatur-Timing bei
Höchstgeschwindigkeit) — genau dort saßen historisch die schwersten Fehler.

### `python/` — C-ABI und GUI

Deckt ab, was C++-Tests nicht erreichen können:

- Die **ctypes-Bindung** gegen `libk1520core.so`. Der C++-Compiler prüft die
  Python-Seite nicht; eine geänderte Signatur bricht **still**, und ctypes
  meldet es erst beim Aufruf — oft als Absturz. `test_c_api.py` vergleicht
  Header, `.so` und Bindung mechanisch miteinander.
- Die **PySide6-GUI** headless (`QT_QPA_PLATFORM=offscreen`): Fensteraufbau,
  Widget-Verdrahtung, Konfigurationsrundlauf.

Grenze: keine Pixelprüfungen. Der Bildschirm ist ein `QOpenGLWidget`, offscreen
gibt es keinen FBO. Bildinhalte prüft die C++-Seite über das VRAM.

---

## 4. Labels

| Label | Bedeutung |
|-------|-----------|
| `unit` `debugtools` `integration` `cli` `system` `python` | Ebene (= Verzeichnis) |
| `fast` | läuft in der Standardrunde mit |
| `slow` | nur auf Anforderung |
| `format_integration` | die **Tiefe** der Formatierung: je Laufwerkstyp ein Format über die ganze Diskette (`dev.sh test-format`) |
| `format_matrix` | die **Breite**: jeder einzelne FORMAT.COM-Menüeintrag, Umfang Smoke (`dev.sh test-matrix`) |

`format_integration` und `format_matrix` sind zusammen mit `slow` deckungsgleich;
beide bleiben bestehen, weil `dev.sh`, die Werkzeugdokumentation und eingespielte
Aufrufe darauf verweisen.  Die Matrix wird beim `cmake` aus
`tests/system/drivers/format_all.py --list-matrix` erzeugt — neue Formate in der
Tabelle dort werden beim nächsten Konfigurieren automatisch zu Tests.

---

## 5. Registrierung

Ein Test = eine Zeile, über `k1520_add_test()`
(`tests/cmake/K1520AddTest.cmake`):

```cmake
k1520_add_test(k5122 SRC cards/test_k5122.cpp LIBS k1520_k5122 LABELS "unit;fast")
```

Der Helfer hält drei Zusagen, die vorher je Test einzeln getippt wurden:
Zielname `k1520_test_<name>`, Binary in `build/` (nicht `build/tests/…`, damit
eingespielte Aufrufe gültig bleiben), Fixture-Pfad als Compile-Definition.

Die Registrierung liegt vollständig in `tests/` — je Ebene eine
`CMakeLists.txt`. Die Wurzel-`CMakeLists.txt` enthält nur noch Bibliotheken,
Werkzeuge und `add_subdirectory(tests)`.

**Zwei stille CMake-Fallen**, beide im Code kommentiert:

1. `gtest_discover_tests(... PROPERTIES LABELS "a;b")` setzt nur das **erste**
   Label — die Liste zerfällt beim Durchreichen in zwei Argumente.
   `k1520_add_test()` maskiert die Semikola.
2. `enable_testing()` muss im **Wurzel**-CMakeLists stehen; nur in `tests/`
   aufgerufen, meldet ctest „No tests were found!!!".

---

## 6. Testdaten

Alle Testdisketten liegen unter `tests/fixtures/disks/` — **nur** die, die ein
registrierter Test wirklich braucht. `disks/` im Projektwurzelverzeichnis ist
davon getrennt das Arbeitsverzeichnis für manuelle Läufe und darf sich jederzeit
ändern.

Namensschema und Zuordnung „welcher Test braucht welche Diskette":
`tests/fixtures/README.md`.

**Regel ohne Ausnahme:** Ein Test mountet nie eine committete Diskette direkt —
der Emulator öffnet sie schreibend. `k1520test::TempDisk` macht die Kopie und
räumt sie weg, auch bei einem `ASSERT`-Abbruch.

---

## 7. Wo die Beschreibung eines einzelnen Tests steht

**Im Test selbst.** Jede Testdatei trägt einen Dateikopf, jeder `TEST()` einen
`@test`/`@brief`-Kommentar mit Ziel und Pass-Kriterium — bei den kniffligen
Fällen zusätzlich die Vorgeschichte („vor dem Fix meldete INIT BAD TRACKS auf
Kopf 1").

Eine separate Prosafassung dieser Kommentare gab es bis 2026-08-07 als
`doc/cpp_testsyste.md` (1349 Zeilen). Sie ist gelöscht: sie war eine Kopie der
Quellkommentare, driftete unvermeidlich (sie beschrieb zuletzt Testdateien, die
es nicht mehr gab, und nannte falsche Binärpfade) und hatte gegenüber dem
Kommentar am Code keinen Mehrwert. Maschinellen Überblick liefern
`ctest -N` und `--gtest_list_tests`.

---

## 8. Bewusste Auslassungen

- **Keine CI.** Das Projekt wird lokal entwickelt; statt eines Workflows
  erzwingt `.githooks/pre-push` die grüne Schnellrunde vor jedem Push.
- **Keine Coverage-Messung.** Bisher kein Bedarf angemeldet; die Lücken sind
  bekannt und in `doc/testsystem_rework.md` benannt.
- **Keine Pixelprüfung der GUI** (siehe §3, `python/`).
- **Ein dauerhaft deaktivierter Test:**
  `KeyboardIntegration.DISABLED_TypeCommandAtCcpEchoesAndProcesses` — die
  CP/A-Statuszeile mit laufender Uhr macht die Eingabe zeitabhängig. Reaktivieren
  ist Schritt 11 des Umbauplans.
