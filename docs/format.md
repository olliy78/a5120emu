# Diskettenformate (FORMAT.COM)

> **Hinweis (2026-08-05, Medium-Umbau §8.7):** Der Floppy-Stack hält eine gemountete Diskette
> jetzt vollständig als internes `DiskMedium` im Speicher; `.img`/`.hfe`/`.dmk` sind reine
> Container-Codecs (`ImgCodec`/`HfeCodec`/`DmkCodec`) davor.  Wo dieses Dokument noch
> `RawSectorImage`/`HfeImage` nennt, sind die entsprechenden Codecs gemeint — Verhalten und
> Offset-/Layout-Modell sind unverändert.  Neu: `.dmk` als drittes Containerformat,
> „Speichern unter" mit Containerwechsel, und eine **echte Leerdiskette**
> (`createDisk` mit leerem Formatnamen), die das Gastsystem selbst formatiert.
> Feinentwurf: `doc/design/09_floppy_drive.md`.

Dieses Dokument listet **alle** im A5120-Emulator über das CP/A-Formatierprogramm
`FORMAT.COM` (V19.05.89) auswählbaren Diskettenformate auf, beschreibt den
Bedien-Dialog (inkl. der mehrseitigen Format-Menüs „X = Menü #2" usw.) und hält fest,
welche Formate der Emulator aktuell **fehlerfrei** schreiben und verifizieren kann.

Quelle: live aus dem Emulator abgegriffen (Treiber `tools/format_driver`, Boot-Disk
`cpa_cpa780_k5601_clock.img`, Ziel-Diskette in Laufwerk **B:**). Reproduktion
siehe Abschnitt *Nachstellen im Emulator*.

---

## 1. Das Programm

| Programm      | Version (Titelzeile)                                          | Auf der Boot-Disk |
|---------------|---------------------------------------------------------------|-------------------|
| `FORMAT.COM`  | „Disketten-FORMAT fuer Buerocomputer, Version **19.05.89**"   | ✔ |

Beide bieten dasselbe Hauptmenü:

```
0   - Formatieren einer Diskette
2   - Pruefen einer Diskette auf Lesbarkeit
3   - Einstellen des Diskettenformats im BIOS
q,x - Rueckkehr ins Betriebssystem
Bitte FORMAT-Funktion auswaehlen (ENTER=0):
```

---

## 2. Angeschlossenes Laufwerk bestimmt das Menü

Welche Formate FORMAT.COM anbietet, **hängt vom angeschlossenen Laufwerk
ab** (siehe Laufwerksliste am Dokumentende, §10). Das Programm liest beim Start den
Laufwerkstyp aus dem BIOS und zeigt ihn in der **Kopfzeile** des Format-Menüs:

| Programm      | Default-Kopfzeile im Emulator |
|---------------|-------------------|
| `FORMAT.COM`  | `Formate fuer 5 1/4", 80 Sp., doppels. ["A": autom. Formaterk.]` |

**Der Laufwerkstyp ist eine BIOS-Eigenschaft, kein K5122-Hardware-Limit.** FORMAT.COM
liest den Typ des gewählten Laufwerks aus dem BIOS-DPB (Feld `dpbtyp`, gesetzt aus dem
Generierungswert `diskA/B/C/D`, z. B. `11580` = K5601); die K5122-Karte streamt
formatagnostisch Bits und kümmert sich nicht um „5¼″ vs. 8″". Deshalb bestimmt allein
die **BIOS-Konfiguration der Boot-Diskette**, welche Menüs erscheinen — und mit den
**Combo-Boot-Disketten** (§11) melden sich B:/C: als Fremdtypen, sodass sich **alle**
Menüs (inkl. der 8″-Menüs) **live im Emulator abgreifen** lassen.

**Zwei Wege zu anderen Formaten:**

1. **Geometrie-Umschalter S/T/U/V/W** (im Menü) reduzieren die *logische* Geometrie
   eines 80-Spur-DS-Laufwerks auf einseitig bzw. 40 Spuren (per Doppel- oder
   Einzelschritt). Kopfzeile und Formatliste passen sich an (alle 5¼″-Varianten:
   §3.4). Ein 80-Spur-DS-Laufwerk kann so 40-Spur-Disketten und einseitige Formate
   physisch ebenfalls schreiben.
2. **Anderer Laufwerkstyp im BIOS** — auf realer Hardware das physische Laufwerk, im
   Emulator die **Combo-Boot-Diskette** (§11), die B:/C: als K5600.10/.20 (5¼″ SS)
   bzw. MF3200/MF6400 (8″, 77 Spuren) konfiguriert. Die Kopfzeile zeigt dann `8"`
   bzw. `40 Sp./einseitig` und es erscheinen die passenden Formate (§3.5, §5).

> **✅ Seit den Combo-Disks (2026-07-02) im Emulator reproduzierbar:** Die früher hier
> vermerkte Einschränkung „8″-Menüs nicht abgreifbar" ist **überholt**. Alle fünf
> Laufwerkstypen-Menüs sind jetzt **live abgegriffen** (`tools/capture_format_menus.py`,
> §11) — die 8″-Tabellen in §5 sind **keine abgeleiteten Schätzungen mehr, sondern
> Emulator-Mitschnitte**. (Hinweis: „@ Spezielles Format" fragt weiterhin „Anzahl phys.
> Spuren … `<= N`" mit dem laufwerksabhängigen `N`.)

> **Spalten in den Tabellen:** `Sektoren×Größe`, betroffene `Spuren`, Anzahl der
> `System`-Spuren, `Kapazität`, `Bezeichnung`. Ein **`A`** am Zeilenende markiert
> Formate, die CP/A per **automatischer Formaterkennung** wieder lesen kann.

---

## 3. FORMAT.COM (V19.05.89) — Formatliste

Default-Laufwerk **5¼″, 80 Spuren, doppelseitig** (Spuren 0..159 = 80 Zylinder × 2 Seiten).

### 3.1 Menü #1 (Taste `X` → Menü #2)

| Wahl | Sektoren×Größe / Layout                | System | Kapazität | Bezeichnung   | A |
|------|----------------------------------------|:------:|----------:|---------------|:-:|
| `0`  | 5×1024, Sp. 0-159; 192 Dir.-Einträge   | 0      | 800k      | CP/A          | A |
| `1`  | 26×128 Sp. 0-2; 5×1024 Sp. 3-159       | 4      | 780k      | CP/A BC       | A |
| `2`  | 5×1024, Sp. 0-159                      | 4      | 780k      | SCP1715       | A |
| `3`  | 5×1024, Sp. 0-159; 192 Dir.-Einträge   | 2      | 790k      | HU Krz        |   |

Geometrie-Umschalter (Menü #1):

| Taste | Geometrie |
|-------|-----------|
| `S` | 80 Spuren, einseitig |
| `T` | 40 Spuren, doppelseitig, Doppelschritte (Standard) |
| `U` | 40 Spuren, einseitig,    Doppelschritte (Standard) |
| `V` | 40 Spuren, doppelseitig, Einzelschritte (nur halbe Diskette) |
| `W` | 40 Spuren, einseitig,    Einzelschritte (nur halbe Diskette) |
| `@` | Spezielles Format (freie Parametereingabe) |

### 3.2 Menü #2 (Taste `Y` → Menü #3, `Z` → Menü #1)

| Wahl | Sektoren×Größe / Layout                  | System | Kapazität | Bezeichnung | A |
|------|------------------------------------------|:------:|----------:|-------------|:-:|
| `4`  | 16×256, Sp. 0-159; ohne ph. Sektorvers.  | 4      | 624k      | SCP         | A |
| `5`  | 16×256, Sp. 0-159; mit ph. Sektorvers.   | 4      | 624k      |             | A |
| `6`  | 26×128, Sp. 0-159; Sektorfolge 1,7,13…   | 0      | 520k      |             | A |
| `7`  | 16×256, Sp. 0-153; Sektorfolge 1,4,7…    | 4      | 600k      | ZIK-NK      |   |

### 3.3 Menü #3 (Taste `X` → Menü #2, `Z` → Menü #1)

| Wahl | Sektoren×Größe / Layout                  | System | Kapazität | Bezeichnung    | A |
|------|------------------------------------------|:------:|----------:|----------------|:-:|
| `E`  | 9×512, Sp. 0-159                         | 0      | 720k      | {MSDOS}        | A |
| `F`  | 9×512, Sp. 0-159; 4K-BDOS-Blöcke         | 2      | 708k      | VORTEX         |   |
| `G`  | 10×512, Sp. 0-159; Sektorfolge 1,4,7…    | 2      | 788k      | NGB            |   |
| `H`  | 5×1024, Sp. 0-159; Sektorvers. 1,4,2,5,3 | 0      | 800k      | FDC3 4M        | A |
| `I`  | 5×1024, Sp. 0-159; ohne ph. Sektorvers.  | 0      | 800k      |                | A |
| `J`  | 10×512, Sp. 0-159, ITT; SCOPY notwendig  | –      | 800k      | {MSDOS}        |   |
| `K`  | 5×1024, Sp. 0-159, P30/P40; SCOPY nötig  | –      | 800k      | {MSDOS}        |   |

### 3.4 Andere 5¼″-Geometrien (einseitig / 40 Spuren)

Über `S/T/U/V/W` schaltet FORMAT.COM auf andere Geometrien um; Kopfzeile und Formatliste
ändern sich. **`T`/`V`** (40 Spuren doppelseitig, Doppel- bzw. Einzelschritt) liefern
dieselbe Liste; ebenso **`U`/`W`** (40 Spuren einseitig). Der Schritt-Unterschied ist nur
physisch (Doppelschritt = jede 2. Spur eines 80-Spur-Laufwerks).

#### `S` — 5¼″, 80 Spuren, **einseitig** (eine Menüseite; Umschalter `U`/`W`)

| Wahl | Sektoren×Größe / Layout                  | System | Kapazität | Bezeichnung | A |
|------|------------------------------------------|:------:|----------:|-------------|:-:|
| `0`  | 5×1024, Sp. 0-79                         | 0      | 400k      | CP/A        | A |
| `1`  | 26×128 Sp. 0-1; 5×1024 Sp. 2-79          | 2      | 390k      | CP/A S      | A |
| `2`  | 26×128, Sp. 0-79; Sektorfolge 1,7,13…    | 2      | 253k      |             | A |
| `3`  | 26×128, Sp. 0-79; Sektorfolge 1,7,13…    | 0      | 260k      |             | A |
| `4`  | 16×256, Sp. 0-79; ohne ph. Sektorvers.   | 3      | 308k      | SCP         | A |
| `5`  | 16×256, Sp. 0-79; mit ph. Sektorvers.    | 3      | 308k      |             | A |
| `6`  | 16×256, Sp. 0-79; Sektorfolge 1,3,5…     | 3      | 308k      |             |   |
| `7`  | 9×512, Sp. 0-79                          | 0      | 360k      |             | A |

#### `T` / `V` — 5¼″, 40 Spuren, **doppelseitig** (Menü #1 / #2 via `Z`/`X`)

Menü #1:

| Wahl | Sektoren×Größe / Layout                  | System | Kapazität | Bezeichnung | A |
|------|------------------------------------------|:------:|----------:|-------------|:-:|
| `0`  | 5×1024, Sp. 0-79                         | 0      | 400k      | CP/A        | A |
| `3`  | 26×128, Sp. 0-79; Sektorfolge 1,7,13…    | 0      | 260k      |             | A |
| `4`  | 16×256, Sp. 0-79; ohne ph. Sektorvers.   | 4      | 304k      | SCP         | A |
| `5`  | 16×256, Sp. 0-79; mit ph. Sektorvers.    | 4      | 304k      |             | A |
| `6`  | 17×256, Sp. 0-79                         | 3      | 326k      | Spectra     |   |
| `7`  | 16×256, Sp. 0-79                         | 8      | 288k      | Epson       |   |

Menü #2 (Taste `Z`; `S` = „einseitiges Format simulieren"):

| Wahl | Sektoren×Größe / Layout                  | System | Kapazität | Bezeichnung | A |
|------|------------------------------------------|:------:|----------:|-------------|:-:|
| `E`  | 9×512, Sp. 0-79                          | 2      | 350k      |             |   |
| `F`  | 9×512, Sp. 0-79                          | 0      | 360k      | {MSDOS}     | A |
| `G`  | 8×512, Sp. 0-79; Sp. 79-40 Rückseite     | 1      | 316k      | CP/M 86     |   |
| `H`  | 20×512, Sp. 0-39; Sektorfolge 0,1,…13    | 1      | 390k      | KAYPRO      |   |

#### `U` / `W` — 5¼″, 40 Spuren, **einseitig** (Menü #1 / #2 via `X`/`Z`)

Menü #1:

| Wahl | Sektoren×Größe / Layout                  | System | Kapazität | Bezeichnung | A |
|------|------------------------------------------|:------:|----------:|-------------|:-:|
| `0`  | 5×1024, Sp. 0-39                         | 0      | 200k      | CP/A        | A |
| `1`  | 26×128 Sp. 0-1; 5×1024 Sp. 2-39          | 2      | 190k      | CP/A S      | A |
| `2`  | 26×128, Sp. 0-39; Sektorfolge 1,7,13…    | 2      | 123k      |             | A |
| `3`  | 26×128, Sp. 0-39; Sektorfolge 1,7,13…    | 0      | 130k      |             | A |
| `4`  | 16×256, Sp. 0-39; ohne ph. Sektorvers.   | 3      | 148k      | SCP         | A |
| `5`  | 16×256, Sp. 0-39; mit ph. Sektorvers.    | 3      | 148k      |             | A |
| `6`  | 15×256, Sp. 0-39; Sektorfolge 1,4,7…     | 3      | 138k      | BAP2001     |   |
| `7`  | 5×1024, Sp. 0-39; ohne ph. Sektorvers.   | 3      | 185k      | Osborne     |   |

Menü #2 (Taste `X`):

| Wahl | Sektoren×Größe / Layout                  | System | Kapazität | Bezeichnung | A |
|------|------------------------------------------|:------:|----------:|-------------|:-:|
| `E`  | 9×512, Sp. 0-39; Sektorfolge 1,3,5…      | 2      | 170k      | DEC VT      |   |
| `F`  | 9×512, Sp. 0-39; 1k-BDOS-Blöcke          | 2      | 171k      | VPPC        |   |
| `G`  | 9×512, Sp. 0-39; Sektorfolge 41,42,…     | 2      | 171k      | Schn. S     |   |
| `H`  | 9×512, Sp. 0-39; Sektorfolge c1,c2,…     | 0      | 180k      | Schn. D     |   |
| `I`  | 8×512, Sp. 0-39; 2k-BDOS-Blöcke          | 1      | 156k      | CP/M 86     |   |
| `J`  | 9×512, Sp. 0-39                          | –      | 180k      | {MSDOS}     | A |
| `K`  | 8×512, Sp. 0-39; 1k-BDOS-Blöcke          | 1      | 156k      | IBM CPC     |   |
| `L`  | 10×512, Sp. 0-39; Sektorfolge 0,1,…      | 1      | 195k      | KAYPRO      |   |

### 3.5 Native 5¼″-Einzelseiten-Laufwerke K5600.10 / K5600.20 — Emulator-Mitschnitt

Die Combo-Boot-Disk `…_5inchCombo` (§11) konfiguriert **B: = K5600.10** (5¼″, 40 Sp.,
DD, **einseitig**, DPB `10540`) und **C: = K5600.20** (5¼″, 80 Sp., DD, **einseitig**,
DPB `10580`). FORMAT.COM zeigt für diese Laufwerke **nativ** dieselben Formatlisten, die
das K5601 nur per Geometrie-Umschalter simuliert — hier als **Live-Abgriff bestätigt**
(`tools/capture_format_menus.py K5600.10 K5600.20`):

| Laufwerk | DPB | Kopfzeile | Formatliste = §3.4 | Umschalter |
|----------|-----|-----------|--------------------|-----------|
| **K5600.10** (MFS 1.2) | `10540` | `Formate fuer 5 1/4", 40 Sp., eins.` | **`U`/`W`** (40 Sp. einseitig, `0-7` + `E-L`) | `X`/`Z`, `@` |
| **K5600.20** (MFS 1.4) | `10580` | `Formate fuer 5 1/4", 80 Sp., eins.` | **`S`** (80 Sp. einseitig, `0-7`) | `U`/`W`, `@` |

Das bestätigt das §3.4-Modell empirisch: Der reale 40-/80-Spur-**Einzelseiten**-Antrieb
liefert byte-identisch dieselbe Formatliste wie das per `U`/`W` bzw. `S` „simulierte"
K5601. K5600.20 bietet zusätzlich die Umschalter `U`/`W` (auf 40 Spuren herunter);
K5600.10 blättert mit `X` auf seine zweite Menüseite (`E-L`).

---

## 5. 8″-Laufwerke (MF3200 / K5602.10 / MF6400) — 77 Spuren

Schließt man an einen A5110/A5120 ein **8″-Laufwerk** an, meldet FORMAT.COM
in der Kopfzeile `8"` mit **77 Spuren** und bieten die passenden 8″-Formate an. Diese
Geräte zeichnen **einseitig** auf (Rückseite nur über doppelt gelochte Disketten nach
Umdrehen). Zwei Dichten:

| Laufwerk | DPB | Aufzeichnung | Kapazität (pro Seite) |
|----------|-----|--------------|-----------------------|
| **MF3200**           | `00877` | 8″, 77 Spuren, **einfache Dichte (SD/FM)** | bis ~350 KByte |
| **K5602.10 / MF6400** (FS 6400) | `10877` | 8″, 77 Spuren, **doppelte Dichte (DD/MFM)** | bis ~690 KByte |

> ✅ **Live aus dem Emulator abgegriffen** (Combo-Boot-Disk `…_8inchCombo`, §11;
> `tools/capture_format_menus.py MF3200 MF6400`). Die folgenden Tabellen sind
> **Emulator-Mitschnitte** der FORMAT.COM-V19.05.89-Menüs, keine abgeleiteten Schätzungen.

### 5.1 MF3200 — 8″, einseitig, **einfache Dichte (SD)** (`00877`, eine Menüseite)

Kopfzeile: `Formate fuer 8", eins., einf. Dichte ["A": autom. Formaterk.]`

| Wahl | Sektoren×Größe / Layout                 | System | Kapazität | A |
|------|-----------------------------------------|:------:|----------:|:-:|
| `0`  | 4×1024, Sp. 0-76                        | 0      | 308k      | A |
| `1`  | 26×128 Sp. 0-2; 4×1024 Sp. 3-76         | 3      | 296k      | A |
| `2`  | 26×128, Sp. 0-76; Sektorfolge 1,7,13…   | 2      | 243k      | A |
| `3`  | 26×128, Sp. 0-76; Sektorfolge 1,7,13…   | 0      | 250k      | A |
| `4`  | 26×128 Sp. 0; 4×1024 Sp. 1-76 (SCP)     | 3      | 296k      | A |
| `5`  | 9×512, Sp. 0-76                         | 0      | 346k      | A |
| `6`  | 26×128 Sp. 0-1; 9×512 Sp. 2-76          | 2      | 336k      | A |
| `7`  | 26×128 Sp. 0-2; 16×256 Sp. 3-76         | 3      | 296k      | A |
| `8`  | 9×512, Sp. 0-79; IH Mittweida (MSDOS-SicherheitsKopie) | – | – | |

### 5.2 K5602.10 / MF6400 — 8″, einseitig, **doppelte Dichte (DD)** (`10877`, `V` = SD)

Kopfzeile: `Formate fuer 8", eins., dopp. Dichte ["A": autom. Formaterk.]`

| Wahl | Sektoren×Größe / Layout                 | System | Kapazität | A |
|------|-----------------------------------------|:------:|----------:|:-:|
| `0`  | 8×1024, Sp. 0-76                        | 0      | 616k      | A |
| `1`  | 26×128 Sp. 0-1; 8×1024 Sp. 2-76         | 2      | 600k      | A |
| `2`  | 26×128 Sp. 0-1; 40×128 Sp. 2-76         | 2      | 374k      | A |
| `3`  | 40×128, Sp. 0-76; Sektorfolge 1,2,3…    | 0      | 384k      | A |
| `4`  | 26×128 Sp. 0; 8×1024 Sp. 1-76 (SCP)     | 2      | 600k      | A |
| `5`  | 16×512, Sp. 0-76                        | 0      | 616k      | A |
| `6`  | 26×128 Sp. 0-1; 16×512 Sp. 2-76         | 2      | 600k      | A |
| `7`  | 9×1024, Sp. 0-76                        | 0      | 692k      |   |

Umschalter: `V` = **einfache Dichte (SD)** (wechselt zur MF3200-Liste §5.1), `@` = Spezielles Format.

---

## 6. „@ Spezielles Format" (freie Parametereingabe)

Die Taste `@` in jedem Format-Menü startet eine **freie Parametereingabe** statt eines
vordefinierten Formats. Beobachtete Prompts (5¼″-Laufwerk im Emulator):

```
Anzahl phys. Spuren auf Diskette ( <=  80):              ← drive-begrenzt (hier 80)
Anz. der phys. Sekt. fuer DatenSpuren pro Seite (1..52): ← Sektoren/Spur
… (weitere: Sektorgröße, System-Spuren, Sektorfolge/-versatz)
```

Die Obergrenze `<= 80` stammt vom angeschlossenen Laufwerk — `@` kann also nur Formate
**innerhalb der physischen Laufwerksgrenzen** erzeugen (ein 8″-77-Spuren-Layout ist auf
dem 5¼″-Laufwerk damit nicht möglich).

---

## 7. Bedien-Dialog (Tastenfolge)

**FORMAT.COM** (Funktion 0 = Formatieren):

```
Funktion:  ENTER (=0, Formatieren)
Laufwerk:  B                       (Einzeltaste, kein ENTER)
           ENTER                   (Quittung „Diskette bitte in Laufwerk B …")
Verify:    ENTER  (= „j" = Vergleichs-Lesen nach dem Schreiben EIN)   ← mit Verify
           n      (= ohne Verify)
Format:    Menü mit X/Y/Z blättern, dann Format-Taste (0-7 / E-K / S…W)
Von Spur:  ENTER (=0)              bzw. dezimale Spurnummer
Bis Spur:  ENTER (=letzte)         bzw. dezimale Spurnummer
Warnung:   j                       („Files auf Laufw. B werden zerstoert! Erlaubt?")
→ „FORMATIEREN auf Spur N, Laenge in Bytes: …" je Spur, dann „FORMATIEREN beendet",
   danach „Wiederholung mit gleichen Parametern? (j/n):"
```

Der Verify-Prompt von FORMAT.COM lautet wörtlich:
`Vergleichs-Lesen nach dem Schreiben? (n, ENTER=j):` — **ENTER bzw. `j` = mit Verify**.

---

## 8. Status im Emulator (Stand 2026-06-28, Branch `formating-disks`)

| Programm      | Ergebnis |
|---------------|----------|
| **FORMAT.COM**  | ✅ **Funktioniert** — formatiert **mit Verify** fehlerfrei. Verifiziert für alle vier Sektorgrößen: **128 B** (Format 1, Systemspuren 0-2), **256 B** (Format 4), **512 B** (Format E), **1024 B** (Format 1, Datenspuren). Voller Lauf Format 1 über **alle 160 Spuren**: `FORMATIEREN beendet` ohne eine einzige `SPUR DEFEKT`-Meldung; `DIR` der frischen Disk → `No File` (gültige, leere CP/A-Disk). |

**Getestet:** nur die **80-Spur-DS-Geometrie** (Default), dort alle vier Sektorgrößen.
**Noch nicht im Emulator verifiziert:** die einseitigen und 40-Spur-Geometrien (S/T/U/V/W,
§3.4) — sie sollten auf dem 80-Spur-DS-Laufwerk physisch schreibbar sein (einseitig =
nur eine Seite, 40-Spur = Doppelschritt), sind aber ungetestet. **Nicht möglich:** die
8″-Formate (§5), da der Emulator kein 8″-Laufwerk modelliert.

### 8.2 Scriptgesteuerte Formatier-Pipeline für alle §3-Formate (Stand 2026-07-01)

> **UPDATE 2026-08-07 — die Pipeline startet jetzt IMMER auf einer ECHTEN LEERDISKETTE.**
> `tests/system/drivers/format_all.py` legt `.hfe`-Ziele als **unformatiertes** Medium in der Geometrie des
> Laufwerks an (`createB` = *leerer* Formatname), und `tests/system/drivers/make_bootdisk.py`
> fährt wieder die volle Anwenderkette **Leerdiskette → FORMAT.COM (alle Spuren, mit
> Vergleichs-Lesen) → CPABCGEN → Kaltstart** in EINEM Treiberlauf; `mk_disk_template` und
> `disks/empty_cpa780.hfe` werden dafür nicht mehr gebraucht.  Verifiziert für alle vier
> Laufwerkstypen (K5601, K5600.10, K5600.20, MF3200, MF6400).
> Die beiden Vorbedingungen dafür sind behoben: der Gap-Blank-Hänger (s. nächster Absatz)
> und das Wettrennen `Fehler 'U' SPUR DEFEKT` beim Vergleichs-Lesen
> (`doc/analyse_format_leerspur.md`).  **Ausnahme `--type img`:** ein rohes Sektorimage
> kennt keinen Zustand „unformatiert" — dieser Pfad legt weiterhin vorformatiert (0xE5) an
> und prüft damit das *Um*formatieren einer gültigen Disk.

> **UPDATE 2026-07-06 — Gap-Blank-`.hfe`-Hänger GELÖST (supersedet §8.2/§8.2.1-Workaround).**
> `DiskImage::create` erzeugt für `.hfe` jetzt eine *voll formatierte* Leerdiskette (echte
> IDAM/DATA/CRC je Spur via `TrackCodec::buildTrack`→`BitCodec::encode`, Daten 0xE5) statt eines
> gap-leeren Templates → FORMAT.COM formatiert sie **ohne Hänger** (End-to-End verifiziert,
> `format_all.py 4 --type hfe`).  `DiskImage::open` lehnt zusätzlich ein markenloses/unformatiertes
> Image ab (statt in die ZVE2-Lese-Koroutine `0x1D0F` zu laufen).  `format_all.py` legt `.hfe`-Ziele
> mit bekannter Geometrie jetzt direkt via `create` an — die frühere Template-Kopie entfällt (nur noch
> Fallback für Geometrien ohne definiertes `DiskFormat`).  Die untenstehende Gap-Blank-Diagnose
> (§8.2.1) ist damit historisch.

> **UPDATE 2026-08-07 — die Matrix ist jetzt Teil der Testsuite.** `format_all.py` liefert
> über `--list-matrix` seine vollständige Prüfmatrix (`boot|drive|geo|key`, 88 Einträge);
> `CMakeLists.txt` legt beim Konfigurieren je Eintrag einen ctest-Test an
> (`format_matrix_<boot>_<drive>_<geo|DS>_<key>`, LABEL **`format_matrix`**, Umfang Smoke
> Spur 0–2, ~9 s je Format). Lauf: `tools/dev.sh test-matrix` (~200 s wall bei `-j8`,
> 1572 s\*proc). Neue Formate/Geometrien in die Tabellen dieses Skripts eintragen — der
> Testsatz wächst beim nächsten `cmake` mit. **Voll-Läufe bleiben manuell**
> (`--full`); die beiden bekannten Voll-Ausreißer (§3 Format `7` → `Fehler 'S'`, `5` als
> `.hfe`) fallen im Smoke nicht an.

`tests/system/drivers/format_all.py` formatiert die nativen K5601-Formate aus §3 (Menü #1/#2/#3)
scriptgesteuert nach Laufwerk **B:** und wertet Verify + Endstatus aus (Treiber:
`tools/format_driver`).  Ergebnis als **.hfe** (formatagnostisch; Phase A) — `.img`
folgt (Phase B).

**Ergebnis (Verify = ein, je Format Smoke über Spur 0-5, deckt System- + Datenspuren
beider Köpfe ab): 13 von 15 §3-Formaten OK** — `FORMATIEREN beendet` ohne `SPUR DEFEKT`.
Abgedeckt: **alle vier Sektorgrößen und alle drei Menüseiten** (`0`/`H`/`I`/`2`/`3`=1024 B,
`1`=gemischt 128+1024 B, `4`=256 B, `6`=128 B, `E`/`F`/`G`/`J`/`K`=512 B; Menü #1 `0-3`,
#2 `4-7`, #3 `E-K`).

**Voll-Disk-Nachweis (`format_all.py 0 --full --dir-verify`):** Format `0` über **alle 160
Spuren** → `FORMATIEREN beendet` ohne `SPUR DEFEKT`; anschließend `A>DIR B:` → **`No File`**
= gültige, leere CP/A-Disk (unabhängig re-gemountetes .hfe).

**Zwei Ausnahmen — Formate mit physischem Sektor-Interleave (`5`, `7`) schlagen im Verify
fehl:** `5` (16×256 **mit** ph. Sektorversatz) → `Fehler 'U'` (Timeout), `7` (16×256,
Sektorfolge 1,4,7…) → `Fehler 'S'`.  Das interleave-**freie** Gegenstück `4` (16×256 ohne
Versatz) verifiziert dagegen fehlerfrei — d. h. die Wurzel ist die **Interleave-Behandlung**
im Format-Write→TrackImage→Rücklese-Pfad (der physisch versetzte Sektor wird beim Verify
nicht gefunden/gelesen).  **Offen (Folgearbeit):** `parseFormatStream`/`buildTrack`-Reihenfolge
für ph. Sektorversatz prüfen.

**Phase B — `.img` (Stand 2026-07-01): 14 von 15 §3-Formaten OK.**  Das B:-Ziel wird per
**`create`** als 0xE5-`.img` in der Geometrie des passenden `DiskFormat` angelegt (`format_all.py
--type img`, s. §9.0) — eine frische 0xE5-`.img` liest über den `ImgCodec` als gültig
formatierte Disk, daher **kein BUSRQ-Hänger**.  Alle Sektorgrößen + Menüseiten verifizieren;
Dateigrößen passen (z. B. `0`=819200, `4`=655360, `6`=532480, `E`=737280 B).  **Interleave im
Rohspeicher unkritisch:** Format `5` (16×256 *mit* ph. Sektorversatz) verifiziert als `.img`
fehlerfrei (Sektoren nach logischer ID am Offset abgelegt), obwohl es als `.hfe` (bit-genaue
physische Ablage) scheitert.  **Einzige Ausnahme (beide Dateitypen):** Format `7` (16×256,
Sektorfolge 1,4,7…, ZIK-NK) → `Fehler 'S'` — bounded Interleave-Sonderfall, Folgearbeit.

**Zwei Emulator-Erkenntnisse dieser Arbeit:**

1. **Unformatierte-Spur-Lesung terminiert per Index-Timeout (K5122-Fix,
   `startReadTransfer`).**  FORMAT.COM **liest jede Zielspur vor** dem Neuformatieren
   (Rotations-/Längenmessung).  Bisher lieferte eine leere/unformatierte Spur *keinen*
   Datenstrom → das vom Lese-`/STR` gesetzte `/BUSRQ` blieb hängen, ZVE2 verklemmte in
   seiner Lese-Koroutine (`IN(16H)`+`JR $` @ `0x1D0F/0x1D21`).  Jetzt streamt die Karte
   für eine unformatierte Spur reinen markenlosen **Gap-Fluss** (`6250 B`, `0x4E`), damit
   die Leseroutine kein IDAM findet und wie auf echter HW über den Index-Timeout endet
   („record not found").  Alle K5122/Boot/Format-Tests bleiben grün.

2. **Das B:-Ausgangs-Template muss eine GÜLTIGE, bereits formatierte Disk sein — kein
   gap-leeres Blank.**  Auf einem gap-leeren Template zwingt der obige Fix zwar jede
   Vorlesung sauber in den Index-Timeout, aber die dadurch verschobene ZVE1↔ZVE2-Bus-
   Arbitrierung führt **formatabhängig zu einem BUSRQ-Hänger** (kleinsektorige Formate
   128/256 B hingen bei Spur 2, 1024 B lief weiter, ein Voll-Lauf hing bei Spur 7 —
   Spin @ `0x1D0F/0x1D21`, `busrq≈96 %`).  Mit einem **gültigen** Template finden die
   Vorlesungen echte Sektoren (schnell, kein Timeout), der Bus bleibt stabil (bewährter
   §8-Lese-/Schreib-Pfad) → **alle Sektorgrößen formatieren+verifizieren zuverlässig**.
   `format_all.py` kopiert daher ein gültiges Template (`disks/cpadisk_*.hfe`) als B:;
   beim Voll-Format wird es komplett überschrieben (sauberes Ziel), beim Smoke behalten
   die nicht formatierten Spuren die lesbaren Template-Daten.

   **Wurzelursache des Gap-Blank-Hängers (Diagnose 2026-07-02, DIAG-Instrumentierung):**
   FORMAT.COM **liest jede Zielspur vor** dem Formatieren.  Auf einer noch UNFORMATIERTEN
   Datenspur liefert der Gap-Fluss kein IDAM → die ZVE2-Lese-Koroutine (`IN(16H)`+`JR $` @
   `0x1D0F/0x1D21`) muss über den **Index-Interrupt** abbrechen (ZVE1s Index-ISR patcht das
   `JR $`).  Der BIOS-Motor-Watchdog (`headup`) schreibt aber **kurz VOR** der Vorlesung
   `OUT(11H)=0x03` = **Index-Interrupt sperren** (`wmode=0`, kein Write) — die
   Index-Keepalive greift nur im `write_mode_`.  Mit gesperrtem Index kann die Koroutine nie
   timeouten → ZVE2 dreht ewig, hält `/BUSRQ` (≈96 %).  Verifiziert am System→Daten-Übergang:
   Format 6 (26×128) formatiert die 3 System-Spuren + 4 Datenspuren, hängt an C=3 H=1
   (Spur 7); Format 0 identisch.

   **Der `headup`-Watchdog ist BIOS-Code, kein Emulator-Teil.** `tim1uu` (BIOS `0xE682`, 1-s-Timer)
   zählt `fl.zto` herunter und ruft `headup` (BIOS `0xE3BF`: `OUT(11H)=0x03` = Port-A-Index-INT
   sperren + Motor aus).  Standard-Z80-PIO: der Index-Puls (den `K5122::update()` alle ~200 ms
   erzeugt) wird nur zum CPU-Interrupt, wenn Port A `ie=1` — sowohl auf echter HW als auch bei uns.

   **Fix-Versuche „Index-Sperre unterdrücken" — GETESTET & WIDERLEGT (2026-07-02):** Messung
   (Index-Puls-Zähler + `OUT(11H)`-Log): der **angewendete** `headup`-Disable (`wmode=0`) feuert
   ca. **alle 7–9 Index-Pulse** — ein **plausibles Verhältnis** (kein grober Timing-Fehler wie beim
   Uhr-Bug).  Drei Keepalive-Varianten (Sperre `0x03` ignorieren während …) verschlimmern den
   Hänger **monoton, je mehr geblockt wird**: nur `write_mode_` → Spur 7 · `+ transferring_` →
   Spur 5 · `+ index_armed_` (Puls-Garantie nach `0x83`) → Spur 2.  ⇒ **Die Index-Sperre ist
   LEGITIM und für den korrekten Ablauf NÖTIG** — sie zu blockieren stört FORMAT.COMs/OS-Index-
   Protokoll und bricht früher.  Der `OUT(11H)=0x03` am Stall ist also **Korrelation, keine
   Ursache**.  Die „Index-Masken/Watchdog-Timing"-Hypothese ist damit **widerlegt** — die Wurzel
   liegt tiefer in der **ZVE1↔ZVE2-Koordination** (welcher Interrupt/welche Bedingung die Read-
   Koroutine im Erfolgsfall bricht, und warum das am Stall ausbleibt).
   **Nur relevant, wenn man ohne gültiges Template formatieren will** — die Pipeline nutzt ein
   gültiges Template und ist davon nicht betroffen.

#### 8.2.1 Folge-Task (nächste Session): Gap-Blank-Hänger cycle-level lösen

**Ziel:** FORMAT.COM formatiert ein FRISCH per `create` erzeugtes, gap-leeres `.hfe` direkt
(ohne gültiges Template) ohne Hänger.

**★ Diagnose vertieft & korrigiert (2026-07-02, Session B — DEBUG/TRACE-Trace + RAM-Disasm):**

- **Es ist KEIN toter Spin, sondern ein UNENDLICHER RETRY-Loop.** Der Ablauf pro Spur (Format 6,
  26×128, Blank-`.hfe`): Blank-Vorlesung (`>>> READ … UNFORMATIERT → 6250 B Gap-Flux`) **wenige
  Male** → `>>> FORMAT-WRITE` (Spur geschrieben) → 6× Verify-Read der jetzt formatierten Spur. Für
  **cyl0/head0, cyl0/head1, cyl1/head0, cyl1/head1** (= log. Spuren 0-3) läuft das sauber durch.
  Bei **cyl2/head0** (log. Spur 4, erster Seek **über cyl1 hinaus**) **retriet die Blank-Vorlesung
  endlos** — nie ein `FORMAT-WRITE C=2`. ZVE2 wird dabei ständig neu resettet+gestartet
  (`OUT 04H=0x00` → `Start aus Reset`), `/STR=1` beendet den Read, ZVE1 retriet. Der Diskriminator
  ist der **cyl1→cyl2-Übergang**, nicht „System→Daten".
- **★ Der Index-Interrupt (`0xE8`) feuert während des Stalls WEITER** (letzte 0xE8-Zustellung +0,9 s
  NACH Stall-Onset, an ZVE1-PC `E48A`/`0DDA`). Gemessen: 294× INT-Quittung `0xE8` über den Lauf,
  davon viele nach dem Stall-Beginn. ⇒ **Sowohl die „Index-Maske"-Hypothese (§8.2) ALS AUCH Lead 2
  („Motor/Index stoppt") sind damit ENDGÜLTIG WIDERLEGT** — der Index erreicht ZVE1 laufend, der
  Retry terminiert trotzdem nicht. (0xFC = CTC-Uhr-Tick dominiert die INT-Statistik mit ~11000×.)
- **Die Koroutine ist eine umdrehungsbasierte SPURLÄNGEN-Messung** (RAM-Disasm der geladenen
  FORMAT-Routine `0x1CEC–0x1D38`, Dump via neuem `format_driver`-`ramdump`):
  `1CEE LD (1D22H),FE` **schärft** die Spin-Falle `1D21 JR $`, ebenso `1CF7/1CFA LD (HL),18H` die
  Fallen `0x1D50`/`0x1DE5`; dann `OUT(04H)=0` (ZVE2-Reset), `IN A,(16H)` (1 Stream-Byte),
  `OUT(11H)=0x83` (Index-INT scharf), `JR $`. Die **Index-ISR patcht `[0x1D22]`** (FE→…), bricht den
  Spin → `1D23` weiter → `CALL 1EB6` (Ergebnis verarbeiten) → `RET`. Die Längenmessung selbst
  **funktioniert** (Screen zeigt korrekt `Laenge: 6127` für Spur 4, Soll 6250) — der Hänger sitzt
  **danach** in der Format-/Retry-Schleife dieser Spur.

**Was NICHT die Ursache ist (nicht erneut versuchen):**
- Index-Sperre `headup`→`OUT(11H)=0x03` (§8.2, monoton widerlegt) **und** „Motor/Index stoppt"
  (Lead 2) — beide durch die laufenden 0xE8-INTs im Stall widerlegt.
- Der `.img`/Valid-Template-Pfad (dort findet die Vorlesung echte Sektoren, kein Retry).
- **Byte-Periode:** Emulator liefert `indexPeriod/kBytePeriodCycles = 490000/150 ≈ 3267` Bytes/Umdr.,
  real ~6250 (`kBytePeriodCycles` evtl. ~2× zu langsam, s. Kommentar k5122.h:287). **ABER** die
  Messung ergibt korrekt 6127 → dieser Wert ist NICHT der Stall-Grund; `kBytePeriodCycles` NICHT
  blind ändern (bricht die getunte Boot-DMA).

**Konkreter nächster Schritt (EINZIGER offener Lead):** Disassembliere die **`0xE8`-Index-ISR** und die
FORMAT-Retry-Schleife der geladenen Routine (RAM-Dumps liegen bereit, s. Repro). Finde den **Zähler/die
Bedingung**, die `[0x1D22]` patcht bzw. „Vorlesung akzeptiert → schreiben" entscheidet, und **warum sie
bei cyl0/cyl1 nach ~4 Versuchen greift, bei cyl2 nie**. Kandidaten: ein von der ISR dekrementierter
Retry-/Umdrehungszähler, der nach dem cyl1→cyl2-Seek anders initialisiert/nicht dekrementiert wird; oder
eine emulator-seitige Zustandsabweichung, die exakt der erste Seek jenseits cyl1 auslöst (Kopfposition,
Statusbit, Index-**Phase** relativ zum Gap-Stream nach frischem Seek). Vergleiche einen ERFOLGREICHEN
cyl1-Retry-Zyklus mit dem cyl2-Zyklus instruktionsweise.

**Werkzeuge (in dieser Session gebaut, `tools/format_driver`):**
- **`savestate <file>`** friert den Stall-Zustand ein (RAM+beide Z80+Floppy), damit `k1520dbg`/
  `boot_trace` ihn ohne Tastatur-Treiber laden können. **`ramdump <lo> <hi> <file>`** dumpt RAM-Regionen.
- **`FD_LOGLEVEL=debug`** + **`FD_GATE=from:to[:level]`** / **`FD_PCGATE=lo:hi[:level]`** öffnen ein
  gezieltes DEBUG/TRACE-Fenster. ⚠️ **DEBUG/TRACE nur aus `build_trace/format_driver`** (LOG_LEVEL=5;
  in `build/` mit LOG_LEVEL=3 sind DEBUG/TRACE in den **Bibliotheken** wegkompiliert — das per-Target-
  Define greift dort nicht).

**Repro (Format 6, Blank-`.hfe`, hängt bei „FORMATIEREN auf Spur 4"):**
`python3 tools/img_to_hfe.py --blank --cyls 80 --heads 2 B.hfe; cp disks/cpa_cpa780_k5601_clock.img A.img;`
Script: `boot 80`/`type 12:00:00`/enter / `boot 5`/`type FORMAT`/enter / `boot 30`/enter (Fkt 0) /
`boot 6`/`type B`/enter / `boot 10`/enter (Verify j) / `boot 8`/`type X`/`boot 3`/`type 6`/`boot 6`/enter /
`boot 5`/`type 9`/enter / `boot 6`/`type j` / `boot 250` / `ramdump 0100 2200 tpa.bin` /
`ramdump 1C00 2100 coro.bin`. Disasm: `python3 tools/z80_disasm2.py --org 0x1C00 --entry 0x1D0F coro.bin`.
Schlüssel-Adressen: Mess-/Retry-Routine `0x1CEC–0x1D38`, Spin-Fallen `0x1D21`/`0x1D50`/`0x1DE5`
(Arm via `[0x1D22]=FE`), Ergebnis `CALL 1EB6`, BIOS-Index-Vektor `ivdsk1 0xE8`. Voller Stand: §8.2.

**Nicht möglich:** die 8″-Formate (§5), da der Emulator kein 8″-Laufwerk modelliert.

### 8.3 §3.4-Geometrien S/T/U/V/W (einseitig / 40-Spur) — Stand 2026-07-02

Der Runner schaltet die Geometrie per `--geo {S,T,U,V,W}` um (sendet den Umschalt-Buchstaben
im Format-Menü); die §3.4-Formatliste erscheint.  Empirisch bestätigtes Stepping-Modell
(FORMAT-WRITE-Positionen, Smoke Format 0):

| Geo | Kopfzeile im Emulator            | phys. Zylinder (Format 0) | Modell |
|-----|----------------------------------|---------------------------|--------|
| `S` | 80 Sp., einseitig                | 0,1,2,…  (Kopf 0)         | einseitig |
| `W` | 40 Sp., einseitig                | 0,1,2,…  (Kopf 0)         | Einzelschritt, physisch=logisch |
| `U` | 40 Sp., einseitig                | 0,2,4,…  (Kopf 0)         | Doppelschritt |
| `V` | 40 Sp., doppelseitig             | 0,1,2,…                   | Einzelschritt |
| `T` | 40 Sp., doppelseitig             | 0,2,4,…                   | Doppelschritt |

**`.hfe`: alle fünf Geometrien formatieren+verifizieren** (S/W/U/V/T, Format 0 bestätigt).  Der
Verify ist **physisch-positions-konsistent** — Schreiben und Rücklesen nutzen dieselbe
Kopfposition, daher spielt Einzel- vs. Doppelschritt für `.hfe` keine Rolle (das 80×2-Template
deckt alle physischen Positionen ab).

**`.img`: einseitig/Einzelschritt sauber** (RawSectorImage-Offset = phys. `cur_cyl_`):
- `S` (einseitig 80): **3/3** — `cpa200`(400k)/`cpa640`(320k)/`k5601_ss80_9x512`(360k).
- `W` (einseitig 40, Einzelschritt): **2/3** — `k5601_ss40_5x1024`(200k)/`_16x256`(160k) OK;
  `W:6` (15×256 „Sektorfolge 1,4,7") scheitert = **derselbe Interleave-Sonderfall** wie Format 7.
- `V` (doppelseitig 40, Einzelschritt): **2/2** — `k5601_ds40_5x1024`(400k)/`_16x256`(320k).
- `T`/`U` (**Doppelschritt**): als `.img` **übersprungen** (`SKIP(.img)`).  Grund: die Karte kennt
  nur Step-Impulse, nicht „doppelt"; `cur_cyl_` = 2×logisch (0,2,…,78) → ein logisch-40-spuriges
  `.img` bräuchte ein Physisch→Logisch-Mapping.  Für Doppelschritt-Disketten daher **`.hfe`**
  verwenden (physisches Bit-Spur-Modell, faithful).

Neue `DiskFormat`s in `FormatParser::builtinFormats()`: `k5601_ss80_26x128`, `k5601_ss80_9x512`,
`k5601_ss40_5x1024`/`_26x128`/`_16x256`/`_15x256`, `k5601_ds40_5x1024`/`_26x128`/`_16x256`/`_17x256`
(+ vorhandene `cpa200`/`cpa640`).  Aufruf z. B. `python3 tests/system/drivers/format_all.py --geo W --type img 0 4`.

> **Ziel-Status:** 80-Spur-DS (§3) + §3.4-Geometrien (S/V/W einseitig/Einzelschritt) formatieren
> +verifizieren als `.hfe`/`.img`; Doppelschritt (T/U) als `.hfe`.  Offen: (a) Interleave-Formate
> (Sektorfolge 1,4,7… — Format 7, W:6), s. §8.4; (b) Doppelschritt-`.img` (Mapping);
> (c) 8″-Laufwerk (§5).  RE-Stand: `doc/design/07_k5122_afs.md`,
> Memory `project_format_all_pipeline`.

### 8.4 „Sektorfolge 1,4,7"-Formate (Format 7 „ZIK-NK", W:6 „BAP2001") — Diagnose 2026-07-02

Format 7 (`16×256, Sp.0-153, Sektorfolge 1,4,7…, 4 System`) und W:6 (`15×256, Sektorfolge
1,4,7…`) melden im Verify `Fehler 'S' SPUR DEFEKT` — auf **beiden** Dateitypen (`.hfe` UND
`.img`).  Black-Box-Diagnose (Stream-Capture via `K5122_FMT_CAPTURE`, IDAM-Parsing, K5122-Log):

- **Der Emulator schreibt provably korrekt:** jede Spur 16 (bzw. 15) Sektoren, IDAM
  `(cyl=phys, head=0, id=1..16, size=1)`, IDs **1–16 sequenziell** im ZVE2-Schreibstrom.
- **Der Fehler beginnt exakt am System→Daten-Übergang:** Format 7 hat **4 System-Spuren** (0–3).
  `bis Spur 3` (nur System) → `FORMATIEREN beendet` ohne Fehler; `bis Spur 4` → `'S'` **bei
  Spur 4 = der ersten DATEN-Spur**.  Die System-Spuren verifizieren also fehlerfrei.
- **Das K5122-Verhalten ist für bestandene (0–3) und fehlgeschlagene (4,5) Spuren IDENTISCH**
  (gleiche READ/FORMAT-WRITE-Sequenz, gleiche Bytezahl) — es ist **kein differenzieller
  Emulator-Bug** an bestimmten Spuren.
- **Nicht die Interleave-Reihenfolge:** `.hfe` (`HfeImage`) bewahrt die physische Sektorfolge
  **bit-genau** und scheitert trotzdem → das `'S'` hängt nicht daran, dass der Raw-Pfad
  (`RawSectorImage`) Sektoren nach logischer ID zurückliest.  (Format 5 „mit ph. Sektorversatz,
  4 System" verifiziert dagegen als `.img` fehlerfrei — dieselbe 16×256-Geometrie, gleiche
  4 System-Spuren, nur andere Interleave-Notation.)

**Fazit:** Das `'S'` ist ein **FORMAT.COM-internes Verdikt im Daten-Spur-Verify** dieser beiden
exotischen Formate, nicht durch abweichendes Emulator-Read/Write ausgelöst.  Die definitive
Ursache erfordert die **Disassemblierung von FORMAT.COMs `'S'`-Verify-Pfad** — ein abgegrenzter, aber substanzieller RE-Schritt für **2 von ~30**
K5601-Formaten (alle Standard-Formate + S/V/W-Geometrien verifizieren fehlerfrei).
Repro: `python3 tests/system/drivers/format_all.py 7 --type img --upto 5` bzw. `--geo W 6`.

### 8.5 Fremd-Laufwerkstypen (Combo-Disks) — Teststand 2026-07-02

Mit den Combo-Boot-Disketten (§11) und `format_all.py --boot/--drive` (native
Formattabellen je Laufwerkstyp) ist das Formatieren+Verifizieren auf den vier
Fremd-Laufwerkstypen erstmals im Emulator testbar. Smoke (Format 0, Spur 0-2, `.hfe`,
mit Verify):

| Laufwerk | DPB | Aufz. | Format 0 (Verify) | Bemerkung |
|----------|-----|-------|-------------------|-----------|
| **K5600.10** (5″ 40 SS) | `10540` | DD/MFM | ✅ OK | native 40-Sp.-eins.-Liste = §3.4 U/W |
| **K5600.20** (5″ 80 SS) | `10580` | DD/MFM | ✅ OK | native 80-Sp.-eins.-Liste = §3.4 S; über C: |
| **MF6400/K5602.10** (8″ 77 DD) | `10877` | DD/MFM | ✅ **OK** | **8″-Format im Emulator neu möglich!** Soll 10416 B, gemessen 10035 |
| **MF3200** (8″ 77 SD)   | `00877` | **SD/FM** | ✅ **OK** (Fix 2026-07-02) | Soll **5208 B**, gemessen 4606 |

**Befund:** Alle vier Fremd-Laufwerkstypen formatieren+verifizieren fehlerfrei — die
Bestätigung, dass der Laufwerkstyp reine BIOS-Software ist und die formatagnostische
K5122 alle Geometrien **und beide Aufzeichnungsverfahren** schreibt.

**FM-8″-Fix (2026-07-02, MF3200):** Der Verify scheiterte anfangs mit `Fehler 'S'` schon
auf Spur 0 — **encoding-, nicht interleave-bedingt**. Wurzelursache: `K5122::parseFormatStream`
erkannte nur **MFM**-Adressmarken (`A1 A1 A1 FE/FB …`). Der FM-FORMAT-Schreibstrom (8″-SD)
trägt die Marken aber **ohne A1-Sync** direkt hinter 0x00-Sync (`00…00 FE …` / `00…00 FB …`,
Gap `0xFF`, Indexmark `FC`), sodass der Parser **0 Sektoren** lieferte → `FORMAT-COMMIT: keine
Sektoren im Strom` → nie geschrieben → Verify findet nichts. **Fix:** `parseFormatStream`
erkennt jetzt beide Verfahren (0x00-Sync-Marken = FM, A1-Sync = MFM) und **detektiert das
Verfahren aus dem Strom**; `commitFormatTrack` baut die gecachte Spur codierungstreu
(`buildTrack(sektoren, fmt_enc)` statt hart MFM). Der Verify-Read nutzte bereits korrekt FM
(Steuerwort `0x87`). Regression: 78 ctest + 58 Harness grün, neuer Test
`K5122FormatStream.ParseFormatStream_FM_RecoversSectors`. Repro:
`python3 tests/system/drivers/format_all.py --boot 8inchCombo --drive B 0 --upto 1`.

### 8.6 8″-SD/FM-BOOTdiskette komplett — formatieren → CPABCGEN → booten (Stand 2026-07-05)

Der §8.5-Fix ließ FORMAT.COM zwar „beendet" melden, das Ergebnis war aber **physisch
falsch**: die Spuren landeten auf **Kopf 1** (das einseitige MF3200 hat nur Kopf 0),
und `CPABCGEN` lehnte die Zieldiskette ab (`Systemspuren … nicht Sektorlaenge 128 o.
256` bzw. `Bdos Err On B: Select`). Ursache: der 8″-Slot wurde als 2-Kopf-K5601
gemountet, und die HFE-Spurablage passte nicht zum einseitigen Medium. Drei weitere
Bugs gefixt — jetzt **bootet der A5120 von einer selbst erzeugten 8″-FM-Diskette** voll
bis `A>` (CP/A 25.09.89):

1. **Single-Sided-Head-Forcing** (`K5122::handleCtrlPortAWrite`, /STR-Latch): bei
   `profile().num_heads <= 1` wird `current_head_ = 0` erzwungen — die Seitenwahl `/FR`
   hat auf einem physisch einseitigen Laufwerk keine Wirkung (das Combo-BIOS fährt `/FR`
   trotzdem und schrieb sonst auf den nicht existierenden Kopf 1). **Voraussetzung:** der
   8″-Slot muss mit dem einseitigen Profil `mf3200_8_ss77` gemountet sein (dazu neu
   `format_driver`-Env `FD_PROFILES="p0,p1,p2,p3"`).

2. **HFE-Single-Sided-Layout** (`HfeImage::read/writeSideBytes`): HFE verschränkt zwei
   Seiten zu je 256 B pro 512-Byte-Block. Bei `num_sides_ == 1` gibt es keine zweite Seite
   — die Spur liegt **kontinuierlich** (volle 512 B/Block). Vorher las/schrieb der Code
   auch einseitig nur 256 B je 512-Block und lief damit **über das Spurende in die
   Folgespur**: eine 26-Sektor-FM-Spur dekodierte als ~35 Sektoren (IDs 1-18, dann 1-…),
   jeder Schreibzugriff korrumpierte die Spur. (Nur `num_sides_==1` betroffen; zwei­seitige/
   5¼″-Disks unverändert.)

3. **FM-Datenfeld-Schreiben** (`K5122::commitWriteField`): FM hat **kein A1-Sync**; das
   Datenfeld beginnt bei der DAM (`FB`/`F8`) direkt hinter dem 0x00-Sync. Der BIOS-dio-
   Schreibpfad (den `CPABCGEN` nutzt) suchte nur `A1 A1 A1` → FM-Schreiben fand kein Sync →
   `CPABCGEN`-`Schreibfehler`. Jetzt verfahrensabhängig (Encoding der Zielspur).

**Ausgangszustand (Stand 2026-08-07): echte Leerdiskette.** FORMAT.COM formatiert eine
frische, unformatierte `.hfe` direkt — der frühere Gap-Blank-Hänger (§8.2/§8.2.1) und das
Wettrennen `Fehler 'U' SPUR DEFEKT` (`doc/analyse_format_leerspur.md`) sind behoben. Die
8″-Variante braucht deshalb **keine vorformatierte Vorlage** mehr (`mk_disk_template` bleibt
als eigenständiges Werkzeug erhalten, wird von der Pipeline aber nicht mehr benutzt).
FORMAT.COM formatiert bis Spur 76 (`beendet`, Verify-clean, Soll 5208 B / gemessen 5066 B),
`CPABCGEN B:` meldet `OK`, der Boot läuft durch.

**Pipeline-Tool** `tests/system/drivers/make_bootdisk.py` (6 Presets, s. §8.6.1): legt eine
**Leerdiskette** in den Ziel-Slot, fährt in EINEM Treiberlauf `FORMAT` (Format des Presets,
alle Spuren, mit Vergleichs-Lesen) → CCP → `CPABCGEN <LW>:` → `DIR <LW>:` (Tastatur-getrieben
über `format_driver` + passende `FD_PROFILES`) und bootet die erzeugte Disk anschließend kalt
zurück; Exit 0 + `BOOTDISK OK`.

#### 8.6.1 Mehr Formate + einseitige Laufwerkstypen (Stand 2026-07-06)

Die Boot-Disk-Pipeline wurde auf weitere Formate/Laufwerkstypen ausgebaut. Dazu:

- **Zwei neue einseitige Laufwerksprofile** (`drive_profile.cpp`): `ss_525_80` (5¼″,
  80 Spuren, 1 Kopf, MFM — **K5600.20**) und `mf6400_8_ss77` (8″, 77 Spuren, 1 Kopf,
  MFM — **K5602.10**, einseitig; das bestehende `mf6400_8_ds77` hat 2 Köpfe und greift
  nicht ins Kopf-0-Forcing).
- **Verallgemeinertes Template-Tool** `mk_disk_template` (ersetzt `mk_fm8_template`; seit
  2026-08-07 von der Pipeline **nicht mehr benutzt** — sie startet auf Leerdisketten —,
  bleibt aber als eigenständiges Werkzeug erhalten):
  `mk_disk_template <out.hfe> <fm|mfm|sys/data> <cyls> <sys_cyls> <sys_nsec> <sys_size>
  <data_nsec> <data_size>` erzeugt eine gültig vorformatierte EINSEITIGE Leerdiskette
  (Systemspuren + Datenspuren, beliebige Sektorgrößen). Die HFE-Seitenlänge wird aus der
  längsten gebauten Spur bestimmt (1 Spur-Byte = 2 HFE-Bytes; sonst würden große Spuren
  wie 8×1024 abgeschnitten).
- **Mischdichte-Unterstützung (System-34):** 8″-DD-Disks (MF6400) haben **FM-Systemspuren
  + MFM-Datenspuren**. Zwei Bausteine machen das lauffähig: (a) `mk_disk_template`
  akzeptiert `fm/mfm` (Sys/Daten getrennt); (b) `HfeImage::readTrack` erkennt das
  Verfahren **pro Spur** automatisch — FM- und MFM-Sync-Zellworte sind disjunkt, also wird
  erst mit dem Header-Verfahren decodiert und bei fehlender Adressmarke das andere
  versucht. Reine FM-/MFM-Disks bleiben unverändert.
- **C:-Laufwerk-Ziele** in `make_bootdisk.py` (MF6400, K5600.20 liegen laut Combo-BIOS auf
  C:): der B:-Slot wird nur belegt, damit FORMATs Laufwerkswahl durchläuft — seit
  2026-08-07 ebenfalls mit einer Leerdiskette (FD_DISKC = Ziel, FD_DISKC_FMT = leer).

**Sechs Presets** in `make_bootdisk.py` (`--preset`); Test-Registrierung als
`format_integration` (langsam, in der Standard-Regression ausgeschlossen — `tools/dev.sh
test` überspringt sie via `-LE format_integration`, `tools/dev.sh test-format` führt nur
sie aus):

| Preset (Test)            | Laufwerk / Verfahren            | Format                         | Status |
|--------------------------|---------------------------------|--------------------------------|:------:|
| `cpa780`                 | 5¼″ MFM (K5601, 2-seitig)       | 26×128 + 5×1024                | ✅ |
| `mf3200_fmt7`            | 8″ SD/FM (MF3200)               | 26×128 + 16×256   (296k)       | ✅ |
| `mf6400_fmt1`            | 8″ DD Mischd. (K5602.10, C:)    | 26×128 FM + 8×1024 MFM (600k)  | ✅ |
| `k5600_10_fmt1`          | 5¼″ 40-Sp. SS/MFM (K5600.10)    | 26×128 + 5×1024   (190k)       | ✅ |
| `k5600_20_fmt1`          | 5¼″ 80-Sp. SS/MFM (K5600.20, C:)| 26×128 + 5×1024   (390k)       | ✅ |
| `mf3200_fmt1`            | 8″ SD/FM (MF3200)               | 26×128 + 4×1024   (296k)       | ⚠️ offen |

**Offen — `mf3200_fmt1` (1024-B-FM-SD-Lesepfad):** Formatieren + `CPABCGEN` laufen durch
(die 1024-B-FM-Datensektoren werden mit gültiger CRC geschrieben, per parseTrack
`datacrc_bad=0`), und das OS bootet und läuft an — aber ein `DIR` scheitert mit
`Bdos Err On A: Bad Sector` beim Lesen der 1024-B-FM-Datenspur (6 Leseversuche auf Spur 3,
dann Abbruch). **256-B-FM (mf3200_fmt7) funktioniert**, der Lese-Stream-Aufbau ist für
256 B und 1024 B identisch — der Unterschied ist reine Sektorgröße. Der Loader liest
@OS.COM aus 1024-B-FM erfolgreich (OS läuft), nur der BIOS-Lesepfad des laufenden OS
scheitert bei 1024-B-FM. Rares Format (SD normalerweise 128/256 B). Nicht als Test
registriert; Preset bleibt für die Analyse.

Repro (ganze Kette, z. B. MF6400-Mischdichte):
```sh
tools/dev.sh tool format_driver
python3 tests/system/drivers/make_bootdisk.py --preset mf6400_fmt1        # → "BOOTDISK OK"
tools/dev.sh test-format                                             # alle Format-Tests
```

**Regression:** volle Suite **583/583** (ohne die 5 `format_integration`-Tests) grün; die
5 Boot-Disk-Tests grün.

---

## 9. Nachstellen im Emulator

### 9.0 Komfort-Runner `tests/system/drivers/format_all.py` (alle §3-Formate)

Für das Formatieren **mehrerer** §3-Formate am Stück (Phase A, .hfe):

```sh
tools/dev.sh tool format_driver                 # Treiber bauen (einmalig)
python3 tests/system/drivers/format_all.py --list              # Formattabelle
python3 tests/system/drivers/format_all.py 0 1 4 6 E --upto 5  # Smoke (Spur 0-5) je Format, mit Verify
python3 tests/system/drivers/format_all.py --all --full        # alle 15, volle 160 Spuren
python3 tests/system/drivers/format_all.py 0 --full --dir-verify --outdir out/formats
```

Der Runner erzeugt je Format eine **Temp-Kopie der Boot-Disk (A:)** und ein
**B:-Ziel**, generiert das FORMAT.COM-Script (Menü-Navigation + Verify), fährt
`format_driver` und prüft `FORMATIEREN beendet` ohne `SPUR DEFEKT`.  Menü:
`0-3` direkt · `4-7` nach `X` · `E-K` nach `X`,`Y`.  Ergebnis-Images unter `--outdir`.

**`.img` vs `.hfe` (`--type`):**
- `--type img` (Phase B): `format_driver` **legt** die `.img` per **`create`** in der
  Geometrie des passenden `DiskFormat` an (0xE5-gefüllt; kein Python-Vorbau).  Eine frische
  0xE5-`.img` liest über den `ImgCodec` als **gültig formatierte** Disk → FORMAT.COMs
  Vorlesung findet echte Sektoren → **kein** BUSRQ-Hänger.  Format→`DiskFormat`-Zuordnung:
  s. `FORMATS`-Tabelle in `tests/system/drivers/format_all.py` bzw. `k5601_16x256`/`k5601_26x128`/
  `k5601_9x512`/`k5601_10x512`/`cpa800`/`cpa780` in `FormatParser::builtinFormats()`.
- `--type hfe` (Phase A, Default): das Ziel ist eine **echte, unformatierte Leerdiskette**
  in der Geometrie des Laufwerks (`createB` = leerer Formatname).  Das ist der Anwenderfall
  und die schärfere Prüfung: FORMAT.COM muss die Spurlänge auf markenlosem Gap-Fluss messen
  und darf sich beim Vergleichs-Lesen nicht auf Restdaten stützen.  (Bis 2026-08-07 war hier
  eine Kopie eines gültigen HFE-Templates nötig, s. §8.2.)

**`create` statt `open` (Emulator-API):** `DiskImage::create(path, fmt, wp)` /
`A5120Machine::createDisk(drive, path, format_name, wp)` legen eine **neue leere** Disk an:
Endung `.hfe` → leeres, formatagnostisches Template (80×2, Format egal); sonst `.img` →
0xE5-Sektorimage in der Geometrie von `format_name` (Pflicht).  So muss man Testimages nicht
mehr vorab per Skript erzeugen.  `tools/format_driver <A> <B> <script> [createB_format]` legt
B: via `create` an, wenn der 4. Parameter gegeben ist.

### 9.1 Direkter Treiber-Aufruf

Treiber `tools/format_driver` (scriptgesteuerter Zwei-Disk-Treiber, Tastatur-Script;
mountet Disk B: **schreibend**, FORMAT-Writes landen in der diskB-Datei):

```sh
tools/dev.sh tool format_driver          # baut + zeigt Usage
# Zwei Disketten anlegen (immer Kopien verwenden, NICHT die Fixtures!):
D=$(mktemp --suffix=.img); cp disks/cpa_cpa780_k5601_clock.img "$D"   # Ziel B:
A=disks/cpa_cpa780_k5601_clock.img                                     # Boot A:
build/format_driver "$A" "$D" script.txt
```

Beispiel-Script (FORMAT.COM, Format 1 = CP/A BC, Spur 0-4, **mit Verify**):

```
boot 80
type 12:00:00
enter
boot 5
type FORMAT
enter
boot 30
enter            # Funktion 0 = Formatieren
boot 6
type B           # Laufwerk B
enter            # Diskette-eingelegt-Quittung
boot 10
enter            # Vergleichs-Lesen = j (mit Verify)
boot 8
type 1           # Format 1 (CP/A BC)
boot 6
enter            # von Spur 0
boot 5
type 4           # bis Spur 4
enter
boot 6
type j           # Warnung bestätigen
boot 90
dump ergebnis
```

Script-Befehle: `boot/run <Mio-Takte>`, `type <text>`, `enter`, `key <name>`,
`dump <label>` (80×24-Bildschirm), `wp <0|1>` (Schreibschutz B). Vollständiger
160-Spuren-Lauf ≈ 2,6 Mrd. Takte (~5 min bei ~9 Mio. Takten/s).


## 10. Laufwerke

Übliche Diskettenlaufwerke an K1520-Rechnern (bestimmen, welche Formate FORMAT.COM
anbietet — siehe §2):

### Diskettenlaufwerk K5600.10
(auch als "Minifolienspeicher MFS 1.2" bezeichnet)
Es handelt sich hier um ein 5¼-Zoll-Diskettenlaufwerk, das Disketten einseitig nach dem FM- oder MFM-Verfahren mit 40 Spuren beschreiben konnte. Die erreichbare Kapazität lag dadurch bei maximal 200 KByte.

### Diskettenlaufwerk K5600.20
(auch als "MFS 1.4" bezeichnet)
Dieses seltene Laufwerk war eine Weiterentwicklung des K5600.10, lehnte sich technisch auch an seinen Vorgänger an. Es konnte Disketten einseitig mit 80 Spuren im FM- oder MFM-Verfahren beschreiben, die erreichbare

### Diskettenlaufwerk K5601
(auch als "MFS 1.6" bezeichnet)
Dieses 5¼-Zoll-Laufwerk wurde ab Mitte der 1980er Jahre in fast allen DDR-Rechnern eingesetzt und war in seiner Speicherkapazität erstmals den 8-Zoll-Laufwerken überlegen. Auf ihm konnten Disketten bis 800 KByte Größe formatiert werden (2 Seiten, 80-Spuren FM oder MFM).

### Diskettenlaufwerk K5602
Dieses seltene 8-Zoll-Diskettenlaufwerk wurde hauptsächlich im Großrechnerbereich, im Bürocomputer A5110 und in den ersten Versionen des A5120 benutzt.
Die Aufzeichnung erfolgte einseitig mit 77 Spuren im FM-Verfahren und ermöglichte Diskettenformate bis 300 KByte.

### Diskettenlaufwerk MF6400
(auch als "FS 6400" bezeichnet)
Auch dieses 8-Zoll-Diskettenlaufwerk war, wie sein Vorgänger, das MF3200, ein Importgerät, hergestellt von der Firma MOM.

Die Aufzeichnung erfolgte einseitig mit 77 Spuren im FM- oder MFM-Verfahren und gestattete damit Kapazitäten bis 600 KByte pro Diskettenseite. Durch Verwendung doppelt gelochter Disketten oder nachträglicher Doppellochung konnte die Diskette nach Umdrehen auch auf der anderen Seite benutzt werden, was eine Kapazitätsverdoppelung bedeutete.

### Diskettenlaufwerk MF3200
Ein 8-Zoll-Importgerät (Firma MOM), Vorgänger des MF6400. Aufzeichnung **einseitig,
77 Spuren, einfache Dichte (SD/FM)** — bis ~350 KByte pro Seite.

### 10.1 BIOS-Laufwerkstyp-Codes (`dpbtyp` / Generierungswert `diskA/B/C/D`)

Der Generierungswert im BIOS (`.prn`-Zeile `diskX equ …`) kodiert Verify/Dichte/Seiten/
Zoll/Spuren; FORMAT.COM leitet daraus die Kopfzeile und die Formatliste ab:

| Code    | Aufzeichnung             | Beispiel-Laufwerk        | Kopfzeile FORMAT.COM               |
|---------|--------------------------|--------------------------|------------------------------------|
| `10540` | DD, SS, 5″, 40 Spuren    | K5600.10 (MFS 1.2)       | `5 1/4", 40 Sp., eins.`            |
| `10580` | DD, SS, 5″, 80 Spuren    | K5600.20 (MFS 1.4)       | `5 1/4", 80 Sp., eins.`            |
| `11580` | DD, DS, 5″, 80 Spuren    | K5601 (MFS 1.6)          | `5 1/4", 80 Sp., doppels.`         |
| `00877` | SD, SS, 8″, 77 Spuren    | MF3200                   | `8", eins., einf. Dichte`          |
| `10877` | DD, SS, 8″, 77 Spuren    | K5602.10 / MF6400        | `8", eins., dopp. Dichte`          |

(Führende Ziffer `1…` = mit Verify nach Schreiben; z. B. `13580` = K5601 mit Verify.)

---

## 11. Laufwerkstypen im Emulator (Combo-Boot-Disketten)

Der Laufwerkstyp ist eine reine **BIOS-Software-Eigenschaft** (§2). Um alle fünf
relevanten Laufwerkstypen ohne echte Hardware im Emulator abzufragen, gibt es zwei
**Combo-Boot-Disketten**, deren BIOS die Laufwerke B:/C: als Fremdtypen konfiguriert:

| Boot-Disk (`disks/…`)                     | A:      | B:                  | C:                        |
|-------------------------------------------|---------|---------------------|---------------------------|
| `cpa_cpa780_k5601_noclock`         | K5601   | K5601               | K5601                     |
| `cpa_cpa780_combo5zoll_noclock`       | K5601   | **K5600.10** `10540`| **K5600.20** `10580`      |
| `cpa_cpa780_combo8zoll_noclock`       | K5601   | **MF3200** `00877`  | **K5602.10/MF6400** `10877` |

Alle `noclock`/`noclk`-Disks booten ohne Uhr-Abfrage direkt nach `A>`. Die Boot-Meldung
zeigt die Laufwerkskonfiguration, z. B. für die 5inchCombo:
`A:5"(80,DD,DS)/B:5"(40,DD,SS)/C:5"(80,DD,SS)`.

### 11.1 Menüs live abgreifen — `tools/capture_format_menus.py`

Der Runner bootet die passende Combo-Disk, startet FORMAT.COM, wählt das Laufwerk,
blättert mit `X`/`Y`/`Z` durch alle Menüseiten und dumpt jeden Bildschirm — **ohne zu
formatieren** (reines Menü-Capturing, es wird kein Format-Key gesendet):

```sh
tools/dev.sh tool format_driver               # Treiber bauen (einmalig)
python3 tools/capture_format_menus.py --list  # Laufwerks-Matrix
python3 tools/capture_format_menus.py --all --outdir out/menus
python3 tools/capture_format_menus.py MF3200 MF6400   # nur die 8″-Laufwerke
```

Technik: `format_driver` mountet jetzt optional ein drittes Laufwerk **C:** über die
Env-Var `FD_DISKC=<pfad>` (bzw. `FD_DISKC_FMT=<DiskFormat>` zum Neu-Anlegen). Der
gewählte Laufwerksbuchstabe liefert FORMAT.COM den Typ aus dem BIOS-DPB — für die reine
Menüanzeige muss der Slot nur belegt sein, das Medium wird nicht gelesen.

Ergebnis: **alle fünf Laufwerkstypen-Menüs sind Emulator-Mitschnitte** — K5601 (§3),
K5600.10/.20 (§3.5), MF3200/MF6400 (§5). Die K5601-Kontrollmessung stimmt byte-genau mit
§3 überein (Tool-Validierung).

### 11.2 Formate auf den neuen Laufwerkstypen testen (Vorbereitung)

`tests/system/drivers/format_all.py` formatiert+verifiziert scriptgesteuert. Für die neuen
Laufwerkstypen wählt man Boot-Disk und Ziel-Laufwerk über `--boot`/`--drive`:

```sh
# K5600.20 (C:, 5" 80 SS) — Smoke über Spur 0-5, mit Verify, als .hfe:
python3 tests/system/drivers/format_all.py --boot 5inchCombo --drive C 0 4 7
# MF3200 (B:, 8" SD) — 8″-Formate:
python3 tests/system/drivers/format_all.py --boot 8inchCombo --drive B 0 5
```

Der Format-Write landet dann physisch im gewählten Laufwerk (B: bzw. C:). Für `.img`-
Ziele braucht jedes neue 8″/SS-Format eine passende `DiskFormat`-Geometrie in
`FormatParser::builtinFormats()` (analog zu den bestehenden `k5601_*`); solange die noch
fehlt, `--type hfe` (formatagnostisch) verwenden. Status/offene Punkte s. §8.5.