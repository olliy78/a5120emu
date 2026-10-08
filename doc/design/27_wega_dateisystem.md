# Entwurf 27 — WEGA-Dateisystem im k1520DiskTool (AP P17)

Stand 2026-10-08.  Ziel: Dateien von und zu WEGA-Datenträgern (P8000) austauschen, ohne
WEGA zu starten — Disketten (Stufe A) und Plattenabbild (Stufe B).  Quellen: WEGA-Kern
`uts/sys/alloc.c` (alloc/free/ialloc/ifree), `cmd/standalone/sa.mkfs.c` (bflist/iput),
`head/sys/{filsys,ino,dir,fblk,param}.h`, `uts/conf/wpar.c` + `uts/h/mdsize.h`
(Partitionen); Befund an den Lieferdisketten: `doc/p8000/wega_datentraeger.md` §3.

## 1. Aufbau (System III, Z8000 = big endian)

| Block | Inhalt |
|---|---|
| 0 | Boot-Block (root1: Z8001-Code `pb.image`, sonst Dienstdaten) |
| 1 | **Superblock** `struct filsys` |
| 2 … s_isize−1 | Inode-Liste, 8 × 64 B je Block; Inode 1 = Bad-Block-Datei, **Inode 2 = Wurzel** |
| s_isize … s_fsize−1 | Datenblöcke |

**Superblock** (Offsets am Abbild `w30root1` nachgemessen; Wortgrenzen des Z8000-C):
`s_isize` u16 @0 · `s_fsize` u32 @2 · `s_nfree` i16 @6 · `s_free[50]` u32 @8 ·
`s_ninode` i16 @208 · `s_inode[100]` u16 @210 · `s_flock/ilock/fmod/ronly` @410–413 ·
`s_time` @414 · `s_tfree` u32 @418 · `s_tinode` u16 @422 · `s_m`/`s_n` @424/426
(Verschränkung der Freiliste) · `s_fname[6]` @428 · `s_fpack[6]` @434 · `s_mach` @440.
**NICFREE = 50**, nicht 100 (`param.h`); `s_isize` zählt Block 0 und 1 mit
(Inodes = (s_isize−2)·8).  Kein Magic.

**Inode** (64 B): `di_mode` u16, `di_nlink`, `di_uid`, `di_gid` (i16), `di_size` u32,
`di_addr[40]` = 13 Adressen à 3 Byte big endian (`ltol3`), Byte 51 unbenutzt,
`di_atime/mtime/ctime` u32 (Sekunden seit 1970, GMT).  Adressen 0–9 direkt, 10 einfach,
11 doppelt, 12 dreifach indirekt; ein indirekter Block = 128 × u32 BE.  Adresse 0 = Loch.

**Dateiarten** (`di_mode & 0170000`): 040000 Verzeichnis, 0100000 Datei, 020000/060000
Zeichen-/Blockgerät (`di_addr[0]` = major<<8 | minor, keine Blöcke), 010000 FIFO
(Blöcke wie Datei), 030000/070000 gemultiplexte Geräte (System III).  **Symlinks gibt es
nicht.**  Hardlinks ja (`di_nlink`).

**Verzeichnis**: 16-B-Einträge (`d_ino` u16, `d_name[14]` mit Nullen aufgefüllt, nicht
abgeschlossen bei 14 Zeichen); `d_ino = 0` = freier Platz; `.`/`..` am Anfang.

**Freiliste**: `s_free[0..s_nfree−1]`; vergeben wird von oben (`s_free[--s_nfree]`).
Wird dabei `s_nfree` 0, ist der eben vergebene Block ein **Ankerblock** (`struct fblk`:
`df_nfree` i16 @0, `df_free[50]` u32 @2) und füllt die Liste neu.  `s_free[0] = 0` am
Ende der Kette.  Freigabe: bei vollem Superblock wird der freigegebene Block selbst zum
Ankerblock.  `s_tfree` zählt die freien Blöcke (ohne die Endmarke 0).

**Inode-Freiliste** `s_inode[100]` ist nur ein **Zwischenspeicher**; maßgeblich ist
`di_mode == 0`.  Ist er leer, sucht `ialloc` die Inode-Liste ab.  `s_tinode` = freie Inodes.

**Zeichen/Zeilenende**: ASCII, LF — also Linux-gleich; `--text` setzt nichts um.

