# K1520emu — Sammlung von Emulatoren und Werkzeugen für K1520 und P8000 Rechner

Hardwarenahe Emulation von Bürocomputern und Steuerrechnern aus der DDR, gebaut aus dem
**K1520-Bausteinsystem** — dem Z80-Bussystem, auf dem A5120, K8915, PRG 710 und PC 1715
beruhen. Mit dabei ist außerdem die **EAW P8000**: kein K1520-Rechner, sondern ein eigener
8/16-Bit-Arbeitsplatzrechner mit UNIX-Ableger WEGA, der nur einzelne Bausteine mit den
anderen teilt.

Nachgebildet wird nicht das Betriebssystem, sondern die **Hardware** — Steckkarten,
Prozessoren, Floppy-Controller, Tastaturen. Boot-ROM, BIOS und Betriebssystem laufen als
echter Maschinencode; deshalb booten die Original-Betriebssysteme von Original-Disketten,
und auch Formatieren mit dem originalen `FORMAT.COM` funktioniert.

> ### ⬇ Fertige Pakete für Windows und Linux
> **[Zur Downloadseite (alle Releases)](https://github.com/olliy78/k1520emu/releases)**
>
> Installation benutzerlokal, **ohne Administratorrechte** — für Windows ein
> Installationsprogramm, für Linux ein Archiv mit `install.sh`. Weder Compiler noch
> Quelltext nötig; Python und Qt holt der Installer selbst. Beispieldisketten mit den
> Betriebssystemen liegen bei.

## Was emuliert wird

| Rechner | Betriebssysteme |
|---|---|
| **robotron A5120** (auch A5120.16 mit 16-Bit-Erweiterung) | CP/A, SCPX, UDOS |
| **robotron K8915** (V3 und V2) | SCPX 8915 |
| **PRG 710 / PRG 710-1** (Numerik-Programmiergeräte) | UDOS, SCPX |
| **robotron PC 1715 / PC 1715W** | SCP 1715, CP/A, CP/Z, UDOS 1715, SCP 3.0 |
| **EAW P8000** (8- und 16-Bit-Teil, Winchester) | UDOS, WEGA (UNIX) |

Dazu steckbare Optionen wie RAM-Floppy, Lochstreifen und Kassette, serielle Schnittstellen nach außen
(Telnet, RFC 2217, Datei) und echte Disketten über einen
[Greaseweazle](https://github.com/keirf/greaseweazle)-Adapter.

## k1520DiskTool — Dateien zwischen PC und Diskette

Ein eigenes Programm mit Oberfläche und Kommandozeile, das Dateien von Diskettenabbildern
holt und zurückschreibt, ohne dass der Emulator laufen muss — praktisch, um Quelltexte am
PC zu bearbeiten und dann im Emulator zu übersetzen, oder um eine Diskette zu sichern.

- Dateisysteme **CP/A, SCPX, UDOS, UDOS 1715/NDOS, SCP1700** (A7100) und **WEGA**, in den
  Abbildformaten `.img`, `.hfe` und `.dmk`; Geometrie und Dateisystem erkennt es selbst.
- Auflisten, herausholen, einfügen, löschen, leere oder bootfähige Disketten anlegen.
- **Diskeditor** bis hinunter auf den einzelnen Sektor, **Dateisystemprüfung** mit
  Reparatur und **Rettung gelöschter Dateien**.
- Echte Disketten lassen sich über einen Greaseweazle direkt öffnen.

## Debugger `k1520dbg` — auch für eigene Programme

Ein interaktiver Debugger im Stil von gdb, der die **ganze Maschine** anhält, nicht nur ein
Programm. Er hängt an der Hardware, nicht am Betriebssystem — untersuchen lassen sich deshalb
Boot-ROM, BIOS, Treiber und Anwendungen unter jedem Gastsystem gleichermaßen, auch eigene
Z80-Programme.

- Haltepunkte, Einzelschritt, **Rückwärtsschritt** und Momentaufnahmen: einmal booten, oft
  weitermachen.
- Speicher, E/A und Bildschirm beobachten, Tastenfolgen einspielen — damit lassen sich auch
  bildschirmgesteuerte Programme automatisiert durchfahren.
- **Quelltext statt Hexdump:** ein kommentiertes Listing oder roher `.MAC`-Quelltext wird an
  die Adressen angeheftet.
- Die Diskette wird kopiert eingelegt; ein Original kann nicht versehentlich beschädigt
  werden.

## Die Programme und ihre Handbücher

- **Emulator** — je Rechner ein eigenes Programm (`a5120emu`, `k8915emu`, `prg710emu`,
  `pc1715emu`, `p8000emu`) mit gemeinsamer Oberfläche: Bildröhre, Laufwerke,
  Bildschirmtastatur.
  **[Handbuch des Emulators](app/help/handbuch.md)** (auch im Programm unter *Hilfe*)
- **k1520DiskTool** —
  **[Handbuch des DiskTools](app/disktool/help/handbuch.md)** ·
  [Kommandozeile](tools/k1520disktool.md)
- **Debugger `k1520dbg`** —
  **[Handbuch des Debuggers](doc/handbuch_k1520dbg.md)** ·
  [Kommandoreferenz](tools/k1520dbg.md) ·
  [Einstieg mit Beispielen](tools/how_to_debug_and_trace.md)

Welche Diskette was enthält: [disks/README.md](disks/README.md).

## Aus dem Quelltext bauen

Wer den Emulator nur benutzen will, nimmt die
**[fertigen Pakete](https://github.com/olliy78/k1520emu/releases)**. Für Entwickler:

```sh
python3 -m venv venv && source venv/bin/activate
python3 -m pip install -r requirements.txt -r requirements-dev.txt
tools/dev.sh build           # C++-Kern bauen
tools/dev.sh test            # Regressionsrunde
bash run_a5120emu.sh         # bzw. run_k8915emu.sh, run_prg710emu.sh, run_pc1715emu.sh,
                             #      run_p8000emu.sh, run_disktool.sh
```

Voraussetzungen: C++17-Compiler, CMake ≥ 3.16, Python ≥ 3.8; gebaut und geprüft auf Linux
(GCC) und Windows (MSVC). **Immer über `tools/dev.sh`** bauen und testen — es stellt sicher,
dass kein veraltetes Binary läuft.

## Weiterführende Dokumente

| Thema | Dokument |
|---|---|
| Einrichten der Entwicklungsumgebung | [SETUP.md](SETUP.md) |
| Architektur des Kerns | [doc/K1520_architecture.md](doc/K1520_architecture.md) |
| Entwürfe je Baugruppe und Maschine | [doc/design/](doc/design/) |
| Testsystem | [tests/README.md](tests/README.md) |
| Werkzeuge (Tracer, Disassembler …) | [tools/README.md](tools/README.md) |
| Diskettenformate | [doc/format.md](doc/format.md) |
| Paket und Installer selbst bauen | [packaging/README.md](packaging/README.md), [doc/design/13_distribution.md](doc/design/13_distribution.md) |
| CI auf GitHub | [doc/ci_pipeline.md](doc/ci_pipeline.md) |
| Offene Punkte | [doc/open_points.md](doc/open_points.md) |

Kommentare und Dokumentation sind überwiegend deutsch.

## Fehler melden

Bugs, Wünsche und Anregungen bitte über die
[Issues](https://github.com/olliy78/k1520emu/issues).

## Lizenz

Der Programmcode steht unter der MIT-Lizenz — maßgeblich ist der englische Text in
[LICENSE](LICENSE). Eine deutsche Übersetzung (nur zur Information) steht in
[LIZENZ.txt](LIZENZ.txt).

**Beigaben Dritter sind davon ausgenommen:** Diskettenabbilder mit Betriebssystemen und
Programmen der damaligen Hersteller (`disks/`, `tests/fixtures/`), EPROM-Abzüge
(`doc/EPROMS/`, `doc/p8000/eproms/`, eingebaut als Datenfelder wie
`core/cards/k2526/rom_data.h`) und digitalisierte Handbücher und Schaltpläne
(`doc/original_docs/`). Die Rechte daran liegen bei den jeweiligen Rechteinhabern; sie
liegen nur bei, um die historische Technik verständlich zu machen und den Emulator zu
prüfen. Rechteinhaber, die mit der Weitergabe nicht einverstanden sind, melden sich bitte
über die [Issues](https://github.com/olliy78/k1520emu/issues). Wortlaut auf Deutsch und
Englisch: [LIZENZ.txt](LIZENZ.txt).
