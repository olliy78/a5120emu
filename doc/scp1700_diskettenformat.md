# SCP1700 / CP/M-86 (A7100) — Diskettenformat und Dateisystem

**Stand:** 2026-08-18 · Referenzdatenträger: eine am Greaseweazle F1 eingelesene
Systemdiskette des A7100 (46 Dateien, 80×2×16×256 + FM-Bootspur, 300 min⁻¹)

Der 16-Bit-Rechner **A7100** fährt **SCP1700**, ein CP/M-86. Seine 5,25″-Disketten
sind fast gewöhnliche Standard-IBM-MFM-Disketten — mit **einer** Besonderheit, an der
jeder Leseversuch scheitert, der sie nicht kennt:

> **Spur 0, Kopf 0 ist in FM aufgezeichnet, und zwar mit HALBER Datenrate**
> (125 kbit/s, 16 Sektoren à 128 B). Alle übrigen 159 Spuren tragen MFM mit
> 250 kbit/s, 16 Sektoren à 256 B.

Eine Diskette mit **zwei Datenraten** ist im ganzen Formatkatalog sonst nirgends zu
finden; Mischdichte ja (8″-System-34), aber nicht Mischrate.

Das ist keine Vermutung. Das CP/A-BIOS des A5120 rechnet ausdrücklich damit:

```
; - Jeder Fehler in Spur 0, Sektor 1 fuehrt zur Annahme von Systemspuren
;   (A7100-System mit 5" FM und 8272-Hardware mit 128er Sektorlaenge)
        call    dsidtr          ;beliebigen Sekt.Id lesen
        jr      nz,selsys       ;Fehler beim Lesen; Systemsp. annehmen
;                                (z.B. SCP1700 mit 5"FM in Spur 0!!)
```
(`biosdsk.mac`, s. `disks/cpa_cpa780_k5601_clock.prn`)

---

## 1 Physik

| | Spur 0 Kopf 0 | alle übrigen Spuren |
|---|---|---|
| Verfahren | **FM** | MFM |
| Datenrate | **125 kbit/s** | 250 kbit/s |
| Flusszeiten (gemessen) | **4 / 8 µs** | 4 / 6 / 8 µs |
| Sektoren | 16 × 128 B, IDs 1…16 | 16 × 256 B, IDs 1…16 |
| Kapazität | 2 048 B | 4 096 B |

Drehzahl 300 min⁻¹, Zylinder 0…79, beide Köpfe. Rohkapazität
2 048 + 159 × 4 096 = **653 312 B**.

Katalogeintrag (`data/formats.yaml`):

```yaml
- name:        scp1700_640
  tracks:
    - { cyls: 0,    heads: 0,   sectors: 16, size: 128, encoding: fm, rate: 125 }
    - { cyls: 0,    heads: 1,   sectors: 16, size: 256 }
    - { cyls: 1-79, heads: 0-1, sectors: 16, size: 256 }
```

`rate: 125` ist der einzige Ort, an dem die halbe Datenrate im Katalog steht; sie
landet als @ref TrackImage::cell_factor an der Spur und überlebt so jedes Speichern
(§4).

### 1.1 Die Bootspur ist über die Umdrehung hinaus beschrieben

Der Referenzdatenträger trägt auf c0h0 **19 Adressmarken für 16 Sektoren**: hinter
Sektor 16 folgen noch einmal die Sektoren 1…4 — byteweise dieselben Daten wie am
Spuranfang. Die Spur wurde offenbar in einem Zug geschrieben, und der Schreibvorgang
lief über den Index hinaus.

