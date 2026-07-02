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

## 8. Status im Emulator (Stand 2026-06-28, Branch `formating-disks`)

| Programm      | Ergebnis |
|---------------|----------|
| **FORMAT.COM**  | ✅ **Funktioniert** — formatiert **mit Verify** fehlerfrei. Verifiziert für alle vier Sektorgrößen: **128 B** (Format 1, Systemspuren 0-2), **256 B** (Format 4), **512 B** (Format E), **1024 B** (Format 1, Datenspuren). Voller Lauf Format 1 über **alle 160 Spuren**: `FORMATIEREN beendet` ohne eine einzige `SPUR DEFEKT`-Meldung; `DIR` der frischen Disk → `No File` (gültige, leere CP/A-Disk). |
| **FORMATB.COM** | ✅ **Format OK**, ⚠️ **Verify-Meldung `'V'` = echte Versionsinkompatibilität (kein Emu-Bug)**. Schreibt die Spur korrekt (`>>> FORMAT-WRITE`, Image-md5 identisch zu FORMAT.COM) und läuft bis `FORMATIEREN beendet`. **Wurzelursache 2026-06-30 geklärt** (s. §8.1, Problem 2): FORMATB.COM ist V**02.04.87**, das BIOS V**25.09.89**; die CDB-Flag-Konvention wurde dazwischen umorganisiert (BIOS-Quelle: „Bit 0 Verify nach Schreiben auf Bit 6 verlegt", „Struktur angepasst an ft.kom"). FORMATBs einkompiliertes CDB-Template (`[0]=0x60`, Bit 5 gesetzt) wird vom 1989er-BIOS als `diofhd` (Kopf-hoch, **kein Transfer**) gedeutet → Verify-Read füllt `0x63B7` nicht → `'V'`. Träte auf echter HW mit dieser BIOS-Version genauso auf. **FORMAT.COM (1989) passt zur Konvention und verifiziert fehlerfrei.** Zusätzlich adressiert FORMATB den Format-Write **immer physisch Laufwerk 0**. |

**Getestet:** nur die **80-Spur-DS-Geometrie** (Default), dort alle vier Sektorgrößen.
**Noch nicht im Emulator verifiziert:** die einseitigen und 40-Spur-Geometrien (S/T/U/V/W,
§3.4) — sie sollten auf dem 80-Spur-DS-Laufwerk physisch schreibbar sein (einseitig =
nur eine Seite, 40-Spur = Doppelschritt), sind aber ungetestet. **Nicht möglich:** die
8″-Formate (§5), da der Emulator kein 8″-Laufwerk modelliert.

### 8.2 Scriptgesteuerte Formatier-Pipeline für alle §3-Formate (Stand 2026-07-01)

