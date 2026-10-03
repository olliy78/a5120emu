# PC 1715W — Hardware aus Schaltplan, Urlader und SCP-3.0-Software (AP-W0)

Stand 2026-10-03. Grundlage für `Pc1715wSpeicher`, `Z80Dma`, `Upd765` und die Bildquelle des
1715W (Plan `doc/design/21_pc1715.md` §4, §8.2). Reine Auswertung, kein Kerncode.

**Belege** (Kürzel in den Tabellen):

| Kürzel | Quelle |
|--------|--------|
| **[Bl. A…E]** | Stromlaufplan 1715W, Zeichnung 1.93.310411.8/04 (5 Blätter), Scans aus xepb.org/robotron `docs/1715w.zip` (Jens Müller). Nicht im Repo; entpackt unter `/tmp/claude-1000/pc1715w/1715W/teil_b/`. |
| **[CRT A/B]** | „201.1 Stromlaufplan CRT-Steuerung 1715W“, 1.93.310412.6/104, gleiche Quelle |
| **[S550]** | `doc/EPROMS/PC1715W/pc1715w_s550_urlader.bin`, kommentiert in **`doc/EPROMS/PC1715W/s550.prn`** |
| **[PROM]** | `pc1715w_74s287_cas.bin` (A41), ausgewertet gegen die Verdrahtung in [Bl. B] |
| **[LDR]** | SCP-3.0-Lader „SCP 3.0 - LOADER PC 1715W V0001 25/05/87“ aus den Systemspuren von `tests/fixtures/disks/pc1715w_scp30_system.hfe` (Spur 0 Sektor 1 ab Offset 80H = 4000H, 79 Sätze; Adressen unten sind Ladeadressen) |
| **[BIOS]** | `SCP3.SYS` derselben Diskette (CP/M-3-Format: resident EC00–FBFFH, gebankt 7800–BFFFH in Bank 1) |
| **[INIT]**, **[LOADCS]**, **[TXT]** | `INIT.COM` (Formatierer „FORMATW“), `LOADCS.RSX` (BDOS 113: ZG/Bild-RAM ↔ TPA), `SCPWPC.TXT` (Systembeschreibung R-BWS) derselben Diskette |
| **[MAME]** | `src/mame/robotron/rt1715.cpp` (master, 2026-10) — nur Gegenprobe |

[?] = Deutung ohne unmittelbaren Beleg. Die Scans sind teils schwer lesbar; Gatterlogik ist
nur so weit verfolgt, wie die Software sie nicht ohnehin festlegt.

**Kurzfassung der Abweichungen vom Plan** (Einzelheiten §7): Bild-RAM ist **2 K**, nicht 4 K;
ZG-RAM sind **zwei getrennte 2-K-Zeichensätze**, gewählt über **1AH Bit 4 XOR GPA0** — kein
Schreibschutz; Motorbits **4–7 = LW0–3**; S550 liest **weder 04H noch 3AH/3BH**; 38H–3FH
benutzt **keine** Software; der 8275 holt das Bild über einen **eigenen Adresszähler** der
CRT-Karte, nicht über den UA858.

---

## 1. E/A-Karte

Dekodierung [Bl. C]: A12 (8205) dekodiert AB7–AB5, sein Ausgang 0 (00–1FH) gibt A13 frei,
Ausgang 1 (20–3FH) A14; A13/A14 dekodieren AB4–AB2 — **je 4 Adressen gespiegelt**.

