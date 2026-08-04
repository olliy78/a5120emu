# UDOS-Diskettenformat und Dateisystem (ZDOS)

**Stand:** 2026-08-04 · Referenzdatenträger `disks/udos_boot_scp.hfe` (UDOS 4.3,
Datenträgername `UDOS.SYS.4.3`, 69 Dateien)

Dieses Dokument beschreibt das Diskettenformat von **UDOS 1526 / 4.x** mit dem
Standardtreiber **ZDOS** so vollständig, dass sich damit unter Linux ein Werkzeug zum
Auflisten, Extrahieren, Einfügen und Löschen von Dateien bauen lässt.

**Alle Angaben sind gemessen, nicht abgeleitet.** Die Datenbasis:

1. die **echte Diskette** (`disks/udos_boot_scp.hfe`, von Hardware eingelesen, 0 CRC-Fehler),
2. das **laufende UDOS im Emulator** (`CAT`, `EXTRACT`, `STATUS` als Sollwerte),
3. die **Originalquellen** in `~/projects/UDOS/` (v. a. `UDOS_/PC1715/FORMATPC.MAC` — das
   Formatierprogramm benennt die Systemspuren im Klartext — und `UDOS_/UDINI.MAC`),
4. die **mitgelieferte Systemdokumentation** auf der Diskette selbst
   (`HELP.DAT.00`–`04`) — sie definiert Dateitypen und Eigenschaften wörtlich.

