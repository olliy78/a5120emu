# PRG 710 / 710-1 — Zweitlader, UDOS-Resident und Tastaturtabelle (AP-P0c Rest)

Stand 2026-10-02. Belege: Datei + Adresse; `[?]` = Hypothese. Abzüge: Spur 2 der UDOS-Disketten
`PRG710_UDOS_MRS_Boot` (710) und `PRG710-1_UDOS_MRS` (710-1), direkt per DiskTool-Sektorzugriff
(libk1520disk, Sektorfolge = ID 1..26 der Spur), nicht aus `bootabbild.bin`.
Dateien hier: `zweitlader_710.bin`, `zweitlader_710-1.bin` (je 0D00H Byte, Ladeadresse 1000H),
`boot4000_udos.bin` (768 B, Spur 21 Sektor 17..22), `resident_710.lst`/`resident_710-1.lst`
(linearer Durchlauf 0000–0BFF), `zweitlader_*_auszug.lst`, `k7609_codes.csv`.

## 1. Ladekette (exakt)

| Schritt | Wo auf der Diskette | Wohin | Wer liest | Einsprung |
|---|---|---|---|---|
| 1 Bootsektor | Spur 0, Seite 0, Sektor 1–4 (4 × 128 B = 512 B, FM/MFM-Probe des ROMs) | 0400H | Boot-ROM `03FDH`→`0195H` | 0400H, wenn 0402H = „SY“ |
| 2 Zweitlader | **Spur 2, Seite 0, Sektoren 1–26 (26 × 128 B = 0D00H), fortlaufend** | 1000H–1CFFH | Bootsektor ruft `03FDH` (Parameterblock 0422H: Adr 1000H, Länge 0D00H, Spur 2, Sektor 1, Flags +1 = 0AH) | 1000H (Bootsektor `JP 1000H` bei Status 80H) |
| 3 Resident | im Zweitlader: Dateioffset 0100H–0CFFH | 0000H–0BFFH (`LDIR` BC=0C00H, `1031H`) | Zweitlader selbst | `RST 00H` (`109DH`) |
| 4 BOOT-Modul | **Spur 21, Seite 0, Sektoren 17–22** (6 × 128 B, 700 B gewollt) | 4000H | Resident-Monitor, Kommando „O“ (`0022H`: `LD A,'O'`; `03A8H`: HL=1510H = Spur 15H, Sektor 17; BC=02BCH) | 4000H, wenn (4002H) = „BO“ (`03BAH`) |
| 5 `OS` | UDOS-Datei, Verzeichnis ab Spur 22 (16H) Sektor 1 | 1000H–25FFH (5632 B) | BOOT-Modul (4000H) über Resident-Treiber `0BFDH` | ENTRY **13DEH** (`OS.fileinfo`: start=13DE) — `4042H`/`405FH`: `JP (HL)` auf das ENTRY der zuerst geladenen Datei `[?]` |
| 6 `ZDOS` | UDOS-Datei | 2600H–3FD4H | BOOT-Modul (zweiter Aufruf, Namenslänge 4) | – (läuft über OS) |

* **Kein Sektornachlauf im ROM und im Zweitlader-Pfad:** das ROM liest je Sektor nur Daten + 2 CRC-Byte
  (`ROM 0AB9ff`: `INI`, `INI`); die Spur-2-Sektoren tragen als 4-Byte-Kontrollblock (UDOS) `00 00 00 00`
  (keine Kette; Boot- und Zweitlader-Spuren sind auf 710 und 710-1 gleich aufgebaut). Es wird **in
  Sektorreihenfolge 1,2,3…** gelesen, die Sektorfolge auf der Spur darf beliebig verschränkt sein
  (Suche nach ID, `ROM 0279ff`).
