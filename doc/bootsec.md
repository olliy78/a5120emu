# CPA780 SYL-Bootlader — Funktionale Analyse

**Quelldatei:** `disks/bootsec.bin`  
**Größe:** 15104 Bytes = 3 × 3328 (System) + 1 × 5120 (Daten-Fueller)  
**Format:** CPA780 SYL-Mixed-Geometry-Diskette  
**CPU:** Zilog Z80  
**Version:** `CP/A-Bootsystem, Version 05.04.88 ladet @OS.COM`

---

> **Status (2026-06):** Dieses Dokument analysiert das **On-Disk-Layout** von
> `disks/bootsec.bin`. Die Byte-Disassemblate stimmen; einige funktionale
> Deutungen sind jedoch durch die Emulator-Laufzeitanalyse korrigiert worden.
> Maßgeblich für den **tatsächlichen Bootablauf** ist
> [`K1520_architecture.md` §14.5/§14.6](K1520_architecture.md). Kurz:
> - Die unten genannten RAM-Adressen (0000H-basiert) sind das *On-Disk*-Bild;
>   zur Laufzeit liegt der Boot-Record bei `0400H`, der Sekundär-Loader lädt
>   52 Sektoren nach `0800H`, die dritte Stufe arbeitet über `0800H–1FFFH`.
> - Ports `11H`/`13H` sind die Steuerregister der beiden K5122-**Z80-PIO**-Tore
>   (nicht SIO); `97H/AFH/0FFH` sind PIO-Interrupt-Control-Words, `62H/60H/0E8H`
>   sind Interrupt-**Vektoren**.
> - `@OS.COM` wird zur Laufzeit über eine nach `0100H` relozierte Kopie des
>   **Boot-ROM-DMA-Treibers** (ZVE2-DMA, `[03F7]/[03F8]`-Handshake, Index-Puls-
>   Interrupt) geladen — nicht über ein CP/M-BDOS (die `CALL 0005H`-Pfade im
>   Binär werden im bestätigten Ladeweg nicht benutzt).
> - Die Routine `FDC_ADRDEC` (`0D07H`) ist eine **CRC-16** (Sektor-Prüfsumme,
>   = `K5122::loaderCrc16()`), kein Spur/Sektor-Dekoder.

---

## 1. Überblick

Der CPA780-Bootlader ist ein selbstständiges Minimal-Betriebssystem, das im ROM des Robotron A5120/A5130 Buerocomputers verankert ist. Es wird beim Einschalten von der Diskette geladen und hat die Aufgabe, die eigentliche Betriebssystem-Datei `@OS.COM` vom CP/M-Dateisystem zu lesen, in den Arbeitsspeicher zu laden und auszuführen.

Der Lader besteht aus **drei logischen Abschnitten**, die auf den ersten drei Systemspuren der Diskette liegen.

---

## 2. Datenträger-Layout (SYL-Format)

Das SYL-Format verwendet **zwei verschiedene Spur-Geometrien** (Mixed Geometry):

### 2.1 Systemspuren (Spuren 0–2) — 128-Byte-Sektoren

| Spur | Format         | Größe      | Inhalt                         | Datei-Offset        |
|------|----------------|------------|--------------------------------|---------------------|
| 0    | 26 × 128 Bytes | 3328 Bytes | SYL-Kopf + Stage-1 Code        | `0000H`–`0CFFH`     |
| 1    | 26 × 128 Bytes | 3328 Bytes | Füllung (`0x53` = `'S'`)       | `0D00H`–`19FFH`     |
| 2    | 26 × 128 Bytes | 3328 Bytes | SYL-Kopf + Stage-2 Code (BIOS) | `1A00H`–`26FFH`     |

Spur 1 enthält ausschließlich `0x53` (`'S'`) und wird vom Bootlader übersprungen.