| Port | Signal / Bauteil | Lesen | Schreiben | Beleg / benutzt von |
|------|------------------|-------|-----------|---------------------|
| **00H** (–03H gespiegelt) | /DMACS, **UA858 DMA** (A2) | DMA-Leseregister (Read-Mask-Folge) | DMA-Steuerwörter WR0–WR6 | [Bl. C]; [S550] 0527/0689, [LDR], [BIOS] |
| **04H–07H** | /CTCCS, **CTC2** U857 (A4), Kanal 0–3 | Zählerstand | Steuerwort/Zeitkonstante/Vektor | [Bl. C]; [BIOS] A785–A79F |
| **08H–0BH** | /CTCCS0, **CTC0** U857 (A5) | dto. | dto. — Baudtakte der SIO: Kanal 0 → SIO-A, Kanal 1 → SIO-B (aus den Gerätetabellen des BIOS: Zeitgeberport 08/09 je Kanal) | [Bl. D]; [BIOS] F838–F85F |
| **0CH–0FH** | /SIOCS0, **SIO0** U856 (A3): 0C Daten A, 0D Daten B, 0E Steuer A, 0F Steuer B | Daten/RR | Daten/WR | [Bl. D]; [BIOS] ADCD–ADFE (Tastatur auf SIO-A Rx), A89B (Kanalinit) |
| 10H–17H | /CTCCS1, /SIOCS1 — Steckplatz (Zusatzkarte CTC1/SIO1) | — | — | [Bl. C] A13 Ausg. 4/5 |
| **18H** | /CRTCS AB1=0 AB0=0: **8275** Parameter/Daten | (nicht benutzt) | Parameter (Reset-Parameter, Cursor X/Y) | [CRT B] D05; [LDR] 5F77, Tab. 558D |
| **19H** | /CRTCS AB1=0 AB0=1: **8275** Befehl/Status | Status (nicht benutzt) | Befehle 00 Reset, 20 Start, 40 Stop, 80 Load Cursor | [LDR] 4D2E, 5F74; [LOADCS] 00C5/00CC |
| **1AH** | /CRTCS AB1=1 AB0=0 + /WR: Flipflop **A07 (D = DB4)** = Signal **ZG2** | — | **Bit 4 = Zeichensatzwahl** (0 → ZG 1 bei 2000H, 1 → ZG 2 bei 2800H), wird mit GPA0 des 8275 verknüpft (K531LP5 = XOR, E08) und wählt den 6516 der Anzeige | [CRT B] A05/A07/E08; [LDR] 5F68 (10H), 5F6D (00H), Init-Tab. (00H); [BIOS] AD83/AD8B |
| **1BH** | /CRTCS AB1=1 AB0=1 + /WR: **/ZRES** (A05) — Rücksetzen des Bild-Adresszählers [?] | — | Wert gleichgültig [?] | [CRT B] A05 → A03; **von keiner Software geschrieben** |
| **1CH** | /FDCCS AB0=0: **U8272** Hauptstatus (MSR) | MSR | — | [Bl. A] A44; [S550] 070E ff. |
| **1DH** | /FDCCS AB0=1: **U8272** Daten | Ergebnisbytes | Befehlsbytes | [S550], [LDR], [BIOS] |
| 1EH/1FH | Spiegel von 1CH/1DH | | | Dekodierung |
| **20H** (–23H) | /KRFD, Latch **A45** (DS8282, STB über A36) „FD-Steuerregister“ | — | Bitbelegung §4.2 | [Bl. A]; [S550] 0019, 035D, 0363 |
| **24H** (–27H) | /BR, **Bankregister** A62 (DB0–DB3) + A63 (DB4–DB7), 74175, Reset durch /RESET | — | Bit 2–0 **Lesebank** („Quellbank“), Bit 6–4 **Schreibbank** („Zielbank“), Bit 3/7 ohne Wirkung auf den Speicher [?] | [Bl. A]; [TXT]; [S550] 0032 (11H); [BIOS] F640 |
| **28H** (–2BH) | /MOS, Latch **A120** (74175, D1–D4 = DB4–DB7), Nachlauf über RC + Schmitt-Trigger A116–A119 → /MO0–/MO3 | — | **Bit 4+n = Motor Laufwerk n** (n = 0…3); Bit 0–3 ohne Wirkung | [Bl. D]; [S550] 0381 (11H/22H/44H/88H), 0333 (00H) |
| 2CH–2FH | /LT107CS — Flipflop A18 (D = DB1) [?], V.24-Leitung 107 wie am 1715 [?] | | | [Bl. C/D]; keine Software gefunden |
| 30H–33H | /LT111CS — V.24-Leitung 111 [?] | | | [Bl. C]; keine Software gefunden |
| **34H** (–37H) | /KON, **A114** DS8282 mit **DIP S8** (8 Schalter, Pull-up 4,7 k: offen = 1) | Schalterstellung | — | [Bl. A]; [S550] 036F, [LDR] 6238/6283, [BIOS] B6F1/B73C |
| 38H–3BH | /SR — Rücksetzen von Flipflops der FD-Ansteuerung (A18 [Bl. D], Datenseparator [Bl. A/B]) [?] | (wirkt auch beim IN, nur /IORQ-dekodiert [?]) | | [Bl. C] A14 Ausg. 6; **keine Software** |
| 3CH–3FH | /RST — Rücksetzen von Flipflops (Prüftechnik V.24 laut [MAME]) [?] | | | [Bl. C] A14 Ausg. 7; **keine Software** |
| **40H** | DMA-Zugriff auf den U8272 mit DACK, AB0 = 0 → MSR [?] | MSR | (Schreiben wirkungslos [?]) | nur aus DMA-Programmen: [S550] 06A4 (2 Byte Speicher → 40H, Force Ready), [LDR]/[BIOS]/[INIT] (2 Byte 40H → Speicher) |
| **41H** | DMA-Zugriff auf den U8272 mit DACK, AB0 = 1 → Daten | Lesedaten | Schreibdaten | DMA-Port B in allen Transferprogrammen (WR4 95H 41H) |

Dekodierung 40H/41H: A12 Ausgang 2 (40H–5FH) [?] — im Scan nicht bis zum U8272 verfolgt; dass
nur die DMA diese Adressen benutzt, ist aus der Software belegt.

**Für den Emulator:** nicht belegte Ports lesen FFH; Schreiben auf 1BH, 2CH–33H, 38H–3FH
annehmen und ignorieren (protokollieren). 34H Vorgabe **FFH** (Bit 0 = 1, s. §4.3).

