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
packaging/build_payload.sh                # → dist/a5120emu-<version>-linux-x86_64.tar.gz
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
tar xzf dist/a5120emu-*.tar.gz -C /tmp
/tmp/a5120emu-*/install.sh --prefix /tmp/a5120emu-test
/tmp/a5120emu-test/bin/a5120emu --paths
```

Der Installer prüft sich am Ende selbst (Kernbibliothek laden, `k1520_version`,
PySide6, Formatkatalog) und bricht ab, statt einen unbrauchbaren
Startmenü-Eintrag zu hinterlassen.

Automatisiert läuft dasselbe als Test:

```sh
venv/bin/python3 -m pytest tests/python/test_packaging.py -v
```

## Dateien

| Datei | Rolle |
|-------|-------|
| `build_payload.sh` | baut den Release-Kern und schnürt das Archiv |
| `install.sh` | Bootstrap-Installer (liegt im Paket, läuft beim Anwender) |
| `launcher.sh` | Startskript-Vorlage; `@ROOT@` wird beim Installieren ersetzt |
| `lib/common.sh` | gemeinsame Bausteine: Meldungen, Plattform, Download, `ensure_uv` |
| `uv_pins.txt` | gepinnte uv-Fassung + Prüfsummen (`build_payload.sh --refresh-uv`) |
| `a5120emu.desktop.in` | Startmenü-Eintrag |
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
