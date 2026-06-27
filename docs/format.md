# Diskettenformate (FORMAT.COM / FORMATB.COM)

Dieses Dokument listet **alle** im A5120-Emulator über die CP/A-Formatierprogramme
`FORMAT.COM` und `FORMATB.COM` auswählbaren Diskettenformate auf, beschreibt den
Bedien-Dialog (inkl. der mehrseitigen Format-Menüs „X = Menü #2" usw.) und hält fest,
welche Formate der Emulator aktuell **fehlerfrei** schreiben und verifizieren kann.

Quelle: live aus dem Emulator abgegriffen (Treiber `tools/format_driver`, Boot-Disk
`cpadisk_autofs_clock_noautoexec.img`, Ziel-Diskette in Laufwerk **B:**). Reproduktion
siehe Abschnitt *Nachstellen im Emulator*.

---

## 1. Die beiden Programme

| Programm      | Version (Titelzeile)                                          | Auf der Boot-Disk |
|---------------|---------------------------------------------------------------|-------------------|
| `FORMAT.COM`  | „Disketten-FORMAT fuer Buerocomputer, Version **19.05.89**"   | ✔ |
| `FORMATB.COM` | „Disketten-FORMAT fuer CP/A auf Buerocomputer, Version **02.04.87**" (älter) | ✔ |

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

Welche Formate FORMAT.COM/FORMATB.COM anbieten, **hängt vom angeschlossenen Laufwerk
ab** (siehe Laufwerksliste am Dokumentende, §10). Das Programm liest beim Start den
Laufwerkstyp aus dem BIOS und zeigt ihn in der **Kopfzeile** des Format-Menüs:

| Programm      | Default-Kopfzeile im Emulator |
|---------------|-------------------|
| `FORMAT.COM`  | `Formate fuer 5 1/4", 80 Sp., doppels. ["A": autom. Formaterk.]` |
| `FORMATB.COM` | `Formate fuer 5 1/4", 80 Spuren, doppelseitig` |

Der emulierte A5120 besitzt ein festes **K5122-Laufwerk vom Typ K5601 / „MFS 1.6"
(5¼″ / 80 Spuren / doppelseitig, DD/DS)** — die Boot-Meldung bestätigt
`A:5"(80,DD,DS)/B:.../C:...`. Deshalb zeigt der Emulator immer die **5¼″-Menüs**.

**Zwei Wege zu anderen Formaten:**

1. **Geometrie-Umschalter S/T/U/V/W** (im Menü) reduzieren die *logische* Geometrie
   eines 80-Spur-DS-Laufwerks auf einseitig bzw. 40 Spuren (per Doppel- oder
   Einzelschritt). Kopfzeile und Formatliste passen sich an (alle 5¼″-Varianten:
   §3.4). Ein 80-Spur-DS-Laufwerk kann so 40-Spur-Disketten und einseitige Formate
   physisch ebenfalls schreiben.
2. **Anderes physisches Laufwerk** am realen Rechner: Mit einem **8″-Laufwerk
   (K5602 bzw. MF6400, 77 Spuren)** zeigt die Kopfzeile `8"` und es erscheinen die
   **8″-/77-Spuren-Formate** (§5). Auch ein echtes 40-Spur- oder einseitiges
   5¼″-Laufwerk (K5600.10/.20) meldet sich entsprechend.

> **Im Emulator nicht reproduzierbar:** Da die emulierte K5122 fest ein 80-Spur-DS-
> 5¼″-Laufwerk ist, lassen sich die **8″-Menüs hier nicht abgreifen** — §5 dokumentiert
> sie aus den Laufwerks-Eckdaten. Selbst „@ Spezielles Format" ist drive-begrenzt
> (fragt „Anzahl phys. Spuren … `<= 80`"), kann also kein 8″-77-Spuren-Layout erzeugen.

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

---

## 4. FORMATB.COM (V02.04.87) — Formatliste

Default-Laufwerk **5¼″, 80 Spuren, doppelseitig**. Etwas kürzere Formatliste als die
neuere FORMAT.COM; keine `;192 Dir.eintr.`-Varianten, keine `A`-Markierung im Menütext.

### 4.1 Menü #1 (Taste `X` → Menü #2)

| Wahl | Sektoren×Größe / Layout            | System | Kapazität | Bezeichnung |
|------|------------------------------------|:------:|----------:|-------------|
| `0`  | 5×1024, Sp. 0-159                  | 0      | 800k      | CP/A        |
| `1`  | 26×128 Sp. 0-2; 5×1024 Sp. 3-159   | 4      | 780k      | CP/A BC     |
| `2`  | 5×1024, Sp. 0-159                  | 4      | 780k      | SCP1715     |
| `3`  | 5×1024, Sp. 0-159                  | 2      | 790k      | HU Krz      |

Geometrie-Umschalter: `S` 80 Sp. einseitig · `T` 40 Sp. Doppelschritt doppelseitig ·
`U` 40 Sp. Doppelschritt einseitig · `V` 40 Sp. Einzelschritt doppelseitig ·
`W` 40 Sp. Einzelschritt einseitig · `@` Spezielles Format.

### 4.2 Menü #2 (Taste `Y` → Menü #3, `Z` → Menü #1)

| Wahl | Sektoren×Größe / Layout                  | System | Kapazität | Bezeichnung |
|------|------------------------------------------|:------:|----------:|-------------|
| `4`  | 16×256, Sp. 0-159; ohne ph. Sektorvers.  | 4      | 624k      | SCP         |
| `5`  | 16×256, Sp. 0-159; mit ph. Sektorvers.   | 4      | 624k      |             |
| `6`  | 26×128, Sp. 0-159; Sektorfolge 1,7,13…   | 0      | 520k      |             |
| `7`  | 16×256, Sp. 0-153; Sektorfolge 1,4,7…    | 4      | 600k      | ZIK-NK      |

### 4.3 Menü #3 (Taste `X` → Menü #2, `Z` → Menü #1)

| Wahl | Sektoren×Größe / Layout                  | System | Kapazität | Bezeichnung |
|------|------------------------------------------|:------:|----------:|-------------|
| `E`  | 9×512, Sp. 0-159; 2k-BDOS-Blöcke         | 2      | 710k      |             |
| `F`  | 9×512, Sp. 0-159; 4K-BDOS-Blöcke         | 2      | 708k      | VORTEX      |
| `G`  | 10×512, Sp. 0-159; Sektorfolge 1,4,7…    | 2      | 788k      | NGB         |

---

## 5. 8″-Laufwerke (K5602 / MF6400) — 77 Spuren

Schließt man an einen A5110/A5120 ein **8″-Laufwerk** an, melden FORMAT.COM/FORMATB.COM
in der Kopfzeile `8"` mit **77 Spuren** und bieten die passenden 8″-Formate an. Diese
Geräte zeichnen **einseitig** auf (Rückseite nur über doppelt gelochte Disketten nach
Umdrehen):

| Laufwerk | Aufzeichnung | Kapazität (pro Seite) |
|----------|--------------|-----------------------|
| **K5602**  | 8″, 77 Spuren, **FM** (Single Density)        | bis ~300 KByte |
| **MF6400** (FS 6400) | 8″, 77 Spuren, **FM oder MFM**     | bis ~600 KByte |

Typische 8″-Sektorlayouts (77 Spuren, einseitig), wie sie FORMAT.COM/FORMATB.COM mit
angeschlossenem 8″-Laufwerk schreiben:

| Aufzeichnung | Sektoren×Größe / Spuren | ≈ Kapazität | Bemerkung |
|--------------|-------------------------|------------:|-----------|
| FM (SD)  | 26×128, Sp. 0-76        | ~250 KByte | IBM-3740-kompatibel, K5602 |
| FM (SD)  | 15×256, Sp. 0-76        | ~290 KByte | K5602 (größere Sektoren)   |
| MFM (DD) | 26×256, Sp. 0-76        | ~500 KByte | MF6400                     |
| MFM (DD) | 15×512, Sp. 0-76        | ~580 KByte | MF6400                     |
| MFM (DD) | 8×1024, Sp. 0-76        | ~620 KByte | MF6400                     |

> ⚠️ **Nicht aus dem Emulator abgegriffen.** Die emulierte K5122 ist fest ein
> 5¼″-K5601-Laufwerk; die 8″-Menüs erscheinen nur auf echter Hardware mit 8″-Laufwerk.
> Die obige Tabelle ist aus den Laufwerks-Eckdaten (§10) und den Standard-8″-Formaten
> abgeleitet — die **exakten Menü-Buchstaben/Kapazitäten** der jeweiligen FORMAT-Version
> sind dort abzulesen, wo ein 8″-Laufwerk angeschlossen ist. Der Nutzer hat 8″/77-Spuren
> auf realer Hardware erfolgreich mit FORMAT.COM/FORMATB.COM formatiert; im Emulator ist
> dieses Format mangels 8″-Laufwerk (noch) nicht nachstellbar.

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

**FORMATB.COM** weicht in der Reihenfolge ab: nach der Laufwerks-Quittung kommt
**direkt** das Format-Menü (es gibt **keinen separaten „Vergleichs-Lesen?"-Prompt**),
danach Von/Bis-Spur und die Warnung.

Der Verify-Prompt von FORMAT.COM lautet wörtlich:
`Vergleichs-Lesen nach dem Schreiben? (n, ENTER=j):` — **ENTER bzw. `j` = mit Verify**.

---

## 8. Status im Emulator (Stand 2026-06-27, Branch `formating-disks`)

| Programm      | Ergebnis |
|---------------|----------|
| **FORMAT.COM**  | ✅ **Funktioniert** — formatiert **mit Verify** fehlerfrei. Verifiziert für alle vier Sektorgrößen: **128 B** (Format 1, Systemspuren 0-2), **256 B** (Format 4), **512 B** (Format E), **1024 B** (Format 1, Datenspuren). Voller Lauf Format 1 über **alle 160 Spuren**: `FORMATIEREN beendet` ohne eine einzige `SPUR DEFEKT`-Meldung; `DIR` der frischen Disk → `No File` (gültige, leere CP/A-Disk). |
| **FORMATB.COM** | ❌ **Hängt bei „FORMATIEREN auf Spur 0"** — kommt nie zu „beendet". Ursache: anderes Schreibprotokoll (kontinuierlicher Stream + MK-Strobes, index-puls-delimitierte Spuren statt /STR-Schreibstrobe), das der K5122 noch nicht committet. Der korrekte Spurinhalt + CRC wird erzeugt, aber nie ins Image geschrieben. |

**Getestet:** nur die **80-Spur-DS-Geometrie** (Default), dort alle vier Sektorgrößen.
**Noch nicht im Emulator verifiziert:** die einseitigen und 40-Spur-Geometrien (S/T/U/V/W,
§3.4) — sie sollten auf dem 80-Spur-DS-Laufwerk physisch schreibbar sein (einseitig =
nur eine Seite, 40-Spur = Doppelschritt), sind aber ungetestet. **Nicht möglich:** die
8″-Formate (§5), da der Emulator kein 8″-Laufwerk modelliert.

> **Ziel** (alle Formate fehlerfrei): FORMAT.COM erfüllt das für die getesteten
> Sektorgrößen der 80-Spur-DS-Geometrie bereits. Offen: (a) einseitige/40-Spur-Geometrien
> verifizieren, (b) für FORMATB.COM fehlt im K5122 der index-/MK-delimitierte Vollspur-
> Commit (Byte-Rate an die Index-Periode gekoppelt), (c) 8″-Laufwerk im Emulator
> modellieren. Details/RE-Stand: `doc/design/07_k5122_afs.md §7.3a` und die Memory-Notiz
> `project_formatb_different_protocol`.

Der frühere CRC-Konventionskonflikt auf den **128-B-Systemspuren** (FORMAT-Verify
meldete `'C' SPUR DEFEKT`) ist mit dem codierungstreuen Ein-CRC-Lesepfad
(Standard-IBM-CCITT, §10.3) **gelöst** — daher verifiziert FORMAT.COM jetzt auch die
Systemspuren.

---

## 9. Nachstellen im Emulator

Treiber `tools/format_driver` (scriptgesteuerter Zwei-Disk-Treiber, Tastatur-Script;
mountet Disk B: **schreibend**, FORMAT-Writes landen in der diskB-Datei):

```sh
tools/dev.sh tool format_driver          # baut + zeigt Usage
# Zwei Disketten anlegen (immer Kopien verwenden, NICHT die Fixtures!):
D=$(mktemp --suffix=.img); cp disks/cpadisk_autofs_clock_noautoexec.img "$D"   # Ziel B:
A=disks/cpadisk_autofs_clock_noautoexec.img                                     # Boot A:
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

Übliche Diskettenlaufwerke an K1520-Rechnern (bestimmen, welche Formate FORMAT.COM/
FORMATB.COM anbieten — siehe §2):

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