---

## 2. Speicher und Banklogik

### 2.1 Bankregister und Adresserweiterung [Bl. A]

- A62 hält DB0–DB3, A63 DB4–DB7 (beide 74175, Takt /BR, Rücksetzen /RESET → **nach Reset
  BR = 00H**).
- Multiplexer **A61** (K531KP11 ≙ 74S257) legt **AB16–AB18** an: Eingang A = DB0–DB2 (Lesebank),
  Eingang B = DB4–DB6 (Schreibbank); Wahl über Flipflop **A100** (aus /WR bzw. MREQ/RD,
  Gatterlogik nicht vollständig verfolgt). Der vierte Kanal (DB3/DB7) ist am Ausgang offen.
- **Lesezyklen (CPU und DMA) benutzen die Lesebank, Schreibzyklen die Schreibbank.**
  Software-Beleg: das BIOS kopiert zwischen Bänken mit der DMA im Speicher-zu-Speicher-
  Betrieb und setzt dazu BR = (Ziel << 4) | Quelle ([BIOS] F5CF–F5FD, Tabelle F5FE); [TXT]:
  „Bit 7–4 Zielbank, Bit 3–0 Quellbank … identisch, wenn kein Interbanktransport aktiv ist“.
- **/CEPROM** = NAND der invertierten DB0–DB2 von A62 (A40, [Bl. A]): EPROM freigegeben, wenn
  **Lesebank = 0**; dazu dekodiert [Bl. C] 0000–0FFFH (A32 NOR AB12/AB13, A40, /EXTRAM, RD).

### 2.2 /CAS-Dekoder 74S287 (A41) [Bl. B], [PROM]

Verdrahtung: PROM-A0…A6 = **AB12…AB18**, A7 = **/MEMDI** (1 = Speicher frei), /CS1, /CS2 = /CAS;
O0…O3 = /CAS1…/CAS4 (aktiv low, Wert 0FH = kein Block). Damit (A7 = 1):

| Bank (AB18–16) | 0000–3FFF | 4000–7FFF | 8000–BFFF | C000–FFFF |
|---|---|---|---|---|
| **0** Hintergrund | — (kein RAM; ROM/ZG/Bild, §2.3) | B1 | B1 | B1 |
| **1** System | B1 | B1 | B1 | B1 |
| **2** TPA | B2 | B2 | B2 | B1 |
| **3** RAM-Disk | B3 | B3 | B3 | B1 |
| **4** RAM-Disk | B4 | B4 | B4 | B1 |
| **5** RAM-Disk | B2 | B3 | B4 | B1 |
| **6**, **7** | — | — | — | — |

B1–B4 = die vier 64-K-Blöcke (/CAS1–/CAS4, je 8 × U2164). Physische Adresse innerhalb
des Blocks = AB0–AB15 unverändert. Folgerungen:

- **C000–FFFF ist in Bank 0–5 gemeinsam** (Block B1) — dort liegt das residente BIOS (EC00H–).
- Bank 0 und Bank 1 sind ab 4000H **dieselbe** Speicherzelle (B1); nur 0000–3FFFH unterscheidet
  sich. Der S550 nutzt das zum Umkopieren ([S550] 0024–003C).
- Bank 5 ist eine Mischsicht: die unteren 48 K der Blöcke B2/B3/B4 je 16 K, d. h. Bank-5-Adresse
  4000H = Bank-3-Adresse 4000H usw. (vom BIOS als RAM-Disk benutzt, [TXT]).
- Bank 6/7: weder RAM noch Hintergrund (A11 auf [Bl. C] gibt /EXTRAM nur bei AB14–18 = 0):
  Lesen offen (FFH [?]), Schreiben verpufft.
- /MEMDI = 0 sperrt alle /CAS (A7 = 0 → immer 0FH). Wer /MEMDI zieht (Steckplatz), ist nicht
  untersucht.
- Die Rechnung in [MAME] `memory_read_byte` (`cas_addr = 128 + (bank << 4) + (A15..12)`) trifft
  genau diese Verdrahtung; die PLD-Gleichung in der `.pld` ebenso.

### 2.3 Bank 0, 0000–3FFFH („Hintergrund“, /EXTRAM)

/EXTRAM ([Bl. C] A11) = AB14–AB18 alle 0 → Bank 0, 0000–3FFFH. Darin:

