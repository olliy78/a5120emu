# `k1520disktool` — Dateiaustausch mit K1520-Disketten

Holt Dateien von CP/A-, SCPX- und **UDOS**-Disketten und schreibt sie zurück —
auf `.img`, `.hfe` und `.dmk`.  Dieselbe Bibliothek treibt die Oberfläche
(`run_disktool.sh`); Feinentwurf: `doc/design/13_k1520disktool.md`.

Aufruf im Projekt immer über `tools/dev.sh tool k1520disktool …` — nie direkt aus
`build/`: es gibt zwei Build-Verzeichnisse mit denselben Werkzeugnamen
(CLAUDE.md „Build & test").

## Kommandos

```
k1520disktool ls     <abbild> [--fs NAME] [--json]   Verzeichnis anzeigen
k1520disktool get    <abbild> [muster…] --to <ordner>  Dateien herausholen
k1520disktool put    <abbild> <datei|ordner…>          Dateien einfügen
k1520disktool rm     <abbild> <muster…>                Dateien löschen
k1520disktool create <abbild> --fs NAME [--label N]    leere Diskette anlegen
k1520disktool info   <abbild>                          Belegung und Erkennung
k1520disktool check  <abbild>                          Prüfbericht
k1520disktool formats                                  bekannte Dateisysteme
```

Schalter: `--fs NAME` (Erkennung übersteuern), `--volume N` (Seite),
`--text`/`--binary`, `--as NAME`, `--force`, `--json`, `--dry-run`,
`--no-backup`.

**Exit-Codes** sind Teil der Schnittstelle:
`0` ok · `1` Fehler · `2` Format/Dateisystem nicht erkannt · `3` passt nicht
(kein Platz) · `4` Ordnerstruktur falsch (fehlendes `SideN/`).

## Beidseitige UDOS-Disketten

Bei UDOS ist **jede Seite ein eigenes Dateisystem** — für den Anwender aber
**eine Diskette**.  Das Werkzeug öffnet beide zusammen und bildet die Trennung im
Dateisystem des Anwenders ab:

```
$ k1520disktool get udos.hfe --to auszug
auszug/
├── Side0/    ← Laufwerk 0
└── Side1/    ← Laufwerk 4

$ k1520disktool put udos.hfe auszug     # verlangt genau diese Unterverzeichnisse
```

Fehlt eines oder liegen lose Dateien daneben, bricht `put` mit Exit 4 ab und
**ändert nichts**.  Einzelne Dateien tragen das Präfix:
`get udos.hfe 'Side1/HELP.*' --to .`

Bei einem Dateisystem (jede CP/M-Diskette, auch beidseitige) ist der Ordner flach.

## Was das Werkzeug zusichert

* **Passt es nicht, wird gar nicht erst geschrieben.**  Vor jeder Stapeloperation
  läuft die Platzprüfung; ein Fehlschlag mittendrin wird zurückgerollt.
* **Sicherungskopie** `<name>~` beim ersten Zurückschreiben (`--no-backup` aus).
* **Kein Raten.**  Passt ein Abbild zu keinem Eintrag in `data/formats.yaml`,
  bricht das Öffnen ab und nennt die **gemessene** Geometrie — die Meldung taugt
  direkt als Vorlage für den fehlenden Katalogeintrag.
* **UDOS auf `.img` wird abgelehnt**: der Sektorkontrollblock steht hinter der
  Daten-CRC, ein rohes Sektorabbild verlöre die gesamte Dateiverkettung.

## Dateisysteme ergänzen

`data/formats.yaml` hat zwei Sektionen: `formats:` (Physik) und `filesystems:`
(logische Ebene — `type`, `data_start`, `block_size`, `dir_entries`).  `data_start`
ist eine **Spur**, kein Byte-Offset; bei gemischter Geometrie (cpa780) wäre er als
Spurzahl nicht ausdrückbar.  Schema: Kopf der Datei und
`doc/design/13_k1520disktool.md` §6.3.

Ein unbekanntes Abbild vermisst `ls` von selbst:

```
$ k1520disktool ls fremd.hfe
Fehler: Das Abbild passt zu keinem Format in data/formats.yaml.
Gemessen:
  Zylinder 0-79, Köpfe 0-1
  c0h0..c0h1 : 26 Sektoren à 128 B, IDs 1-26, mfm
  c1h0..c79h1 :  9 Sektoren à 512 B, IDs 1-9, mfm
```

## Prüfstand

`ctest -R cli_dt_` fährt die Kommandos als Prozess (Ausgabe **und** Exit-Codes);
`ctest -R "CpmFileSystem|UdosFileSystem|DiskVolume"` die Bibliothek darunter;
`ctest -R DiskTool.*Roundtrip` schreibt mit dem Werkzeug und liest mit dem
**echten CP/A bzw. UDOS** im Emulator zurück (Label `format_integration`).
