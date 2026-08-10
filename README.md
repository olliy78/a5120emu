# Robotron A5120 — K1520-Emulator

Hardwarenahe Emulation des **Robotron A5120**, eines Z80-Bürocomputers des VEB Robotron
Dresden (DDR, 1982). Nachgebildet wird nicht das Betriebssystem, sondern der **K1520-Bus
mit seinen Steckkarten** — es gibt keine BIOS-Traps: Boot-ROM, BIOS und Betriebssystem
laufen als echter Z80-Code.

Deshalb bootet die Maschine mehrere Original-Betriebssysteme von Original-Disketten:
**CP/A** (CP/M-2.2-Derivat der Akademie der Wissenschaften), **SCPX** und **UDOS** —
inklusive Formatieren mit dem originalen `FORMAT.COM`.

```
  app/          Python/PySide6-Oberfläche (CRT-Darstellung, Laufwerke, Tastatur)
    ↓ ctypes
  libk1520core.so   stabile C-ABI  (core/api/k1520_api.h)
    ↓
  machines/a5120    verdrahtet die Karten auf den Bus, treibt die Laufschleife
  cards/            K2526 (ZRE/CPU)  K3526 (RAM)  K7024 (Bildschirm)
                    K8025 (V.24)     K5122 (Floppy)
  primitives/       Z80, Z80-PIO, Z80-CTC, Z80-SIO, EPROM/RAM
  bus/              K1520Bus (Speicher/E-A-Dispatch, INT-Daisy-Chain) + Koppelbus
```

Besonderheiten, die den Kern von einem üblichen CP/M-Emulator unterscheiden:

- **Zwei Z80 auf der K2526** — ZVE1 als Hauptprozessor hinter der Q240-Schutzlogik,
  ZVE2 als DMA-Prozessor für das Laden der Bootsektoren; beide koordinieren sich nur
  über gemeinsames RAM.
- **Formatagnostischer Floppy-Controller** — der K5122 liest einen rotierenden Flussstrom
  (`TrackImage`/`BitCodec`), FM oder MFM, mit echten Sync-Marken und IBM-CCITT-CRC. Die
  Diskette liegt vollständig im Speicher; `.img`, HFE v1 und DMK sind reine Container.
- **Formatieren funktioniert wirklich** — eine unformatierte Leerdiskette lässt sich unter
  dem Gast mit `FORMAT.COM` formatieren und anschließend bootfähig machen.

## Einrichten und starten

Vollständige Anleitung: **[SETUP.md](SETUP.md)**. Kurz:

```sh
python3 -m venv venv && source venv/bin/activate
python3 -m pip install -r requirements.txt -r requirements-dev.txt
tools/dev.sh build          # baut build/ (Release, LOG_LEVEL=3)
bash run_gui.sh             # setzt LD_LIBRARY_PATH und startet app/main.py
```

Voraussetzungen: C++17-Compiler, CMake ≥ 3.16, Python ≥ 3.8, Linux.
Die Oberfläche ist in **[APP_README.md](APP_README.md)** beschrieben.

### Für Anwender: verteilbares Paket

Wer den Emulator nur benutzen will, braucht weder Quellbaum noch Compiler.
`packaging/build_payload.sh` schnürt ein ~2-MB-Archiv; dessen `install.sh` holt sich
Python und Qt mit [`uv`](https://github.com/astral-sh/uv) in eine eigene Laufzeitumgebung
**innerhalb der Installation** — benutzerlokal, ohne Administratorrechte, ohne Rückwirkung
auf das System.

```sh
packaging/build_payload.sh                  # → dist/k1520emu-<version>-linux-x86_64.tar.gz
tar xzf dist/k1520emu-*.tar.gz -C /tmp
/tmp/k1520emu-*/install.sh                  # fragt nach dem Ziel, Vorschlag ~/K1520emu
```

Einmalig ~120 MB Download, danach läuft der Emulator ohne Netz; belegt werden ~146 MB.
Arbeitsdisketten landen im Dokumentenordner (`<Dokumente>/K1520emu/Disketten`), also
getrennt vom Programm — der Emulator schreibt Änderungen an einer eingelegten Diskette
dorthin zurück. Bedienung: **[packaging/README.md](packaging/README.md)**, Entwurf und
Begründungen: **[doc/design/13_distribution.md](doc/design/13_distribution.md)**. Linux und
macOS sind aufgebaut, Windows steht aus.

## Bauen und testen

**Immer über `tools/dev.sh`** — es baut das passende Verzeichnis vorher neu. Es gibt zwei
Build-Verzeichnisse mit denselben Werkzeugnamen (`build/` mit LOG_LEVEL=3, `build_trace/`
mit LOG_LEVEL=5); ein Werkzeug direkt aus einem davon zu starten testet leicht veraltete
Objektdateien.

```sh
tools/dev.sh test            # Regressionsrunde, ~12 s
tools/dev.sh test-all        # zusätzlich die langsamen Format-/Boot-Disk-Läufe
tools/dev.sh tool k1520dbg   # ein Werkzeug starten (k1520dbg, boot_trace, floppy_diag …)
```

Geprüft wird zuerst lokal: `.githooks/pre-push` fährt die Regressionsrunde vor jedem Push.
Einmal je Arbeitskopie aktivieren mit `git config core.hooksPath .githooks`.

Dieselbe Runde gibt es als Gegenprobe auf sauberem System in GitHub Actions — **von Hand
angestoßen**, nichts läuft bei einem Push von selbst:

```sh
gh workflow run ci.yml --ref main          # Bauen + Regression
gh workflow run release.yml --ref main     # verteilbares Paket schnüren
```

Was dort läuft, was in GitHub eingestellt sein muss und wie man einen Lauf startet:
`doc/ci_pipeline.md`.

## Wo was steht

| Thema | Datei |
|---|---|
| Architektur des Kerns (maßgeblich) | `doc/K1520_architecture.md` |
| Einzelentwürfe je Baugruppe | `doc/design/*.md` |
| Testsystem: ausführen, erweitern | `tests/README.md` |
| Fehlersuche im Boot-Pfad | `tools/how_to_debug_and_trace.md` |
| Diskettenformate und Formatier-Pipeline | `doc/format.md` |
| Verteilbares Paket, Installer | `packaging/README.md`, `doc/design/13_distribution.md` |
| Pipeline auf GitHub: Bedienung, Einstellungen | `doc/ci_pipeline.md` |
| Offene Punkte | `doc/open_points.md` |

Kommentare und Dokumentation sind überwiegend deutsch.

## Verzeichnisse

```
core/        C++-Kern → libk1520core.so
app/         PySide6-Oberfläche
tools/       Debugger (k1520dbg), Tracer (boot_trace), Disassembler, Hilfsskripte
packaging/   verteilbares Paket: Payload schnüren, Installer, Schlankmachen
tests/       Testebenen unit/ debugtools/ integration/ cli/ system/ python/
doc/         Architektur, Entwürfe, Analysen, EPROM-Abzüge
data/        formats.yaml — Katalog der Diskettengeometrien
disks/       Disketten für Werkzeuge und Handversuche
boot_disk/   Original-CP/A-Programme (@OS.COM, FORMAT.COM, M80 …)
cpa_src/     Original-CP/A-BIOS-Quellen (.mac)
cparun/      eigenständiges Unterprojekt: CP/M-Programme direkt auf dem Host ausführen
```

## Lizenz

MIT — siehe [LICENSE](LICENSE).