| Bereich | Lesen | Schreiben | Beleg |
|---------|-------|-----------|-------|
| 0000–0FFF | **EPROM S550** (2 K, A0–A10, gespiegelt 0800H) — nur bei Lesebank 0 | verpufft (kein RAM) | [Bl. C] A6, A32/A40; [MAME] gleich |
| 1000–1FFF | offen (FFH [?]) | verpufft | keine Dekodierung gefunden; [MAME] gleich |
| 2000–27FF | **ZG-RAM 1** (U6516 D09 oder D08, 2 K) | ja | [CRT B]: ZG-Wahl CPU-seitig = AB11; [LOADCS] Befehl 0/2 → 2000H; [LDR] 4D33 schreibt SC619 nach 2000H |
| 2800–2FFF | **ZG-RAM 2** (zweiter U6516, 2 K) | ja | [LOADCS] Befehl 1/3 → 2800H; [LDR] 4D3F löscht 2800–2FFFH |
| 3000–37FF | **Bild-RAM** (U6516 D03, **2 K**) | ja | [CRT A] D03 mit A0–A10; [LOADCS] Befehl 4/5 → 3000H, Puffer „2K“; [LDR] Bildpuffer 3000–377FH |
| 3800–3FFF | Spiegel des Bild-RAM (AB11 nicht ausgewertet) [?] | dto. | [CRT A]: Adressmux nur AB0–AB10 |

CRT-Karte: Auswahl = /EXTRAM ∧ AB13 ∧ MREQ (A02 [CRT A]); AB12 trennt ZG (0) und Bild (1).
**Kein Schreibschutz**: 1AH ist die Zeichensatzwahl (§3). Die Software stoppt die Anzeige
(8275-Befehl 40H) vor dem Laden eines Zeichensatzes und startet sie danach wieder ([LOADCS]
00BC–00CC, [LDR] 4D2C), Bild-RAM wird bei laufender Anzeige geschrieben. Ob ein CPU-Zugriff
auf die CRT-Karte wartet (/WAIT1/2, Schalter S3/S4 [Bl. C]; Wartezustandslogik A58/A59/A99/A34
auf [Bl. A], die AB16–AB18 auswertet) ist nicht verfolgt [?] — für den Emulator ohne Belang.

### 2.4 Zustand nach Reset, Ablauf des Urladers

BR = 00H → Lesen 0000H = S550. S550 kopiert sich nach 4000H (Bank 0 = B1), schaltet **BR = 11H**,
kopiert zurück nach 0000H (jetzt RAM B1) und läuft ab 003FH aus dem RAM. Übergabe an den Lader
mit BR = 11H, DI, IM 2. Der SCP-3.0-Lader steht in Bank 1 ab 4000H; er schaltet zum Laden des
Zeichensatzes BR = 00H (liest dabei bei 4D78H — Bank 0 ≙ B1, also sein eigener Code — und
schreibt nach 2000H) und zurück auf 11H. Das BIOS rechnet CP/M-Bank b → BR-Bank b + 1 ([BIOS]
F626: `BR = ((b+1) << 4) | (b+1)`), Bank 0 erreicht es als CP/M-Bank FFH ([LOADCS] `C = FFH`).

### 2.5 DMA und Speicher

Der UA858 erzeugt eigene MREQ/RD/WR-Zyklen über denselben Bus; er sieht **dieselbe
Bankabbildung** (Lesezyklus → Lesebank, Schreibzyklus → Schreibbank). Floppy-Lesen schreibt
also in die **Schreibbank**; das BIOS stellt dafür BR auf die Zielbank des Puffers und wartet
im gemeinsamen Bereich (C000H+) auf das Ende ([BIOS] FB1C–FB57: `BR = (bank+1)*11H`, Warten,
Bank zurück). Der Lader wählt ebenso (5C84). Speicher-zu-Speicher (Interbank-MOVE) über die
DMA im Continuous-Betrieb, Quelle Port A in der Lesebank, Ziel Port B in der Schreibbank (§4.5).

---

## 3. Bild

- **8275** (KR580WG75, D05 [CRT B]) an 18H/19H; Takt CCLK aus der „Taktzentrale“ der CRT-Karte:
  Quarz **13,824 MHz**, Zeichentakt ÷ 8 = 1,728 MHz [CRT A] (wie [MAME]).
- **Parametersatz** ([LDR] Tabelle 558D, ebenso BIOS): `19H←00` Reset, `18H←4F 57 6B 6D`,
  `19H←20` Start Display; Zeichensatz `1AH←00`. Das heißt: 80 Zeichen/Zeile, 24 Zeilen,
  2 Zeilen Rücklauf, **12 Linien je Zeile**, Unterstreichung in Linie 7 (U = 6 → 7. Linie [?]),
  Linienzählermodus 0, Feldattribute nicht transparent (F = 1), Cursor-Format 10
  (nicht blinkender Block [?]), Zeilenrücklauf 28 Zeichen. Mit 108 Zeichenzeiten je Linie und
  26 × 12 = 312 Linien: 16,0 kHz / 51,3 Hz.
