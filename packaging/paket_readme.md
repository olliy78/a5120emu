# K1520-Emulator

Emulator für Rechner am **K1520-Bus**.  Enthalten ist der Bürocomputer
**robotron A5120** mit seinen Steckkarten (ZRE/K2526, OPS/K3526, ABS/K7024,
ASS/K8025, AFS/K5122); er bootet die Originalsysteme CP/A, SCPX 1526 und UDOS
von echten Diskettenabbildern.  Weitere Maschinen der Familie bekommen später
ein eigenes Programm in derselben Installation.

## Installieren

```sh
./install.sh
```

> **Windows:** dieses Archiv ist nur der Rohstoff des Installationsprogramms.
> Zum Installieren nehmen Sie `K1520emu-<Fassung>-win-x64-setup.exe` vom
> Release — der Assistent fragt nach Ziel und Diskettenordner und richtet alles
> selbst ein.

Der Installer **fragt, wohin** (Vorschlag: `~/K1520emu`), richtet dort eine
**eigene** Python-Laufzeitumgebung mit Qt ein und trägt den Emulator ins
Startmenü ein.  Es muss ein **eigener, neuer Ordner** sein: das Entfernen löscht
ihn später vollständig, und deshalb lehnt der Installer ein bereits belegtes
Verzeichnis (und erst recht das Heimatverzeichnis) ab.

* **Keine Administratorrechte nötig** — es wird ausschließlich ins
  Benutzerverzeichnis geschrieben, das System bleibt unberührt.
* **Internet nur bei der Installation**: einmalig werden ~120 MB geladen
  (Python und Qt sind bewusst nicht im Paket, das hielte es klein).  Danach
  läuft der Emulator ohne Netz.
* Platzbedarf nach der Installation: **~160 MB**.  Der Installer wirft dafür
  alles weg, was der Emulator nie lädt (QML/Quick, Qt-Entwicklungswerkzeuge,
  CPythons Testsuite …) und prüft anschließend nach, dass die Oberfläche noch
  aufbaut.  `--no-slim` behält alles (~400 MB).

Weitere Möglichkeiten:

```sh
./install.sh --prefix ~/programme/K1520emu   # ohne Rückfrage dorthin
./install.sh -y                              # ohne Rückfrage in den Vorschlag
./install.sh --no-shortcut                   # ohne Startmenü-Eintrag
./install.sh --help                          # alle Optionen
```

## Starten

Über das Startmenü („A5120 Emulator") oder auf der Kommandozeile:

```sh
a5120emu
```

Das Paket enthält ein zweites Programm, das **k1520DiskTool**: es tauscht Dateien
zwischen Ihrem Rechner und K1520-Disketten aus (CP/A, SCPX und UDOS; `.hfe`,
`.dmk`, `.img`). Startmenü-Eintrag „k1520DiskTool" oder:

```sh
k1520disktool                 # Oberfläche
k1520disktool-cli ls disk.hfe # Kommandozeile
```

Beim Öffnen ist eine Diskette **schreibgeschützt** — Ändern verlangt, den Haken
„Nur lesen" zu entfernen. So kann beim bloßen Ansehen nichts kaputtgehen.

### Echte Disketten am Greaseweazle

Beide Programme können statt einer Datei ein **echtes Laufwerk** benutzen, wenn ein
[Greaseweazle](https://github.com/keirf/greaseweazle)-Adapter angeschlossen ist:
im Emulator der Knopf „Physisch…" am Laufwerkskasten, im Diskettenwerkzeug
*Datei ▸ Physisches Laufwerk…* (oder `k1520disktool-cli --physical ls`). Gelesen und
geschrieben wird **spurweise nach Bedarf** — der Umweg über ein Abbild entfällt.

Die dafür nötigen Bausteine richtet der Installer mit ein; ohne Adapter stören sie
nicht, der Menüpunkt bleibt dann gesperrt und nennt den Grund. Vor dem ersten
Schreibversuch die Diskette sichern.

### Der Debugger `k1520dbg`

Das dritte Programm im Paket, und das einzige ohne Oberfläche: es untersucht fremde
Programme Schritt für Schritt — Haltepunkte, Register, Speicher, Rückwärtslaufen,
Disassemblat mit dem eigenen Quelltext daneben.

```sh
k1520dbg meine_diskette.hfe        # Sitzung öffnen, `help` zeigt die Befehle
```

| | |
|---|---|
| Handbuch | `<Installation>/share/doc/handbuch_k1520dbg.md` |
| Dazu | `<Installation>/share/tools/z80_disasm2.py` — statisches Vollisting einer `.COM` |
| Lizenzen der Fremdsoftware | `<Installation>/share/doc/lizenzen/` |

Einen Startmenü-Eintrag hat er bewusst nicht — er wird in einen vorhandenen
Arbeitsablauf aus Editor, Assembler und Konsole eingebunden. Unter **Windows** öffnet
der Eintrag *„K1520-Werkzeuge (Eingabeaufforderung)"* eine Konsole, in der `k1520dbg`
und `k1520disktool-cli` ohne Pfadangabe laufen; die Datei `bin\k1520dbg.cmd` dahinter
ist ausdrücklich **zum Kopieren und Anpassen** gedacht (Arbeitsordner und eigener
Assembler stehen als Beispielzeilen darin).

## Wo liegt was

| | |
|---|---|
| Programm | wohin bei der Installation gewählt (Vorschlag `~/K1520emu`) |
| Arbeitsdisketten | `~/Dokumente/K1520emu/Disketten` |
| Konfiguration | `~/.config/k1520emu/config.yaml` |

Die Beispieldisketten werden beim ersten Start in den Diskettenordner
ausgepackt.  Der Emulator schreibt Änderungen an einer eingelegten Diskette
dorthin zurück — die Originale im Programmverzeichnis bleiben unangetastet und
ein Update überschreibt nichts davon.

Der Dokumentenordner heißt auf jedem System anders (`Dokumente`, `Documents`,
…); maßgeblich ist der, den Ihr Dateimanager anzeigt.  Wo er gelandet ist, sagt
`a5120emu --paths`.

## Aktualisieren

Neues Paket entpacken und `./install.sh` erneut aufrufen.  Der Installer
**findet die vorhandene Installation von selbst** und schlägt sie vor — Enter
genügt.  Die Programmdateien werden ersetzt und die Laufzeitumgebung neu
eingerichtet (Qt wird dabei erneut geladen); **Disketten und Konfiguration
bleiben unberührt**, sie liegen außerhalb der Installation.

## Entfernen

```sh
./install.sh --uninstall            # Programm
./install.sh --uninstall --purge    # samt Disketten und Konfiguration
```

Entfernt wird **nur, was der Installer angelegt hat**.  Haben Sie eigene
Dateien in den Ordner gelegt, bleiben sie dort — dann bleibt auch der Ordner
stehen, und der Installer sagt Ihnen, was noch darin liegt.

## Wenn etwas nicht startet

```sh
<installationsverzeichnis>/bin/a5120emu --paths
```

zeigt, welche Pfade der Emulator auflöst (Bibliothek, Formatkatalog,
Disketten).  Jede dieser Auflösungen lässt sich mit einer Umgebungsvariablen
überschreiben: `K1520_HOME`, `K1520_LIB`, `K1520_FORMATS`, `K1520_DISKS`.
`K1520_DEBUG=1` macht das Laden der Kernbibliothek gesprächig.