* **Der Resident-Treiber liest dagegen 4 Byte hinter der Daten-CRC** (`resident 0AC2: LD BC,0416H / INIR`,
  Ziel `0E9FH`–`0EA2H`): das ist der UDOS-Kontrollblock. Das BOOT-Modul folgt der UDOS-Kette über
  `(0EA1H)` = Byte 2/3 des Blocks, L = Sektor−1, H = Spur (`4110H`: `LD HL,(0EA1H)` →
  `(432FH)`; Ende der Kette: H = FFH, `40EAH`). Die K5122-Nachbildung muss also **nach der Daten-CRC
  die vier Byte des UDOS-Anhangs liefern** (auf dem A5120 bereits so).
* Das ROM probiert die Sektorlänge selbst (`(03FBH)` 11H → rotiert 22H/44H/88H bei Längenfehler,
  `ROM 02A2`); die Spuren 0–2 sind 128 B/Sektor, 26 Sektoren, MFM (Format `udos_ds77`).
* BOOT-Modul-Kennung `BOOT FO UDOS V4 MP 850215` (`boot4000_udos.bin` +4); **auf 710 und 710-1
  bitgleich** (MD5 1863fa7d…). Es benutzt nur die Resident-Vektoren `0BEBH`, `0BF1H`, `0BFDH`.
* Die Zweitlader-Dateien unterscheiden sich 710 ↔ 710-1 (Spur 2: 710 MD5 5f214124…, 710-1 a5d7a8dc…),
  die vier 710-1-Disketten untereinander nicht.

### Speicherbild nach dem Start von UDOS

0000–0BFF Resident (Monitor, Treiber, Tabellen); 0C00–0FFF Systemzellen/Stapel (SP = 0D00H, Zeilenpuffer
0D04H); 1000–25FF `OS`; 2600–3FD4 `ZDOS`; 4000–44xx BOOT-Modul (nur beim Laden); F800–FFFF VRAM
(sichtbar nur während E8H[F] = FFH).

## 2. Zweitlader (1000H–109DH) — was er tut

`zweitlader_710_auszug.lst`. Reihenfolge: `OUT (83H),03H` (CTC K3 Reset) → Zellen 0EDDH, 0FC7H, 0FCEH–0FD1H löschen
→ **IM-2-Vektor `(0FE6H)` = 09FFH (710) bzw. 0A2FH (710-1)** (CTC-Kanal 3 des ROM-Vektors E0H; ROM hatte 0397H)
→ Bootlaufwerk aus `(03FCH)` → `(0EDCH)` = Laufwerk × 20H → **Resident kopieren** (1100H → 0000H, 0C00H Byte)
→ **Laufwerkssonde**: für Laufwerk 0…3 `OUT (18H),77H rotiert` (EEH/DDH/BBH/77H), bis 78 × `OUT (10H),5FH / DFH`
und `IN (12H)`: Bit 7 = 0 („Spur 0 erreicht“/bereit) → Laufwerk da (A = FFH), sonst 0; Ergebnis & Maske
51H/51H/01H/01H → **`0BE1H`–`0BE4H`** (je Laufwerk 1 Byte; 0x51 = Typ 5, Sektorfaktor 1). Danach `OUT (18H),FFH`
→ `(0FDBH)` = 6FH (Konsolen-Flags), `(0FDEH)` = F800H (Cursor), Bildschirm löschen (`CALL 0BEBH` mit 1FH)
→ **K8025-Init** (siehe §6) → `OUT (5CH),0` → `RST 00H`.

## 3. Resident — Aufbau (0000H–0BFFH, beide Fassungen gleich bis auf die in §9 genannten Stellen)

