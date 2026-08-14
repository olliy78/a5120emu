# Paketierung

Werkzeuge, die aus dem Arbeitsbaum ein **verteilbares Paket** machen.  Was das
Paket enthält, was es nachlädt und warum es so aufgeteilt ist, steht im Entwurf
**`doc/design/13_distribution.md`** — hier steht nur, wie man es bedient.

Kurzform: das Paket enthält die kompilierte Kernbibliothek, die GUI und die
Daten (~5–10 MB).  **Python und Qt sind nicht enthalten**; der Installer holt
sie mit [`uv`](https://github.com/astral-sh/uv) in eine Laufzeitumgebung
*innerhalb der Installation* — benutzerlokal, ohne Administratorrechte.

## Paket bauen

```sh
packaging/build_payload.sh                # → dist/k1520emu-<version>-linux-x86_64.tar.gz
packaging/build_payload.sh --disks all    # alle Disketten aus disks/ statt der Auswahl
packaging/build_payload.sh --no-archive   # nur den Baum, kein Archiv
```

Der Bau geht in ein **eigenes** Verzeichnis (`build_dist/`), damit `build/` und
`build_trace/` unberührt bleiben.  Zwei Einstellungen sind dabei nicht
verhandelbar:

* `-DK1520_FORMATS_DEFAULT=` — leert den einkompilierten Fallback-Pfad des
  Formatkatalogs.  Bliebe er gesetzt, trüge jede ausgelieferte Bibliothek den
  absoluten Pfad des Baurechners als Suchkandidaten mit sich herum.
* `-static-libstdc++ -static-libgcc` — die Bibliothek soll auch dort laden, wo
  eine ältere libstdc++ steht.  Die glibc-Untergrenze kommt dagegen vom
  Baurechner: für Veröffentlichungen auf einer alten Baseline bauen
  (manylinux_2_28 o. ä.), nicht auf der Entwicklungsmaschine.

## Probeinstallation

```sh
tar xzf dist/k1520emu-*.tar.gz -C /tmp
/tmp/k1520emu-*/install.sh --prefix /tmp/k1520emu-test   # ohne --prefix wird gefragt
/tmp/k1520emu-*/install.sh --data ~/Disketten            # ohne --data wird gefragt
#   Der Datenordner ist getrennt vom Installationsziel: dort schreibt der Autosave
#   hinein, er soll ein Update überleben und in der Datensicherung auftauchen.
#   Vorgabe <Dokumente>/K1520emu — nur eine ABWEICHENDE Wahl landet als K1520_DATA
#   im Starter, sonst bliebe ein fester Pfad stehen (siehe app/paths.py).
/tmp/k1520emu-test/bin/a5120emu --paths
```

Der Installer prüft sich am Ende selbst — Kernbibliothek laden,
`k1520_version`, PySide6, Formatkatalog **und das Hauptfenster wirklich bauen**
— und bricht ab, statt einen unbrauchbaren Startmenü-Eintrag zu hinterlassen.
Der Fensterbau ist zugleich die Gegenprobe zum Schlankmachen: fehlte ein
Qt-Plugin, käme er nicht durch.

Automatisiert läuft dasselbe als Test:

```sh
venv/bin/python3 -m pytest tests/python/test_packaging.py -v
```

**Beim Ausprobieren wissenswert.** Ein Update ist derselbe Aufruf; der
Installer findet die vorhandene Installation über den Starter in `~/.local/bin`
und schlägt sie vor, die Laufzeitumgebung wird dabei neu aufgebaut (§3.2 des
Entwurfs).  Das Ziel ist gegen Unfälle gesichert: `$HOME`, `/` und jedes nicht
leere fremde Verzeichnis werden abgelehnt; `--uninstall` fasst nur an, was sich
als Installation ausweist, und entfernt daraus **nur das Inventar aus dem
Ausweis** — fremde Dateien im Ordner überleben.

## Windows: Setup bauen und ausprobieren

```sh
packaging/build_payload.sh --disks none --setup     # in der Git-Bash, braucht iscc im PATH
```

Erzeugt zusätzlich `dist/K1520emu-<version>-win-x64-setup.exe`.  **Der Assistent
installiert selbst** — es gibt kein `install.ps1` mehr: er lädt Python
(python-build-standalone, Prüfsumme aus `python_pins.txt`), packt es aus, legt
die Laufzeitumgebung an, holt Qt mit `pip --require-hashes`, schlankt, schreibt
die Starter und fährt denselben Rauchtest wie unter Linux.  Jeder Schritt steht
mit Klartext in der Statuszeile und in `<Installation>\bootstrap.log`.

Alles Nachladbare läuft in `PrepareToInstall`, also **vor der ersten kopierten
Datei**: scheitert es, gibt es keine halbe Installation und keinen
Startmenü-Eintrag, der ins Leere zeigt.  Deinstalliert wird über
*Einstellungen → Apps*; Inno entfernt nur, was es angelegt hat, das Nachgeladene
steht namentlich im Abschnitt `[UninstallDelete]`.

Still (wie in der CI):

```powershell
.\K1520emu-1.2.3-win-x64-setup.exe /VERYSILENT /DIR=C:\Temp\k1520 /Daten=C:\Temp\disketten /LOG=setup.log
```

Der ganze Lauf steckt im Job `paket` von `.github/workflows/windows-ci.yml`
(`gh workflow run windows-ci.yml --ref main -f paket=true`) — installieren,
Schlankmachen prüfen, deinstallieren, und dabei nachsehen, dass eine fremde
Datei im Zielordner überlebt.

## Dateien

| Datei | Rolle |
|-------|-------|
| `build_payload.sh` | baut den Release-Kern und schnürt das Archiv |
| `install.sh` | Bootstrap-Installer Linux/macOS (liegt im Paket, läuft beim Anwender) |
| `k1520emu.iss` | Windows-Installationsprogramm (Inno Setup ≥ 6.5). Es **installiert selbst** — laden, auspacken, Laufzeitumgebung, Schlankmachen, Starter, Rauchtest, Deinstallieren; kein PowerShell beteiligt (Guards in `tests/python/test_packaging.py`) |
| `python_pins.txt` | gepinnter Python für das Windows-Setup: Fassung, Größe, SHA256 (`build_payload.sh --refresh-python`) |
| `launcher.cmd`, `disktool_launcher.cmd` | Windows-Starter; die Startmenü-Verknüpfung zeigt dagegen direkt auf `pythonw.exe`, sonst öffnet sich ein Konsolenfenster |
| `launcher.sh` | Startskript-Vorlage; `@ROOT@` wird beim Installieren ersetzt |
| `slim.py` | wirft nach dem Installieren heraus, was nie geladen wird (~400 → ~146 MB) |
| `lib/common.sh` | gemeinsame Bausteine: Meldungen, Plattform, Download, `ensure_uv` |
| `uv_pins.txt` | gepinnte uv-Fassung + Prüfsummen für die **Unix**-Installer (`build_payload.sh --refresh-uv`) |
| `a5120emu.desktop.in` | Startmenü-Eintrag der Maschine A5120 |
| `icon.svg` | Symbol |
| `paket_readme.md` | wird als `README.md` **ins Paket** gelegt (Anwendertext) |

## Pins auffrischen

```sh
packaging/build_payload.sh --refresh-uv       # uv (Unix-Installer)
packaging/build_payload.sh --refresh-python   # Python (Windows-Setup)
```

Beide holen die neueste Fassung und schreiben sie samt Prüfsummen in
`uv_pins.txt` bzw. `python_pins.txt`.  Die Prüfsummen reisen mit dem Paket — der
Installer vergleicht das Heruntergeladene gegen diese Werte und benutzt es
sonst nicht.

**Warum zwei Bezugswege.** Unter Linux holt `uv` den Python; unter Windows lädt
das Setup ihn direkt von python-build-standalone.  Das ist keine Doppelung aus
Bequemlichkeit: `uv python install` legt zum Schluss einen Junction auf die
Nebenversion an, und wo OneDrive „Dateien bei Bedarf" läuft, verweigert dessen
Filtertreiber das — `os error 448`, und die Installation bricht ab
(astral-sh/uv #19616).  Abschalten lässt sich der Junction nicht.  Kopf von
`python_pins.txt`.

## Noch nicht hier

macOS folgt als Schritt 5 des Entwurfs (§10).  Die plattformübergreifenden
Teile — `app/paths.py`, die Modulpfad-Auflösung im Kern, `uv_pins.txt` mit den
macOS-Tripeln — sind bereits darauf ausgelegt.
