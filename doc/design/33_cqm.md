# 33 — CopyQM-Abbilder (`.cqm`) als vierter Container

Stand 2026-10-17, Zweig `cqm`.  Kern: `core/peripherals/floppy_drive/cqm_codec.{h,cpp}`,
Tests `tests/unit/peripherals/test_cqm_codec.cpp`, Merkposten `doc/merkposten/cqm.md`.

## 1. Aufgabe und Abgrenzung

Die P8000-/WEGA-Disketten liegen als CopyQM-Abbilder vor (142 Dateien in
`robotron/P8000/discs/`).  Der Kern soll sie **lesen** (und seit 2026-10-17 auch **schreiben**,
Abschnitt 7) — Emulator (Mounten), DiskTool
(`ls`/`get`/`check`/`info`/`save-as`) und C-ABI (`k1520_mount…`, `k1520d_open`) — als
vierter `ContainerType::Cqm` neben Img/Hfe/Dmk.  **Schreiben war zunaechst ausdruecklich nicht
vorgesehen** (`save()` scheiterte, Schreibschutz erzwungen); das ist mit Abschnitt 7 ueberholt.
`saveAs`/`exportTo` in `.img`, `.hfe`, `.dmk` gingen immer.

## 2. Format

Alle Zahlen little-endian.  Kopf 133 Byte (`kHeaderSize`), danach Kommentar, danach der
Datenstrom.  Die Belegung wurde an den 142 Abbildern **und** gegen `drvqm.c` aus libdsk
1.5.21 (John Elliott) verifiziert; der Pruefwert stammt von dort (an keinem anderen Weg
zu erraten, siehe 2.2).

| Offset | Groesse | Inhalt |
|--------|---------|--------|
| 0x00 | 2 | `CQ` |
| 0x02 | 1 | Version, in allen Abbildern 0x14 |
| 0x03 | 2 | Sektorgroesse (256/512/1024 in den Abbildern) |
| 0x10 | 2 | Sektoren je Spur |
| 0x12 | 2 | Seiten |
| 0x1C | 60 | Bezeichnung, NUL-terminiert (`720K Double-Sided`) |
| 0x58 | 1 | Blind-Flag (0 DOS, 1 blind, 2 HFS); alle Abbilder 1 |
| 0x59 | 1 | Dichte (0 DD, 1 HD, 2 ED); alle Abbilder 0 |
| 0x5A | 1 | gespeicherte Zylinder (`used`) |
| 0x5B | 1 | Zylinder insgesamt |
| 0x5C | 4 | Pruefwert der entpackten Nutzdaten (0 = nicht gesetzt) |
| 0x60 | 11 | Etikett (`** NONE **`), nicht NUL-pflichtig |
| 0x6B / 0x6D | 2 / 2 | DOS-Zeit / DOS-Datum des Abbilds |
| 0x6F | 2 | Kommentarlaenge |
| 0x71 | 1 | erste Sektor-ID minus 1 |
| 0x74 / 0x75 / 0x76 | 1 | Interleave / Skew / Quellaufwerk (nur gemeldet) |
| 0x84 | 1 | Kopfpruefbyte: Summe der Bytes 0x00..0x84 ≡ 0 (mod 256) |
| 0x85 | n | Kommentar (Laenge von 0x6F), kein NUL |

### 2.1 Datenstrom

Folge von Bloecken `i16 n`: `n < 0` → das **eine** folgende Byte `-n`-mal; `n > 0` → `n`
Rohbytes; `n = 0` ohne Wirkung.  Die Bloecke laufen **ueber Sektor- und Spurgrenzen
hinweg** (die echten Abbilder tun das, `ari.cqm` beginnt mit `03 00 "ari" 9f ff 00 …`);
es gibt keinen Neubeginn je Spur und kein Spurkopf.  Ausgepackt ergeben sich
`used × Seiten × Sektoren × Sektorgroesse` Bytes in der Reihenfolge Zylinder → Kopf →
Sektor (ID `secbase+1 …`) — **dieselbe Reihenfolge wie ein `.img`**, an 54 `.tar`-
Gegenstuecken byteweise bestaetigt (Abschnitt 4).  Interleave/Skew des Kopfs beschreiben
nur die Spur der Original-Diskette; der Strom liegt in logischer Folge.