- **DMA des 8275 (Zeilenpuffer)**: nicht über den UA858. Die CRT-Karte hat einen **eigenen
  11-Bit-Adresszähler** (3 × DL093 ≙ 7493, C02/D02/B02 [CRT A]), der mit jedem DACK
  weiterzählt; Multiplexer C01/D01/B01 (DL257) schalten die Adresse des Bild-RAM zwischen
  Zähler (DACK) und CPU-AB0–AB10 um; der 8275 bekommt die Daten über den Datenbus der Karte
  mit seinem DACK. Rücksetzen des Zählers: **/ZRES (Port 1BH)** über A03 mit einem zweiten
  Signal — vermutlich der Bildrücklauf, da **keine Software 1BH schreibt** [?].
  Für den Emulator: Bildanfang = Bild-RAM-Offset 0 (Adresse 3000H) je Bild, 80 × 24 =
  1920 Byte linear, so wie der Lader seinen Bildpuffer führt (Anfang 3000H, Zeile 2 3050H,
  letzte Zeile 3730H, Ende 3780H — [LDR] Zellen 557E–558C). Sondercodes (Zeilen-/Bildende)
  würden den Zähler wie beim echten 8275 nur weniger weit treiben.
- **Zeichengenerator** [CRT B]: zwei U6516 (D09, D08), Adresse über Multiplexer C07/D07/B07:
  Anzeige-seitig **A0–A6 = CC0–CC6** (Zeichencode, 128 Zeichen), **A7–A10 = LC0–LC3**
  (Linienzähler); CPU-seitig AB0–AB10. Chipwahl Anzeige = **ZG2 XOR GPA0** (1AH Bit 4,
  8275-Attribut GPA0), CPU = **AB11**. Daten → zwei 4-Bit-Schieberegister DL295 (C11, C12) →
  Video. Damit ist die Organisation des ZG-RAM **linienweise**: Byte `L·128 + Code`.
- **`.ZGF`-Dateien** (2048 Byte, 14 Stück auf der SCP-3.0-Diskette): genau dieses Format —
  `SC619.ZGF` Zeichen 41H („A“): Linie 1 = 18H, 2 = 24H, 3–5 = 42H, 6 = 7EH, 7–9 = 42H;
  „g“ (67H) mit Unterlänge bis Linie 10. **Bit 7 = linkes Pixel**, 8 Pixel breit, Linien 0–11
  benutzt (12–15 = 0). Der in den Lader eingebaute Zeichensatz (4D78H, 07FFH Byte) ist
  **bytegleich mit `SC619.ZGF`** (deutsch). Die Bitreihenfolge im Schieberegister ist aus dem
  Scan nicht sicher abzulesen; die Glyphen ergeben aber nur mit Bit 7 links lesbare Zeichen.
- Attribute: Helligkeit/Invers/Unterstreichung über LTEN/RVV/VSP/HLGT des 8275 → Videologik
  E03/E06/E09 [CRT B]; Einzelheiten nicht ausgewertet (wie beim 1715 [?]).
- Ob ein Zugriff des Gastes über Port 19H den Status liest: weder Lader noch BIOS tun es.
  8275-Interrupt (IRQ → X2) wird nicht benutzt (kein Befehl A0H „Enable Interrupt“).

---

## 4. Floppy

### 4.1 U8272 (A44) [Bl. A]

- CS = /FDCCS (1CH–1FH), A0 = AB0, RD/WR vom Bus, DB0–DB7; **DRQ → FDDRQ → RDYDMA** (über
  A34, invertiert) → **RDY des UA858** (WR5 82H: RDY low-aktiv) — der UA858 hat nur einen
  Kanal, er ist fest der Floppy zugeordnet; DACK ← DMA-Zugriff auf 40H/41H [?].
- **INT** des U8272 → Leitung 10 [Bl. A] (weiter nicht verfolgt; [MAME] lässt ihn offen).
  **Keine Software benutzt ihn**: S550, Lader und BIOS pollen MSR und SENSE INTERRUPT STATUS.
- **TC** = DMA-Interrupt (Blockende, INTDMA) ∧ **KRFD Bit 7** (A33/A26/A109-Logik [Bl. A]);
  [MAME] macht es genauso (`tc_w`). Software: Bit 7 wird vor dem Transfer gesetzt
  ([S550] 0498), die DMA-ISR nimmt es zurück ([S550] 06F0, [BIOS] FB78).
- **RST** = KRFD Bit 6 = 0 ([S550] 035D: 30H = Reset, 70H = Lauf).
- CLK: Quarz **Q1 8 MHz** [Bl. B] (Teilung über KRFD Bit 0 [?]).
- Laufwerkswahl: US0/US1 → 8205 A46 → /SE0–/SE3 (Treiber A98/A25), freigegeben über
  KRFD Bit 4 (ÜSEL, A110) [?]. HDSEL, DW, WE, RW/SEEK usw. über 75450-Treiber auf X6 (intern)
  und X8 (extern) [Bl. A].
- Motor: Port 28H Bit 4–7 (§1), mit Nachlauf-RC.

### 4.2 KRFD 20H (A45, D00–D07) [Bl. A]