### 1.1 Varianten

- **Diskette** (Lieferung WEGA 3.0): K5601-Format 80 Zyl. × 2 Köpfe × 9 × 512 =
  1440 Blöcke; Block n = Byte n·512 der linearen Sektorfolge (Zylinder, Kopf, Sektor) =
  `.img`-Reihenfolge.  `s_isize = 59` (= 1440/25 + 2), `s_m/s_n = 1/72`.
- **Platte** K5504.50 (1024 Zyl. × 5 Köpfe × 18 × 512): rohes LBA-Abbild, Zylinder 0
  trägt PAR/BTT (Z0/K0/S1: `"DEFEKT"` @0, `"PARMTR"` @256) und gehört **nicht** zum
  Blockraum: WDC-Block 0 = Zylinder 1 (Vorlauf Köpfe·Sektoren = 90 Sektoren);
  Defektspuren der BTT verschieben die Zielspur um je eine Spur (`doc/p8000/wdc_firmware.md`).
  **Die Partitionen stehen NICHT auf der Platte**, sondern im Kern (`md_sizes[]` in
  `uts/conf/wpar.c`, Größen aus `mdsize.h`): md0 /usr 0/13000, md1 swap 13000/3000,
  md2 / 16000/7000, md3 /tmp 23000/4000, md4 /z 27000/60732.

## 2. Einbindung

- `FsType::Wega`, Katalogprofil **`wega720`** (`k5601_9x512`, `detect_rank: -10`).
  Mehr Profile nicht: das Dateisystem beschreibt sich selbst.
- `WegaFileSystem` arbeitet auf einem **Blockgerät** (`WegaBlockDev`): `WegaSpaceDev` über
  dem `SectorSpace` (Diskette), `WegaSpeicherDev` über einem Byte-Puffer (Platte).
- **Namen sind Pfade** relativ zur Wurzel (`bin/ls`); `list()` liefert den ganzen Baum,
  Verzeichnisse als Typ `d`, Geräte `c`/`b`, FIFO `p`; `attributes` = `ls -l`-Rechte,
  `date` = mtime (GMT), dazu `FileEntry::unix_*` (Modus, uid, gid, Links, Inode, mtime).