### 2.2 Pruefwert (Tabellenfehler)

CRC-32, reflektiert, Polynom 0xEDB88320, Startwert 0, **kein** Endxor — aber mit CopyQMs
Fehler: der Tabellenindex ist `(byte ^ crc) & 0x3F` statt `& 0xFF` (libdsk: "they shift the
crc ^ data 2 bits up to address longwords, but they do that in an eight bit register, so
that the top 2 bits are lost").  Folge: nur 64 der 256 Tabelleneintraege werden je benutzt,
und **die Bits 6 und 7 jedes Datenbytes gehen nicht in den Pruefwert ein** — ein Bitkipper
dort bleibt unbemerkt (Test `KippenderDatenbyteFaelltAmPruefwertAuf` haelt das fest).  Ohne
diese Kenntnis passt weder CRC-32 noch eine seiner Varianten (ausprobiert: 8 Varianten ueber
Nutzdaten, gepackten Strom, ganze Datei).  Alle 142 Pruefwerte stimmen.

## 3. Abbildung auf das Medium

* Geometrie aus dem Kopf, **kein Katalogeintrag noetig**: `DiskMedium(total_cyls, Seiten,
  MFM)`; jede Spur ueber `TrackCodec::buildTrack` mit Interleave 1, IDs `secbase+1…`,
  Zylinder-/Kopfnummer im ID-Feld — Aufbau identisch zu `ImgCodec::load`; ein Test
  (`MediumGleichImgAusDerTar`) vergleicht Spurbytes **und** Marken gegen das `.img`, das
  `ImgCodec` mit `k5601_9x512` aus der `.tar` macht.
* Zylinder `used … total-1` (kommt in den 142 Abbildern nicht vor) werden **leer
  formatiert mit 0xF6**, wie CopyQM/libdsk sie fuellen — nicht unformatiert, damit ein
  Gast sie lesen kann.
* Drehzahl: 360 nur fuer HD mit 15 × 512 (5,25″-1,2M), sonst 300 — beeinflusst nur die
  Zellenzahl einer spaeter erzeugten `.hfe`.
* FM kennt CopyQM nicht; alles MFM.

## 4. Befunde an den 142 Abbildern

Alle 142 dekodieren fehlerfrei, Kopfpruefbyte und Pruefwert stimmen, `used == total == 80`,
Blind 1, Dichte 0, Sektorbasis 0, **kein Rest hinter dem Strom**.

| Gruppe | Anzahl | Befund |
|--------|--------|--------|
| 9 × 512, 2 Seiten, 80 Zyl. ("720K") | 117 | Hauptmenge; 5 davon (emscp32, w22calc, w31word, w31wrddk, wdos_dcp) tragen den Kommentar "Image file created by unregistered CopyQM", Bezeichnung "0K Double-Sided", Interleave 1, Quellaufwerk 2 |
| 16 × 256 ("640K") | 19 | UDOS/NDOS, OS/M, w30strt2 (Kommentar "P8000-SOFTWARE WEGA-Startdisk. 889 347/3.0 PRS.: 5229", Interleave 1) |
| 5 × 1024 ("800K") | 6 | OS/M-Datendisketten |

`.tar`-Gegenstuecke (55 Dateien, alle 737 280 B oder Vielfache): **54 stimmen byteweise**;
`ariane`=ariane1+2, `konsul`=konsul1..3, `lcc`=lcc1+2, `psc`=psc1+2, `w11data`=w11data1..7,
`w21pasc`, `wegaf77`=wegaf771+2, `wegbas`=wegbas12+22.  **Einzige Abweichung:
`curspdoc.tar`** — ein einziges Byte in Sektor 546 (Spur 30/0, Sektor 6, Offset 25: CQM
`p`, tar `x`, im Text "Indextyp" bzw. "Indextxp"); der CQM-Pruefwert stimmt (und Bit 3
liegt in der erfassten Hälfte), das CQM ist also das Treue, die `.tar` hat einen
Bitkipper.  Der Test erwartet genau diese eine Abweichung.

Dateisystem-Erkennung (`k1520disktool info`, vollstaendige Tabelle im Scratchpad
`cqm_erkennung.txt`): `wega720` 31 (WEGA 3.0/3.1 root/usr/doc), `udos1715` 12 (NDOS-
Systemdisketten, irts*, src, udos*, w30start/strt2, w31start/star2), `scpx640` 7
(OS/M 640K), `cpa_auto` 7 (OS/M 800K sowie konsul2 mit 0 Dateien), **keines erkannt 85**:
84 sind **V7-tar/cpio-Datenstroeme** direkt auf der Diskette ohne Dateisystem (der
Grund fuer die `.tar`-Gegenstuecke) — "nichts erkannt" ist dort die richtige Auskunft —
und `wdos_dcp` ist eine MS-DOS-Diskette (FAT, "DCP 3.3"), die das DiskTool nicht liest.

Auffaelligkeiten ausserhalb der Abbilder: `discs/` enthaelt 13 `*.crdownload` (abgebrochene
Downloads: rempc172, w10rem, w11rem13, w41pascc, wegrem13/23/33, wgaend12/22, teils auch
als `.tar`) — ignoriert, sie sind keine gueltigen CQMs; `discs/WEGA3.0/` enthaelt 17 CQMs,
die byteidentisch zu denen in `discs/` sind, und `*_cqm.scp`-Flussabbilder (nicht Teil
dieses Auftrags).

## 4a. tar-Disketten (Stand 2026-10-17)

Befund an den 142 Abbildern: **84 tragen einen rohen `tar`-Strom ab Sektor 0**, davon 73 mit
Kopf bei Byte 0 und 11 als **Fortsetzung** (Diskette 2…n eines Satzes; erster Kopf bei
Byte 512…296448).  Reines V7-tar (Magic leer, Typ `0`, vier harte Links in `mgen`), kein
cpio, 4146 Einträge, keine Dopplungen; 204…217 Namen je Satz absolut (`/z/ariane/…`).
Sätze mit abgeschnittener Datei am Diskettenende: ariane1, konsul1/2, lcc1, psc1,
w11data1–4/6, wegaf771; die Verkettung (`tar-satz`) macht sie vollständig und entspricht
byteweise den `.tar`-Dateien.  Hinter dem Archivende (Nullblock) steht Altbestand der
Formatierung; er wird nicht gelesen.  Die einzige verbleibende Diskette ohne Erkennung ist
`wdos_dcp` (MS-DOS/FAT).  Tabelle: Scratchpad `cqm_erkennung2.txt` (tar720 84, wega720 31,
udos1715 12, cpa_auto 7, scpx640 7, nichts 1).

Umsetzung: `core/filesystem/tar/tar_fs.{h,cpp}` (`TarFileSystem`, `TarQuelle` über
Diskette oder Speicher), `FsType::Tar`, Profil `tar720` in `data/formats.yaml`
(Begründung dort: Erkennung zuletzt; ein Eintrag, nur 720K belegt).  Erkennung:
gültige Kopfpruefsumme (vorzeichenlos oder -behaftet), druckbarer Name, plausible Groesse;
Typen `0–7`, GNU `L`/`K`, `x`/`g` werden übersprungen.  `DiskVolume::openTarSatz`
verkettet die linearen Ströme; CLI `k1520disktool tar-satz`.  Befunde `tar.abgeschnitten`,
`tar.fortsetzung`, `tar.pruefsumme`, `tar.doppelt`, `tar.pfad`.

## 5. Entscheidungen

1. ~~Nur lesen.~~  **Ueberholt (2026-10-17): schreibbar, siehe Abschnitt 7.**
2. **Strikt bei Kopf und Pruefwert**: falsche Kennung, falsches Kopfpruefbyte, abgeschnittene
   Datei, ueberlanger Block, falscher Pruefwert → Laden scheitert.  Pruefwert 0 (alte
   CopyQM-Versionen) wird nicht geprueft (`crc_checked = false`).  Bytes hinter dem Strom
   werden toleriert und gezaehlt (`trailing_bytes`).
3. **`ImageCodec::detect` erkennt CQM an der Kennung**, nicht an der Endung (Vorrang wie bei
   HFE); `fromExtension` kennt `.cqm`.  Eine `.cqm` ohne Kennung faellt auf `.img` zurueck
   und scheitert dort.
4. **`FsProfile::allowsContainer("cqm")` = `allow_img`** — ein CQM traegt nur Sektordaten
   und damit genau das, was auch ein `.img` traegt (UDOS mit Gap-Verkettung also nie).
5. ~~`DiskVolume` oeffnet `.cqm` immer schreibgeschuetzt.~~  **Ueberholt (Abschnitt 7):** wie
   `.img` beim Oeffnen geschuetzt, aber frei aufhebbar; erzwungen bleibt der Schutz nur bei
   tar-Disketten.
6. **Kommentar**: `DiskImage::cqmInfo()->comment` / `DiskVolume::cqmInfo()`; `k1520disktool
   info` druckt eine Zeile `CopyQM: …`.  Die C-ABI bietet **noch keinen Zugriff** — er
   braeuchte eine neue Funktion in `k1520_disk_api` samt ctypes-Deklaration in `app/`.
   **Nachgetragen:** `k1520d_container_info(h, key)` + `DiskTool.container_info()`.

## 6. Offen

* GUI-Anzeige des Kommentars (die C-ABI `k1520d_container_info` steht).
* GUI-Dateidialoge (`app/`) kennen `.cqm` noch nicht.
* Interleave/Skew werden gemeldet, aber nicht angewandt; ein CQM mit echtem Interleave im
  Strom (alte CopyQM-Versionen, `interleave == 0`) ist an keinem Abbild beobachtet.
* Nicht gespeicherte Zylinder (`used < total`) sind nur synthetisch getestet.

## 7. Schreiben (Stand 2026-10-17)

`CqmCodec::save/encode` (+ `packRle`), `DiskImage` bindet eine `.cqm` schreibbar
(`bindingWritable() == true`: Autoflush nach 0,5 s Schreibpause, `flush()`, `saveAs()`,
`create()` mit Format), `FloppyDriveV2::mount` und `DiskVolume` erzwingen **keinen** Schreibschutz
mehr (`k1520d_read_only_forced` bleibt fuer tar und nur gemessene Geometrien).

**Darstellbar** ist ein Medium, wenn `DiskMedium::rawCompatible()` gilt (wie `.img`) **und**:
alle Spuren MFM mit voller Datenrate (CopyQM kennt kein FM — eine FM-Spur wird abgelehnt,
statt still als MFM wiedergeladen zu werden); je Spur gleich viele (1..64) gleich grosse
(128..4096) Sektoren mit lueckenlosen IDs `base..base+n-1` (`base` >= 1, Kopfbyte 0x71 =
`base-1`), auf allen Spuren identisch; beide Seiten (oder, wenn Seite 1 ganz unformatiert
ist, eine); Zylinder 0..last beschrieben, unformatierte Endzylinder werden nicht gespeichert
(`used < total`, der Leser fuellt sie mit 0xF6).  Alles andere (gemischte Geometrie wie die
CP/A-Systemspuren mit 128 B, unformatierte Luecken, Doppelschritt-Medien, Daten hinter der CRC)
wird mit Begruendung abgelehnt.

**Nie ein Teilzustand:** zuerst wird die ganze Datei im Speicher aufgebaut und **durch den
Leser gegengeprueft** (entpackte Daten gleich, Pruefwert ok), dann in `<datei>.k1520tmp`
geschrieben und per `rename` ersetzt.  Scheitert eine Stufe, bleibt die Zieldatei unveraendert,
die Nachbardatei wird entfernt.  (Die anderen Codecs schreiben noch in-place; nur `.cqm` tut
das so.)

**Politik bei nicht darstellbarem Medium (wie `.img`):** `flush()` scheitert mit Grund in
`DiskImage::lastError()`, `saveFailed()` wird wahr, die Datei bleibt, die Maschine laeuft mit
dem Speicherabbild weiter; `autoFlush` versucht es erst nach der naechsten Aenderung (Medium-
Revision) wieder.  Neu: `FloppyDriveV2` haengt dann den Hinweis `Abbild nicht speicherbar:
<Grund>` an `notices()` (gilt fuer jeden Container) und nimmt ihn beim naechsten Erfolg zurueck.

**Kopffelder bleiben:** `SourceInfo` traegt jetzt `raw_header` (133 Byte) und `comment_raw`.
Beim Schreiben mit Vorlage (Autoflush, `flush`, `saveAs`/`exportTo` einer geladenen `.cqm`)
bleiben bei **gleicher Geometrie** alle Kopfbytes ausser Geometrie/Pruefwert/Kopfpruefbyte
erhalten (Bezeichnung, Dichte, Blind, Etikett, Zeit/Datum, Interleave/Skew, Quellaufwerk,
Unbekanntes) samt Kommentar.  Aendert sich die Geometrie, gelten nur Kommentar und Etikett;
Bezeichnung/Dichte/Interleave werden neu gesetzt.  Ohne Vorlage (neue Datei, Umweg ueber
`.hfe`): Version 0x14, Bezeichnung `"<KB>K Double-Sided|Single-Sided"` (KB =
Zylinder x Seiten x Sektoren x Groesse / 1024), Blind 1, Dichte 0 (1 bei 512 B x >= 15,
2 bei >= 30 Sektoren), Etikett `"** NONE ** "`, Zeit/Datum 0, Interleave 1, Skew 0,
Kommentar `"Image file created by k1520emu"`.

**RLE:** ein Strom ueber die ganze Diskette (Bloecke ueber Sektor-/Spurgrenzen), Wiederholung
ab 5 gleichen Bytes, Bloecke hoechstens 32767 Byte (int16; -32768 und 0 werden nie
erzeugt).  Der Schnitt ist **nicht** der des Originals — Datei1 -> Medium -> Datei2 ist
nicht byteidentisch (die 142 Abbilder werden gleich gross bis ~1 % kleiner/groesser), wohl aber
inhaltsgleich.  **Pruefwert 0 ist doppeldeutig:** CRC ueber nur Nullbytes ist 0 und gilt beim
Lesen als "nicht gesetzt" (`crc_checked == false`) — Eigenheit des Formats.

**Gegenprobe:** `CqmEchtdaten.SpeichernUndWiederLadenIstVerlustfrei` (142 Abbilder, mit
`K1520_CQM_DIR`): laden -> speichern -> laden, Sektordaten, Spurbytes/Marken und alle
Kopffelder gleich.  **libdsk (`drvqm.c`) steht nicht zur Verfuegung** und wurde nicht
gegengeprueft; stattdessen bestaetigte eine unabhaengig geschriebene Python-Dekodierung samt
`tests/python/cqm_bau.py::cqm_crc` und Kopfpruefbyte die erzeugten Dateien.  Ob ein echtes
CopyQM sie anstandslos nimmt, ist unbelegt (Annahme: ja, da Kopf und Strom dem Muster der
142 Originale folgen).