Für die Formaterkennung zählen deshalb die **verschiedenen** Sektor-IDs
(`MeasuredTrack::uniqueSectors`) — der Treiber liest über die ID, ein Doppelgänger
bringt keinen Platz. Nach der rohen Zahl („19×128") passte die Diskette zu keinem
Format des Katalogs.

Auf dem Referenzdatenträger ist **Sektor 10 der Bootspur unlesbar** (kein Adressfeld,
auch nach vielen Umdrehungen nicht — ein echter Schaden der Diskette, kein Lesefehler).
`boot-get` bricht darum mit `Systemspur 0 Sektor 10 nicht lesbar` ab; das Dateisystem
ist davon nicht betroffen.

## 2 Dateisystem

Ein gewöhnliches **CP/M-2.2-Verzeichnis** — CP/M-86 unterscheidet sich darin nicht.
32-B-Einträge, Extents, 128-B-Sätze; was anders ist (Programmdateiformat `.CMD`,
8086-Code), liegt im Dateiinhalt.

| Größe | Wert | woher |
|---|---|---|
| Systemspuren | c0h0, c0h1, c1h0, c1h1 = **14 336 B** | Verzeichnis beginnt danach |
| Dateisystem ab | **c2h0** (`data_start`) | gemessen |
| Blockgröße | **2 048 B** | 8 Zeiger × 2 048 = 16 KB = 1 Extent |
| Blockzeiger | **16 Bit** (8 je Eintrag) | > 255 Blöcke |
| Blöcke | **312** (0…311, höchster benutzter 311) | 156 Datenspuren × 4 096 / 2 048 |
| Verzeichnisplätze | **128** (Blöcke 0 und 1) | gemessen |
| Sektorversatz | **0** | Dateien liegen sektorfortlaufend |

Der Datenträger im Referenzzustand: **46 Dateien, 618 KB belegt, 2 KB frei** — der
Liefersatz eines CP/M-86 (`SCP.SYS`, `RASM86.CMD`, `LINK86.CMD`, `LIB86.CMD`,
`DDT86.CMD`, `SID86.CMD`, `ED.CMD`, `PIP.CMD`, `STAT.CMD`, `SUBMIT.CMD`, `BASIC.CMD`,
die 8087-Bibliotheken `*87.L86`, dazu `BIOS.A86` und `SCPX.H86` im Quelltext).

Profil (`data/formats.yaml`, Sektion `filesystems:`):

```yaml
- name:        scp1700
  format:      scp1700_640
  type:        cpm
  data_start:  { cyl: 2, head: 0 }
  block_size:  2048
  dir_entries: 128
```

Ein eigener Eintrag ist nötig, weil `CpaDpbRule` hier **nicht** gilt: die Regel bildet
das CP/A-BIOS nach, nicht das SCP1700.

## 3 Erkennung

Die Datenspuren allein (16×256 MFM) passen auch zu `cpa640`, `k5601_16x256` und damit
zu UDOS1715 — **die FM-Bootspur ist das Unterscheidungsmerkmal**. `scp1700_640` ist das
einzige Format, das sie beschreibt; für alle anderen ist c0h0 eine Lücke, und
umgekehrt fällt eine gewöhnliche 16×256-Diskette bei `scp1700_640` mit „Verfahren mfm,
Format sagt fm" durch. Ein `detect_rank` braucht es deshalb nicht.

```
$ k1520disktool measure scp1700.hfe
  c0h0 : 16 Sektoren à 128 B, IDs 1-16, fm
  c0h1..c79h1 : 16 Sektoren à 256 B, IDs 1-16, mfm
Passt zu:
  scp1700_640
```

## 4 Was der Kern dafür können musste

Drei Dinge im Kern waren auf **eine** Rate je Diskette gebaut:

1. **Der HFE-Leser nagelte den Abtastfaktor an der ersten Spur mit Marken fest.**
   Das ist hier die FM-Bootspur — danach galt ihr Faktor für die ganze Diskette, und
   alle 159 MFM-Spuren kamen als „unformatiert" bzw. als ein Zufallssektor je Spur
   zurück. Der Faktor gilt jetzt **je Spur**: der bewährte kommt zuerst und genügt
   sich selbst, ein anderer muss sich mit mindestens vier Adressmarken ausweisen
   (eine einzelne Scheinmarke darf ihn nicht umwerfen — genau daran hing der frühere
   Schaden, s. `TrackSync::completeRead`). Dasselbe gilt am echten Laufwerk.

2. **Die halbe Datenrate war nirgends aufgehoben.** Sie steht jetzt als
   `TrackImage::cell_factor` an der Spur: beim Laden wird der Zellstrom
   heruntergerechnet (`BitCodec::downsampleCells`), beim Zurückschreiben wieder
   gestreckt (`BitCodec::upsampleCells`) — in die Datei wie auf die echte Diskette.
   Ohne das ginge die Bootspur mit **doppelter** Rate auf die Scheibe und wäre für den
   A7100 unlesbar. `HfeCodec::save` bemisst die Spurlänge deshalb in **Zellen**, nicht
   in Bytes ×2; sonst bekäme eine halbrate Spur nur die halbe Umdrehung.

3. **Eine überabgetastete Aufnahme ist nicht dasselbe wie eine gemischte Diskette.**
   „Überabgetastet" (und damit nicht treu zurückschreibbar) heißt jetzt: es gibt
   **keine** Spur mit nominaler Rate. Eine SCP1700-Diskette ist eine gewöhnliche
   Datei und bleibt beschreibbar.

Nebenbefund derselben Arbeit: der FM-Dekoder begann die Spur exakt am ersten
Markenbyte und warf dessen Sync-Feld weg. Beim **zweiten** Rundlauf durch die Datei
fehlte das 00-Sync-Feld, das `strong()` vor der Marke verlangt — der erste Sektor
verschwand (eine frisch angelegte Diskette hatte 15 statt 16 Sektoren, IDs 2…16).
Er geht jetzt vor die Marke zurück, solange dort Sync- und Gap-Bytes stehen.
Wächter: `HfeCodec.FmSpurMitHalberRate_UeberlebtDenRundlauf`.

## 5 Stand

* **Lesen und Schreiben** über `k1520disktool` (Datei und `--physical`), Oberfläche
  inbegriffen — das Dateisystem ist gewöhnliches CP/M.
* **Anlegen**: `k1520disktool create <datei> --fs scp1700` erzeugt die Diskette mit
  korrekter FM-Bootspur (16×128, halbe Rate).
* **Am echten Laufwerk nachgewiesen** (2026-08-18): Verzeichnis über den eigenen
  Greaseweazle-Pfad gelesen, Dateien byteweise gleich wie aus der Abbilddatei.
* **Offen**: das Bootabbild der Referenzdiskette lässt sich nicht herausschreiben —
  ihr Bootspur-Sektor 10 ist beschädigt (§1.1). Ob eine so erzeugte Diskette im A7100
  **bootet**, ist nicht geprüft (kein Gerät zur Hand); der Emulator kennt keinen 8086.

## 6 Wächter

| Test | prüft |
|---|---|
| `Scp1700.WirdOhneVorgabeErkannt` | Erkennung ohne `--fs` |
| `Scp1700.BootspurIstFmMitHalberDatenrate` | FM + `cell_factor == 2`, MFM daneben |
| `Scp1700.BootspurZaehltVerschiedeneSektorenNichtDoppelte` | Doppelgänger am Spurende |
| `Scp1700.VerzeichnisUndInhaltStimmen` | 46 Dateien, Größen, Textinhalt, 2 KB frei |
| `Scp1700.SchreibenUndZurueckschreibenBleibtLesbar` | Schreiben → Speichern → Öffnen |
| `HfeCodec.FmSpurMitHalberRate_UeberlebtDenRundlauf` | zwei Rundläufe durch die Datei |
| `FormatCatalog.Formatnamen_SindEinStabilerVertrag` | `scp1700_640` im Katalog |
| `FsCatalog.ProfilnamenSindEinStabilerVertrag` | `scp1700` im Katalog |

Fixture: `tests/fixtures/disks/scp1700_640k_a7100_system.hfe` (die Referenzaufnahme).
