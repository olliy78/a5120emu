# cparun

**cparun** ist ein schlanker CP/M-Kommandozeilen-Runner, der CP/M-`.com`-Programme
direkt auf einem Linux-, macOS- oder Windows-Host ausführt. Er stellt eine
CP/M-2.2-BDOS-Emulationsschicht bereit, die alle Dateioperationen auf das
Host-Dateisystem abbildet — es werden keine Disketten-Images benötigt.

cparun wurde ursprünglich als Teil des
[a5120emu](https://github.com/olliy/a5120emu)-Projekts entwickelt, um
Build-Werkzeuge wie den M80-Makroassembler und den LINKMT-Linker aus Makefiles
und CMake-Skripten auf Linux aufzurufen.

---

## Funktionen

- Vollständige Z80-CPU-Emulation (dokumentierter und undokumentierter Befehlssatz)
- CP/M-2.2-BDOS-Teilmenge, ausreichend für typische transiente Programme:
  - Konsol-E/A (Funktionen 1, 2, 6, 9, 10, 11)
  - Datei-E/A: Öffnen, Schließen, Erstellen, Löschen, Umbenennen,
    Erster/Nächster Treffer (Funktionen 15–23)
  - Sequentieller und wahlfreier Datensatzzugriff (Funktionen 20, 21, 33, 34, 35, 36, 40)
  - Datenträgerverwaltungs-Stubs (Funktionen 12–14, 24–32, 37)
- Groß-/Kleinschreibungsunabhängige Dateisuche im Host-Dateisystem
- FCB-Analyse mit Laufwerkspräfix (`A:`, `B:`, …)
- Kommando-Tail und doppelter FCB-Aufbau (bei `0x005C` / `0x006C`) gemäß CP/M-Konvention
- TPA von ca. 60 KB (`0x0100`–`0xEFFF`)
- Sauberer Programmausstieg über `JP 0000H` oder BDOS-Funktion 0

---

## Voraussetzungen

- C++17-Compiler (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.16 oder neuer
- Linux, macOS, Windows (MSVC oder MinGW/MSYS2) oder WSL

---

## Build

### Linux / macOS

```sh
cd cparun
cmake -B build
cmake --build build
```

Das fertige Programm liegt unter `build/cparun`.

Systemweite Installation:

```sh
cmake --install build --prefix /usr/local
```

### Windows — Visual Studio (MSVC)

Eine **Developer Command Prompt for VS** (oder ein Terminal mit geladener
MSVC-Umgebung) öffnen, dann:

```bat
cd cparun
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

Die ausführbare Datei befindet sich unter `build\Release\cparun.exe`.

### Windows — MinGW / MSYS2

In einer MSYS2-MINGW64-Shell:

```sh
cd cparun
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

Das Programm liegt unter `build/cparun.exe`.

---

## Verwendung

```
cparun [Optionen] <Befehl> [Argumente...]
```

### Optionen

| Option | Beschreibung |
|---|---|
| `-dir <Pfad>` | Arbeitsverzeichnis, in dem `.com`-Dateien und Datendateien liegen (Standard: aktuelles Verzeichnis) |
| `-h`, `--help` | Hilfe anzeigen und beenden |

### Argumente

| Argument | Beschreibung |
|---|---|
| `<Befehl>` | Name des auszuführenden CP/M-Programms, mit oder ohne `.com`-Endung |
| `[Argumente...]` | Argumente, die dem CP/M-Programm über Kommando-Tail und FCBs übergeben werden |

### Beispiele

M80-Makroassembler auf eine Quelldatei in `cpa_src/` anwenden:

```sh
cparun -dir cpa_src m80 bios.erl=bios
```

Objektdateien mit LINKMT linken:

```sh
cparun -dir cpa_src linkmt "@OS=cpabas,ccp,bdos,bios/p:0BB80"
```

Ein Programm im aktuellen Verzeichnis ausführen:

```sh
cparun myprog arg1 arg2
```

---

## Funktionsweise

1. Die Z80-CPU wird initialisiert und Page Zero mit CP/M-kompatiblen
   Sprungvektoren eingerichtet (`0x0000` Warmstart, `0x0005` BDOS-Einsprung).
2. Die `.com`-Datei wird ab `0x0100` aus dem Arbeitsverzeichnis geladen
   (Suche ohne Beachtung der Groß-/Kleinschreibung).
3. Die Argumentzeichenkette wird in FCB1 (`0x005C`), FCB2 (`0x006C`)
   und den Kommando-Tail (`0x0080`) gemäß CP/M-2.2-Konventionen zerlegt.
4. Die CPU läuft, bis das Programm über BDOS-Funktion 0 oder
   `JP 0000H` (Warmstart) beendet wird.
5. Alle BDOS-Aufrufe (`CALL 0005H`) werden über einen HALT-Trap abgefangen
   und in C++ verarbeitet. Dateioperationen werden transparent auf das
   Host-Dateisystem umgeleitet.

---

## Lizenz

MIT — siehe [LICENSE.txt](LICENSE.txt).
