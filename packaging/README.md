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

## Dateien

| Datei | Rolle |
|-------|-------|
| `build_payload.sh` | baut den Release-Kern und schnürt das Archiv |
| `install.sh` | Bootstrap-Installer (liegt im Paket, läuft beim Anwender) |
| `launcher.sh` | Startskript-Vorlage; `@ROOT@` wird beim Installieren ersetzt |
| `slim.py` | wirft nach dem Installieren heraus, was nie geladen wird (~400 → ~146 MB) |
| `lib/common.sh` | gemeinsame Bausteine: Meldungen, Plattform, Download, `ensure_uv` |
| `uv_pins.txt` | gepinnte uv-Fassung + Prüfsummen (`build_payload.sh --refresh-uv`) |
| `a5120emu.desktop.in` | Startmenü-Eintrag der Maschine A5120 |
| `icon.svg` | Symbol |
| `paket_readme.md` | wird als `README.md` **ins Paket** gelegt (Anwendertext) |

## uv-Pins auffrischen

```sh
packaging/build_payload.sh --refresh-uv
```

Holt die neueste uv-Fassung und schreibt Version samt Prüfsummen in
`uv_pins.txt`.  Die Prüfsummen reisen mit dem Paket — der Installer vergleicht
das Heruntergeladene gegen diese Werte und führt es sonst nicht aus.

## Noch nicht hier

Windows (Inno Setup, per-user) und macOS folgen als Schritte 3 und 4 des
Entwurfs (§10).  Die plattformübergreifenden Teile — `app/paths.py`, die
Modulpfad-Auflösung im Kern, `uv_pins.txt` mit den Windows-/macOS-Tripeln —
sind bereits darauf ausgelegt.