`tools/format_all.py` formatiert die nativen K5601-Formate aus §3 (Menü #1/#2/#3)
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
--type img`, s. §9.0) — eine frische 0xE5-`.img` liest über `RawSectorImage` als gültig
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
   `OUT(11H)=0x03` = **Index-Interrupt sperren** (`wmode=0`, kein Write) — die FORMATB-
   Keepalive (§8.1) greift nur im `write_mode_`.  Mit gesperrtem Index kann die Koroutine nie
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
`python3 tools/img_to_hfe.py --blank --cyls 80 --heads 2 B.hfe; cp disks/cpadisk_autofs_clock_noautoexec.img A.img;`
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
(+ vorhandene `cpa200`/`cpa640`).  Aufruf z. B. `python3 tools/format_all.py --geo W --type img 0 4`.

> **Ziel-Status:** 80-Spur-DS (§3) + §3.4-Geometrien (S/V/W einseitig/Einzelschritt) formatieren
> +verifizieren als `.hfe`/`.img`; Doppelschritt (T/U) als `.hfe`.  Offen: (a) Interleave-Formate
> (Sektorfolge 1,4,7… — Format 7, W:6), s. §8.4; (b) Doppelschritt-`.img` (Mapping);
> (c) **FORMATB.COM-Verify** (§8.1); (d) 8″-Laufwerk (§5).  RE-Stand: `doc/design/07_k5122_afs.md`,
> Memory `project_format_all_pipeline`/`project_formatb_different_protocol`.

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
Ursache erfordert die **Disassemblierung von FORMAT.COMs `'S'`-Verify-Pfad** (analog zur
FORMATB-Analyse §8.1) — ein abgegrenzter, aber substanzieller RE-Schritt für **2 von ~30**
K5601-Formaten (alle Standard-Formate + S/V/W-Geometrien verifizieren fehlerfrei).
Repro: `python3 tools/format_all.py 7 --type img --upto 5` bzw. `--geo W 6`.

### 8.1 FORMATB.COM — vollständige Diagnose (Stand 2026-06-28)

Per Disassembly (FORMATB.COM + BIOS-Quelle `cpadisk_*.prn`) und gezielten Trace-Experimenten
vollständig aufgeklärt. Es gibt **zwei** voneinander unabhängige Probleme.

#### Wie FORMATB eine Spur formatiert (Mechanismus)

FORMATB formatiert **nicht** über das normale BIOS-`dio`, sondern fährt ZVE2 direkt mit einer
selbstmodifizierenden Co-Routine. Ablauf einer Spur:

1. **Eintritt:** ein `/STR`-Schreibstrobe (`OUT(10H)=B4`, /WE=0, mit sauberer /STR-Flanke nach
   `B9/BD`) setzt `write_mode_`; ZVE2 streamt die Spur über `OUT(14H)`.
2. **Drei `JR $`-Schleifen**, vom BIOS (PC `0xDEEA`) als Opcode `0x18` „scharf gemacht":
   ZVE2-Leading-Gap `0x38F6`, ZVE2-Trailing-Gap `0x398B`, und **ZVE1-Wartepark `0x38C7`**
   (`18 FE`).
3. **FORMATB hängt seine eigene ISR an den Disketten-Index-Interrupt** (`ivdsk1`, Vektor
   `0xE8`, lt. BIOS-Quelle; IM2-Tabellen-Slot per `LDIR` mit `0x3A2E` überschrieben). Die ISR
   `0x3A2E` = `LD (HL),3E; EX DE,HL; EI; RETI` patcht **eine** Gap-Schleife (Opcode `18`→`3E`,
   d.h. fällt durch) und **vertauscht HL↔DE** — der **erste** Index patcht so die Leading-,
   der **zweite** die Trailing-Schleife.
4. Nach beiden Patches läuft ZVE2 zu Ende, schreibt sein **dtrret** bei `0x3897`
   (`XOR A; LD (38C8H),A` → ZVE1-`JR $` `18 FE`→`18 00`, fällt durch) und **weckt damit ZVE1**.

ZVE2 läuft mit **IFF=0** (kein EI/IM); seine Gap-Schleifen sind also **nur per Speicher-Patch
durch ZVE1s Index-ISR** brechbar, nicht per ZVE2-Interrupt. Das Ganze braucht also **mehrere
Index-Interrupts pro Spur**.

#### Problem 1 — Hang: der Index-Interrupt wird mitten im Format abgeschaltet ✅ GELÖST

FORMATB hängte bei `FORMATIEREN auf Spur 0`, weil der Index-Interrupt nach dem **ersten** Mal
abgeschaltet wurde → die ISR feuerte nie ein zweites Mal → Trailing-Schleife nie gepatcht →
ZVE2 hängt in `0x3988`, ZVE1 ewig im `JR $`-Park.

**Ursache (BIOS-Quelle):** Der BIOS-1-Sekunden-Timer `tim1uu` (`0xE682`) zählt den
Index-Watchdog `fl.zto` herunter (`SUB 4`/s) und ruft bei Ablauf `headup` (`0xE3BF`:
`LD A,3; OUT (flcoac=11H),A` = **Index-Interrupt sperren** + Motor aus). Normalerweise frischt
die **BIOS**-Index-ISR `fl.zto` bei jedem Index auf — FORMATBs ISR (`0x3A2E`) tut das nicht.
Auf echter Hardware ist dieser Motor-Abschalt-Watchdog während einer laufenden Übertragung
unterdrückt (`pretx+1 == 0` → `tim1uu` überspringt `headup`); da FORMATBs Format-Write am
BIOS-`dio` vorbeiläuft, wird dieser „Transfer läuft"-Zustand nicht gesetzt → `headup` schlägt
mitten im Format zu.

**Fix (K5122, `k5122.cpp::ioWrite`):** Solange ein Vollspur-FORMAT-Write läuft (`write_mode_`),
ignoriert die Karte das Port-A-Interrupt-**Sperr**-Wort (`OUT(11H)` mit Bits3-0=`0011`, Bit7=0).
Der Index-Interrupt bleibt damit über die ganze Format-Übertragung aktiv, FORMATBs ISR patcht
beide Gap-Schleifen, ZVE2 erreicht sein dtrret, ZVE1 wird geweckt. Tightly-scoped (nur im
`write_mode_`, nur das Sperrwort) → Boot/Read/FORMAT.COM unberührt; alle Tests grün
(569 gtest + 58 Harness). Ergebnis: FORMATB schreibt die Spur korrekt (Image-md5 identisch zu
FORMAT.COM) und läuft bis `FORMATIEREN beendet`.

#### Problem 2 — Verify: `'V' SPUR DEFEKT` — WURZELURSACHE GEKLÄRT 2026-06-30: FORMATB(1987)↔BIOS(1989)-Versionskonflikt (KEIN Emulator-Bug)

Per Instruktions-Trace (ZVE1-PC ab Format-Ende) + Disassembly der jetzt residenten FORMATB-
Verify-Routine **vollständig aufgeklärt** — die frühere „kein /STR / ZVE2 hält den Bus"-Hypothese
war falsch. Korrigiertes Bild:

1. **Der `>>> WRITE S=1 bytes=128` ist ein PHANTOM, kein Testmuster-Write.** Der Schreib-Puffer
   `write_buf_` enthält an dieser Stelle **6357 Bytes `0x4E`** (Format-Gap, nicht das vermutete
   `0x53`-Muster). Auslöser ist eine **streunende `/STR`-Flanke** (`OUT(10H)=0xB6`, /WE=0, mit
   `busrq=1`, `write_mode_=1`) während FORMATBs Format-Ende: unsere `/STR`-Flanken-Logik deutet
   sie im ZVE2-Kontext als Sektor-Schreib-Commit (`commitWrite`), schreibt 128 Gap-Bytes in
   Sektor 1 und reißt `write_mode_`/`transferring_` vorzeitig ab. **Es gibt kein echtes
   Testmuster-Schreiben.**

2. **Die Verify-Routine läuft korrekt durch — sie liest über den NORMALEN BIOS-Lesepfad.**
   Schleife `0x088B` (Wiederhol-Zähler `[0x383A]`=2), `CALL sub_09C5` (`0x08A2`) → `sub_0A30`
   (Lese-Eintrag, `RES 4,(IY+0)`) → `sub_0D26` → BIOS `diskio`. `sub_0D26` springt über den
   BIOS-Erweiterungs-Sprungvektor `(004EH)+0x21` → `cpmx21` (`0xD329`: `JP diskio`).

3. **Der BIOS `diskio` (`0xDF1E`) ÜBERSPRINGT den Transfer, weil im Verify-CDB das
   `diofhd`-Bit (Bit 5, „Kopf hochnehmen") GESETZT ist.** `diskio` kopiert das CDB nach
   `diocdb`, testet `bit diofhd,(hl)` (`0xDF2A`) und macht bei gesetztem Bit `jp nz,headup`
   (`0xDF2C`, Kommentar: *„ja, kein Transfer"*). `headup` (`0xE3BD`) sperrt nur den Index-
   Interrupt (`OUT(11H)=3`), schaltet den Motor aus (`OUT(18H)=0xFF`) und kehrt zurück — **ohne
   jeden Plattenzugriff**. Daher **kein Lese-`/STR`, kein `>>> READ`**, der Rücklese-Puffer
   `0x63B7` bleibt ungefüllt → Vergleich `0x63B7` vs. Soll `0x3B07` (`CPI`-Schleife `0x08B1–08C1`)
   schlägt fehl → `JP NZ 0x08D5` → `A=56H` (`'V'`) → `[0x10BC]`. Das CDB bei `0x37CA` zeigt
   `[0]=0x60` = `diofhd`(Bit5) **und** `diofps`(Bit6, „trk/sid/sec schon physisch") gesetzt.

**Wurzelursache (Schritt (a) erledigt 2026-06-30): Das `diofhd`-Bit ist NICHT von FORMATB gesetzt —
es ist Teil des BAKED-IN-CDB-Templates und wird vom NEUEREN BIOS umgedeutet.** Befund-Kette:

- Das CDB bei `0x37CA` hat `[0]=0x60` (bit5+bit6) **bereits beim frischen FORMATB-Menü, vor jedem
  Format** — also ein in `FORMATB.COM` einkompilierter Template-Wert. FORMATB setzt/löscht Bit 5
  des CDB **nirgends** (verifiziert: kein `SET/RES 5` auf `(IY+0)`/`(HL)=37CA` im ganzen Programm;
  das einzige `RES 5,(HL)` @`0x0CA9` ist Text-Großschreibung). Die beiden CDBs sind
  `37CA: 60 FF FF FF 01 00 B7 63` (Adresse `0x63B7` = Rücklese-Puffer) und
  `37D7: 60 FF FF FF 01 00 07 3B` (Adresse `0x3B07` = Soll-Muster).
- **Die CDB-Flag-Konvention wurde zwischen den BIOS-Versionen umorganisiert** — die BIOS-Quelle
  (`cpadisk_*.prn`) dokumentiert es wörtlich:
  - `diof00 equ 0  ;** frei fuer Anw. ** (frueher Verify nach Schreiben)` — Bit 0 war **früher**
    „Verify nach Schreiben", ist jetzt frei.
  - `; +0: cdbfl ;Flags (Struktur angepasst an ft.kom)` — die Flag-Struktur wurde angepasst.
  - `; ft.kom, Bit 7 eingefuehrt: =0 Vorderseite, =1 Rueckseite` / `; ft.kom, Bit 0 (Verify nach
    Schreiben) auf Bit 6 verlegt` — Bits wurden **verschoben/neu eingeführt**.
  - In der **neuen** (1989er) Konvention ist Bit 5 = `diofhd` („Kopf hochnehmen, kein Transfer").
- **FORMATB.COM ist V02.04.87 (1987)**, das laufende BIOS V25.09.89 (1989). FORMATB prüft nur
  „System CP/A … Version ab **3/87** erforderlich" (`0x0D38`) und setzt seine CDB-Flags nach der
  **3/87-Konvention**; die hier laufende 9/89-BIOS-Konvention deutet das (in 3/87 anders gemeinte)
  Bit 5 als `diofhd` → `diskio` macht Kopf-hoch statt Lesen → `0x63B7` leer → `'V'`.

**Fazit:** Die FORMATB-Verify-Fehlmeldung ist eine **echte Software-Versionsinkompatibilität
zwischen FORMATB.COM (02.04.87) und dem BIOS (25.09.89)** — sie träte auf **echter Hardware mit
dieser BIOS-Version genauso** auf und ist **kein Emulationsfehler**. Das Formatieren selbst gelingt,
weil FORMATB es über ZVE2 direkt (am BIOS-`dio` vorbei) fährt; nur der Verify nutzt den BIOS-`dio`
und trifft so auf die geänderte CDB-Konvention. **FORMAT.COM (V19.05.89) passt zur 1989er-BIOS-
Konvention und verifiziert daher fehlerfrei.**

**Restpunkte (keine Blocker mehr):** (b) optional zur Absicherung FORMAT.COMs Verify-CDB-Flags
dumpen (Erwartung: Bit 5 = 0); (c) die streunende `0xB6`-`/STR` (Phantom-`commitWrite` mit
Gap-Bytes) im `write_mode_`-Reststand abfangen — kosmetisch (korrumpiert nur kurz Sektor 1 der
ohnehin frisch formatierten Spur, ändert am Verify-Ergebnis nichts).

#### Sekundär — Laufwerkswahl

FORMATB adressiert den Format-Write **immer physisch Laufwerk 0** (`OUT(18H)` mit drive=0,
unabhängig vom Buchstaben); FORMAT.COM re-selektiert `0xDD` (D1) für „B". Auf dem
Mehr-Laufwerk-Emulator formatiert FORMATB damit das Bootlaufwerk statt B/C.

#### Reproduktion / Schlüssel-Adressen

`tools/format_driver` mit `FD_LOGLEVEL=info` (zeigt `>>> READ/WRITE/FORMAT-WRITE`).
⚠️ **IMMER Temp-Kopien für BEIDE Disks (A: UND B:)** — FORMATB adressiert physisch Laufwerk 0,
formatiert also das als A: gemountete Boot-Image, wenn man es direkt nutzt (korrumpiert →
Blank-Screen, `git checkout disks/…`). Script-Sequenz FORMATB Spur 0: `boot / type 12:00:00 /
FORMATB / ENTER (=Fkt 0) / B / 1 / ENTER (von 0) / 0 (bis 0) / j`. Den Verify-Pfad tracen: nach
dem `>>> FORMAT-WRITE` die ZVE1-PC mitschreiben (temporärer `LOG_INFO` in der A5120-Run-Loop,
mit `disks/cpadisk_autofs_clock_noautoexec.prn` als `-l`-Listing annotierbar).

FORMATB ZVE2-Format: ZVE2-Routine `0x38DF`, Leading-Gap `0x38F4/0x38F6`, Trailing-Gap
`0x3988/0x398B`, dtrret `0x3897` (→ `0x38C8`), ZVE1-`JR $` `0x38C7`, Disk-ISR `0x3A2E`
(`EI;RETI`@`0x3A31`, IM2-Slot `0xE8`/`0xEA`), BIOS-Loop-Arm PC `0xDEEA`.
FORMATB Verify (Problem 2): Schleife `0x088B`, Wiederhol-Zähler `[0x383A]`, `CALL sub_09C5`
@`0x08A2`, Vergleichsschleife `0x08B1–08C1` (`CPI`, Rücklese-Puffer `0x63B7` vs. Soll `0x3B07`),
`'V'`-Fehler `0x08D5` (`A=56H`→`[0x10BC]`); Verify-CDB `0x37CA` (`[0]=0x60`: `diofhd`+`diofps`);
Lese-/Schreib-Eintrag `sub_0A30`(`RES 4,(IY+0)`)/`sub_0A39`(`SET 4,(IY+0)`), BIOS-Aufruf
`sub_0D26` (`JP (004EH)+0x21`).
BIOS: `cpmx21=0xD329` (`JP diskio`), `diskio=0xDF1E`, Head-up-Abzweig `0xDF2A` (`bit diofhd,(hl)`)
/`0xDF2C` (`jp nz,headup` = „kein Transfer"), `diofhd`=Bit5 / `diofps`=Bit6, `diocdb=0xE0F9`,
Index-Vektor `ivdsk1=0xE8`/`ivdsk2=0xEA`, Motor-Watchdog `headup=0xE3BD/0xE3BF`,
1-s-Timer `tim1uu=0xE682`, Watchdog-Zähler `fl.zto=0xE3A3`, Transfer-Suppression
`pretx+1=0xE3AB`, Timer-5-ISR `tim5it=0xE5FD`, 1ms-Warteschleife `wait1z=0xE7F8`.

Der frühere CRC-Konventionskonflikt auf den **128-B-Systemspuren** (FORMAT-Verify
meldete `'C' SPUR DEFEKT`) ist mit dem codierungstreuen Ein-CRC-Lesepfad
(Standard-IBM-CCITT, §10.3) **gelöst** — daher verifiziert FORMAT.COM jetzt auch die
Systemspuren.

---

## 9. Nachstellen im Emulator

### 9.0 Komfort-Runner `tools/format_all.py` (alle §3-Formate)

Für das Formatieren **mehrerer** §3-Formate am Stück (Phase A, .hfe):

```sh
tools/dev.sh tool format_driver                 # Treiber bauen (einmalig)
python3 tools/format_all.py --list              # Formattabelle
python3 tools/format_all.py 0 1 4 6 E --upto 5  # Smoke (Spur 0-5) je Format, mit Verify
python3 tools/format_all.py --all --full        # alle 15, volle 160 Spuren
python3 tools/format_all.py 0 --full --dir-verify --outdir out/formats
```

Der Runner erzeugt je Format eine **Temp-Kopie der Boot-Disk (A:)** und ein
**B:-Ziel**, generiert das FORMAT.COM-Script (Menü-Navigation + Verify), fährt
`format_driver` und prüft `FORMATIEREN beendet` ohne `SPUR DEFEKT`.  Menü:
`0-3` direkt · `4-7` nach `X` · `E-K` nach `X`,`Y`.  Ergebnis-Images unter `--outdir`.

**`.img` vs `.hfe` (`--type`):**
- `--type img` (Phase B): `format_driver` **legt** die `.img` per **`create`** in der
  Geometrie des passenden `DiskFormat` an (0xE5-gefüllt; kein Python-Vorbau).  Eine frische
  0xE5-`.img` liest über `RawSectorImage` als **gültig formatierte** Disk → FORMAT.COMs
  Vorlesung findet echte Sektoren → **kein** BUSRQ-Hänger.  Format→`DiskFormat`-Zuordnung:
  s. `FORMATS`-Tabelle in `tools/format_all.py` bzw. `k5601_16x256`/`k5601_26x128`/
  `k5601_9x512`/`k5601_10x512`/`cpa800`/`cpa780` in `FormatParser::builtinFormats()`.
- `--type hfe` (Phase A): B: ist eine **Kopie eines gültigen HFE-Templates**
  (`disks/cpadisk_*.hfe`, s. §8.2 — **kein** gap-leeres Blank, das würde formatabhängig
  hängen).

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