- `get` legt Unterordner an; `put <ordner>` überträgt **rekursiv** (auch leere Ordner);
  `put datei --as pfad/name` legt fehlende Verzeichnisse an (`mkdir -p`).
  Neu: `FileSystem::makeDirectory` (Vorgabe: „kennt keine Unterverzeichnisse"),
  `DiskVolume::makeDirectory`, CLI `mkdir`.
- `.img` ist **erlaubt** (nichts außerhalb der Sektoren).
- **Kein Bootbereich** im Sinne des Werkzeugs (`bootAreaSize = 0`); Block 0 bleibt beim
  Schreiben unberührt.

## 3. Erkennung (ohne Magic)

Plausibilitätsprüfung `looksLikeWega`: `3 ≤ s_isize < s_fsize ≤ Blöcke des Datenträgers`,
`s_nfree ≤ 50`, `s_ninode ≤ 100`, `s_tfree ≤ Datenblöcke`, `s_tinode ≤ Inodes`, alle
`s_free[i]` im Datenbereich (außer der Endmarke 0), Inode 2 ist ein Verzeichnis mit
Größe ≥ 32 und Vielfachem von 16, erster Block im Datenbereich, beginnt mit `.`→2 und
`..`→2.  Eine CP/M-Diskette scheitert schon an Block 1 (0xE5 ⇒ s_isize 58853 …).

## 4. Schreiben — Entscheidungen

1. **Die Algorithmen des Kerns, nicht eigene.**  alloc/free/ialloc/ifree wie
   `alloc.c`, mkfs wie `sa.mkfs` (Inode-Liste fsize/25+2, Freiliste in Verschränkung
   m/n, Inode 1 leere Bad-Block-Datei, Wurzel `040777` uid/gid 0).  Eine von uns
   beschriebene Diskette sieht für WEGA aus wie eine selbst beschriebene.
2. **Jede Operation ist eine Transaktion**: alle Blockänderungen (Superblock zuletzt)
   sammeln sich in einer Arbeitskopie und gehen erst bei Erfolg ans Gerät.  „Kein Platz"
   mittendrin hinterlässt nichts.  Stapel (`insertAll`) zusätzlich über die
   Momentaufnahme des `DiskMedium` wie bisher.
3. **Anlegen/Überschreiben**: neue Datei = neue Inode (Modus 0644, ausführbar ⇒ 0755 aus
   den Linux-Rechten), uid/gid 0 (`wega`), Zeiten = jetzt.  Überschreiben behält Inode,
   Besitzer und (ohne Angabe) Rechte, gibt die alten Blöcke frei (`itrunc`).
   Verzeichnisinhalte werden beim Wachsen ganz neu geschrieben (freigeben, neu vergeben —
   die LIFO-Freiliste gibt dieselben Blöcke zurück).
4. **Löschen** = `unlink`: `d_ino = 0` (Name bleibt stehen), nlink−1, bei 0 Blöcke
   freigeben und Inode löschen + `ifree`.  Verzeichnisse nur leer (`rmdir`, Elter-nlink−1).
5. **Namen** ≤ 14 Zeichen, sonst Fehler (nicht stumm kürzen); `/` trennt.
6. **Superblock**: `s_time` = jetzt, `s_fmod = 0` beim Festschreiben.
7. **Platte**: Schreibschutz als Vorgabe wie bei physischen Datenträgern; vor dem ersten
   Zurückschreiben `<abbild>~`.

## 5. Prüfung (`check`, schreibt NIE)

Schnell (beim Öffnen): Superblock + Inode-Liste.  Voll: + Freiliste, Baum, Belegung.

| Kennung | Schwere | Bedeutung |
|---|---|---|
| `wega.super.lesen` / `.groesse` / `.nfree` / `.ninode` | Fehler | Superblock unbrauchbar |
| `wega.super.inodeliste` | Warnung | `s_inode[]` außerhalb der Inode-Liste |
| `wega.super.inodecache` | Info | Zwischenspeicher nennt belegte Inode (Kern übergeht sie) |
| `wega.super.wurzel` | Fehler | Inode 2 kein Verzeichnis |
| `wega.inode.lesen` / `.art` | Fehler | Inode-Block unlesbar / unbekannte Dateiart |
| `wega.inode.zaehler` | Warnung | `s_tinode` ≠ freie Inodes |
| `wega.frei.ausserhalb` / `.kette` | Fehler | Freiliste nennt Unsinn / Kette kaputt |
| `wega.frei.doppelt` | **Gefahr** | Block zweimal frei ⇒ doppelte Vergabe |
| `wega.frei.zaehler` | Warnung | `s_tfree` ≠ Länge der Freiliste |
| `wega.dir.lesen` / `.inode_ausserhalb` / `.freier_inode` | Fehler | Verzeichnisverweise |
| `wega.dir.groesse` / `.punkt` | Warnung | Größe kein Vielfaches von 16 / `.`,`..` falsch |
| `wega.block.ausserhalb` | Fehler | Dateiblock außerhalb des Datenbereichs |
| `wega.block.doppelt` / `.frei_belegt` | **Gefahr** | Block zweimal belegt / belegt und frei |
| `wega.block.verloren` | Warnung | Blöcke weder belegt noch frei (eine Sammelzeile) |
| `wega.inode.links` / `.verwaist` | Warnung | Linkzähler falsch / belegt, aber namenlos |

Inode 1 (Bad-Block-Datei, nlink 0) ist nie „verwaist".  Reparaturen: keine (Etappe 1);
die Kennungen sind Vertrag.  Falschmeldungen schlimmer als fehlende: alle Lieferdisketten
und die installierte Platte müssen ohne Befund ab Warnung prüfen.

## 6. Offen

- `.fileinfo` für WEGA (Besitzer, Rechte, Zeiten durch `get`→`put` tragen) — heute nur
  das Ausführbar-Bit.
- Rettung gelöschter Dateien (Inode bleibt bis zur Wiedervergabe lesbar, der Name im
  Verzeichnis auch) — Kandidat für eine spätere Etappe.
- Reparaturen (Freiliste neu aufbauen wie `fsck -s`).
- Oberfläche: Dateiliste und Eigenschaften kommen über das Muster mit; Baumansicht,
  Plattenabbild (Stufe B) nur in der Kommandozeile.