Wie das im Einzelnen geprüft wurde, steht in [§11](#11-validierung).

> ### ⚠ Das Wichtigste zuerst: `.img` reicht nicht
>
> Die Verkettung der Dateien steht **nicht im Sektordatenfeld**, sondern in den
> **4 Bytes unmittelbar hinter der Daten-CRC**, also im Zwischenraum (Gap) zwischen
> zwei Sektoren. Ein gewöhnliches Sektorabbild (`.img`, das nur die 128 Nutzbytes je
> Sektor speichert) **verliert das Dateisystem vollständig** — aus so einem Abbild lässt
> sich keine Datei mehr rekonstruieren.
>
> Ein UDOS-Werkzeug muss deshalb auf einem **spurbasierten Format** arbeiten (`.hfe`,
> flussnahe Formate) oder die Kontrollblöcke in einer eigenen Seitendatei führen.

---

## 1. Physische Ebene

| Eigenschaft | Wert (Referenzdatenträger) |
|---|---|
| Aufzeichnung | **MFM**, Standard-IBM-Feldaufbau (`A1 A1 A1 FE` IDAM / `A1 A1 A1 FB` DAM) |
| Sektorgröße | **128 Byte** (`size`-Code 0 im IDAM) |
| Sektoren je Spur | **26**, Sektor-IDs 1…26, physisch in ID-Reihenfolge (kein Hardware-Interleave) |
| Spuren | 0…76 nutzbar (77 Spuren); die Karte reserviert Platz für 78 (`0x4E`) |
| Seiten | 2 — aber **jede Seite ist ein eigenes Dateisystem** (s. §2) |
| Daten-CRC | Standard IBM-CCITT über `A1 A1 A1 FB` + Daten |

UDOS 4.x kann laut Systemdokumentation auch 256/512/1024-Byte-Sektoren und
40/77/80-Spur-Laufwerke (`SET DISKCON=`); dieses Dokument beschreibt die hier gemessene
Variante `41` (5,25″, 80 Spuren SS, 128 Byte). Die logischen Strukturen sind davon
unabhängig, die festen Spurnummern in §3 sind es **nicht** notwendigerweise.

### 1.1 Der Sektorkontrollblock — das Herzstück

Direkt hinter der Daten-CRC stehen **4 zusätzliche Bytes**:

```
A1 A1 A1 FB │ 128 Datenbytes │ CRC CRC │ bb bb ff ff │ Gap…
                                          └─ Sektorkontrollblock ─┘
```

* `bb bb` = **Rückwärtszeiger** (voriger Satz)
* `ff ff` = **Vorwärtszeiger** (nächster Satz), `FF FF` = Dateiende

Damit spart UDOS sich eine FAT und kann Dateien trotzdem fragmentieren. Ein Beispiel
aus der Rohspur 22 (Kopfsektor der Datei `DIRECTORY`):

```
A1 A1 A1 FE  16 00 01 00  D6 13        ← IDAM: Spur 22, Kopf 0, Sektor 1, 128 B
4E …                                    ← Gap 2
A1 A1 A1 FB  00 00 00 00 00 00 05 16 …  ← DAM + 128 Datenbytes
3D CC                                   ← Daten-CRC
05 16 05 16                             ← Kontrollblock: zurück (5,22), vor (5,22)
41 F2 12 12 12 …                        ← Gap 3
```

> **Genau 4 Bytes, keine CRC.** Der ZVE2-Lesecoroutine holt sie mit `LD BC,0416H / INIR`
> — vier Bytes über Port `16H`. Die Folgebytes (`41 F2 …`) gehören zum Gap: sie stehen
> auch hinter **nie beschriebenen** Sektoren einer frisch formatierten Spur
> (dort lautet der Kontrollblock `4E 4E 4E 4E`, danach identisch `41 F2 12 12 …`).
> Über den ganzen Datenträger ist das erste dieser Bytes in 4003 von 4057 Sektoren
> konstant `0x41` — eine CRC wäre das nicht.
>
> `UDOS_/UDINI.MAC` beschreibt einen **6**-Byte-Kontrollblock (`DW backptr, foreptr, CRC`)
> mit einer CRC-CCITT-Variante (Vorbesetzung `30B2H`). Auf diesem Datenträger ist keine
> Vorbesetzung und keine Abdeckung gefunden worden, die zu den gespeicherten Bytes passt
> (65536 Vorbesetzungen × beide Byte-Reihenfolgen durchprobiert, kein Treffer; zudem
> tragen Sektoren mit **identischen** Zeigern **verschiedene** Folgebytes). `UDINI.MAC`
> ist ein SCPX-Hilfsprogramm und beschreibt offenbar eine andere/ältere Ausprägung.
> **Für ZDOS 4.3 gilt: 4 Bytes.**

### 1.2 Zeigerformat

Ein Zeiger ist **2 Byte**:

| Byte | Bedeutung |
|---|---|
| 0 | **Sektorindex, 0-basiert** (`0` = Sektor-ID 1, … `25` = Sektor-ID 26) |
| 1 | **Spurnummer**, 0-basiert |

`FF FF` = Ende der Kette. Beispiel: `08 15` → Spur `0x15` = 21, Sektorindex 8 = **Sektor-ID 9**.

⚠ Der Zeiger enthält **keine Seitenangabe** — er kann nur innerhalb des eigenen
Dateisystems zeigen.

---

## 2. Seiten sind getrennte Dateisysteme

Beide physischen Seiten tragen einen vollständigen, unabhängigen Datenträger:
eigene Belegungskarte, eigenes Verzeichnis, eigener Datenträgername. UDOS spricht sie als
**getrennte Laufwerke** an — Seite 0 als Laufwerk `0`, Seite 1 als Laufwerk `4`
(allgemein: Laufwerk *n* = Vorderseite, Laufwerk *n*+4 = Rückseite).

Das bestätigt das laufende System:

```
DRIVE 0   UDOS.SYS.4.3        DRIVE 4   UDOS.SYS.4.3
1152 SECTORS USED              692 SECTORS USED
 850 SECTORS AVAILABLE        1310 SECTORS AVAILABLE
```

Beachte: die Sektor-IDAMs beider Seiten tragen **`head = 0`**. Die Seite ergibt sich
allein aus der Kopfwahl, nicht aus dem Sektorkopf.

---

## 3. Flächenaufteilung

`UDOS_/PC1715/FORMATPC.MAC` benennt die Systemspuren direkt:

```asm
CP  15H   ; BOOTSSPUR
CP  16H   ; DIRECTORYSPUR
CP  17H   ; BITMAPSPUR
```

Auf dem Referenzdatenträger sind **auf beiden Seiten identisch** genau 44 Sektoren
außerhalb jeder Datei belegt:

| Spur | Sektoren | Inhalt |
|---:|---|---|
| 0 | 1–3 | **Urlader Spur 0** — beginnt mit `'SYL' 04 00 0D 00 10` (Kennung, Länge `0D00`, Ladeadresse `1000`); entspricht `UDOS_/SYL0BC43.MAC` |
| 1 | 1–6 | **Urlader Spur 1** — `'SYL' 00 10 00 0D` (Wortfolge vertauscht); entspricht `UDOS_/SYL1BC43.MAC` |
| 2 | 1–26 | **POSINIT + residenter Kern**, das 0x0D00 Byte große Nutzabbild (`31 00 0D 3E FF D3 13 …` = `UDOS_/UDBC43PO.MAC`) |
| **21** (`15H`) | 17–22 | **Bootabbild** `BOOT FO UDOS V4 MP 850215` — genau das, was der UDOS-Monitor mit seinem `O`-Kommando lädt (Spur 21, Sektor 17, 700 Byte nach `0x4000`, Signatur `BO` bei `0x4002`) |
| **22** (`16H`) | ganze Spur | **Verzeichnisspur** — enthält die Datei `DIRECTORY` und die Kopfsektoren der zuerst angelegten Dateien |
| **23** (`17H`) | 1–3 | **Belegungskarte** (§4) |

Alle übrigen Sektoren sind normaler Datenbereich.

---

## 4. Belegungskarte (Spur 23, Sektoren 1–3)

Drei physisch aufeinanderfolgende Sektoren = 384 Byte:

```
+000 … +023   Datenträgername, 24 Byte, mit 0x0D aufgefüllt      "UDOS.SYS.4.3\r\r\r…"
+024 … +335   78 × 4 Byte Belegung, ein Eintrag je Spur 0…77
+336 … +346   11 × 0x33          ┐
+347          0xF7               │ konstanter „Nachlauf" (so schreibt ihn FORMATPC.MAC:
+348 … +374   27 × 0x77          ┘  LOMP1 B=0Bh/C=33h, 0F7h, LOMP2 B=1Bh/C=77h)
+375, +376    16 Bit LE — „belegt"-Zähler, s. Warnung unten
+377          0x00
+378          Sektoren je Spur                                    0x1A = 26
+379          Anzahl Spuren                                       0x4D = 77
+380, +381    16 Bit LE — **freie Sektoren**                      850
+382, +383    0x00 0x00
```

### 4.1 Bitbelegung eines Spureintrags

4 Byte = 32 Bit, **MSB zuerst**: Bit 31 (Byte 0, Bit 7) = Sektor-ID 1, … Bit 6
(Byte 3, Bit 6) = Sektor-ID 26. **Bit gesetzt = belegt.** Die 6 überzähligen Bits
(Byte 3, Bit 5…0) sind immer gesetzt.

```
00 00 00 3F   → Spur vollständig frei
FF FF FF FF   → Spur vollständig belegt (bzw. gesperrt/nicht vorhanden)
E0 00 00 3F   → Sektoren 1,2,3 belegt, Rest frei      (Spur 0)
FC 00 FF 3F   → Sektoren 1–6 und 17–24 belegt         (Spur 1)
```

Spuren ≥ „Anzahl Spuren" trägt der Formatierer als `FF FF FF FF` ein (gesperrt); auf dem
Referenzdatenträger ist das genau Spur 77.

### 4.2 ⚠ Den Zählern nicht trauen — die Karte ist die Wahrheit

Der Freizähler bei +380 stimmt (850 = eigene Auszählung = `STATUS`-Ausgabe). Der Zähler
bei +375 aber **nicht**: er ergibt zusammen mit dem Freizähler stets die Konstante
**2464 = `0x09A0`** (beide Seiten), die auch als Festwert in `FORMATPC.MAC` steht —
und passt damit nicht zur tatsächlichen Kapazität von 77 × 26 = 2002 Sektoren.

`STATUS` rechnet selbst: `belegt = Spuren × Sektoren_je_Spur − frei`. Ein Werkzeug sollte
**immer aus den Bits auszählen** und beim Schreiben beide Zähler konsistent nachführen.

---

## 5. Das Verzeichnis

Das Verzeichnis ist **eine gewöhnliche Datei** namens `DIRECTORY` vom Typ `D`. Ihr
Kopfsektor liegt auf **Spur 22, Sektor 1** (Zeiger `00 16`). Das ist der einzige feste
Einstiegspunkt des Dateisystems.

Auf den Datensätzen der Datei folgen dicht gepackte Einträge variabler Länge:

```
┌────────┬──────────────┬──────────┬──────────┐
│ Flag/  │ Name         │ Sektor-  │ Spur     │
│ Länge  │ (n Zeichen)  │ index    │          │
└────────┴──────────────┴──────────┴──────────┘
   1 Byte     n Bytes      1 Byte     1 Byte
```

* **Bit 0…5** des ersten Bytes = **Namenslänge** (Namen bis 32 Zeichen).
* **Bit 7** = **SECRET** (geheim). Nachgewiesen: `CAT` ohne `P=&` listet **exakt** die
  Einträge mit gelöschtem Bit 7 (39 von 69 Dateien) — es ist eine im Verzeichnis
  gespiegelte Kopie der `S`-Eigenschaft aus dem Dateikopf, damit `CAT` filtern kann,
  ohne jeden Kopfsektor zu lesen.
* **Bit 6** kam auf diesem Datenträger nicht vor.
* Die letzten 2 Bytes sind der **Zeiger auf den Kopfsektor** der Datei (Format §1.2).

Eine Eintragsliste endet mit dem Byte **`0xFF`**; der Rest des 128-Byte-Sektors ist
Altbestand und **muss ignoriert werden** (dort stehen Reste gelöschter Einträge). Die
Auswertung beginnt in **jedem** Datensatz der Datei `DIRECTORY` neu bei Offset 0.

Beispiel (Spur 22, Sektor 6, die ersten Bytes):

```
89 'DIRECTORY' 00 16    → geheim, 9 Zeichen, Kopf bei Spur 22 Sektor  1
82 'DO'        0C 16    → geheim, 2 Zeichen, Kopf bei Spur 22 Sektor 13
84 'MOVE'      0D 16
…
06 'DELETE'    18 14    → nicht geheim, Kopf bei Spur 20 Sektor 25
FF                      → Ende der Liste in diesem Sektor
```

---

## 6. Der Dateikopfsektor

Jede Datei beginnt mit **einem** 128-Byte-Kopfsektor (auch wenn ihre Sätze länger sind).
Die belegten Felder:

| Offset | Größe | Feld | Anmerkung |
|---:|---|---|---|
| 0…5 | 6 | `00 00 00 00 00 00` | reserviert |
| 6…7 | 2 | Zeiger auf den **Verzeichnissektor** mit dem eigenen Eintrag | = Rückwärtszeiger des Kopfsektors |
| 8…9 | 2 | Zeiger auf den **ersten Satz** | = Vorwärtszeiger des Kopfsektors |
| 10…11 | 2 | Zeiger auf den **letzten Satz** | |
| 12 | 1 | **Dateityp** | `80`=P, `81`=P1, `40`=D, `20`=A, `10`=B |
| 13…14 | 2 LE | **Satzanzahl** (RECORD COUNT) | |
| 15…16 | 2 LE | **Satzlänge** in Byte (RECORD LENGTH) | 0080/0100/0200/0400 beobachtet |
| 17…18 | 2 LE | meist Kopie der Satzlänge; bei `OS` = `0000` | Bedeutung nicht geklärt |
| 19 | 1 | **Eigenschaften** | `80`=W, `40`=E, `20`=L, `10`=S |
| 20…21 | 2 LE | **Einsprungadresse** (ENTRY) | nur bei Typ P/P1 ausgewertet |
| 22…23 | 2 LE | **Bytes im letzten Satz** | `0000` = letzter Satz leer |
| 24…29 | 6 | **Erstellung**: `JJMMTT` **oder** Versionstext (`V 4.3 `) | ASCII, frei wählbar |
| 30…31 | 2 | `FF 00` | Trenner |
| 32…37 | 6 | **Letzte Änderung**: `JJMMTT` | ASCII; UDOS überschreibt das Feld bei jeder Änderung mit dem Systemdatum (nachgewiesen: `900808` → `880315` nach einem `SET`) |
| 38…39 | 2 | `FF 00` bzw. `FF FF` | Trenner |
| 40…41 | 2 LE | Anfang des 1. **Speichersegments** | |
| 42…43 | 2 LE | Länge des 1. Speichersegments | weitere Segmente folgen paarweise |
| ab 48 | | `FF`-Füllung | |

Nachgeprüft an `EXTRACT` im laufenden System, z. B.

```
%EXTRACT SD
RECORD COUNT=0007 RECORD LENGTH=0080 BYTES IN LAST RECORD=0080
ENTRY=E800 LOW ADDRESS=E800 HIGH ADDRESS=EB7F STACK SIZE=0000
SEGMENTS:
E800 EB79
```
gegen den Kopfsektor von `SD` (Spur 31, Sektor 9):
`… 81 07 00 │ 80 00 │ 80 00 │ 00 │ 00 E8 │ 80 00 │ '900517' FF 00 │ '900808' FF 00 │ 00 E8 7A 03 …`
→ Typ `81`=P1, 7 Sätze, Satzlänge `0080`, ENTRY `E800`, letzter Satz `0080` Byte,
Segment `E800` + `037A` = `E800…EB79`. ✔

*Nicht* eindeutig zugeordnet sind `HIGH ADDRESS` und `STACK SIZE`; ein Datei-Werkzeug
braucht sie nicht.

### 6.1 Dateitypen (wörtlich aus `HELP.DAT.00` der Diskette)

| Typ | Byte | Bedeutung |
|---|---|---|
| `A` | `20` | ASCII — Textdatei |
| `P` | `80` | PROCEDURE — ausführbares Programm |
| `P1` | `81` | Procedure, Untertyp 1 (aktivierbare Treiber/Module, z. B. `ZDOS`, `SD`) |
| `B` | `10` | BINARY |
| `D` | `40` | DIRECTORY |

Der Typ ist **nicht** Teil des Namens (anders als bei SCP/CP/M).

### 6.2 Eigenschaften (PROPS, wörtlich aus `HELP.DAT.00`)

| Bit in Offset 19 | Buchstabe | Bedeutung |
|---|---|---|
| `0x80` | `W` | WRITE PROTECTED — schreibgeschützt |
| `0x40` | `E` | ERASE PROTECTED — löschgeschützt |
| `0x20` | `L` | LOCKED — Eigenschaften nicht änderbar |
| `0x10` | `S` | SECRET — geheim (wird ohne `P=&` nicht gelistet) |
| `0x08` | `R` | RANDOM — wahlfreier Zugriff |
| `0x04` | `F` | FORCE MEMORY ALLOCATION |
| — | `&` | (kein Bit, sondern der Suchoperator „alle Eigenschaften") |

Die vier oberen Bits sind über die Kombinationen `WELS`/`WES`/`WS` eindeutig belegt.
`R` und `F` trägt keine Datei des Referenzdatenträgers; ihre Bitlage wurde durch
**Setzen im laufenden System** bestimmt (`SET PROPERTIES OF CODE TO R` → Offset 19 von
`00` auf `08`; `SET PROPERTIES OF TAST TO F` → `00` auf `04`).

---

## 7. Sätze, Verkettung und Belegung

**Ein Satz („Record") ist die Zuteilungseinheit, nicht der Sektor.**

* Ein Satz belegt `Satzlänge / 128` **physisch aufeinanderfolgende Sektoren derselben
  Spur** (aufsteigende Sektor-ID).
* **Alle** Sektoren eines Satzes tragen **denselben** Kontrollblock; die Zeiger adressieren
  jeweils den **ersten Sektor** des Vorgänger- bzw. Nachfolgesatzes.
* Ein Satz **überschreitet nie eine Spurgrenze** (über alle 69 Dateien geprüft: 0 Verstöße).
* Der Kopfsektor zählt als eigenes Glied: sein Vorwärtszeiger zeigt auf den 1. Satz, sein
  Rückwärtszeiger auf den Verzeichnissektor.
* Der letzte Satz endet mit `FF FF`.

Beispiel `CAT` (4 Sätze à 1024 Byte = 4096 Byte):

```
Kopf    Spur 22 Sektor 18   zurück (5,22)=Verzeichnis   vor (8,21)
Satz 1  Spur 21 Sektor  9…16   ← 8 Sektoren, alle mit  zurück (17,22)  vor (16,20)
Satz 2  Spur 20 Sektor 17…24                           zurück ( 8,21)  vor ( 0,20)
Satz 3  Spur 20 Sektor  1… 8                           zurück (16,20)  vor ( 8,20)
Satz 4  Spur 20 Sektor  9…16                           zurück ( 0,20)  vor  FF FF
```

Bei Satzlänge 128 (ein Sektor je Satz) ergibt die Verkettung typisch einen
**Interleave von 5** (Sektor 1 → 6 → 11 → 16 → 21 → 26 → 2 → 7 → …), so wie die
Verzeichnisdatei selbst auf Spur 22 liegt.

### 7.1 Dateilänge

```
Länge = (Satzanzahl − 1) × Satzlänge + (Bytes_im_letzten_Satz oder Satzlänge, falls 0…)
```
Konkret: ist `Bytes im letzten Satz` kleiner als die Satzlänge (und ≠ 0), wird der letzte
Satz entsprechend gekürzt. Bei `0000` ist der letzte Satz vollständig ungenutzt bzw. leer —
`NOTE.TO.SD` (16 Sätze, `0000`) wird von UDOS mit 16 vollen Sätzen geführt.

> Für Typ `A` (Text) gilt zusätzlich: **Zeilenende ist `CR` (0x0D)**, Dateiende `0x1A`.

---

## 8. Algorithmen für ein Linux-Werkzeug

### 8.1 Einlesen des Datenträgers

```
für jeden Zylinder c, jede Seite h:
    Spur dekodieren (MFM)
    für jeden Sektor: (ID, 128 Datenbytes, 4 Bytes hinter der Daten-CRC)
    → Index  sek[seite][spur][id] = (daten, kontrollblock)
```
Im Projekt erledigt das `HfeImage::readTrack()` + `TrackCodec::parseTrack()`;
`LogicalSector::tail` liefert die Bytes hinter der CRC (die ersten 4 sind der
Kontrollblock, `kSectorTailBytes` = 8).

### 8.2 Auflisten

```
belegung  = lies_karte(seite)                 # Spur 23, Sektoren 1..3
verzeichnis = lies_datei(seite, sektor=0, spur=22)
für jeden Datensatz von verzeichnis:
    i = 0
    solange i < 128:
        f = satz[i];  wenn f == 0xFF: nächster Datensatz
        n = f & 0x3F
        name  = satz[i+1 : i+1+n]
        zeiger = (satz[i+1+n], satz[i+2+n])
        geheim = (f & 0x80) != 0
        kopf  = lies_sektor(zeiger)           # Metadaten nach §6
        i += 3 + n
```

### 8.3 Datei lesen

```
kopf   = sektor(zeiger)
satzlen = LE16(kopf[15:17]);  n = max(1, satzlen // 128)
anzahl  = LE16(kopf[13:15]);  rest = LE16(kopf[22:24])
(si, sp) = kopf.kontrollblock.vorwaerts
inhalt = b""
solange (si, sp) != (0xFF, 0xFF):
    für k in 0..n-1:  inhalt += sektor(seite, sp, si+1+k).daten
    (si, sp) = sektor(seite, sp, si+1).kontrollblock.vorwaerts
wenn 0 < rest < satzlen:  inhalt = inhalt[: -(satzlen - rest)]
```

### 8.4 Datei schreiben (Einfügen)

1. **Satzlänge wählen** (128 ist immer sicher; größere Sätze sparen Verwaltung, brauchen
   aber `satzlen/128` *zusammenhängende freie* Sektoren in **einer** Spur).
2. **Platz suchen**: in der Belegungskarte je Spur einen Block passender Größe suchen.
   Ein Kopfsektor braucht genau 1 Sektor.
3. **Sektoren schreiben**: Daten + Daten-CRC + 4-Byte-Kontrollblock. Die Kette
   rückwärts/vorwärts konsistent setzen; letzter Satz `FF FF`; Kopfsektor rückwärts auf
   den Verzeichnissektor, in den der Eintrag kommt.
4. **Kopfsektor** nach §6 füllen (Typ, Satzanzahl, Satzlänge, Bytes im letzten Satz,
   Datum `JJMMTT`, ggf. ENTRY/Segmente).
5. **Verzeichniseintrag** anhängen: im letzten Datensatz der Datei `DIRECTORY` vor dem
   `0xFF` einfügen. Passt er nicht mehr in 128 Byte, muss die Datei `DIRECTORY` selbst um
   einen Satz wachsen (neuer Sektor, Kette und Satzanzahl im eigenen Kopf nachführen).
6. **Belegungskarte** aktualisieren: Bits setzen, Freizähler bei +380 dekrementieren,
   Zähler bei +375 auf `2464 − frei` nachführen (so hält es der Originalformatierer).

### 8.5 Löschen

Verzeichniseintrag ausschneiden (nachfolgende Einträge nach vorn schieben, `0xFF`
nachziehen) und die Bits aller Sektoren der Datei inklusive Kopfsektor in der
Belegungskarte zurücksetzen. Die Kontrollblöcke auf dem Medium bleiben stehen — deshalb
darf ein Werkzeug **niemals** aus „Kontrollblock ≠ `4E 4E 4E 4E`" auf „belegt" schließen
(auf dem Referenzdatenträger sind so 364 längst freigegebene Sektoren als scheinbar belegt
zu sehen). **Maßgeblich ist allein die Belegungskarte.**

### 8.6 Was nicht angetastet werden darf

Die Systembereiche aus §3 (44 Sektoren) und die Spuren 21–23 insgesamt. Wer eine
bootfähige Diskette erzeugen will, muss zusätzlich Spur 0/1/2 und das Bootabbild auf
Spur 21 mitschreiben.

---

## 9. Namen und Datumsangaben

* Namen bis **32 Zeichen**, Buchstaben, Ziffern und **Punkte** (`NOTE.TO.UDOS.4.3`,
  `HELP.DAT.00`). Der Punkt ist ein normales Namenszeichen, keine Typtrennung.
* Groß-/Kleinschreibung ist signifikant; Kommandos stehen praktisch immer in Großschrift.
* Datumsfelder sind **6 ASCII-Zeichen `JJMMTT`** (`900808` = 8.8.1990). Statt eines Datums
  kann im Erstellungsfeld ein **Versionstext** stehen (`V 4.3 `) — ein Werkzeug muss beides
  vertragen und darf nicht auf Ziffern bestehen.

---

## 10. Vollständiges Beispiel (Referenzdatenträger)

```
Seite 0 = Laufwerk 0 ─ „UDOS.SYS.4.3"
├── Spur 23 S1..3  Belegungskarte: 77 Spuren × 26 Sektoren, 850 frei
├── Spur 22 S1     DIRECTORY (Typ D, 10 Sätze × 128 B)
│   └── Kette S1 → S6 → S11 → S16 → S21 → S26 → S2 → S7 → S12 → S17 → S22
│       Einträge: DIRECTORY, DO, MOVE, ACTIVATE, CAT, … (48 Dateien)
├── Spur 22 S18    CAT   (Typ P, 4 × 1024 B, ENTRY 4000, PROPS WS, 791019/900808)
│   └── Sätze: 21/9-16, 20/17-24, 20/1-8, 20/9-16          → 4096 Byte
└── …
Seite 1 = Laufwerk 4 ─ „UDOS.SYS.4.3"  (21 Dateien, u. a. HELP.DAT.00…04)
```

---

## 11. Validierung

Jede Aussage dieses Dokuments ist gegen mindestens zwei unabhängige Quellen geprüft.

| Prüfung | Ergebnis |
|---|---|
| Alle 69 Dateien extrahiert, Länge gegen die Kopfmetadaten | **69/69 exakt**, keine Abweichung |
| Extrahierte Datei `OS` gegen den Nukleus im RAM des gebooteten Systems (`dump 0x1000 5504`) | **5233 von 5504 Byte gleich (95,1 %)**; die 271 Unterschiede beginnen bei Offset 12 und sind die zur Laufzeit veränderten Variablen |
| Verzeichnis-Bit 7 gegen `CAT` ohne `P=&` | **exakte Übereinstimmung** (39 gelistete Dateien = alle Einträge mit gelöschtem Bit 7) |
| Kopffelder gegen `EXTRACT` (Satzanzahl/-länge/Bytes im letzten Satz/ENTRY) | stimmt für alle geprüften Dateien, inkl. Teilsätze (`OS` 0180, `ASM` 0200, `HELP.DAT.00` 003F, `NOTE.TO.SD` 0000) |
| Belegungskarte ausgezählt gegen den gespeicherten Freizähler | **850 = 850** (Seite 0), **1310 = 1310** (Seite 1) |
| dieselbe gegen `STATUS` im laufenden System | **850 / 1310 frei** ✔, belegt 1152 / 692 = 77·26 − frei ✔ |
| Jeder Dateisektor in der Belegungskarte als belegt markiert | **0 Verstöße** (Seite 0 und 1) |
| Satz überschreitet Spurgrenze | **0 Verstöße** über alle 69 Dateien |
| Systembereiche = belegte Sektoren minus Dateisektoren | **44 auf beiden Seiten**, identische Lage |
| Spurnamen aus `FORMATPC.MAC` (`BOOTSSPUR 15H`, `DIRECTORYSPUR 16H`, `BITMAPSPUR 17H`) | decken sich mit dem Fundort auf dem Medium |
| Kartenaufbau (24-Byte-Name, 4 Byte/Spur, Nachlauf `0x33`/`0xF7`/`0x77`, `1A`/Spurzahl) | Byte für Byte wie in `FORMATPC.MAC` erzeugt |

### 11.1 Rezepte zum Nachvollziehen

Sektoren samt Kontrollblock ausgeben (Werkzeug in wenigen Zeilen über die
Projektbibliothek):

```cpp
HfeImage img(pfad, /*write_protect=*/true);
TrackImage t = img.readTrack(cyl, head);
for (auto& s : TrackCodec::parseTrack(t))
    /* s.id, s.data, s.tail[0..3] = Kontrollblock */;
```

Sollwerte aus dem laufenden UDOS holen (Konsolstrom mitschneiden statt den Bildschirm
scrollen zu lassen — `0x0BEB` ist der `OUTA`-Vektor):

```sh
cat > /tmp/u.dbg <<'EOF'
gscreen "Neues Datum" 60000000
g 2000000
keys 150388
g 8000000
logpoint 0x0BEB A
keys cat\s*\sp=&\sf=l\r
g 200000000
q
EOF
tools/dev.sh tool k1520dbg disks/udos_boot_scp.hfe -x /tmp/u.dbg \
  | rg -o 'A=([0-9]+)\(' -r '$1' \
  | python3 -c "import sys;print(''.join(chr(int(l)) for l in sys.stdin))"
```

⚠ **Die Tastatur ist invertiert** — Kommandos im Debugger *klein* tippen, UDOS macht
daraus Großschrift (`doc/analyse_udos.md` §14.2).

---

## 12. Schreiben und Formatieren im Emulator (Stand 2026-08-04: ✅)

Beim Prüfen des Schreibmodells aus §8 sind **zwei Emulatorfehler** aufgefallen; beide
sind behoben, Schreibzugriffe und `FORMAT` funktionieren.

1. **Sektorkontrollblock ging beim Schreiben verloren** — ein einziger Schreibzugriff
   (`SET PROPERTIES …`, `COPY`, `MOVE` …) ersetzte **alle 26 Kontrollblöcke der
   betroffenen Spur** durch Gap-Füllbytes (`ERROR CA = POINTER CHECK ERROR`). Ursache:
   `TrackCodec::buildTrack()` gab `LogicalSector::tail` nicht aus, und
   `K5122::commitWriteField()` verwarf den neu geschriebenen Block — das Spiegelbild
   des in `doc/analyse_udos.md` §13.4 behobenen Lesefehlers.
   Volle Beschreibung, Messwerte und Regressionsliste: **`doc/udos_bug1.md`**.
2. **Laufwerksauswahl nibbelvertauscht** (Port 18H des K5122) — UDOS bildet sein
   Anwahlbyte mit `AND 0F0H` (`0xD0` = Laufwerk 1); der Emulator las das High-Nibble
   als Motor statt als Select und landete auf Laufwerk 0. `FORMAT` beschrieb damit B:
   und verifizierte anschließend A: → „DEFEKTIVE TRACK" auf jeder Spur, am Ende
   `NOT FOR UDOS USEABLE`. Details: `doc/design/07_k5122_afs.md` §8.

**Verifiziert am laufenden System** (Boot-Diskette auf A:, Leerdiskette auf B:):

```
%FORMAT
SYSTEMDISK? N        DRIVE? 1        ID? TESTDISK        READY? Y
%STATUS
DRIVE 1   TESTDISK      14 SECTORS USED    1988 SECTORS AVAILABLE
%MOVE CAT S=0 D=1 P=&
%CAT D=1 F=L P=&
DIRECTORY            1  D    10  0080  WELS
CAT                  1  P     4  0400  WS   4000  791019
```

77 Spuren à 26 × 128 B, Belegungskarte auf Spur 23 mit dem 24-Byte-Datenträgernamen und
`00 00 00 3F` je freier Spur (§4), Verzeichnis auf Spur 22 mit Interleave 5 — alles
genau nach diesem Dokument. `1988 = 77·26 − 14` bestätigt die Systembereiche aus §3.

> **Rückseite nicht vergessen.** `FORMAT` fragt nur nach Laufwerk 0…3 und formatiert
> bei einseitiger `SET DISKCON`-Einstellung (`41`) nur Seite 0. Steckt in dem Laufwerk
> eine physisch zweiseitige Diskette, meldet UDOS beim Start für das zugehörige
> Rückseiten-Laufwerk (*n*+4, §2) folgerichtig `DISK INITIALIZATION ERROR C8` — dort
> steht eben kein UDOS-Dateisystem. Mit einer beidseitig UDOS-formatierten Diskette
> ist die Meldung weg und `STATUS` listet 0, 1, 4 und 5.

### 12.1 `COPY.DISK` — der schärfste Test des Schreibpfads

`COPY.DISK` (ohne Parameter: Laufwerk 0 → 1) kopiert **sektorweise**, am Dateisystem
vorbei — es reicht also genau das durch, was §1.1 beschreibt, Kontrollblock inklusive.
Ergebnis auf einer zuvor mit `FORMAT` geleerten Zieldiskette:

```
%COPY.DISK
DRIVES READY ?Y
ERROR C4 ON TRACK 33 DRIVE 00
%STATUS
DRIVE 1   UDOS.SYS.4.3    1152 SECTORS USED    850 SECTORS AVAILABLE
```

Laufwerk 1 trägt danach Kennung und Belegung der Quelle; `CAT D=1` listet die kopierten
Dateien. Der Abgleich der Abbilder (Sektordaten **und** Kontrollblock, Seite 0):
**2001 von 2001 Sektoren identisch**, 0 Abweichungen.

> ⚠ **`ERROR C4 ON TRACK 33 DRIVE 00` ist echt und kein Emulatorfehler.** Die
> Referenzdiskette `disks/udos_boot_scp.hfe` wurde von echter Hardware eingelesen und
> hat auf **Spur `0x33` = 51, Seite 0** einen physisch fehlenden Sektor (**S13**; die
> übrigen 25 sind ID- und Daten-CRC-sauber). UDOS meldet ihn und kopiert weiter — daher
> 2001 statt 2002 Sektoren. Spur- und Laufwerksnummer stehen in der Meldung **hexadezimal**.

Guards: `UdosFormat.FormatsDriveOneIntoUsableZdosDisk` und
`UdosFormat.CopyDiskDuplicatesSystemDiskSectorBySector` (`tools/dev.sh test-format`).

---

## 13. Offene Punkte

1. **`R`- und `F`-Bit** in Offset 19 des Kopfsektors — auf diesem Datenträger nicht belegt.
2. **Offset 17…18** des Kopfsektors: meist Kopie der Satzlänge, bei `OS` `0000`.
3. **`HIGH ADDRESS` / `STACK SIZE`** aus `EXTRACT` sind im Kopf nicht lokalisiert
   (für Dateizugriff irrelevant).
4. **Andere Sektorgrößen/Spurzahlen** (`SET DISKCON=` kennt 128/256/512/1024 Byte und
   40/77/80 Spuren): ob die Systemspuren 21/22/23 dann verschoben sind, ist ungeprüft.
   Ein Werkzeug sollte die Spurnummern konfigurierbar halten und die Belegungskarte über
   ihre Signatur (24-Byte-Name + `0x0D`-Füllung, danach `…3F`-Muster) verifizieren.
5. **Die 2 Bytes hinter dem Kontrollblock** (`41 F2` u. ä.) sind als Gap/Schreibnaht
   eingeordnet, nicht als CRC (§1.1) — beim Zurückschreiben auf echte Hardware wäre zu
   prüfen, ob UDOS sie erwartet.

---

## 14. Quellen

* `disks/udos_boot_scp.hfe` — der vermessene Datenträger
* `~/projects/UDOS/UDOS_/PC1715/FORMATPC.MAC` — Formatierprogramm, benennt Systemspuren
  und erzeugt die Belegungskarte
* `~/projects/UDOS/UDOS_/UDINI.MAC` — Kontrollblock-Vorlage und CRC-Routine (abweichende
  Ausprägung, s. §1.1)
* `~/projects/UDOS/UDOS/FORMAT/FOR7658.MAC`, `UDOS/DISKCOPY/DISKCOPY.MAC` — „Sektor 0 ist
  Pointersektor", UDOS-Standardformat
* `~/projects/UDOS/README.md` — Bestandsaufnahme aller UDOS-Quellen
* Systemdokumentation **auf der Diskette selbst**: `4/HELP.DAT.00…04`, `4/NOTE.TO.UDOS.4.3`
* `doc/analyse_udos.md` §13.4 (Sektorkontrollblock im Lesepfad), §14 (Interaktivbetrieb),
  §15 (Befehlsreferenz)