* `0000H` Kaltstart: `SP=0D00H`, `0289H` (I=0FH, IM 2, EI), Monitor-Zellen, dann **Kommando „O“** (Auto-Boot, s. §1).
* **Monitor** (`0093H`–`0400H`): Prompt „+“ (2BH); Kommandos D (Dump), R (Register/ändern), J (Sprung), I (EI/DI-Merker 0EBCH),
  B (Haltepunkt), M (Move), F (Fill), P (Port lesen/schreiben `IN/OUT (C)`, `0117H`/`0120H`), G (Go mit Registerkontext, 0EA6H),
  Q, **O** (Systemlader), **L**/**S** (Sektor lesen/schreiben: Parameter Adresse, Spur/Sektor, Länge → FCB 0E89H → `0BFDH`).
  Fehlerkürzel „?“ (3FH), „ERROR: xx“ (Text 033AH).
* **Sprungleiste** (`0BE5H`–`0BFDH`, vom OS und von BOOT benutzt):

| Adresse | Inhalt | Funktion |
|---|---|---|
| 0BE5H | `JP 0613H` | Zeichen zum Drucker (ZIFSS 5CH/5DH) |
| 0BE8H | `JP 0633H` | Tastatur abfragen: Z = keine Taste, sonst A = Zeichen |
| 0BEBH | `JP 0480H` | Zeichen auf Konsole (OUTA) |
| **0BEEH** | **Datenwort** (710: 067BH, 710-1: 06E7H) + 43H | Zeiger auf die Tastenumsetztabelle (OS: `1469H`, `1584H` legen ihn in die Gerätezeiger 1085H/1087H; 710-1 liest `LD HL,(0BEEH)` selbst, `06A0H`; 710 nicht) |
| 0BF1H | `JP 045DH` | Text (Länge vorangestellt) ausgeben; bei CR folgt LF und, falls eine Taste gedrückt ist, Pause bis zur nächsten |
| 0BF4H | `JP 03F9H` | Parameter aus Zeilenpuffer holen |
| 0BF7H | `JP 0418H` | Zeile lesen |
| 0BFAH | `JP 008BH` | Debugger/Monitor (Einsprung) |
| 0BFDH | `JP 0700H` (710) / `0730H` (710-1) | **Floppytreiber**, IY = FCB |

* Der OS-Gerätetabelleneintrag „FLOPPY“ verweist auf 0BFDH (`OS +10FEH`).
* `0BE1H`–`0BE4H`: **Laufwerkstabelle = das Byte, das `SET DISKCON` schreibt** (`SET` enthält `E1 0B` an +545H;
  `doc/udos_diskettenformat.md` §12.3). Vorgabe aus dem Zweitlader 51H, 51H, 01H, 01H. **Korrektur zum Plan:**
  `DISKCON` liegt auf dem PRG bei **0BE1H**, **nicht** bei 0FCAH (0FCAH ist hier der Schrittzähler des Treibers;
  der OS-Kern verwendet 0FCAH nirgends).

## 4. Konsole / Bildschirm (`resident 0480H–05F2H`)

* VRAM **F800H–FFFFH**, **80 Spalten × 24 Zeilen** (Zeile = 50H Byte; Zeichen FF7FH = letzte Zelle; Scrollen = `LDIR`
  F850→F800 mit BC=0730H, neue letzte Zeile mit 20H gefüllt, `0521H`). Sichtbar macht es **E8H[F] = FFH**:
  `OUTA` schaltet mit `LD BC,F0E8H / OUT (C),D` (D=FFH) ein, **DI**, schreibt, danach `OUT (C),B` (B=F0H) → RAM, **EI**
  (`0494H`/`05FAH`, ebenso Zweitlader/SCPX `SYL17`: `LD B,F0H`, `LD A,FFH`).
* Cursorzeiger `(0FDEH)`; der Cursor wird durch **Bit 7 des Zeichens unter dem Cursor** dargestellt (`05EEH: SET 7,(HL)`,
  gerettetes Zeichen in `(0608H)`, beim nächsten Zeichen zurückgeschrieben `04A8H`).
* Steuerzeichen: 01H Home · 04H, 06H werden als Zeichen (Symbol) geschrieben · 08H links · 09H rechts (ohne zu schreiben) ·
  0AH runter (scrollt) · 0BH hoch · 0DH Zeilenanfang · **1FH Bildschirm löschen (füllt F800–FFFF mit 00H)** ·
  <20H sonst ignoriert · **ESC (1BH) + Zeile + Spalte** (Zeile ≤ 17H, Spalte ≤ 4FH geklemmt, `0534H`/`04B3H`) ·
  **14H–17H + 2 Parameter**: Zeilenbereichs-Funktionen (Löschen/Einfügen per `LDIR`/`LDDR`, `04D4H`ff, Modus = Bit 0–1) `[?]`.
* `(0FDBH)` Konsolen-Flags (Vorgabe 6FH): Bit 6/7 = 0 ⇒ **Ausgabe zusätzlich auf den Drucker** (`0486H`: `AND C0H` → `CALL 0BE5H`);
  Bit 5 = 0 ⇒ nächstes Buchstabenzeichen mit `AND 1FH` (Steuerzeichen, Einmal-Strg, dann wieder 1); Bit 4 = 1 ⇒ Buchstaben
  umschalten (XOR 20H); Bit 0–3 nur Merker.
* Druckerausgabe `0613H`: `IN (5CH)` auf XOFF (13H) prüfen, dann auf XON (91H = 11H mit Parität) warten; `IN (5DH)` Bit 2 (Tx leer)
  abwarten; `OUT (5CH),A`.

## 5. Tastatur PRG 710 — 8279 (C8H/C9H)

**Abfrage: reines Polling, kein Interrupt.** Resident `0633H`: `IN A,(C9H)`, `AND 07H` (FIFO-Füllstand; 0 ⇒ Rückkehr mit Z), sonst
`IN A,(C8H)` und Umsetzung. SCPX-BIOS `B15x` `E097H` identisch (wartet in Schleife). **Befehle an C9H gibt der Resident nie aus**
(nur `IN`); initialisiert wird **im Boot-ROM** (`ROM 0157H`: `02H`, `C1H`) bzw. im SCPX-BIOS (`E082H`: `02H`, `C1H`).
`02H` = Tastaturmodus, kodierte Abtastung, N-Key-Rollover; `C1H` = Clear All. Es erscheint **nur ein Code je Tastendruck**
(kein Loslass-Code); eine Wiederholfunktion gibt es in der Hardware nicht (die UDOS-Editoren simulieren sie, `^R`).
Das genügt der Nachbildung: Taste gedrückt → ein Byte in den FIFO (Tiefe 8), Status Bit 0–2 = Anzahl.

**Code `S RRR CCC`:** Bit 6 = Umschaltstufe („Shift“-Eingang des 8279), Bit 5–3 = Abtastzeile, Bit 2–0 = Rückleitung;
Bit 7 (CNTL) kommt **in keiner Tabelle vor** — beide Betriebssysteme führen nur 128 Einträge (UDOS-Tabelle 067BH–06FAH,
SCPX-Tabelle `B15x` DE00H+02E0H–035FH). Nachbildung: Bit 7 nie setzen. Die Polarität des Shift-Bits am 8279 (Eingang
mit Pull-up) ist nicht aus dem Code ablesbar `[?]`; für den Emulator genügt: **Taste ohne Shift ⇒ Bit 6 = 0**, Shift gedrückt ⇒ Bit 6 = 1
(so ist die Tabelle in beiden Systemen angelegt).

**Umsetzung** (`resident 063AH–0677H`): `A = Tabelle[Code]`; < 40H ⇒ Zeichen direkt; 40H–7FH ⇒ Buchstaben/Zeichen mit
Flag-Bearbeitung (XOR 20H bei Bit 4 von `(0FDBH)` für 40H–5AH und 60H–7AH, bei Einmal-Strg `AND 1FH`); ≥ 80H ⇒
**Umschalttaste**: `(0FDBH) XOR (A AND 7FH)`, kein Zeichen (A=0 ⇒ Z). Umschalter-Werte: 81H/82H/84H/88H = Merker 0–3,
90H = Buchstaben-Umschaltung (CASE), A0H = Einmal-Strg, C0H = Drucker ein/aus.

**Gegenprobe gegen `B152V24.SYS`/`B151V24.SYS`/`B152IFSS.SYS`** (identische 128-Byte-Tabelle bei DE00H+02E0H): bis auf
15 Einträge **gleich**; die Buchstaben sind **vertauscht** (UDOS: ohne Shift GROSS, SCPX: ohne Shift klein — Folge der
OS-eigenen Anfangswerte der Flags 6FH ↔ 00H, nicht der Hardware). Unterschiede (Code: UDOS / SCPX):
08: 08/18 · 10: 81/13 · 18: C0/10 · 19: 07/05 · 28: A0/90 · 39: 10/04 · 3A: 11/08 · 3B: 0A/18 · 48: 04/18 · 50: 82/13 ·
58: 84/10 · 68: 90/A0 · 78: 88/03 · 7E: 20/FF · 7F: 20/18. Dieselben vier Tasten sind in beiden Systemen Cursor (19H hoch,
39H rechts, 3AH links, 3BH runter — UDOS ^G/^P/^Q/^J, SCPX ^E/^D/^H/^X); **ET1 = 37H = CR**, **ET2/ST = 38H = ESC**
(`SCREEN.BED`: „ET1 entspricht ^M“, „^[ entspricht ET2 bzw. ST“); TAB = 3CH/7CH; Strg-Taste (Einmal) und Umschalt-Taste
(CASE) teilen sich den Code 28H/68H (unverschoben/verschoben) in beiden Systemen.

Tabelle: **`k7609_codes.csv`** (64 Zeilen = Codes 00H–3FH; `shift_zeichen` = Code + 40H). Zeichen aus der UDOS-Tabelle;
SCPX-Abweichungen in der Spalte `anmerkung`. Die **Aufschriften** der Tasten (S1…S9, CL, …) sind nicht aus dem Code
ableitbar: S1–S9/CL tauchen in keinem der beiden Systeme als eigener Code auf `[?]` — sie sind unter den Codes mit Wert 20H
(Leerzeichen-Platzhalter 16H/17H/1DH–1FH/3DH–3FH und Schattenplätze) oder als Merkertasten (81H…88H) zu suchen, **am Gerät
bzw. am Tastenbild zuordnen**. Offen: Leertaste = welcher der 20H-Codes `[?]` (alle liefern 20H, die Nachbildung kann einen wählen,
z. B. 16H).

## 6. Tastatur PRG 710-1 — K7672 an der K8025 (5EH/5FH)

* **Init im Zweitlader** (`zweitlader_710-1_auszug.lst 107EH–10D2H`): `OTIR` 2 Byte an 58H (CTC K0: `07H`, TC `01H` ⇒ 153,6 kHz), dann 8 Byte an
  5FH (SIO A32 Kanal B): `00H`, `18H` (Kanalreset), `04H`/`4CH` (×16, 2 Stopp, keine Parität), `03H`/`C1H` (Rx 8 Bit an), `05H`/`68H` (Tx 8 Bit an) =
  **9600 Bd, 8N2**; danach **wird `ESC [ ? 1 1 h` an die Tastatur gesendet** (Tx-leer-Warten `5FH` Bit 2, Daten 5EH; Bedeutung des
  Modus 11 in der K7672-Firmware `[?]`). Weder Interrupt noch WR1/WR2: **reines Polling**.
* **Abfrage** (`resident 0633H` ff. der 710-1): `IN (5FH)` Bit 0 = Zeichen da; kein Zeichen und kein ESC ausstehend ⇒ Z. ESC (1BH) merkt
  sich ein Flag (`(0716H)` Bit 0) und wartet bis ≈ 1000H Durchläufe auf das nächste Byte (Zeitablauf ⇒ nacktes ESC = 1BH).
  Folgen `ESC c` ⇒ A5H, `ESC ?`, `ESC O x` (Flag Bit 1) und `ESC [ x` (Bit 2): Anwendungs-Zifferntasten/Cursor/PF1–PF4. Das Zeichen wird mit
  +40H (nach `O`/PF 50H–53H) oder +80H versehen und in der **Paartabelle** bei `(0BEEH)` = 06E7H (Paare *Eingang → Zeichen*, 00H = Ende) gesucht:
  C8→01, C1→07, C2→0A, C3→10, C4→11, DA→16, CA→1F, C5→1F, 90→C0, 91→C1, 92→C2, 93→C3, B0→C4 … B6→CA, B7→A6, B8→A0, B9→A1, BA→A2.
  (C1–C4 = ESC [ A–D ⇒ Cursor hoch/runter/rechts/links = ^G/^J/^P/^Q wie am 710.) Treffer ≥ 80H werden wie am 710 als Umschalter
  auf `(0FDBH)` ausgewertet (`06CAH`: < 80H Zeichen; 80H–8FH verworfen (A = 0); 90H–9FH und ≥ B0H unverändert als Funktionszeichen geliefert;
  A0H–AFH ⇒ Flag-Bit (Wert & 0FH) in `(0FDBH)` umschalten, kein Zeichen — z. B. A6H = Drucker ein/aus, A0H–A2H = Merker 0–2).
* Dieselben Bildschirm-/Druckerroutinen wie am 710.

## 7. Floppytreiber (`0700H`/`0730H` ff.)

* **FCB (IY)**: +1 Flags (Bit 0 = asynchron: Rückkehr sofort, sonst wartet der Aufruf auf Status Bit 7; `(IY+1) OR 05H` muss 0FH ergeben ⇒
  Lesen 0AH, Schreiben 0EH, Bit 2 = Schreiben; sonst Fehler C1H), +2/+3 Pufferadresse, +4/+5 Länge in Byte, +6/+7 Fertig-Routine, +8/+9 Fehler-Routine,
  **+10 Status** (80H = gut; C1H… = Fehler: C1 Flags, C2 Laufwerk fehlt, C3 schreibgeschützt, C5 Spur > Höchstspur, C6 Datenfehler,
  C8 `[?]`; 4xH sind Zwischenzustände), +11 Laufwerk (Bit 5–6), Seite (Bit 7), Sektor−1 (Bit 0–4), +12 Spur. Belegt wird nur **ein** FCB gleichzeitig
  (`(0FC7H)` ≠ 0 ⇒ Warten, `0700H`).
* **Ablauf zeitgesteuert über CTC K3 (Port 83H)** + Zustandsmaschine: die Fortsetzung steht in `(0EA3H)`, die ISR (`09FFH`/`0A2FH`) ruft sie.
  Zeitkonstanten: `61B7H` Schrittzeit/Einschwingen (Typ-Satz patcht 61H), `18B7H`, `30B7H`, `02C7H` (Zähler 2 × CTC-K2-Takt), Ende `C7H/64H`
  (Motornachlauf = 100 × K2-Periode). K2 (Port 82H) programmiert nur das ROM: `37H`/`C0H` ⇒ 256 × 192 Takte.
* **Laufwerkstyp-Nibble (Tabelle 0BE1H…) wählt einen von 5 Patchsätzen zu je 11 Byte** (`0B5B`ff., Zielliste `0B45H`): Typ 1,2 → Satz 0, 3 → 1, 4 → 2,
  **5 → Satz 3 (`28 04 51 50 00 11 0B 00 3E F6 F6`)**, 6 → 4. Patcht u. a. Schrittzeit-TC (0855H), Höchstspur-Konstante (0820H: 4DH → bei Typ 5 **50H = 80 Spuren**),
  Spur, ab der die Schreibstromumschaltung `12H` Bit 3 greift (0828H; 14H = 20), FM-/MFM-Zweig der Schreibsync (`0AFAH`: Typ 1,2 = FM, ≥ 3 = MFM),
  Länge der Schreiblücken (`0A93H`, `0AEBH`) und die Rücksprungweite der MKE-Warteschleife (`0A65H`/`0AA5H`: F6H = Strobe wiederholen, FAH = nur lesen). Seite 1 verlangt Typ 5
  (`0772H`: `CP 50H`), Typ-Nibble 0 = Laufwerk fehlt.
* **Sektorlänge:** Low-Nibble des Tabellenbytes (1 = 128 B, 2, 4, 8) wird in die Leseroutine gepatcht (`0A83H`, `0A8CH`, wie `ROM 0300H`).
* **Marken-FF (`12H` Bit 1):** 710 — nach dem Strobe `OUT (10H),D / OUT (10H),E` Schleife **solange Bit 1 = 1** (`JR NZ`, `0A5CH`–`0A64H`, ID und Datenfeld
  `0A9CH`–`0AA4H`); vor jedem Leseversuch `XOR A / OUT (14H),A` (`0A51H`). 710-1 — Strobe **einmal**, dann warten **bis Bit 1 = 1** (`JR Z`, `0A85H`–`0A91H`,
  `0ACBH`–`0AD3H`) und danach **einmal `OUT (14H),A`** (mit A = 02H) `[?]`. Das ist **die einzige Treiberabweichung 710 ↔ 710-1**
  (außer Tastatur, ISR-Adresse und der Umordnung eines `SUB 01H` im Schreibpfad `0B0FH`).
* K5122-Bits (aus dem Treiber): `10H` Bit 7 = /Schritt (Impuls 9FH→1FH→9FH), Bit 5 = Richtung (BFH = nach innen/höhere Spur `[?]`); `12H` lesen: Bit 7 = nicht Spur 0
  (Suche läuft, bis 0), Bit 5 = 0 ⇒ schreibgeschützt (Fehler C3), Bit 1 = MKE, Bit 0 = bereit/Index `[?]`; `12H` schreiben: 04H (Spur ≤ 20) / 0CH (Spur > 20,
  Schreibstrom); `14H` Daten schreiben (Sync 00×n, A1 A1 A1, FBH, Daten, CRC), `16H` Daten lesen (`INI`), `18H` Laufwerkswahl EEH/DDH/BBH/77H.
  Schreibsteuerworte `A4H`→`B4H`→`B6H` (`0AE3H`–`0AE8H`), Lesesteuerwort `BBH` (`10H`, `07E1H`), vor dem Schreiben `91H`.
  Lesen: Sync-Wiederholung bis FEH (ID) bzw. Datenmarke, ID-Vergleich Spur/Sektor/Längencode, **Seitenbyte unverglichen** (`0A77H`).

## 8. Interrupts und Ports

* **IM 2, I = 0FH**; belegt ist nur **CTC K3, Vektor E6H ⇒ `(0FE6H)`** = ISR (710: 09FFH, 710-1: 0A2FH; ROM: 0397H). Kein Interrupt von K5122-PIOs,
  K8025/SIO (keine WR1/WR2/WR-Vektoren), ADA, EPROMmer: **die ISR ist ausschließlich der Floppy-Zeitgeber**. Der Resident programmiert **nie** den
  CTC-Vektor (`OUT (80H)` nur im ROM: E0H). ISR: `OUT (83H),03H` (Kanal 3 stoppen), `EI`, Fortsetzung `(0EA3H)`, `RETI`.
* **Portliste Resident + Zweitlader (710)**: 10H K5122 Steuer-PIO A (Daten) · 12H K5122 Steuer-PIO B (Status/Formatwert) · 14H Schreibdaten · 16H Lesedaten ·
  18H Laufwerkswahl (8212) · 83H CTC K3 · 58H/5DH/5CH ZIFSS-Init+Druck (Zweitlader: 58H `07H,01H`; 5DH `04H,4DH,03H,41H,05H,28H` = 9600 Bd, 7 Bit, ungerade Parität, 2 Stopp; 5CH Daten) ·
  C8H/C9H 8279 (nur Lesen) · E8H[F] (nur FFH/F0H, über `OUT (C),r` mit B = F0H) · Monitor-Kommando `P`: beliebiger Port.
  **710-1 zusätzlich/anders:** 5EH/5FH K7672 (Daten/Status, Init + Polling), 58H CTC K0; **kein** C8H/C9H.
  *Nicht benutzt:* 80H/81H/82H (nur ROM), 84H–87H (EPROMmer-PIO), C4H–C7H, D0H–D3H, E0H–E7H, EAH, EBH.
  (Die Bytes `D3 F6`/`D3 FA` bei `0B63H`ff. sind Datentabelle, keine Port-Zugriffe.)
* **E8H–EBH (Arbeitsmodell §4b):** der Resident berührt **nur E8H[Seite F]** (Werte **FFH = VRAM sichtbar**, **F0H = RAM**, Seitennummer in A8–A15 = B-Register
  bzw. A bei `OUT (E8H),A`). Weder Zweitlader noch Resident noch `OS`/`ZDOS` schreiben EAH/EBH oder andere Seiten. Seite 0 bleibt auf dem vom ROM gesetzten Wert
  (E8H[0] = 10H ⇒ RAM). **Das Arbeitsmodell wird damit bestätigt, nicht widerlegt** (EAH ≠ Identität: nirgends benutzt). UDOS-Anwendungsprogramme
  (`ADM_O2` `3EH FFH/F0H … OUT (E8H),A`) benutzen dasselbe Muster für die Seite F. `MPSS`, `MRS4.OVL2` zeigen Byte-Treffer `D3 EB` bzw. `DB EB` — nicht als Code verifiziert `[?]`
  (`EPROM43` benutzt EBH/EAH/E9H nachweislich als Fremd-EPROMmer, siehe Plan §3.4).
* `OS` und `ZDOS`: lineare Dekodierung findet **kein einziges `IN`/`OUT`**.

## 9. Unterschiede 710 ↔ 710-1 im Resident (Befehlsstrom-Vergleich, 51 Differenzblöcke)

1. Tastaturroutine `0633H` (8279-Tabellenumsetzung ↔ K7672-ESC-Auswertung, ca. 55 Befehle), Tabelle (067BH 128 B ↔ 06E7H Paarliste) und
   Zeigerwort bei 0BEEH.
2. MKE-Polarität im Leseablauf (§7), `OUT (14H),A`-Lage, ein vertauschtes `SUB 01H` im Schreibpfad; Einsprung `0BFDH` zeigt auf 0700H bzw. 0730H.
3. ISR-Adresse 09FFH ↔ 0A2FH (im Zweitlader gesetzt); K8025-Init (§6) im Zweitlader.
   Alles andere (Konsole, Monitor, Drucker, Typtabelle, Zeitgeber) ist befehlsgleich.

## 10. Bootsektor (512 B bei 0400H)

`18 03 'SYL'` · `LD IY,0422H` · `A = (03FCH) >> 3 & E0H → (IY+11)` · `CALL 03FDH` · `(IY+10) = 80H ⇒ JP 1000H`, sonst `JP 03F4H` („DISKERROR“).
Parameterblock 0422H: `00 0A | 00 10 | 00 0D | … | Spur 02`. Der erste Sektor (Code, 80H Byte) ist auf 710 und 710-1 gleich; Sektor 2–4 (ab +80H) unterscheiden sich, werden aber nicht ausgeführt `[?]`.