Die Systemspuren können auch in **256-Byte-Sektoren** formatiert sein (beide Größen sind gültig, siehe Abschnitt [5.7](#57-disketten-format-erkennung)).

### 2.2 Datenspuren (Spur 3+) — 1024-Byte-Sektoren

| Spur | Format         | Größe      | Inhalt                            |
|------|----------------|------------|-----------------------------------|
| 3+   | 5 × 1024 Bytes | 5120 Bytes | CP/M-Verzeichnis + `@OS.COM` etc. |

Die bootsec.bin-Datei enthält neben den 3 Systemspuren eine init-befüllte Datenspur (alle `0x53`) als Referenz:

| Datei-Offset | Inhalt                            | Größe        |
|--------------|-----------------------------------|--------------|
| `0000H`–`26FFH` | System-Spuren 0–2 (Code)       | 9984 Bytes   |
| `2700H`–`3AFFH` | Spur 3 (Daten-Fueller, `0x53`) | 5120 Bytes   |
| **Gesamt**   |                                   | **15104 Bytes** |

---

## 3. RAM-Belegung nach dem Laden

```
RAM-Adresse  Inhalt
0000H-007FH  SYL-Kopf (Track 0, Sektor 0) — Reset-Code
0080H-0CFFH  Stage-1 (Track 0, Sektoren 1-25) — Mini-BDOS, Treiber-Stubs
0D00H-19FFH  Stage-2 (Track 2, Sektoren 0-25) — Boot-BIOS, FDC-Treiber
```

---

## 4. Startsequenz

### 4.1 ROM-Lader (Hardware)

Das ROM des A5120 lädt beim Einschalten automatisch die drei Systemspuren:

1. Track 0, Sektor 0 → RAM `0000H`–`007FH` (SYL-Kopf)
2. Track 0, Sektoren 1-25 → RAM `0080H`–`0CFFH` (Stage-1)
3. Track 2, Sektoren 0-25 → RAM `0D00H`–`19FFH` (Stage-2 / Boot-BIOS)

Danach springt der ROM-Code zu RAM-Adresse `0000H`.

### 4.2 SYL-Kopf (0000H)

Der erste Sektor beginnt mit der ASCII-Kennung `SYL` (0x53 0x59 0x4C). Die drei Bytes werden vom Z80 als harmlose Registerbefehle (`LD D,E; LD E,C; LD C,H`) ausgeführt, gefolgt von 43 NOP-Bytes (0x00), bis:

```
002EH:  C3 40 01   JP 0140H    ; Kaltstart-Einstieg in Stage-1
```

Dieser Sprung leitet zur eigentlichen Kaltstart-Initialisierung weiter.

Der SYL-Kopf enthält außerdem einen zweiten Codeblock (Offset `0037H`–`0068H`), der von der ROM-Firmware als **FDC-Initialisierungsroutine** aufgerufen wird, bevor `JP 0165H` die Stage-1-Kaltstart-Routine anspringt:

```
0037H:  LD A,29H / OUT (10H),A   ; FDC Port A: Step-Kommando senden
003BH:  LD A,A9H / OUT (10H),A   ; FDC: Track-0-Restore
003FH:  LD B,08H                  ; 8 Wartezyklen
0041H:  DEC C / JR NZ,0041H       ; interne Schleife
0044H:  DJNZ 0041H                ; äußere Schleife

0046H:  LD HL,07F2H / LD (HL),06H  ; Disk-Parameter initialisieren
004BH:  LD HL,0001H / LD (03F3H),HL ; Konfigurationszeiger setzen
0054H:  LD (03F5H),HL               ; weiterer Parameter
0057H:  LD HL,042EH / LD (0175H),HL ; FDC-Parameterblock patchen
005DH:  LD A,97H / OUT (11H),A    ; SIO-Steuerwort (8N1)
0061H:  LD A,FFH / OUT (11H),A    ; SIO abschließen
0065H:  POP HL                     ; DPB-Zeiger vom Stack (von ROM gepusht)
0066H:  LD E,01H                   ; Kaltstart-Kennung
0068H:  JP 0165H                   ; → COLDBOOT in Stage-1
```

### 4.3 Stage-1 Kaltstart (0080H–0CFFH)

Stage-1 wird bei `0080H` mit einem **Warmstart-Vektors** eröffnet:

```
0080H:  JP 1800H   ; Warmstart → Boot-BIOS WARM_BIOS
```

Dieser Sprung wird während des normalen Betriebs vom CP/M-System über die BIOS-Warmstart-Schnittstelle angesprungen. Beim Kaltstart wird `0140H` angesprungen (via SYL-Kopf), nicht `0080H`.

Ab **0106H** folgt die BDOS-Einstiegspunkt-Weiche:

```
0106H:  JP 0911H   ; → BDOS_MAIN (BDOS-Hauptprogramm in Stage-1)
```

Die Bytes `0109H`–`0110H` bilden eine **Sprungtabelle** mit vier 16-Bit-Einträgen (je 2 Bytes), die auf BIOS-Dienste zeigen:

| Slot | Adresse | Ziel     | Funktion           |
|------|---------|----------|--------------------|
| 0    | 0109H   | 0999H    | BIOS-Dienst 0      |
| 1    | 010BH   | 09A5H    | BIOS-Dienst 1      |
| 2    | 010DH   | 09ABH    | BIOS-Dienst 2      |
| 3    | 010FH   | 09B1H    | BIOS-Dienst 3      |

Die **DISPATCH**-Routine bei `0140H` liest einen Eintrag aus der Sprungtabelle und führt einen indirekten Sprung aus:

```
0140H:  INC HL          ; HL → nächster Tabelleneintrag
0141H:  LD D,(HL)       ; D = High-Byte der Zieladresse
0142H:  LD HL,(0B5CH)   ; HL = OS-Arbeitsbereichszeiger
0145H:  EX DE,HL
0146H:  JP (HL)         ; → BIOS-Dienst
```

Die **COLDBOOT**-Routine bei `0165H` wird direkt vom SYL-Kopf aufgerufen. Sie richtet den Stack ein, initialisiert die OS-Datenstrukturen und übergibt die Kontrolle an Stage-2 (BOOT_MAIN / WARM_BIOS).

#### Mini-BDOS in Stage-1

Ab `0085H` bis ca. `0CFFH` enthält Stage-1 das **Mini-BDOS** des Boot-Systems — ein vollständiges, kleines CP/M-kompatibles BDOS mit:

- BDOS-Dispatcher (`0911H`): BDOS-Funktionen 0–0x28
- FCB-Verwaltung (File Control Block)
- Sektor-Lese-Dienst (`READ_SEC` bei `0407H`): liest einen Sektor vom FDC
- CP/M-Fehlermeldungen als Strings:

```
01BAH:  "Bdos Err On $"
01CAH:  "Bad Sector$"
01D5H:  "Select$"
01DCH:  "File R/O$"
```

Das FCB für `@OS.COM` liegt bei `08CCH`:

```
08CCH:  DB 00H, '@','O','S',' ',' ',' ',' ',' ', 'C','O','M', ...
```

---

## 5. Stage-2 — Boot-BIOS (0D00H–19FFH)

### 5.1 Überblick

Stage-2 liegt in Track 2 (Datei-Offset `0x1A00`–`0x26FF`, RAM `0D00H`–`19FFH`). Es enthält den eigentlichen **Boot-BIOS-Treiber**, der:

1. Den FDC initialisiert und kalibriert
2. Die Disketten-ID überprüft
3. Das CP/M-Verzeichnis nach `@OS.COM` durchsucht
4. `@OS.COM` in den Speicher lädt
5. Zu `@OS.COM` springt

### 5.2 Hardware-Ports

Die Portnummern und -namen sind **identisch** mit `cpa_src/biosdskt.mac` (K5122-Floppy-Controller-Karte, zwei Z80-PIO-Bausteine).

| Port | Name    | Richtung | Funktion                                  |
|------|---------|----------|-------------------------------------------|
| 10H  | flcoad  | OUT      | Steuer-PIO Port A — FDC-Kommandos/Signale |
| 11H  | flcoac  | OUT      | Steuer-PIO Port A Steuerregister (SIO)    |
| 12H  | flcobd  | IN       | Steuer-PIO Port B — FDC-Statusbits        |
| 13H  | flcobc  | OUT      | Steuer-PIO Port B Steuerregister          |
| 14H  | fldaad  | OUT      | Daten-PIO Port A — Schreib-Datenbytes     |
| 15H  | fldaac  | OUT      | Daten-PIO Port A Steuerregister           |
| 16H  | fldabd  | IN       | Daten-PIO Port B — Lese-Datenbytes        |
| 17H  | fldabc  | OUT      | Daten-PIO Port B Steuerregister           |
| 20H  | flsel   | OUT      | Laufwerks-Auswahl-Latch (Bit 7–4: /Sel)   |
| 21H  | flmot   | OUT      | Motor-Steuerung (Bit 7–4: /Mot on je LW)  |
| 0AH  |         | IN       | FDC-Datenstatus (Bit 6 = Daten bereit)    |
| 0EEH |         | OUT      | Geräte-Selektor (Laufwerksauswahl)        |

**Fehlercodes** (Rückgabe in Register A aus der `floppy`-Routine in `biosdskt.mac`):

| Code | Bedeutung                                |
|------|------------------------------------------|
| `'C'`| CRC-Zeichen falsch                       |
| `'D'`| Laufwerk nicht existent                  |
| `'R'`| Laufwerk nicht bereit (kein Disk)        |
| `'S'`| Sektor nicht gefunden                    |
| `'T'`| Spurnummer > 84 (zu groß)                |
| `'U'`| Time-out (keine Sektormarke)             |
| `'W'`| Schreibgeschützt                         |

### 5.3 FDC-Adress-/CRC-Dekoder (FDC_ADRDEC, 0D07H)

Die Routine bei `0D07H` verarbeitet den AMF-Bitstrom (Address Mark Field) des K5600/K5122-FDC über eine XOR+RRCA-Kette und dekodiert Spur (B) und Sektor (C) aus dem kompakten Format.

**Quellennachweis:** Der Algorithmus ist **bytegenau identisch** mit `advance_dma_ptr` in `cpa_src/format.mac` (Adresse `0DC7H` in `FORMAT.COM`). Dort dient er der FDC-CRC-16-Berechnung beim Formatieren. Beide Verwendungen entstammen demselben CPA-BIOS-Quellcode-Pool.

Der Algorithmus:

```
Eingabe:  IY → AMF-Puffer, E = Anzahl Bytes, BC = laufende CRC
Ausgabe:  BC = dekodiertes Ergebnis (B=Spurbyte, C=Sektorbyte)

Für jeden Eintrag (format.mac: advance_dma_ptr = 0DC7H):
  A = IY[0]; XOR B; B = A
  RRCA×4; AND 0FH; XOR B; B = A   → Spur-High (4 Bits)
  RRCA×3; D = A
  AND 1FH; XOR C; C = A            → Sektor (5 Bits)
  A = D; RRCA; AND F0H; XOR C; C = A
  A = D; AND E0H; XOR B; B = C; C = A
  INC IY; DEC E; JR NZ → nächster Eintrag
```

### 5.4 BOOT_MAIN — Kaltstart-Hauptroutine (0D2EH)

```
0D2EH:  LD IY,0400H     ; IY → PARAM_TAB (FDC-Parameterblock in Stage-1)
0D32H:  LD E,0FEH       ; 2 Versuche
0D34H:  LD BC,0FFFFH    ; Vergleichswert Disketten-ID
0D37H:  LD A,00H
0D39H:  OUT (0EEH),A    ; Laufwerk A: aktivieren

; ─ 3 Leseversuche ─
0D3BH:  LD A,03H; PUSH AF
0D3EH:  CALL 0407H      ; READ_SEC: ersten Sektor lesen
0D41H:  POP AF; DEC A
0D43H:  JR NZ,0D3BH     ; wiederholen

; ─ Disketten-ID prüfen ─
0D45H:  LD A,B; CP (IY+0)
0D49H:  JP NZ,0605H     ; → Fehlerbehandlung
0D4CH:  LD A,C; CP (IY+1)
0D50H:  JP NZ,0605H

; ─ 2 KB RAM nullen ─
0D53H:  LD HL,0000H; LD BC,0800H
...     ; RAM 0000H–07FFH mit 0x00 füllen

; ─ Z80 Interrupt-Modus 2 einrichten ─
0D6AH:  LD HL,06C1H; LD C,1DH; LDIR  ; IV-Tabelle kopieren
0D7DH:  LD A,07H; LD I,A              ; I-Register = High-Byte der IV-Tabelle

; ─ SIO initialisieren ─
0D81H:  LD A,62H; OUT (11H),A         ; SIO A Steuerwort
0D85H:  LD A,60H; OUT (13H),A

; ─ BDOS-Zeiger patchen ─
0D8CH:  LD (0001H),HL                 ; JP-Ziel bei Adresse 0001H setzen

; ─ Motorstart, Track 0 fahren ─
0DB0H:  OUT (10H),A                   ; FDC: Step-Kommando
0DB3H:  IN A,(12H); RLCA              ; Index-Puls da?
...     ; Warte-Schleifen, bis Laufwerk bereit

; ─ @OS.COM-Verzeichnissuche ─
0DDcH:  ...                           ; Verze.-Sektoren lesen, "@OS     COM" suchen

; ─ @OS.COM laden ─
0E4FH:  CALL READ_SEC                 ; Sektor-Block lesen
...     ; Block-Transfer bis @OS.COM vollständig
```

### 5.5 Verzeichnissuche

Stage-2 durchsucht die Datensektoren des CP/M-Dateisystems nach einem Verzeichniseintrag mit dem Namen `@OS     COM` (8+3 CP/M-Namensformat, linksoptimiert). Die Suche erfolgt über:

1. Lesen des ersten Verzeichnis-Sektors (Offset 0, Zone 0 des Dateisystems)
2. Byte-für-Byte-Vergleich der FCB-Einträge mit `@OS     COM`
3. Bei Treffer: Startblock-Nummer aus FCB extrahieren
4. Alle Datenblöcke laden (multi-sector transfer)

### 5.6 WARM_BIOS (1800H)

Der Warmstart-Einstieg bei `1800H` (= Stage-2 Offset `0B00H`) wird vom Warmstart-Vektor bei `0080H` (JP 1800H) angesprungen. Er führt erneut FDC-Initialisierung und OS-Laden durch — entspricht dem BIOS WBOOT-Vektor in Standard-CP/M 2.2.

### 5.7 Disketten-Format-Erkennung und Multi-Format-Unterstützung

Der Bootlader unterstützt **mehrere Disketten-Geometrien**. Die Format-Anpassung erfolgt auf zwei Ebenen:

#### DPB-Übergabe vom ROM (Kaltstart-Konfiguration)

Beim Kaltstart übergibt das ROM-BIOS einen Zeiger auf den **Disk Parameter Block (DPB)** im HL-Register. Der SYL-Header speichert ihn auf dem Stack und holt ihn in `FDC_INIT` zurück:

```
0065H: POP HL    ; DPB-Zeiger vom ROM (gepusht vor JP 0000H)
0066H: LD E,01H  ; Kaltstart-Kennung
0068H: JP 0165H  ; → COLDBOOT → INIT_OS → speichert HL in (0B5CH)
```

Der DPB konfiguriert das Mini-BDOS für die korrekte Sektor-Größen-Übersetzung. Das `ft.*`-Parameterblock-Byte `ft.len` (RAM `0405H`) enthält den Sektorlängen-Code:

| `ft.len` | Physische Sektorgröße |
|----------|-----------------------|
| `0`      | 128 Bytes             |
| `1`      | 256 Bytes             |
| `2`      | 512 Bytes             |
| `3`      | 1024 Bytes            |

**Systemspuren:** Das SYL-Format erlaubt 128-Byte oder 256-Byte-Sektoren. `cpabcgen.com` prüft dies und meldet `Systemspuren auf Zieldiskette nicht Sektorlaenge 128 o. 256` wenn die Zieldiskette ein anderes Format hat.

#### @OS.COM-Laden über das Mini-BDOS (format-unabhängig)

Das eigentliche Laden von `@OS.COM` geschieht **nicht** direkt über Low-Level-FDC-Code, sondern über das Mini-BDOS (BDOS-Funktion `14H` = sequentiell lesen, FCB bei `08CCH`):

```
READ_FILE:
  LD C,1AH; CALL 0005H   ; BDOS 26: DMA-Adresse setzen
  LD DE,08CCH; LD C,14H  ; BDOS 20: sequentiell lesen (FCB @OS.COM)
  CALL 0005H
  ...
  LD DE,0080H; ADD HL,DE ; HL += 128 (nächster 128-Byte-Block)
```

Das Mini-BDOS nutzt intern den DPB zur Sektor-Übersetzung, sodass `@OS.COM` von Datenspuren mit **1024-Byte-Sektoren** gelesen wird, obwohl die CP/M-Schnittstelle immer 128 Bytes liefert. Die Sektor-Gap zwischen physischer (1024 B) und logischer (128 B) Größe wird transparent im DPB-Translations-Layer aufgelöst.

#### ft.kom-Bit 4: 5" vs. 8" Disk

Das `ft.kom`-Byte im `ft.*`-Parameterblock (RAM `0400H`, aus `biosdskb.mac`) kodiert den Disketten-Typ:

| Bit | Name     | Wert 0           | Wert 1          |
|-----|----------|------------------|-----------------|
| 3   | MFM      | FM-Modus         | MFM-Modus       |
| 4   | 5"       | 8"-Laufwerk      | 5.25"-Laufwerk  |
| 5   | 40T      | 80-Spur-LW       | 40-Spur-LW      |

Das vorliegende `bootsec.bin` ist für **5.25"-Laufwerke** konfiguriert (Bit 4 = 1).

---

## 6. Speicherdiagramm

```
RAM-Adresse   Inhalt
────────────────────────────────────────────────────────────
0000H         SYL-Kennung "SYL" (53 59 4C)
0003H         NOP × 43
002EH         JP 0140H          ← Kaltstart-Einstieg
0037H         FDC init code (FDC_INIT)
0068H         JP 0165H          ← Kaltstart Alt-Einstieg
007FH         ─ Ende SYL-Kopf ─

0080H         JP 1800H          ← Warmstart-Vektor (WBOOT)
0083H         [Start Mini-BDOS-Code / Dispatch-Tabellen]
0106H         JP 0911H          ← BDOS-Einstieg
0109H         Sprungtabelle (4 × 2 Bytes)
0111H         INIT_OS
0140H         DISPATCH (indirekter Sprung)
0165H         COLDBOOT (Kaltstart-Initialisierung)
0400H         PARAM_TAB (FDC-Parameterblöcke, via IY adressiert)
0407H         READ_SEC (Sektor-Lese-Dienst)
0605H         ERR_HANDLER (Fehlerbehandlung)
08CCH         FCB für @OS.COM ("@OS     COM")
0911H         BDOS_MAIN (BDOS Hauptprogramm)
0CFFH         ─ Ende Stage-1 ─

0D00H         "SYL" (zweiter SYL-Kopf, Spur 2)
0D07H         FDC_ADRDEC (Adress-Dekodierung)
0D2EH         BOOT_MAIN (Kaltstart, FDC, OS-Suche/Laden)
0DDCH         OS_SEARCH (Verzeichnissuche)
1800H         WARM_BIOS (Warmstart-BIOS-Einstieg)
19FFH         ─ Ende Stage-2 ─
```

---

## 7. Schlüssel-Konstanten

| Symbol       | Adresse  | Bedeutung                              |
|--------------|----------|----------------------------------------|
| `SYL_ID`     | `0000H`  | SYL-Disketten-Kennung                  |
| `BOOT_ENTRY` | `002EH`  | Kaltstart von ROM angesprungen         |
| `FDC_INIT`   | `0037H`  | FDC-Initialisierung                    |
| `WBOOT_JP`   | `0080H`  | Warmstart-Vektor (JP 1800H)            |
| `BDOS_JP`    | `0106H`  | BDOS-Einstieg (JP 0911H)               |
| `JUMP_TAB`   | `0109H`  | BIOS-Dispatch-Tabelle (4 Einträge)     |
| `DISPATCH`   | `0140H`  | Generischer BIOS-Dispatcher            |
| `COLDBOOT`   | `0165H`  | Kaltstart-Initialisierung              |
| `PARAM_TAB`  | `0400H`  | FDC-Parameterblock-Tabelle             |
| `READ_SEC`   | `0407H`  | Sektor lesen (von Stage-2 gerufen)     |
| `FCB_OS`     | `08CCH`  | FCB für `@OS.COM`                      |
| `T2_SYL_HDR` | `0D00H`  | SYL-Kennung in Track 2                 |
| `FDC_ADRDEC` | `0D07H`  | K5600-FDC Adress-Dekodierung (IY→BC)   |
| `BOOT_MAIN`  | `0D2EH`  | Boot-BIOS Kaltstart-Hauptprogramm      |
| `OS_SEARCH`  | `0DDCH`  | @OS.COM-Verzeichnissuche               |
| `WARM_BIOS`  | `1800H`  | Boot-BIOS Warmstart-Einstieg           |

---

## 8. Vollständige Boot-Ablaufbeschreibung

```
ROM-Code:
  ├── Lese Track 0, Sektoren 0-25 → RAM 0000H-0CFFH
  ├── Lese Track 2, Sektoren 0-25 → RAM 0D00H-19FFH
  ├── LD HL, <DPB-Adresse>  ; DPB-Zeiger für SYL-Init
  ├── PUSH HL
  └── JP 0000H

RAM 0000H (SYL-Kopf):
  LD D,E; LD E,C; LD C,H  (SYL-Kennung, als Code harmlos)
  NOP × 43
  JP 0140H ─────────────────────────────────────────────┐
                                                         │
  FDC_INIT (wird von ROM über indirekten Aufruf erreicht)│
  ├── OUT (10H): FDC Step-Kommando                       │
  ├── OUT (10H): Track-0-Restore                         │
  ├── Warte-Schleifen                                    │
  ├── RAM-Strukturen initialisieren                      │
  ├── POP HL  (DPB-Zeiger vom Stack)                    │
  ├── LD E,01H                                           │
  └── JP 0165H ──────────────────────────────────────┐   │
                                                      │   │
RAM 0165H COLDBOOT: ◄─────────────────────────────────┘   │
  ├── Stack einrichten (SP = 0B5AH)                        │
  ├── OS-Strukturen initialisieren (INIT_OS 0111H)          │
  └── JP 0D2EH ─────────────────────────────────────────┐  │
                                                          │  │
RAM 0140H DISPATCH: ◄──────────────────────────────────────┘  │
  └── (Dispatcher für BDOS-Dienste im laufenden Betrieb)      │
                                                               │
RAM 0D2EH BOOT_MAIN: ◄─────────────────────────────────────────┘
  ├── IY = 0400H (PARAM_TAB)
  ├── OUT (0EEH): Laufwerk A: aktivieren
  ├── CALL 0407H × 3  (READ_SEC: ersten Sektor lesen, 3 Versuche)
  ├── CP (IY+0/1): Disketten-ID prüfen
  ├── RAM 0000H-07FFH nullen
  ├── Z80 Interrupt-Modus 2 einrichten (I-Register = 07H)
  ├── SIO initialisieren (OUT 11H, 13H)
  ├── BDOS-Zeiger patchen (0001H)
  ├── FDC: Motor starten, Track 0 anfahren
  │   ├── OUT (10H): Step-Kommando
  │   ├── IN (12H): Index-Puls?
  │   └── Warte-Schleifen × 8
  ├── OS_SEARCH: Verzeichnissektoren lesen
  │   ├── FCB 08CCH: "@OS     COM" als Suchname
  │   ├── Sektor für Sektor vergleichen
  │   └── Bei Treffer: Block-Pointer ermitteln
  ├── @OS.COM laden: CALL READ_SEC (alle Blöcke)
  └── JP <@OS.COM Einstieg>  (OS läuft!)

Bei CP/M Warmstart:
  CP/M → 0000H (WBOOT):
  └── JP 0080H (in SYL-Kopf, Warmstart-Vektor bei 0080H)
      └── JP 1800H (WARM_BIOS in Stage-2)
          ├── FDC re-initialisieren
          ├── @OS.COM erneut laden
          └── JP <CCP-Einstieg>
```

---

## 9. Technische Besonderheiten

### 9.1 IY-Register als Parameter-Zeiger

Stage-2 nutzt das Z80-IY-Register konsequent als Zeiger auf Disketten-Parameter-Tabellen (`PARAM_TAB` bei `0400H` in Stage-1). Jeder Eintrag in der Tabelle kodiert kompakt Spur-Nummer, Seiten-Kennung und Sektor-Nummer im K5600-FDC-Format. Die Routine `FDC_ADRDEC` (0D07H) dekodiert sie iterativ.

### 9.2 Self-Modifying Code

Der SYL-Kopf patcht beim Kaltstart Sprungvektoren innerhalb von Stage-1:
- Adresse `0175H` wird mit `042EH` überschrieben (FDC-Parameterblock-Zeiger)
- Adresse `0001H` wird mit einer BDOS-Dispatch-Adresse überschrieben

### 9.3 Warmstart-Mechanismus

Der Warmstart-Vektor bei `0080H` (JP 1800H) wird von der CP/M-Konvention benutzt: Sprung zu `0000H` → `JP 0080H` → `JP 1800H` → WARM_BIOS. Dies entspricht dem Standard-CP/M-BIOS-Warmboot-Mechanismus.

### 9.4 Fehlerbehandlung

Tritt beim Diskettenzugriff ein Fehler auf (ungültige Disketten-ID, Lesefehler), springt der Bootlader zu `ERR_HANDLER` (`0605H`). Schwerwiegende Fehler führen zu:
```
LD A,88H; OUT (03H),A   ; Fehlercode an LED-Port
DI; HALT                 ; CPU anhalten
```

---

## 10. cpabcgen.com — Bootsystem-Erzeuger

`docs/cpabcgen.com` ist das CPA-Hilfsprogramm zum Schreiben eines neuen Boot-Sektors auf eine Zieldiskette:

```
CP/A fuer Buerocomputer: Anlegen einer neuen Systemdiskette, Version 05.04.88
Aufruf: CPABCGEN <ZielDisk.>:[ <QuellSystem, Standard @OS.COM>]
```

### 10.1 Eingebetteter Boot-Sektor

`cpabcgen.com` enthält eingebettete Bootsector-Daten für **Spur 0** und **Spur 2** (je 3328 Bytes, ab Datei-Offset `0x0700` bzw. `0x1400`). Die ersten 128 Bytes des eingebetteten Track-0-Sektors sind **byte-identisch** mit `disks/bootsec.bin`. Die eingebettete Version-Kennung lautet `"Bootloader, Version 24.02.87"` (älter als `bootsec.bin` mit `Version 05.04.88`).

Spur 1 (53H-Füllung) und die initiale Datenspur werden **zur Laufzeit generiert**, nicht aus eingebetteten Daten gelesen.

### 10.2 Unterstützte Formate

Das Programm unterstützt **nur 5.25"-Laufwerke mit 40 Spuren**:

> `Systemdiskette kann nur auf 40-Spur-LW ausprobiert werden!`

Beim Schreiben prüft es das Format der Zieldiskette:

| Prüfung | Fehlermeldung |
|---------|---------------|
| System-Sektorgröße ≠ 128 oder 256 Bytes | `Systemspuren auf Zieldiskette nicht Sektorlaenge 128 o. 256` |
| Ungültige Anzahl Systemspuren | `Zieldiskette hat unzulaessige Anzahl Systemspuren` |
| @OS.COM nicht gefunden | `QuellSystemfile nicht gefunden` |
| CP/A-Version zu alt | `Tut mir leid, System CP/A ab 3/88 erforderlich` |

### 10.3 8"-Disketten-Boot

CPA kann theoretisch auch von **8"-Disketten** booten. `biosdskt.mac` enthält separate Motor-Anlauf-Konstanten:
- 8"-Laufwerk: `ld l,252+1` (3 Index-Pulse = 3 Umdrehungen)
- 5"/40T-Laufwerk: `ld l,250+1`

Ein 8"-Boot-Sektor würde benötigen:
- `ft.kom` Bit 4 = `0` (8"-Modus statt 5.25")
- `ft.kom` Bit 5 = `0` (77/80-Spur statt 40-Spur)
- Angepasste Motor-Anlauf-Schleife und Schritt-Timing
- Angepasste DPB-Werte für 26 Sektoren × 128 Bytes × 77 Spuren (SSSD 8")

Das vorliegende `bootsec.bin` und `cpabcgen.com` unterstützen **ausschließlich 5.25"-40-Spur-Laufwerke**. Ein separater Boot-Sektor-Erzeuger für 8"-Laufwerke ist in diesem Projekt nicht vorhanden.

---

## 11. Bezug zu CPA-Quellen

Teile des Bootloaders stammen nachweislich aus den CPA-BIOS-Quellcode-Dateien in `cpa_src/`. Die folgende Tabelle dokumentiert die Entsprechungen:

| Bootloader-Symbol / Bereich          | CPA-Quelle              | Label / Symbol               | Evidenz |
|--------------------------------------|-------------------------|------------------------------|---------|
| Hardware-Ports `10H`–`17H`, `20H`, `21H` | `biosdskt.mac`      | `flcoad`…`flmot`             | Portnummern und Symbolnamen byte-identisch |
| FDC-Kommando `0A9H` (FDC_INIT)       | `biosdskt.mac`          | `ld a,10101001b` — Kopf laden, Fault reset | Bitreihenfolge und Kontext identisch |
| AMF-Port-A-Belegung (8 Bits)         | `biosdskt.mac`          | Legende AMF Port A/B         | Vollständige Bit-Tabelle (s. dort) |
| `PARAM_TAB`-Struktur (ft.*-Block)    | `biosdskb.mac`          | `ft.kom`…`ft.adr` (10 Bytes) | Reihenfolge und Semantik identisch |
| `FDC_ADRDEC`-Algorithmus (`0D07H`)   | `format.mac:0DC7H`      | `advance_dma_ptr`            | Binary-identisch, Byte für Byte |
| `FCB_OS`-Vorlage (`@OS.COM`-FCB)     | `bioscld3.mac`, `biosnuc.mac` | `fcb@os`              | Struktur und Initialisierung identisch |
| `WBOOT_JP` (`JP 1800H`)              | `biosnuc.mac`, `bios.mac` | `WARMST` / `wbinit`        | Warmstart-Adressmuster identisch |
| Motor-Spin-up-Timing (`252+1` Index-Pulse) | `biosdskt.mac`   | `dskdaw`, `dskmo1`           | Schleifenstruktur und Konstanten identisch |
| Schritt-Timing (0.1-ms-Einheit)      | `biosdskt.mac`          | `stepw`/`stepw1`             | `djnz`-Wartestruktur identisch |

### 11.1 biosdskt.mac — Physischer Floppy-Treiber

`cpa_src/biosdskt.mac` ist die direkteste Quelle: Sie enthält den vollständigen physischen FDC-Treiber für die K5120/K5122-Hardware mit exakt denselben PIO-Portnummern (`flcoad=10H`, `flcobd=12H` usw.), der `ft.*`-Parameterblock-Schnittstelle (via IY-Register), der Motor-Anlauf-Logik und dem AMF-Bit-Legenden-Kommentar. Der Bootloader nutzt eine vereinfachte, eigenständige Variante dieses Treibers.

### 11.2 biosdskb.mac — ft.*-Parameterblock

`cpa_src/biosdskb.mac` definiert den `ft.*`-Arbeitsbereich (10 Bytes: `ft.kom`, `ft.lwn`, `ft.trk`, `ft.sid`, `ft.sec`, `ft.len`, `ft.anz`, `ft.stp`, `ft.sti`, `ft.adr`). Diese Struktur entspricht direkt der `PARAM_TAB` bei `0400H` im Bootloader; Stage-2 greift darauf über IY zu.

### 11.3 format.mac — FDC_ADRDEC-Algorithmus

`cpa_src/format.mac` (FORMAT-Utility, 3988 Zeilen) enthält als Subroutine `advance_dma_ptr` (Laufadresse `0DC7H` in FORMAT.COM) den **byte-identischen** Algorithmus wie `FDC_ADRDEC` bei `0D07H` im Bootloader. Beide verarbeiten AMF-Bitströme des K5600/K5122-FDC mit der gleichen XOR+RRCA-Kette, um eine laufende 16-Bit-CRC zu berechnen.

### 11.4 bioscld3.mac / biosnuc.mac — @OS.COM-FCB

`cpa_src/bioscld3.mac` und `cpa_src/biosnuc.mac` definieren das Symbol `fcb@os` — einen File Control Block für `@OS.COM`, der beim CCP-Warmstart (wbootv=3) verwendet wird. Die Struktur ist identisch mit `FCB_OS` bei `08CCH` im Bootloader; auch die Initialisierungsschritte (extent=0, record=0) stimmen überein.

---

## 12. Referenzen

- [`disks/bootsec.bin`](../disks/bootsec.bin) — analysierte Binärdatei
- [`docs/bootsec.mac`](bootsec.mac) — kommentiertes Disassemblat im M80/MAC-Format
- [`cpa_src/biosdskt.mac`](../cpa_src/biosdskt.mac) — physischer FDC-Treiber (Ports, ft.*, Motor, AMF-Legende)
- [`cpa_src/biosdskb.mac`](../cpa_src/biosdskb.mac) — ft.*-Parameterblock-Definitionen
- [`cpa_src/bioscld3.mac`](../cpa_src/bioscld3.mac) — CCP-Lader, fcb@os (Warmstart)
- [`cpa_src/biosnuc.mac`](../cpa_src/biosnuc.mac) — BIOS-Nucleus, fcb@os-Referenz
- [`cpa_src/format.mac`](../cpa_src/format.mac) — FORMAT-Utility (advance_dma_ptr = FDC_ADRDEC)
- [`cpa_src/bios.mac`](../cpa_src/bios.mac) — Produktions-BIOS (in `@OS.COM`)
- [`docs/cpabcgen.com`](cpabcgen.com) — Boot-Sektor-Erzeuger (5.25"/40T, Version 05.04.88, embed. Bootlader Version 24.02.87)
- [`src/floppy.cpp`](../src/floppy.cpp) — Emulator-Implementierung der SYL-Geometrie
- [`src/cpa_bios.cpp`](../src/cpa_bios.cpp) — Emulator-Boot-Sequenz (`bootFromDisk()`)