| Bit | Name im Plan | Bedeutung | Software |
|-----|--------------|-----------|----------|
| 0 | FO | **1 bei 8″-Format, 0 bei 5,25″** (Takt/Datenrate [?]) | [S550] 03A2: gesetzt, bei Flags Bit 1 (5,25″) gelöscht |
| 1 | TIME 1 | Datenseparator/Zeitkonstante [?] | [LDR]/[BIOS] B7AE: aus Laufwerksparameter (IX+19) |
| 2 | TIME 2 | dto. [?] | dto. |
| 3 | PRE | Präkompensation ein | [LDR] 62D6–62E2: ab Zylinder (IX+13) |
| 4 | ÜSEL | 1 = Ruhe (Laufwerke abgewählt [?]), 0 während eines Auftrags | [S550] 03A2 (0), 032A (1) |
| 5 | FIX | [?] — 0 nur während eines Transfers, der bei Sektor 1 beginnt | [S550] 0498, 06F0, 0501 |
| 6 | /RESET FDC | 0 = U8272 im Reset | [S550] 0019 (30H), 035D |
| 7 | TC-Freigabe | 1 = DMA-Blockende erzeugt TC | [S550] 0498 / 06F0 |

### 4.3 KON 34H (DIP S8)

Bit 0 = **1: 8″-Formate erlaubt** — S550/BIOS prüfen es nur bei Formaten mit Flags Bit 1 = 0
(8″) und melden sonst Fehler F1H ([S550] 0369, [BIOS] B73C). Die übrigen Bits liest keine
gefundene Software [?]. Vorgabe im Emulator: FFH.

### 4.4 Benutzte U8272-Befehle

| Befehl | Code | S550 | Lader | BIOS | INIT |
|--------|------|------|-------|------|------|
| SPECIFY (ND = 0 → DMA-Betrieb) | 03 | ✓ | ✓ | ✓ | ✓ |
| RECALIBRATE | 07 | ✓ | ✓ | ✓ | ✓ |
| SEEK | 0F | ✓ | ✓ | ✓ | ✓ |
| SENSE INTERRUPT STATUS | 08 | ✓ | ✓ | ✓ | ✓ |
| SENSE DRIVE STATUS (RDY Bit 5, WP Bit 6) | 04 | ✓ | ✓ | ✓ | ✓ |
| READ ID (\|40H MFM) | 0A | ✓ | ✓ | ✓ | |
| READ DATA (\|40H MFM, MT = SK = 0) | 06 | ✓ | ✓ | ✓ | |
| WRITE DATA (\|40H) | 05 | | ✓ | ✓ | |
| SCAN EQUAL (\|40H) — **Prüflesen nach dem Schreiben**, DMA Speicher → FDC, STP = 1 | 11 | | ✓ | ✓ | |
| FORMAT A TRACK (\|40H) | 0D | | | | ✓ |

Nicht benutzt: READ DELETED, WRITE DELETED, READ TRACK, SCAN LOW/HIGH, MT-Bit, SK-Bit.
Mehrere Sektoren in einem Befehl: EOT = R + Zahl − 1 auf derselben Spur/Seite; Ende über TC.
Erfolg = DMA-Status „Blockende erreicht“ (Bit 5 = 0) **und** ST0 Bit 7–6 = 00 ([S550] 0539).
Befehlsbytes werden je Byte nach MSR RQM (Bit 7) gesendet; steht DIO (Bit 6) schon, holt der
Treiber erst ein hängendes Ergebnis ab ([S550] 070E); Ergebnis je Byte mit RQM ∧ DIO, Ende bei
CB (Bit 4) = 0. Warten auf Suchende: MSR Bit 0–4 = 0 mit SENSE INTERRUPT STATUS dazwischen.

### 4.5 DMA-Programme (UA858, Port 00H)

Alle Floppy-Transfers (S550 0689H, Lader 66B4H, BIOS BB7BH, INIT 173FH — identisch bis auf die
gepatchten Bytes):

```
C3 C3 C3 C3 C3 C3   6 x RESET
C7 CB               Reset Port-A-Timing, Reset Port-B-Timing
83                  Disable DMA
7D|79 aa aa ll ll   WR0: Transfer, Adresse A + Blocklänge folgen; 7D = A->B (Lesen), 79 = B->A (Schreiben)
                    aa = Puffer, ll = Länge - 1   (=> der Baustein überträgt Blocklänge + 1 Byte)
14                  WR1: Port A = Speicher, +1
28                  WR2: Port B = E/A, fest
95 41 12 14         WR4: Byte-Betrieb, Port-B-Adresse 41H, Interrupt-Steuerbyte 12H
                         (Interrupt bei Blockende, Vektor folgt), Vektor 14H
82                  WR5: /RDY low-aktiv, kein Auto-Restart (Stopp bei Blockende), /CE nur
CF                  LOAD
01|05               WR0 allein: Richtung UMKEHREN — 01 = B->A (FDC -> Speicher, Lesen),
                    05 = A->B (Speicher -> FDC, Schreiben/Scan)
CF                  LOAD (nochmals, mit der endgültigen Richtung)
AB                  Enable Interrupts
87                  Enable DMA
```

