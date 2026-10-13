# K1520emu — Emulator für Robotron-Rechner am K1520-Bus

Hardwarenahe Emulation von Bürocomputern und Steuerrechnern aus der DDR, gebaut aus dem
**K1520-Bausteinsystem** und seinen Verwandten. Nachgebildet wird nicht das Betriebssystem,
sondern die **Hardware** — Steckkarten, Prozessoren, Floppy-Controller, Tastaturen. Boot-ROM,
BIOS und Betriebssystem laufen als echter Maschinencode; deshalb booten die
Original-Betriebssysteme von Original-Disketten, und auch Formatieren mit dem originalen
`FORMAT.COM` funktioniert.

> ### ⬇ Fertige Pakete für Windows und Linux
> **[Zur Downloadseite (aktuelles Release)](https://github.com/olliy78/k1520emu/releases/latest)**
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

Dazu steckbare Optionen wie RAM-Floppy und Lochstreifen, serielle Schnittstellen nach außen
(Telnet, RFC 2217, Datei) und echte Disketten über einen
[Greaseweazle](https://github.com/keirf/greaseweazle)-Adapter.

## Die Programme und ihre Handbücher

- **Emulator** — je Rechner ein eigenes Programm (`a5120emu`, `k8915emu`, `prg710emu`,
  `pc1715emu`, `p8000emu`) mit gemeinsamer Oberfläche: Bildröhre, Laufwerke,
  Bildschirmtastatur.
  **[Handbuch des Emulators](app/help/handbuch.md)** (auch im Programm unter *Hilfe*)
- **k1520DiskTool** — Dateien zwischen PC und Diskettenabbild austauschen, ohne dass der
  Emulator läuft: CP/A, SCPX, UDOS, NDOS, SCP1700, WEGA in `.img`, `.hfe` und `.dmk`; mit
  Diskeditor, Dateisystemprüfung und Rettung gelöschter Dateien.
  **[Handbuch des DiskTools](app/disktool/help/handbuch.md)** ·
  [Kommandozeile](tools/k1520disktool.md)
- **Debugger `k1520dbg`** — hält die ganze Maschine an, nicht nur ein Programm; auch für
  eigene Z80-Programme unter CP/A, SCPX oder UDOS.
  **[Handbuch des Debuggers](doc/handbuch_k1520dbg.md)** ·
  [Kommandoreferenz](tools/k1520dbg.md) ·
  [Einstieg mit Beispielen](tools/how_to_debug_and_trace.md)

Welche Diskette was enthält: [disks/README.md](disks/README.md).

## Aus dem Quelltext bauen

Wer den Emulator nur benutzen will, nimmt die
**[fertigen Pakete](https://github.com/olliy78/k1520emu/releases/latest)**. Für Entwickler:

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
