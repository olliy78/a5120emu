# A5120-Emulator

Emulator des Bürocomputers **robotron A5120** samt K1520-Bus und seinen
Steckkarten (ZRE/K2526, OPS/K3526, ABS/K7024, ASS/K8025, AFS/K5122).  Er bootet
die Originalsysteme CP/A, SCPX 1526 und UDOS von echten Diskettenabbildern.

## Installieren

```sh
./install.sh
```

Das legt den Emulator unter `~/.local/opt/a5120emu` an, richtet dort eine
**eigene** Python-Laufzeitumgebung mit Qt ein und trägt ihn ins Startmenü ein.

* **Keine Administratorrechte nötig** — es wird ausschließlich ins
  Benutzerverzeichnis geschrieben, das System bleibt unberührt.
* **Internet nur bei der Installation**: einmalig werden ~120 MB geladen
  (Python und Qt sind bewusst nicht im Paket, das hielte es klein).  Danach
  läuft der Emulator ohne Netz.
* Platzbedarf nach der Installation: ~400 MB.

Weitere Möglichkeiten:

```sh
./install.sh --prefix ~/programme/a5120emu   # anderes Ziel
./install.sh --no-shortcut                   # ohne Startmenü-Eintrag
./install.sh --help                          # alle Optionen
```

## Starten

Über das Startmenü („A5120 Emulator") oder auf der Kommandozeile:

```sh
a5120emu
```

## Wo liegt was

| | |
|---|---|
| Programm | `~/.local/opt/a5120emu` (bzw. `--prefix`) |
| Arbeitsdisketten | `~/.local/share/a5120emu/disks` |
| Konfiguration | `~/.config/k1520emu/config.yaml` |

Die mitgelieferten Beispieldisketten werden bei der Installation in das
Diskettenverzeichnis kopiert.  Der Emulator schreibt Änderungen an einer
eingelegten Diskette dorthin zurück — die Originale im Programmverzeichnis
bleiben unangetastet und ein Update überschreibt nichts davon.

## Aktualisieren

Neues Paket entpacken und `./install.sh` erneut aufrufen.  Die
Laufzeitumgebung bleibt stehen, es werden nur die Programmdateien ersetzt;
Disketten und Konfiguration bleiben unberührt.

## Entfernen

```sh
./install.sh --uninstall            # Programm
./install.sh --uninstall --purge    # samt Disketten und Konfiguration
```

## Wenn etwas nicht startet

```sh
~/.local/opt/a5120emu/venv/bin/python3 ~/.local/opt/a5120emu/app/main.py --paths
```

zeigt, welche Pfade der Emulator auflöst (Bibliothek, Formatkatalog,
Disketten).  Jede dieser Auflösungen lässt sich mit einer Umgebungsvariablen
überschreiben: `K1520_HOME`, `K1520_LIB`, `K1520_FORMATS`, `K1520_DISKS`.
`K1520_DEBUG=1` macht das Laden der Kernbibliothek gesprächig.