Fortsetzen beim sektorweisen Prüflesen: `D3 8B AB 87` (Continue, Reinitialize Status Byte,
Enable Interrupts, Enable DMA) ([LDR] 620E, [BIOS] B6C7). Abschalten: `83`, in der ISR
`A3` (Reset and Disable Interrupts). Status lesen: `BB 7F` (Read Mask: alle 7) und 7 × `IN (00H)`
(Status, Zähler L/H, Adresse A L/H, Adresse B L/H) ([S550] 0527).

FDC-Grundzustand („Blindlauf“): `C3×6, 7D BA 06 01 00, 24, 28, 80, 85 40, 82, CF, B3, 87` im S550
(2 Byte aus der festen Speicherzelle 06BAH nach Port 40H, **Force Ready**); Lader/BIOS/INIT
fügen vor `B3` noch `01 CF` ein (Richtung 40H → Speicher). Zweck nicht belegt [?].

Interbank-MOVE ([BIOS] Tabelle F5FE, OTIR 21 Byte):
`C3 C7 CB 83 79 <A> <len-1> 14 10 80 AD <B> 82 CF 05 CF B3 87` — WR2 10H (Port B **Speicher**,
+1), WR4 ADH (**Continuous**, Port-B-Adresse folgt), Force Ready; Quelle A (Lesebank), Ziel B
(Schreibbank). Für `Z80Dma` heißt das: Speicher↔E/A **und** Speicher↔Speicher, Byte- und
Continuous-Betrieb, die Doppel-LOAD-Folge mit Richtungswechsel, Force Ready, Read Mask/Status,
Interrupt mit Vektor bei Blockende, Continue.

---

## 5. Interrupts

| Quelle | Vektor (I = F3H BIOS, 12H S550, 4BH Lader) | Programm | Beleg |
|--------|------------------------------------------|----------|-------|
| UA858 DMA, Blockende | **14H** (WR4-Vektor) | S550 06E7, Lader 5CB9, BIOS FB6C | WR4 `95 41 12 14` |
| CTC2 Kanal 0–3 | Basis **08H** → 08/0A/0C/0E | Kanal 2 = 1-s-Uhr (F648) | [BIOS] A785–A79F |
| SIO0 | vom BIOS zur Laufzeit gesetzt (Empfang F6EDH, Ext./Status F737H) [?] | | [BIOS] |

CTC2-Programmierung ([BIOS] A785–A79F): Kanal 0 Vektor 08H; **Kanal 1** `27H 9CH` Zeitgeber,
Vorteiler 256, Zeitkonstante 156, kein Interrupt → 3,9936 MHz / 256 / 156 = **100,0 Hz** an
ZC/TO1; **Kanal 2** `C7H 64H` Zähler mit Interrupt, Zeitkonstante 100 → **1 Hz**; das belegt
**ZC/TO1 → C/TRG2** ([Bl. C] wie im Plan). Kanal 0/3 programmiert das BIOS nicht;
C/TRG3 = /RQ [?] [Bl. C]. CTC0 (08H–0BH) ohne Interrupt (Baudtakte).

Rangfolge: [Bl. C] führt IEI/IEO von DMA (A2) und CTC2 (A4) über die Steckverbinder X1/X2
weiter; die Reihenfolge **DMA → CTC2 → SIO0** (Plan, [MAME]) ist aus dem Scan nicht
zweifelsfrei abzulesen [?]. Sie spielt für die Software keine Rolle: die DMA unterbricht nur
während eines Floppyauftrags, und die Uhr verträgt eine Verzögerung.

IM 2 überall ([S550] 001D, [LDR] 4D29, [BIOS] A773).

---

## 6. Urlader S550

Kommentiertes Listing: **`doc/EPROMS/PC1715W/s550.prn`**. Kurz:
1. DI, KRFD 30H (FDC im Reset), IM 2, I = 12H; kopiert sich über Bank 0/4000H in RAM Bank 1
   (BR = 11H) und läuft ab 003FH aus dem RAM.
2. Laufwerke 0, 1, 2, 3 reihum (endlos): FDC-Reset, DMA-Blindlauf, Motor an, warten,
   READ ID mit vier Formatbeschreibungen (8″ MFM/FM, 5,25″), N aus dem ID-Feld.
3. Spur 0 / Kopf 0 / Sektor 1 nach 13B2H. Kopf `03 F0` → Formatbeschreibungen ab Offset 64H,
   Ladeliste ab Offset 0CH (Adresse, Satzzahl à 128 Byte, Ende FFH), Startadresse Offset 9.
   SCP 3.0: 79 Sätze ab Satz 1 nach **4000H**, Start 4000H.
4. Laden spurweise mit einem READ DATA je Spurrest per DMA in den 10-K-Puffer 13B2–3BB1H.
5. FDC-Reset, Motoren aus, DI, Sprung; H = Bootlaufwerk, BR = 11H.
6. **Keine Meldungen, kein Bildschirm**, kein V.24-Boot; jeder Fehler → nächstes Laufwerk.

