# UDOS1715 / NDOS — Diskettenformat und Dateisystem

**Stand:** 2026-08-17 · Referenzdatenträger: eine am Greaseweazle eingelesene
Systemdiskette („SYSTEM", 67 Dateien, 80×32×256, 0 CRC-Fehler)

UDOS1715 ist die Ausprägung von UDOS für den **PC 1715**. Sie benutzt zur
Diskettenverwaltung nicht ZDOS, sondern **NDOS**, und das ist kein Beiwerk, sondern der
ganze Unterschied:

> Der PC 1715 hat einen **µPD765**-Floppycontroller. Der kann nur ganze IBM-Sektoren
> lesen und schreiben — die vier Bytes hinter der Daten-CRC, in denen ZDOS auf dem
> A5120 seine Dateiverkettung führt (`doc/udos_diskettenformat.md` §1.1), sind für ihn
> nicht erreichbar. NDOS legt die Verkettung deshalb **in eigene Sektoren**:
> **Zeigersektoren**.

Daraus folgt der praktisch wichtigste Unterschied zu ZDOS:

| | UDOS 4.x / ZDOS (A5120) | **UDOS1715 / NDOS (PC 1715)** |
|---|---|---|
| Sektor | 128 B + 4 B Kontrollblock **hinter der CRC** | **256 B, reines Standard-IBM** |
| Verkettung | Rück-/Vorwärtszeiger je Sektor im Gap | **Zeigersektoren** mit Adressliste |
| `.img` möglich? | **nein** (Kontrollblock ginge verloren) | **ja** — nichts steht außerhalb der Sektoren |
| Seiten | jede Seite ein eigener Datenträger | **eine Diskette = ein Dateisystem** |
| Spur | 26 Sektoren einer Seite | **32 Sektoren = beide Seiten eines Zylinders** |

**Quellen.** Maßgeblich ist das **Systemhandbuch, das auf der Diskette selbst liegt**
(Datei `UDOS.TEXT`, Typ A, 206 Sätze = 52 674 Byte) — „Universelles
Disk-Operations-System UDOS, Zentrum für Forschung und Technologie des KEAW Berlin,
*Unterschiede zum Betriebssystem UNOS bzw. UDOS 1526*". Es ist als
`doc/original_docs/UDOS1715_Systemhandbuch.txt` abgelegt; die Abschnittsnummern unten
verweisen darauf. Jede Angabe ist zusätzlich am Referenzdatenträger **nachgemessen**
(§9).

Der Quellenfundus `~/projects/UDOS/` beschreibt eine **ältere** PC-1715-Ausprägung
(`UDOS_/PC1715/*.MAC`, AMF-Floppykarte, 26×128, ZDOS-Zeiger) und passt **nicht** auf
diese Disketten. Der 765-Formatierer `UDOS/FORMAT/FOR7658.MAC` von 1992 löst dasselbe
Problem noch anders (128 B/Sektor, „Sektor 0 ist Pointersektor"). UDOS1715 ist ein
dritter, eigener Weg.

---

## 1. Physische Ebene (Handbuch §2.6.2)

| Eigenschaft | Wert |
|---|---|
| Aufzeichnung | **MFM**, IBM-34-Format (`A1 A1 A1 FE` IDAM / `A1 A1 A1 FB` DAM) |
| Sektorgröße | **256 Byte** (`size`-Code 1) |
| Sektoren je Spur und Seite | **16**, IDs **1…16**, in ID-Reihenfolge ab Index |
| Kopfnummer im ID-Feld | **echt** — 00 auf Seite 0, **01 auf Seite 1** |
| Zylinder | 0…4FH (80), Zylinder 0 außen |
| Kapazität | 2 · 16 · 80 = 2560 Sektoren = **640 KByte** |
| CRC | Standard-IBM-CCITT über ID- bzw. Datenfeld |

Das entspricht bitgenau der Geometrie `k5601_16x256` des Formatkatalogs.

### 1.1 Die Spur ist der ganze Zylinder

> „Durch Zusammenfassen der Sektoren von Vorder- und Rückseitenspuren gleicher
> Spurnummer erzeugt BFOS scheinbare Spurlängen von 32 Sektoren." (§1.1)
>
> „Für die Arbeit mit UDOS werden die Sektornummern intern nach der Beziehung
> **(Sektornummer − 1) + Kopfnummer · 16** (= 0…1FH) gebildet." (§2.6.2)

Das ist die Umrechnung, durch die jeder Zugriff geht:

```
UDOS-Sektornummer  s = 0…31        Kopf = s ≥ 16
                                   physische Sektor-ID = (s mod 16) + 1
```

Deshalb ist eine UDOS1715-Diskette **ein** Datenträger und nicht — wie bei ZDOS — zwei
voneinander unabhängige Seiten.

### 1.2 Zulässige Formate (§1.2)

| Format | Kapazität | Sektoren |
|---|---|---|
| **80 × 32 × 256** | 640 KB | 2560 — der Standard |
| 80 × 16 × 256 | 320 KB | 1280 — nur einseitig benutzbare Disketten |
| 40 × 16 × 256 | 160 KB | 640 — einfache Spurdichte (Doppelschritt) |

Bei den kleineren Formaten trägt der Formatierer die physisch nicht vorhandenen
Sektoren in der Belegungskarte **als belegt** ein; dasselbe tut er mit beim Formatieren
erkannten Schadstellen.

### 1.3 Adressen

Ein Zeiger ist **2 Byte**: `Sektornummer, Spurnummer` — dieselbe Reihenfolge wie bei
ZDOS, nur reicht die Sektornummer bis 31.

| Byte | Bedeutung |
|---|---|
| 0 | **UDOS-Sektornummer** 0…1FH (Bits 5…7 sind auf der Diskette immer 0) |
| 1 | **Spurnummer** (= Zylinder) 0…4FH |

`FF FF` heißt „keiner" (Kettenende). Die Bits 5…7 des ersten Bytes sind nicht frei: in
der *Laufzeit*-Diskadresse von `FILE.DEBUG` steht dort die **Laufwerksnummer** (§4);
auf der Diskette müssen sie 0 sein.

---

## 2. Flächenaufteilung (§1.2.1 / §1.2.2)

Auf **jeder** formatierten Diskette sind 13 Sektoren fest belegt:

| Spur | Sektor | Inhalt |
|---:|---|---|
| 16H (22) | 00 | **Descriptor der DIRECTORY** — der einzige feste Einstiegspunkt |
| 16H | 02 | **Zeigersektor der DIRECTORY** |
| 16H | 05, 0A, 0F, 04, 09, 0E, 03, 08, 0D | die 9 **Datenrecords der DIRECTORY** (Interleave 5) |
| 17H (23) | 00, 01 | **Diskettenbelegungsplan** (180H Byte, s. §3) |

Eine **Systemdiskette** belegt zusätzlich Spur 00 Sektor 00…0F (16 Sektoren) mit
**Urlader und BFOS**; damit sind es 29 feste Sektoren.

> Ein Werkzeug darf die Spuren 16H und 17H nie als freien Platz behandeln und muss auf
> einer Systemdiskette auch Spur 0 in Ruhe lassen. Die Belegungskarte sagt das ohnehin —
> aber sie ist beim Anlegen einer neuen Diskette erst noch zu schreiben.

---

## 3. Diskettenbelegungsplan (§1.3)

Zwei Sektoren, Spur 17H Sektor 00 und 01 (physisch Seite 0, IDs 1 und 2); benutzt
werden davon die ersten **180H = 384 Byte**, der Rest des zweiten Sektors ist Altbestand.

| Offset | Inhalt |
|---|---|
| `000H`–`017H` | **Diskettenname**, 24 Byte, mit `0DH` aufgefüllt |
| `018H`–`157H` | **Belegungsplan**: 80 Spuren × 4 Byte |
| `158H`–`176H` | unbenutzt (`00`) |
| `177H`/`178H` | 16 Bit LE — **belegte** Sektoren |
| `179H` | unbenutzt (`00`) |
| `17AH` | Sektoren je Spur — `20H` (32) oder `10H` (16) |
| `17BH` | Anzahl formatierter Spuren — `50H` (80) oder `28H` (40) |
| `17CH`/`17DH` | 16 Bit LE — **freie** Sektoren |
| `17EH`/`17FH` | unbenutzt |

Ein Spureintrag ist 32 Bit, **MSB zuerst**: Bit 31 (Byte 0 Bit 7) = Sektor 0, … Bit 0
(Byte 3 Bit 0) = Sektor 31. **Bit gesetzt = belegt.** Beispiele vom Referenzdatenträger:

```
Spur 16H:  BC E7 00 00   Sektoren 0,2,3,4,5 und 8,9,10,13,14,15 belegt (DIRECTORY)
Spur 17H:  C0 00 00 00   Sektoren 0,1 belegt (die Karte selbst)
Spur 4FH:  FF FF FF FF   voll
```

### 3.1 Anders als bei ZDOS: **die Zähler stimmen**

ZDOS führt bei `+375` einen Zähler, der sich zur Konstanten 2464 ergänzt und nichts mit
der Kapazität zu tun hat (`doc/udos_diskettenformat.md` §4.2). NDOS trägt dort die
**wirkliche** Zahl belegter Sektoren ein: auf dem Referenzdatenträger `177H` = 1673,
`17CH` = 887, Summe 2560 = 80 · 32 ✓, und beides deckt sich bitgenau mit der
Auszählung der Karte. Ein Werkzeug führt beide Zähler entsprechend nach — die
**Bits bleiben trotzdem die Wahrheit**.

Ebenfalls anders: der Bereich zwischen Belegungsplan und Zählern ist bei NDOS mit `00`
gefüllt, nicht mit dem ZDOS-Nachlauf `11×33H · F7H · 27×77H`. Genau daran lassen sich
die beiden Karten unterscheiden.

---

## 4. Die DIRECTORY (§1.4)

Das Verzeichnis ist eine **gewöhnliche Datei** namens `DIRECTORY`, Typ `D`,
Eigenschaften `WELS`, Satzlänge 100H. Ihr Descriptor liegt fest auf Spur 16H Sektor 00.

Jeder Datensatz enthält dicht gepackte Einträge:

```
┌──────────────┬──────────────┬───────────────────────┐
│ Bit 0-6 Länge│ Dateiname    │ Adresse des           │
│ Bit 7 SECRET │ (n Zeichen)  │ Descriptors (2 Byte)  │
└──────────────┴──────────────┴───────────────────────┘
     1 Byte         n Bytes            2 Bytes
```

* **Bit 0…6** = Namenslänge (bis 32 Zeichen). ZDOS benutzt dafür nur Bit 0…5 — der
  Unterschied ist folgenlos, solange Namen ≤ 63 Zeichen sind, aber die Maske ist `7FH`.
* **Bit 7** = `S`-Eigenschaft (SECRET), gespiegelt aus dem Descriptor, damit `CAT`
  filtern kann, ohne jeden Descriptor zu lesen.
* Nach dem letzten Eintrag eines Satzes steht `FFH`; der Rest des Satzes ist
  Altbestand und **muss ignoriert werden**.
* Passt ein Eintrag nicht mehr in den Satz, kommt er **vollständig in den nächsten**.
  Sind alle Sätze voll, wächst die Datei `DIRECTORY` um einen Satz.

---

## 5. Der Descriptor einer Datei (§3.2.2)

Der Descriptor ist **100H Byte** lang. Die ersten 80H Byte sind **bitgleich der
ZDOS-Descriptor** — dieselben Felder an denselben Offsets. Neu ist nur `80H/81H`.

| Offset | Größe | Feld |
|---:|---|---|
| `00H`–`05H` | 6 | unbenutzt |
| `06H`/`07H` | 2 | Rückzeiger auf den **DIRECTORY-Satz** mit dem eigenen Eintrag |
| `08H`/`09H` | 2 | Zeiger auf den **ersten Datenrecord** |
| `0AH`/`0BH` | 2 | Zeiger auf den **letzten Datenrecord** |
| `0CH` | 1 | **Dateityp / Subtyp** (s. u.) |
| `0DH`/`0EH` | 2 LE | **Anzahl der Datenrecords** |
| `0FH`/`10H` | 2 LE | **Recordlänge** (80H, 100H, 200H, 400H, 800H, 1000H) |
| `11H`/`12H` | 2 LE | **Blocklänge** (= Recordlänge oder unbenutzt) |
| `13H` | 1 | **Eigenschaften** |
| `14H`/`15H` | 2 LE | **ENTRY** — Startadresse, nur bei Typ P |
| `16H`/`17H` | 2 LE | **Bytes im letzten Record** |
| `18H`–`1FH` | 8 | **Erstellungsdatum** (6 ASCII `JJMMTT` + `FF 00`) |
| `20H`–`27H` | 8 | **Datum der letzten Änderung** |
| `28H`/`29H` | 2 LE | Anfangsadresse des 1. **Segments** (nur P) |
| `2AH`/`2BH` | 2 LE | Länge des 1. Segments; weitere Segmente folgen, Abschluss `00 00 00 00` |
| `7AH`/`7BH` | 2 LE | **LOW ADDRESS** |
| `7CH`/`7DH` | 2 LE | **HIGH ADDRESS** |
| `7EH`/`7FH` | 2 LE | **STACK SIZE** |
| **`80H`/`81H`** | 2 | **FIRSTBL — Adresse des ersten Zeigersektors der Datei** |
| `82H`–`FFH` | | unbenutzt |

### 5.1 Typbyte `0CH` — Typ **und** Subtyp

| Bit | Bedeutung |
|---|---|
| 0…3 | **Subtyp 0…15** (frei wählbar; I/O-Treiber müssen Subtyp 1 haben) |
| 4 | Typ **B** — BINARY |
| 5 | Typ **A** — ASCII |
| 6 | Typ **D** — DIRECTORY |
| 7 | Typ **P** — PROCEDURE |

Damit ist auch das ZDOS-„P1" erklärt: `81H` = Typ P, Subtyp 1. Das Werkzeug schreibt
`P`, `P1`, … `P15` und liest sie genauso.

### 5.1a Die Segmentliste — mehr als ein Paar

`28H/29H` und `2AH/2BH` sind Anfang und Länge des **ersten** Segments; danach folgen
weitere Paare, abgeschlossen mit `00 00 00 00`, bis `79H`. Das sind bis zu 20
Segmente. Bei Typ A/B/D steht dort kein Segment, sondern Anwenderinhalt
(„sonst frei für Anwender").

Nachgemessen auf dem Referenzdatenträger:

| Datei | Segmente |
|---|---|
| `ZLINK` | `4000+06A7` `62A7+0002` `71E9+060B` `7AF5+01C9` `7FBE+0001` `843F+4ABE` |
| `IMAGER` | `4400+0041` `8442+0026` `876E+3EF1` |
| `EDI`, `EDR`, `LTS`, `Z8ASM`, `Z8ASM2` | je zwei |
| 47 weitere P-Dateien | je eines |

> **Wer nur das erste Segment mitschleppt, zerstört solche Dateien beim
> Zurückschreiben.** Das Werkzeug führt deshalb die ganze Liste — als Text
> `"4400+0041 8442+0026"` durch `FileEntry::segments`, `WriteOptions::udos_segments`,
> das Beiblatt (`segs=`), die CLI (`--segment`) und ein einzelnes Feld im
> Eigenschaften-Dialog. Dasselbe gilt für ZDOS (`doc/udos_diskettenformat.md` §6.3).
> Wächter: `Udos1715Segmente.SechsSegmenteUeberlebenDasZurueckschreiben`.

### 5.2 Eigenschaftsbyte `13H`

`80H` = W (write protected) · `40H` = E (erase protected) · `20H` = L (locked) ·
`10H` = S (secret) · `08H` = R (random) · `04H` = F (force) · Bit 0/1 = vom System
benutzt. Identisch mit ZDOS.

### 5.3 Dateilänge

```
Länge = (Anzahl Records − 1) × Recordlänge + (Bytes im letzten Record, falls 0 < x < RL)
```

Bei Recordlänge **80H** werden von jedem Sektor nur die ersten 128 Byte genutzt; die
zweiten 128 Byte des Sektors bleiben unbenutzt (§3.2.1). Ein Record größer als 100H
belegt `Recordlänge / 100H` **physisch aufeinanderfolgende** Sektoren derselben Spur —
über die Kopfgrenze hinweg, denn die Spur ist der ganze Zylinder (§1.1). Nachgemessen:
kein Record des Referenzdatenträgers überschreitet die Spurgrenze, aber mehrere
überschreiten die Kopfgrenze (z. B. `CAT`, Record ab Sektor 0DH mit 4 Sektoren →
0DH…10H).

---

## 6. Zeigersektoren (§3.2.3)

Ein Zeigersektor ist 256 Byte:

| Offset | Inhalt |
|---|---|
| `00H`–`F9H` | bis zu **125 Adressen** à 2 Byte |
| `FAH`/`FBH` | **ADRCTR** — relative Adresse der **letzten** Eintragung (zeigt auf ihr erstes Byte) |
| `FCH`/`FDH` | **BCKZGR** — voriger Zeigersektor |
| `FEH`/`FFH` | **FORZGR** — nächster Zeigersektor, `FFFF` = letzter |

Daraus:

```
Anzahl Einträge = ADRCTR / 2 + 1
```

* **Im ersten Zeigersektor einer Datei steht auf Byte 0/1 die Adresse des Descriptors**
  — dort bleiben also 124 Datenrecordadressen. In allen weiteren sind es 125.
* Der **Rückwärtszeiger des ersten** Zeigersektors zeigt ebenfalls auf den Descriptor.
* `FIRSTBL` (Descriptor `80H`) zeigt auf den ersten Zeigersektor.

Nachgemessen an `UDOS.TEXT` (206 Records): zwei Zeigersektoren, ADRCTR = 248 (125
Einträge = Descriptor + 124 Records) und 162 (82 Records) → 124 + 82 = 206 ✓.

---

## 7. Wie ein Werkzeug arbeitet

### 7.1 Auflisten

```
descriptor(16H,00) → Zeigersektorkette → Datenrecords der DIRECTORY
für jeden Satz: Einträge bis 0FFH auswerten  →  Name, SECRET, Descriptoradresse
```
Name und SECRET-Bit kosten drei Spuren, alles Weitere je Datei einen Descriptor
irgendwo auf der Diskette — derselbe Unterschied wie `CAT` gegen `CAT F=L` und der
Grund, warum `listNames()`/`loadDetails()` getrennt sind.

### 7.2 Datei lesen

```
d      = descriptor(zeiger)
adr    = alle Einträge der Zeigersektorkette ab FIRSTBL   (der erste ist d selbst)
n      = max(1, Recordlänge / 100H)
inhalt = für jede Recordadresse a: die n Sektoren ab a, davon je
         min(100H, Recordlänge) Byte                    ← Recordlänge 80H nutzt nur die Hälfte
kürzen auf die Länge nach §5.3
```

### 7.3 Datei schreiben

1. Recordlänge wählen (Vorgabe 100H; größere brauchen zusammenhängende Sektoren in
   **einer** Spur).
2. Bedarf: 1 Descriptor + `ceil((Records + 1) / 125)`\* Zeigersektoren + Records ×
   Sektoren je Record. (\*genauer: der erste Zeigersektor fasst 124 Records, jeder
   weitere 125.)
3. Platz aus der Belegungskarte nehmen, Systemspuren aussparen.
4. Datenrecords schreiben, dann die Zeigersektorkette, dann den Descriptor.
5. Verzeichniseintrag anhängen (ggf. DIRECTORY um einen Satz verlängern — dann auch
   deren Descriptor und Zeigersektor nachführen).
6. Belegungskarte mit **beiden** Zählern zurückschreiben.

### 7.4 Löschen

Verzeichniseintrag herausschneiden und die Bits aller Sektoren der Datei —
Datenrecords, Zeigersektoren, Descriptor — in der Karte zurücksetzen. Wie bei ZDOS
bleiben die Sektorinhalte stehen; **maßgeblich ist allein die Karte**.

### 7.5 Was nicht angetastet werden darf

Spur 16H und 17H komplett; auf einer Systemdiskette zusätzlich Spur 0.

---

## 8. `.img` ist hier erlaubt — und das ist der Punkt

Weil nichts außerhalb der Sektordatenfelder steht, ist eine UDOS1715-Diskette
verlustfrei als rohes Sektorabbild darstellbar. `allow_img: true` im Profil ist deshalb
kein Zugeständnis, sondern richtig — und die Testfixture ist entsprechend ein 640-KB-`.img`
statt eines 2-MB-`.hfe`.

Die lineare Reihenfolge des `.img`-Codecs (Zylinder außen, Kopf innen, Sektoren nach
aufsteigender ID) fällt dabei genau mit der UDOS-Sektornummerierung zusammen:
`Byte-Offset = ((Spur · 2 + Kopf) · 16 + ID − 1) · 256` und
`UDOS-Sektor = (ID − 1) + Kopf · 16`.

---

## 9. Validierung

Am Referenzdatenträger („SYSTEM", 67 Dateien) mit einem unabhängig geschriebenen
Python-Prüfskript gegengerechnet:

| Probe | Ergebnis |
|---|---|
| Alle 2560 Sektoren fehlerfrei gelesen (Greaseweazle, `ibm.scan`) | 2560/2560 |
| Zeigersektorketten aller 67 Dateien geschlossen | ✔ |
| Erster Eintrag jedes ersten Zeigersektors = Descriptor | 67/67 |
| BCKZGR des ersten Zeigersektors = Descriptor | 67/67 |
| `Anzahl Adressen − 1` = Feld „Anzahl Records" | 67/67 |
| Erster/letzter Record = Descriptorfelder `08H`/`0AH` | 67/67 |
| SECRET im Verzeichnis = Bit `10H` im Descriptor | 67/67 |
| Kein Record überschreitet die Spurgrenze | ✔ |
| Segmentlisten überstehen `get` → `put` unverändert | 66/66 Dateien |
| **Belegungskarte ↔ Auszählung aus allen Dateien** | **1673 = 1673, beide Richtungen ohne Rest** |
| Zähler `177H` + `17CH` = 80 · 32 | 1673 + 887 = 2560 ✔ |

Die letzte Zeile ist die schärfste: die aus Descriptoren, Zeigersektoren und
Datenrecords aller Dateien plus den 13+16 festen Sektoren errechnete Belegung stimmt
**sektorgenau** mit der Karte überein — es gibt weder einen Sektor in der Karte, den
keine Datei beansprucht, noch umgekehrt.

---

## 10. Quellen

* `doc/original_docs/UDOS1715_Systemhandbuch.txt` — das Handbuch von der Diskette
  selbst (Datei `UDOS.TEXT`); **die maßgebliche Quelle**.
* `doc/udos_diskettenformat.md` — ZDOS auf dem A5120; die gemeinsame Herkunft, an der
  sich die Unterschiede ablesen lassen.
* `~/projects/UDOS/UDOS_/PC1715/` — eine **ältere** PC-1715-Ausprägung (AMF-Karte,
  26×128, ZDOS-Zeiger). Beschreibt dieses Format **nicht**.
* `~/projects/UDOS/UDOS/FORMAT/FOR7658.MAC` — der 765-Formatierer von 1992, dritter
  Weg (128 B/Sektor, Sektor 0 als Pointersektor).
