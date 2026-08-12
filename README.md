# K1520 Emulator für Robotron A5120

Hardwarenahe Emulation des **Robotron A5120**, eines Z80-Bürocomputers des VEB Robotron
Dresden (DDR, 1982). Nachgebildet wird nicht das Betriebssystem, sondern der **K1520-Bus
mit seinen Steckkarten**: Boot-ROM, BIOS und Betriebssystem laufen als echter Z80-Code auf
nachgebildeter Hardware.

Deshalb bootet die Maschine mehrere Original-Betriebssysteme von Original-Disketten:
**CP/A** (CP/M-2.2-Derivat der Akademie der Wissenschaften), **SCPX** und **UDOS** —
inklusive Formatieren mit dem originalen `FORMAT.COM`.

> ### ⬇ Fertige Pakete für Windows und Linux
> **[Zur Downloadseite (aktuelles Release)](https://github.com/olliy78/a5120emu/releases/latest)**
>
> Installation benutzerlokal, **ohne Administratorrechte** — für Windows ein
> Installationsprogramm, für Linux ein Archiv mit `install.sh`. Weder Compiler noch
> Quelltext nötig; Python und Qt holt der Installer selbst.

Mitgeliefert sind drei Programme: der **Emulator**, das **k1520DiskTool** für den
Dateiaustausch mit Disketten und der **Debugger `k1520dbg`** — Letzterer nicht nur für die
Arbeit am Emulator, sondern auch, um eigene Z80-Programme unter CP/A, SCPX oder UDOS zu
untersuchen.

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

## k1520DiskTool — Dateien zwischen PC und Diskette

Ein eigenständiges zweites Programm, mit **Oberfläche** (`bash run_disktool.sh`) und
Kommandozeile. Es holt Dateien von einer Diskette herunter und schreibt sie zurück,
ohne dass dafür der Emulator laufen muss — praktisch, um Quelltexte am PC zu bearbeiten
und dann unter CP/A zu übersetzen, oder um eine Diskette zu sichern.

- Liest und schreibt die Dateisysteme von **CP/A, SCPX und UDOS** in den Abbildformaten
  `.img`, `.hfe` und `.dmk`.
- **Auflisten, herausholen, einfügen, löschen**, leere Disketten anlegen, Belegung und
  Prüfbericht anzeigen. Text- und Binärmodus (Zeilenenden, `0x1A` als Dateiende).
- **Erkennt Geometrie und Dateisystem selbst**; passt kein bekanntes Profil, vermisst es
  die fremde Diskette und sagt, was es gefunden hat.
- **Beidseitige UDOS-Disketten** führt es als eine Diskette mit zwei Seiten — bei UDOS ist
  jede Seite technisch ein eigenes Dateisystem.
- Beim Öffnen ist eine Diskette **schreibgeschützt**; Ändern verlangt eine ausdrückliche
  Freigabe, und vor dem Schreiben entsteht eine Sicherungskopie. Stapeloperationen sind
  Transaktionen: erst planen und urteilen, dann schreiben.

```sh
bash run_disktool.sh                                   # Oberfläche
tools/dev.sh tool k1520disktool ls   disks/cpa780.hfe  # Verzeichnis
tools/dev.sh tool k1520disktool get  disks/cpa780.hfe '*.MAC' --to ./quellen
tools/dev.sh tool k1520disktool put  disks/cpa780.hfe  hello.com
```

Bedienung: **[tools/k1520disktool.md](tools/k1520disktool.md)** · Feinentwurf:
**[doc/design/13_k1520disktool.md](doc/design/13_k1520disktool.md)**

## Debugger `k1520dbg` — auch für eigene Z80-Programme

Ein interaktiver Debugger im Stil von gdb, der die **ganze Maschine** anhält, nicht nur
ein Programm. Er ist beim Bau des Emulators entstanden — für den Boot-Pfad und das
Zusammenspiel der beiden Prozessoren —, taugt aber genauso, um **eigene Anwendungen unter
CP/A, SCPX oder UDOS** zu untersuchen: das Gastsystem merkt nichts davon, und es braucht
weder einen Monitor im Gast noch freien Speicher dort.

- **Haltepunkte** auf Adressen, mit Bedingung, Zählwerk und einmalig; dazu Haltepunkte auf
  **Ereignisse** — angenommener Interrupt, NMI, `RETI`, Diskettenzugriff.
- **Schritt hinein/über/heraus**, Rückwärtsschritt (`rs`), Rückwärtslauf (`rc`),
  benannte Momentaufnahmen und ein Zustandsspeicher: einmal booten, oft weitermachen.
- **Speicher und E/A beobachten**, Bildschirminhalt durchsuchen, Tastenfolgen einspielen,
  auf einen Text im Bildschirm warten — damit lassen sich auch bildschirmgesteuerte
  Programme automatisiert durchfahren.
- **Quelltext statt Hexdump:** ein kommentiertes `.prn`-Listing wird an die Adressen
  angeheftet. Für Fremdsysteme, zu denen es kein Listing gibt, assembliert der Debugger
  **`.MAC`/`.ASM`-Quelltext selbst** und findet den Ladeversatz auf Wunsch allein
  (`@auto`) — er sagt dann auch, ob die Quelle zum laufenden Programm passt.
- **Chip-Zustand im Klartext** (PIO, CTC, SIO), die IM-2-Vektortabelle, Interrupt-Protokoll.

```sh
tools/dev.sh tool k1520dbg meine_diskette.hfe          # startet und hält an
#  b 0x0100          Haltepunkt am CP/M-Programmstart
#  lst mein.mac@auto Quelltext anheften, Ladeversatz selbst bestimmen
#  g / s / rs        laufen / Schritt / Schritt zurück
```

Die Diskette wird dabei **kopiert und die Kopie eingelegt** — ein Original kann nicht
versehentlich beschädigt werden (`--rw`, wenn Änderungen bleiben sollen).

Einstieg mit durchgerechneten Aufgaben: **[tools/how_to_debug_and_trace.md](tools/how_to_debug_and_trace.md)** ·
vollständige Kommandoliste: **[tools/k1520dbg.md](tools/k1520dbg.md)** ·
nicht-interaktiver Tracer: **[tools/boot_trace.md](tools/boot_trace.md)**

## Einrichten und starten

Vollständige Anleitung: **[SETUP.md](SETUP.md)**. Kurz:

```sh
python3 -m venv venv && source venv/bin/activate
python3 -m pip install -r requirements.txt -r requirements-dev.txt
tools/dev.sh build          # baut build/ (Release, LOG_LEVEL=3)
bash run_gui.sh             # setzt LD_LIBRARY_PATH und startet app/main.py
```

Voraussetzungen: C++17-Compiler, CMake ≥ 3.16, Python ≥ 3.8. Gebaut und geprüft wird
auf **Linux (GCC)** und **Windows (MSVC)**; `tools/dev.sh win` baut zusätzlich per
MinGW-w64 nach Windows und fährt die Tests unter `wine`.
Die Oberfläche ist in **[APP_README.md](APP_README.md)** beschrieben.

### Verteilbares Paket selbst schnüren

Wer den Emulator nur benutzen will, nimmt die **[fertigen Pakete](https://github.com/olliy78/a5120emu/releases/latest)**
— dieser Abschnitt beschreibt, wie sie entstehen. `packaging/build_payload.sh` schnürt ein
~3-MB-Archiv; dessen Installer holt Python und Qt mit
[`uv`](https://github.com/astral-sh/uv) in eine eigene Laufzeitumgebung **innerhalb der
Installation** — benutzerlokal, ohne Administratorrechte, ohne Rückwirkung auf das System.

```sh
packaging/build_payload.sh                  # → dist/k1520emu-<version>-linux-x86_64.tar.gz
tar xzf dist/k1520emu-*.tar.gz -C /tmp
/tmp/k1520emu-*/install.sh                  # fragt nach Ziel und Diskettenordner
```

Unter Windows (in der Git-Bash) erzeugt dasselbe Skript mit `--setup` zusätzlich ein
Installationsprogramm; gebaut wird beides von `.github/workflows/release.yml`, sobald ein
Versions-Tag `v*` gesetzt wird.

Einmalig ~120 MB Download, danach läuft der Emulator ohne Netz; belegt werden ~150 MB
(Linux) bzw. ~123 MB (Windows). Arbeitsdisketten landen im Dokumentenordner
(`<Dokumente>/K1520emu/Disketten`), also getrennt vom Programm — der Emulator schreibt
Änderungen an einer eingelegten Diskette dorthin zurück, und ein Update fasst den Ordner
nie an. Bedienung: **[packaging/README.md](packaging/README.md)**, Entwurf und
Begründungen: **[doc/design/13_distribution.md](doc/design/13_distribution.md)**.

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
| Debugger: Einstieg mit Beispielen | `tools/how_to_debug_and_trace.md` |
| Debugger: Kommandoreferenz | `tools/k1520dbg.md`, `tools/boot_trace.md` |
| k1520DiskTool: Bedienung, Entwurf | `tools/k1520disktool.md`, `doc/design/13_k1520disktool.md` |
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