Der SCP-3.0-Lader richtet danach als Erstes ein: IM 2/I = 4BH, 8275 Stopp, Zeichensatz SC619
nach 2000H, ZG 2 löschen, BR = 11H, dann den eigenen Treiber (Befehl 80H) — und öffnet über eine
Mini-BDOS `SCP3.SYS` (Meldungen „SCP3LDR error: failed to open/read SCP3.SYS“).

---

## 7. Widersprüche und offene Fragen

### 7.1 Zu Plan §4 (`doc/design/21_pc1715.md`)

| Plan sagt | Befund |
|-----------|--------|
| Bild-RAM **4 K** bei 3000–3FFFH | **2 K** (ein U6516, A0–A10) [CRT A], 80 × 24 = 1920 Byte; [LOADCS] „Puffer 2K“; 3800H Spiegel [?] |
| Zeichengenerator-RAM 2000–2FFFH (eine Einheit) | **zwei 2-K-Zeichensätze**: ZG 1 2000–27FFH, ZG 2 2800–2FFFH; Anzeige wählt per 1AH Bit 4 XOR GPA0 |
| „1AH/1BH Schreibschutz ZG [?]“ | **1AH = Zeichensatzwahl** (DB4-Flipflop A07), **1BH = /ZRES** (Bildzähler-Reset [?]); kein Schreibschutz |
| MOS 28H: „Bit 7 LW0, Bit 3 LW1“ | **Bit 4 + n = LW n** ([Bl. D] A120 an DB4–DB7; [S550] schreibt 11H für LW0, 22H für LW1). Bit 7 wäre LW3 |
| Urlader: „IN 04/1C/1D/34/3A/3B“ | nur **IN 00 (DMA-Register), 1C, 1D, 34**; 04/3A/3B sind Operandenbytes |
| 38H/3CH „Rücksetz-Flipflops“ | Dekodierung bestätigt (/SR 38–3BH, /RST 3C–3FH), aber **keine Software** greift zu; 3AH/3BH lesend ohne Bedeutung |
| MSR/Daten „zusätzlich auf 40H/41H (nur über DMA)“ | bestätigt aus der Software (DMA-Port B 41H; 40H nur im Blindlauf) |
| Bank 3–5 RAM-Disk | bestätigt ([TXT]); Bank 5 ist **keine** eigene RAM-Menge, sondern eine Mischsicht von B2/B3/B4 (§2.2) |
| „8275 liest das eigene 4-K-Bild-RAM“ | ja, eigenes RAM — aber 2 K und über einen **Adresszähler der CRT-Karte**, nicht über den UA858 |

### 7.2 Zu [MAME]

- Motor: `mon_w(BIT(data,7))` für LW0, `BIT(data,3)` für LW1 — falsch nach [Bl. D] und [S550].
- Bild-RAM `& 0xfff` (4 K) und Zeichensatz 4 K mit `GPA0 ? 0x800` — 1AH (ZG2) fehlt; nach
  [CRT B] wählt **ZG2 XOR GPA0**. Mit 1AH = 00H (Lader-Vorgabe) stimmt MAME zufällig.
- Bild-DMA in MAME: `m_dma_adr` läuft modulo 1920 und wird nie zurückgesetzt; echte Karte:
  Zähler mit Reset (/ZRES und [?] Bildrücklauf).
- U8272-Takt 8 MHz / 4 = 2 MHz in MAME; Schaltplan: Quarz 8 MHz (Teilung [?]).
- Rest (E/A-Karte, PROM-Rechnung, TC = DMA-INT ∧ KRFD7, RESET = KRFD6, Daisy-Chain
  DMA → CTC2 → SIO0) stimmt mit dem Befund überein bzw. widerspricht ihm nicht.

### 7.3 Offen [?]

1. KRFD Bit 0 (FO), 1/2 (TIME), 5 (FIX) — genaue Wirkung (Datenrate/Takt, Datenseparator).
   Für einen Sektorebenen-`Upd765` ohne Belang; Bit 6/7 sind die tragenden.
2. Zweck des DMA-Blindlaufs an Port 40H im FDC-Grundzustand; was ein DMA-Schreiben auf 40H bewirkt.
3. Wohin INT des U8272 geht (Leitung 10, [Bl. A]); C/TRG3 = /RQ?
4. Rücksetzen des Bild-Adresszählers (VRTC?) — für den Emulator: je Bild von 3000H.
5. Wartezustandslogik A58/A59/A99/A34 (/WAIT abhängig von AB16–AB18, M1, MREQ; Brücken X15).
6. DIP S8 Bits 1–7; Vorgabe FFH.
7. Bitreihenfolge im ZG-Schieberegister (DL295) — Software-Befund Bit 7 links.
8. Interruptrangfolge im Scan (Plan: DMA → CTC2 → SIO0).
9. /LT107CS (2CH), /LT111CS (30H): wie am 1715 (V.24-Leitungen) — nicht ausgewertet.
