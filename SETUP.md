# K1520 A5120 Emulator - Setup & Schnellstart

## Voraussetzungen

- Python 3.8 oder später
- C++ Compiler (g++, clang)
- CMake 3.16+

## Installation

### 1️⃣ Virtual Environment einrichten

```bash
cd <Projektverzeichnis>          # dorthin, wo diese Datei liegt
python3 -m venv venv
source venv/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install -r requirements.txt      # Laufzeit (PySide6, PyYAML)
python3 -m pip install -r requirements-dev.txt  # zusätzlich für die Tests (pytest)
```

> Alle Pfade in dieser Anleitung sind **relativ** zum Projektverzeichnis — die
> Arbeitskopie darf überall liegen.  `python3 -m pip` statt `pip` benutzen: bei
> einem kopierten venv zeigt der Shebang von `venv/bin/pip` unter Umständen noch
> auf den Interpreter der Quell-Arbeitskopie.

### 2️⃣ C++ Core bauen

```bash
tools/dev.sh build
```

`dev.sh` konfiguriert und baut `build/` (Release, LOG_LEVEL=3) und meldet, ob etwas
neu gebaut wurde.  Roh geht auch `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
— aber Tools und Tests immer über `dev.sh` starten, sonst testet man leicht
veraltete Objektdateien (es gibt zwei Build-Verzeichnisse mit gleichen Toolnamen).

### 3️⃣ Git-Hook aktivieren (einmalig je Arbeitskopie)

```bash
git config core.hooksPath .githooks
```

Damit läuft vor jedem `git push` die Regressionsrunde (`tools/dev.sh test`, ~12 s);
bei rotem Ergebnis wird der Push abgelehnt.  Das ist die erste Verteidigungslinie —
dieselbe Runde gibt es als Gegenprobe auf sauberem System in GitHub Actions, dort
aber nur **von Hand angestoßen** (`doc/ci_pipeline.md`).
Der Wert ist **relativ** — er bleibt gültig, egal wo die Arbeitskopie liegt.
Einzelnen Push erzwingen: `git push --no-verify`.

### 4️⃣ GUI starten

```bash
export LD_LIBRARY_PATH=$PWD/build:$LD_LIBRARY_PATH
source venv/bin/activate
python3 app/main.py
```

**Oder einfacher:**
```bash
bash run_gui.sh
```

---

## Tests ausführen

Alles läuft über `ctest`; `tools/dev.sh` baut vorher das richtige Verzeichnis.

```bash
tools/dev.sh test              # Regression: C++ + Python, ohne die langsamen (~12 s)
tools/dev.sh test-python       # nur die Python-Ebene (C-ABI + GUI)
tools/dev.sh test-format       # nur die langsamen Format-/Boot-Disk-Tests (~51 s)
tools/dev.sh test-all          # alles zusammen
tools/dev.sh test -R K2526     # einzelne Gruppe über ein Namensmuster
```

Die Python-Ebene (`tests/python/`) prüft die C-ABI und die GUI headless; sie wird nur
registriert, wenn pytest/PySide6/PyYAML installiert sind (siehe Schritt 1️⃣).
Einzelheiten: `tests/python/README.md`, Testdisketten: `tests/fixtures/README.md`.

---

## Virtual Environment deaktivieren

```bash
deactivate
```

---

## Troubleshooting

### `ModuleNotFoundError: No module named 'PySide6'`
**Lösung:** venv ist nicht aktiviert
```bash
source venv/bin/activate
```

### `libk1520core.so not found`
**Lösung:** LD_LIBRARY_PATH nicht gesetzt oder Build fehlgeschlagen
```bash
export LD_LIBRARY_PATH=$PWD/build:$LD_LIBRARY_PATH
```

### GUI startet nicht
**Lösung:** X11/Display nicht verfügbar. Ob Kern und Bindung stimmen, lässt sich
headless prüfen — die Python-Testebene läuft unter `QT_QPA_PLATFORM=offscreen`:
```bash
tools/dev.sh test-python
```

---

## Struktur

```
a5120emu_ui/
├── venv/       Virtual Environment (nach Setup)
├── build/      CMake-Bauverzeichnis (LOG_LEVEL=3); build_trace/ mit LOG_LEVEL=5
├── core/       C++-Kern → libk1520core.so
│   ├── api/            C-ABI (k1520_api.*)
│   ├── bus/            K1520Bus + Koppelbus
│   ├── cards/          K2526, K3526, K7024, K8025, K5122
│   ├── primitives/     Z80, PIO, SIO, CTC, EPROM/RAM
│   ├── peripherals/    Laufwerke, Tastatur K7637
│   └── machines/       a5120 — verdrahtet die Karten
├── app/        PySide6-Oberfläche (main.py, core_binding/, ui/)
├── tools/      dev.sh, k1520dbg, boot_trace, Disassembler
├── tests/      unit/ debugtools/ integration/ cli/ system/ python/
├── doc/        Architektur, Entwürfe, Analysen
└── data/       formats.yaml — Katalog der Diskettengeometrien
```

---

**Weiter:**
- [README.md](README.md) — Überblick über Aufbau und Werkzeuge
- [APP_README.md](APP_README.md) — die Oberfläche
- [tests/README.md](tests/README.md) — Testsystem
- [doc/K1520_architecture.md](doc/K1520_architecture.md) — Architektur des Kerns
- [doc/open_points.md](doc/open_points.md) — offene Punkte
