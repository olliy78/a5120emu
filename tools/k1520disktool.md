# `k1520disktool` — Dateiaustausch mit K1520-Disketten

Holt Dateien von CP/A-, SCPX- und **UDOS**-Disketten und schreibt sie zurück —
auf `.img`, `.hfe` und `.dmk`.  Dieselbe Bibliothek treibt die Oberfläche
(`run_disktool.sh`); Feinentwurf: `doc/design/13_k1520disktool.md`.

Aufruf im Projekt immer über `tools/dev.sh tool k1520disktool …` — nie direkt aus
`build/`: es gibt zwei Build-Verzeichnisse mit denselben Werkzeugnamen
(CLAUDE.md „Build & test").

## Kommandos

```
k1520disktool ls     <abbild> [-l]                     Verzeichnis anzeigen
k1520disktool get    <abbild> [muster…] --to <ordner>  Dateien herausholen
k1520disktool put    <abbild> <datei|ordner…>          Dateien einfügen
k1520disktool rm     <abbild> <muster…>                Dateien löschen
k1520disktool create <abbild> --fs NAME [--label N]    leere Diskette anlegen
       [--boot abbild.bin]                             … bootfähig (Systemspuren)
k1520disktool boot-get <abbild> <datei.bin>            Systemspuren herausschreiben
k1520disktool boot-put <abbild> <datei.bin>            Bootabbild einspielen
k1520disktool info   <abbild>                          Belegung und Erkennung
k1520disktool check  <abbild>                          Prüfbericht
k1520disktool formats                                  bekannte Dateisysteme
```

Schalter: `--fs NAME` (Erkennung übersteuern), `--volume N` (Seite),
`--text`/`--binary`, `--as NAME`, `--force`, `--dry-run`, `--no-backup`.

## Ausgabe weiterverarbeiten

`ls` gibt **ohne `-l` nur die Namen** aus, einen je Zeile — bei mehreren Seiten mit
ihrem Präfix, sodass sie als Argument wieder brauchbar sind.  Kopf- und Fußzeile
gehen dabei nach **stderr**, die Standardausgabe bleibt also reine Nutzlast:

```sh
$ k1520disktool ls udos.hfe | grep '^Side1/'
Side1/HELP.DAT.00
…
$ k1520disktool ls udos.hfe 2>/dev/null | wc -l
69
```

Mit `-l` kommt die Tabelle mit Typ, Größe, Eigenschaften und Datum — für Menschen.

**`--json`** liefert `ls`, `info`, `check` und `formats` maschinenlesbar:

```sh
$ k1520disktool info udos.hfe --json | jq '.volumes[].free'
108800
167680
```

**Exit-Codes** sind Teil der Schnittstelle:
`0` ok · `1` Fehler · `2` Format/Dateisystem nicht erkannt · `3` passt nicht
(kein Platz) · `4` Ordnerstruktur falsch (fehlendes `SideN/`).

## Bootfähige Diskette anlegen

Bootfähig wird eine Diskette nicht durch ihr Dateisystem, sondern durch die
**Systemspuren** davor: das Lade-ROM liest Spur 0 blind ein, lange bevor es
irgendein Dateisystem gibt.  Diese Spuren gehören keiner Datei; das Werkzeug
behandelt sie als **ein Byteband** in einer `.bin`.

```sh
# 1. Bootabbild aus einer vorhandenen Bootdiskette holen
k1520disktool boot-get disks/cpa_cpa780_k5601_noclock.hfe boot_cpa780.bin

# 2. neue Diskette damit anlegen …
k1520disktool create neu.hfe --fs cpa780 --boot boot_cpa780.bin

# 3. … und die Systemdateien hineinkopieren (@OS.COM und der Rest)
k1520disktool get disks/cpa_cpa780_k5601_noclock.hfe --to auszug
k1520disktool put neu.hfe auszug
```

Fertige Abbilder für CP/A und SCPX liegen unter `disks/boot_*.bin` (`disks/README.md`).
Eine vorhandene Diskette lässt sich nachträglich bootfähig machen: `boot-put`.

Wie gross die Systemspuren sind, hängt am Dateisystem — `info` sagt es:

| Dateisystem | Systemspuren | woraus |
|-------------|--------------|--------|
| `cpa780`  | 15104 B | c0h0 + c0h1 + c1h0 (je 26×128) + c1h1 (5×1024) |
| `scpx640` | 16384 B | 4 × 16×256 |
| `scpx798` | 18432 B | 2 × 16×256 + 2 × 5×1024 |
| `udos_*`  | 13312 B | Spuren 0–2 (Urlader + Nukleus) **+ Bootspur 21**, je Seite |
| `cpa800`  | — | keine: das Dateisystem beginnt auf Zylinder 0 |

Ein **kürzeres** Abbild ist erlaubt (der Rest bleibt formatierte Leerspur), ein
**längeres** ist ein Fehler — und zwar bevor irgendetwas angelegt wird:

```
$ k1520disktool create neu.hfe --fs cpa780 --boot zu_gross.bin
Fehler: Das Bootabbild ist 20000 Byte gross, die Systemspuren von 'cpa780'
        fassen aber nur 15104 Byte.
```

### UDOS

Der UDOS-Urlader sucht sein System **über das Verzeichnis** — mit den Systemspuren
allein meldet er `OS NOT FOUND`. Eine vollständige, laufende Diskette entsteht so:

```sh
k1520disktool get    udos_boot_scp.hfe --to auszug     # Dateien + Beiblatt
k1520disktool create neu.hfe --fs udos_ds77 --label UDOS.SYS.4.3 --boot boot_udos43.bin
k1520disktool put    neu.hfe auszug
```

Sie fährt den Selbststart (`OS.INIT`: Banner, `DATE`), meldet sich mit `%` und führt
Befehle aus (`CAT`, `STATUS`, `PRINT`) — Wächter
`DiskToolBootdiskette.GebauteUdosDisketteBootetUndFuehrtBefehleAus`. Die kleinste
bootfähige Diskette ist Systemspuren + `OS` + `ZDOS`.

Das **Beiblatt aus `get` muss dabei sein** (s. u.): ohne die Kopfsektorangaben wird aus
einer Systemdatei eine gewöhnliche Binärdatei, und die Diskette bootet nicht.

## UDOS: was eine Datei ausser ihren Bytes hat

Eine Linux-Datei trägt nur Bytes. Ein UDOS-**Kopfsektor** trägt zusätzlich Angaben, die
das Betriebssystem zum *Laden* braucht:

| Angabe | Schalter | Beispiel `ZDOS` |
|--------|----------|-----------------|
| Angabe | Schalter | `ZDOS` | Kopfsektor |
|--------|----------|--------|-----------|
| Typ (`A`/`P`/`P1`/`B`) | `--type` | `P1` | 12 |
| Eigenschaften (W E L S R F) | `--props` | `WS` | 19 |
| Satzlänge (Vielfaches von 128) | `--record-len` | `1024` | 15 |
| zweite Längenangabe | `--block-len` | `1024` | 17 |
| ENTRY — Einsprungadresse | `--entry` | `0x2600` | 20 |
| SEGMENTS — Anfang:Länge | `--segment 2600:5521` | | 40/42 |
| Kopfsektor 44…47 (Bedeutung offen) | `--extra` | `0` | 44 |
| LOW:HIGH:STACK | `--mem 2600:3FD4:0080` | | 122/124/126 |
| Erstellungsvermerk / Änderungsdatum | `--created` / `--date` | `V 4.2 ` / `900808` | 24 / 32 |

`ls -l` zeigt Typ, Eigenschaften und ENTRY, `ls --json` und `attr` alle Felder. Drei
davon sind keine Formsache:

* die **Satzlänge** bestimmt die Belegung — ein Satz liegt in `Satzlänge/128`
  aufeinanderfolgenden Sektoren EINER Spur;
* die **zweite Längenangabe** ist *nicht* immer deren Kopie (bei 256/512 Byte steht
  dort 0) — mit dem falschen Wert startet ein neu geschriebener Nukleus nicht mehr;
* **LOW/HIGH/STACK** sind das, was der Lader zuteilen lässt (mehr als das Segment).
  Fehlen sie, weist UDOS die Datei mit `MEMORY PROTECT VIOLATION` ab.
  Hintergrund: `doc/udos_diskettenformat.md` §14.

Anzeigen und ändern lassen sie sich auch an einer Datei, die schon auf der Diskette
liegt — der Inhalt bleibt dabei unangetastet:

```sh
$ k1520disktool attr udos.hfe ZDOS
ZDOS                 Typ P1  Eigenschaften WS      5632 Byte
  ENTRY 2600   Satzlaenge 1024   zweite Laenge 1024
  SEGMENT 2600 + 5521 Byte
  LOW 2600  HIGH 3FD4  STACK 0080   Zusatz 00000000
  erstellt 'V 4.2'   geaendert '900808'

$ k1520disktool attr udos.hfe CAT --props WEL --mem 4000:5FFF:0200
```

**Von Hand angeben muss man das selten**: `get` legt neben den Dateien ein Beiblatt
`udos-dateiangaben.txt` an, und `put` liest es wieder — sowohl beim Einfügen eines
ganzen Ordners als auch bei einer einzelnen Datei daraus (auch aus der Oberfläche).
Ausdrückliche Schalter gehen dem Beiblatt vor.

```
# Datei  Typ  Eigenschaften  Start  Satzlaenge  Ladeadresse  Abbildlaenge
Side0/OS   P    WES          13DE   512         1000         5632
Side0/ZDOS P1   WS           2600   1024        2600         5521
```

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
* **Kein Raten — und wo gerechnet wird, steht es dabei.**  Passt kein Eintrag in
  `data/formats.yaml`, wird die Geometrie vermessen und die Diskette **nur lesend**
  geöffnet (s. u.); geht auch das nicht, bricht das Öffnen ab und nennt die
  **gemessene** Geometrie — die Meldung taugt direkt als Vorlage für den fehlenden
  Katalogeintrag.
* **UDOS auf `.img` wird abgelehnt**: der Sektorkontrollblock steht hinter der
  Daten-CRC, ein rohes Sektorabbild verlöre die gesamte Dateiverkettung.
* **Beim Lesen kann nichts kaputtgehen**: `ls`, `get`, `info`, `check` und
  `save-as` öffnen das Abbild **schreibgeschützt** — der Schutz reicht bis in die
  Container-Schicht, die dann selbst beim Schließen nichts schreibt.  Nur `put`
  und `rm` öffnen schreibend; dort ist der Aufruf schon der bewusste Schritt.
  In der Oberfläche entspricht dem der Haken **„Nur lesen"**, der beim Öffnen
  gesetzt ist.

## Wenn kein Profil passt: `cpa_auto`

Für eine CP/A-Diskette **braucht** es keinen `filesystems:`-Eintrag.  Findet die
Erkennung keinen, rechnet das Werkzeug den DPB nach derselben Regel aus, mit der
auch das CP/A-BIOS beim LOGIN arbeitet: Sektorlängencode der Datenspur, Spurzahl,
ein-/beidseitig, Inhalt der Spur 0 → Systemspuren, Blockgröße, Verzeichnisplätze
(`doc/design/13_k1520disktool.md` §6.4, Analyse in `doc/cpa_format_detection.md`).
Das Dateisystem heißt dann `cpa_auto`, und `info` sagt, was herauskam:

```
$ k1520disktool info neu_formatiert.hfe
Format:      k5601_ss80_26x128
Dateisystem: cpa_auto
Medium:      nach der CP/A-Regel abgeleitet — 2 Systemspuren, 2048-B-Bloecke,
             128 Verzeichnisplaetze, Versatz 6
```

Ein **benanntes** Profil geht immer vor; mit `--fs cpa_auto` lässt sich die Regel
trotzdem erzwingen (z. B. um sie gegen ein Profil zu halten).

Zwei Dinge, die dabei auffallen können:

* *„Verzeichnis nicht angelegt (Füllbyte 0xF6)"* — die Diskette ist formatiert, aber
  nie eingerichtet worden.  Sie gilt als leer, was sie auch ist.
* *„die Diskette trägt ein MS-DOS-Dateisystem (FAT)"* — FORMAT.COM kann DOS-Disketten
  anlegen (Menüpunkte `{MSDOS}`).  Die liest das Wirtssystem, nicht dieses Werkzeug.

## Dateisysteme ergänzen

`data/formats.yaml` hat zwei Sektionen: `formats:` (Physik) und `filesystems:`
(logische Ebene — `type`, `data_start`, `block_size`, `dir_entries`).  `data_start`
ist eine **Spur**, kein Byte-Offset; bei gemischter Geometrie (cpa780) wäre er als
Spurzahl nicht ausdrückbar.  Schema: Kopf der Datei und
`doc/design/13_k1520disktool.md` §6.3.  Ein Eintrag lohnt nur, wo die Regel oben
nicht greift oder ein besserer Name gewünscht ist.

## Fremde Diskette ohne Katalogeintrag

Passt **gar kein** `formats:`-Eintrag, wird die Geometrie aus dem Abbild vermessen
und die Diskette trotzdem geöffnet — `Format: (gemessen)`.  So lässt sich eine
fremde Diskette ansehen, ohne vorher den Katalog zu erweitern.

> **Schreiben ist dabei gesperrt, und zwar unaufhebbar.**  Die Geometrie ist
> gemessen, nicht belegt; ein Schreibvorgang landete beim geringsten Irrtum an der
> falschen Stelle, und fremde Abbilder sind meist Einzelstücke.  Wer schreiben
> will, trägt das Format in `data/formats.yaml` ein — `measure` liefert die Vorlage.

Trägt die vermessene Geometrie nichts Lesbares, nennt die Meldung die Messung:

```
$ k1520disktool ls fremd.hfe
Fehler: Das Abbild passt zu keinem Format in data/formats.yaml, und auf der
gemessenen Geometrie liegt kein lesbares Dateisystem (…).
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
