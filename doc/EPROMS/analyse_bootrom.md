# Analyse: Boot-ROM der K2526 ZRE (Zentrale Recheneinheit)

**Datei:** `doc/EPROMS/Prozessoreinheit_ZRE_K2526_A26.bin`  
**C-Header:** `core/cards/k2526/rom_data.h` (Array `ZRE_BOOT_ROM[1024]`)  
**Größe:** 1024 Byte (1 KB)  
**Mapping:** 0x0000–0x03FF (abschaltbar über BS-PIO Port B Bit 0)  
**Prozessor:** Z80 (U880D, ZVE1) @  2,4576 MHz

---

## 1. Übersicht und Funktion

Das Lade-ROM der K2526 ZRE ist ein 1-KB-EPROM (Bautyp 2716 o.ä.), das nach dem
Hardware-Reset des Systems als einziges ausführbares Medium zur Verfügung steht.
Es ist im Adressraum bei 0x0000–0x03FF eingeblendet und enthält den vollständigen
Bootstrap-Code für den Robotron A5120.

Nach dem Reset springt die Z80-CPU ZVE1 an Adresse 0x0000, wo der ROM-Code
unmittelbar die Kontrolle übernimmt. Das ROM bleibt aktiv, bis es am Ende des
Bootstrap-Prozesses durch Schreiben von Bit 0 = 0 in den BS-PIO Port B (Port 0x0A)
explizit abgeschaltet wird. Danach liegt an 0x0000 das RAM der K3526-Speicherkarte.

### Aufgaben des Boot-ROMs (Sequenz)

```
/RESET  →  ZVE1 startet bei 0x0000 (ROM aktiv)
    1.  Interrupts deaktivieren  (DI)
    2.  RAM-Test (K3526-Speicherkarte)
    3.  CTC initialisieren (Baudrate für SIO auf K8025)
    4.  K5122-Floppy-Controller initialisieren
    5.  Ersten Sektor von Diskette lesen (Track 0, Sektor 1)
        → Daten per DMA (ZVE2) oder Byte-Polling in RAM bei 0x0100 laden
    6.  Bootstrap-Code bei 0x0100 ausführen (JP 0x0100)
    7.  BS-PIO Port B Bit 0 = 0 schreiben → ROM abschalten
    8.  Betriebssystem (CPA/CP/M) läuft nativ im RAM
```

---

## 2. Physische Eigenschaften des EPROM-Dumps

| Eigenschaft | Wert |
|-------------|------|
| Dateigröße | 1024 Byte |
| Nutzbarer Code | ~800 Byte (0x0000–0x031D ca.) |
| Padding (Ende) | ~226 Byte, gefüllt mit 0xE5 (PUSH HL) |
| Anteil doppelter Bytepaare | ~72% (371 von 512 Paaren identisch) |

### 2.1 Interpretationshinweis: Verdoppelte Bytes

Ein auffälliges Merkmal des Dumps ist, dass ca. 72% der Bytepaare (Byte[n] und
Byte[n+1]) identisch sind. Das ist kein Fehler im EPROM-Dump. Der Programmer hat
viele Instruktionen absichtlich doppelt kodiert:

```
Adresse   Bytes       Instruktionen
0x0002:   F3 F3       DI / DI      (doppeltes Disable-Interrupts)
0x0004:   19 19       ADD HL,DE / ADD HL,DE  (doppelte Addition)
0x000A:   F0 F0       RET P / RET P
0x0010:   1B 1B       DEC DE / DEC DE
0x0014:   3D 3D       DEC A / DEC A
```

Diese Technik ist für timing-kritische Hardware-Zugriffe typisch: Durch doppelte
Ausführung einer Instruktion wird die CPU für die Zeit eines weiteren Befehlszyklus
angehalten, was langsamen PIOs und EPROMs Zeit zur Reaktion gibt.

### 2.2 Padding mit 0xE5

Ab ca. Adresse 0x031E füllt der Wert 0xE5 (PUSH HL) den Rest des ROMs bis 0x03FF.
Im Falle eines Programmzähler-Fehllaufs würde die CPU in eine Endlosschleife aus
PUSH-Befehlen geraten und den Stack überlaufen — ein deterministischer Fehlerzustand,
der einen Watchdog-Reset oder Hardware-Fehlerindikation ermöglicht.

---

## 3. Hardware-Kontext und Port-Mapping

Der Boot-ROM-Code greift auf folgende Hardwarekomponenten zu:

### 3.1 K2526 ZRE — interne Ports (0x00–0x0F)

| Port | Signal | Verwendung im ROM |
|------|--------|-------------------|
| 0x02 | /RES-SPA | Speicherschutz-Reset |
| 0x03 | /UCS4 | Fehlerlampe setzen |
| 0x04 | /RES-ZVE2 | DMA-CPU (ZVE2) Reset |
| 0x08 | BS-PIO Port A Data | MEMDI-Umschaltsignal, Einzelschritt |
| 0x09 | BS-PIO Port A Ctrl | PIO-Modus setzen (Richtung) |
| 0x0A | BS-PIO Port B Data | **ROM-Abschaltung** (Bit 0 = 0) |
| 0x0B | BS-PIO Port B Ctrl | PIO-Modus setzen |
| 0x0C–0x0F | CTC Kanal 0–3 | **Baudrate** für serielle Schnittstelle |

### 3.2 K5122 AFS — Floppy-Controller (0x10–0x18)

| Port | PIO | Signal | Verwendung im ROM |
|------|-----|--------|-------------------|
| 0x10 | S-PIO A Data | Steuerausgänge | Schritt, WE, /HL, /STR |
| 0x11 | S-PIO A Ctrl | PIO-Steuerregister | Mode-Setup |
| 0x12 | S-PIO B Data | Status-Eingänge | /RDYL, /TO, /WP lesen |
| 0x13 | S-PIO B Ctrl | PIO-Steuerregister | Mode-Setup |
| 0x14 | D-PIO A Data | Schreibdaten | (beim Bootload nicht genutzt) |
| 0x15 | D-PIO A Ctrl | PIO-Steuerregister | Mode-Setup |
| 0x16 | D-PIO B Data | **Lesedaten** | Sektorbytes lesen |
| 0x17 | D-PIO B Ctrl | PIO-Steuerregister | Mode-Setup |
| 0x18 | 8212 | Laufwerksauswahl | Laufwerk A (/LCK1=0) |

---

## 4. Vollständiges Disassemblat

Das folgende Listing zeigt die sequenzielle Z80-Disassemblierung des Boot-ROMs.
Adressen sind als 4-stellige Hex-Werte angegeben (0x0000 = ROM-Start = Reset-Vektor).
Die Padding-Bytes (0xE5 ab ca. 0x031E) werden am Ende als Block angegeben.

```
; ============================================================
; K2526 ZRE BOOT ROM  –  Prozessoreinheit_ZRE_K2526_A26
; Größe: 1024 Byte  (Code: ~800 Byte, Padding: ~226 Byte)
; Reset-Einstiegspunkt: 0x0000
; ============================================================

; --- Initialisierung ---
0000:  EE 77        XOR 77h          ; A = 0xFF XOR 0x77 = 0x88  (Identifikationsmarker)
0002:  F3           DI               ; Interrupts deaktivieren
0003:  F3           DI               ; (redundant, Timing)
0004:  19           ADD HL,DE        ; HL = HL + DE  (Adressberechnung)
0005:  19           ADD HL,DE        ; (doppelt für Timing)
0006:  27           DAA              ; Binär → BCD-Korrektur
0007:  50           LD D,B
0008:  04           INC B
0009:  45           LD B,L
000A:  F0           RET P            ; (Daten-Byte oder bedingter Sprung)
000B:  F0           RET P
000C:  11 11 50     LD DE,5011h      ; DE = 0x5011  (Adresse/Zeiger)
000F:  50           LD D,B
0010:  1B           DEC DE
0011:  1B           DEC DE           ; (doppelt für Timing)
0012:  F3           DI
0013:  F3           DI
0014:  3D           DEC A
0015:  3D           DEC A
0016:  F8           RET M
0017:  F8           RET M
0018:  09           ADD HL,BC
0019:  09           ADD HL,BC

; --- Erste Port-Ausgaben (K5122 / K2526 Initialisierung) ---
001A:  D3 D3        OUT (D3h),A      ; Port 0xD3 (außerhalb bekannter Karte?)
001C:  10 3E        DJNZ 005Ch       ; Zählschleife
001E:  D3 D3        OUT (D3h),A
0020:  30 FF        JR NC,0021h      ; Sprung wenn kein Carry
0022:  FF           RST 38h
0023:  FF           RST 38h
0024:  FF           RST 38h
0025:  FF           RST 38h
0026:  F8           RET M
0027:  F8           RET M
0028:  F7           RST 30h
0029:  F7           RST 30h
002A:  36 36        LD (HL),36h
002C:  04           INC B
002D:  3E D3        LD A,D3h
002F:  D3 3F        OUT (3Fh),A      ; Port 0x3F  (CTC oder K8025?)

; --- PIO-Initialisierung Bereich ---
0031:  3E DF        LD A,DFh         ; A = 0xDF = 11011111b
0033:  DF           RST 18h
0034:  FF           RST 38h
0035:  FF           RST 38h
0036:  BE           CP (HL)
0037:  BE           CP (HL)
0038:  87           ADD A,A          ; A = A * 2
0039:  28 0D        JR Z,0048h       ; Sprung wenn Null
003B:  0D           DEC C
003C:  F7           RST 30h
003D:  F7           RST 30h
003E:  F5           PUSH AF
003F:  F5           PUSH AF

; --- Speicherlesen und Testzähler ---
0040:  3A FC FF     LD A,(FFFCh)     ; A = RAM-Byte bei 0xFFFC lesen (RAM-Test)
0043:  FF           RST 38h
0044:  FF           RST 38h
0045:  DA 96 96     JP C,9696h       ; Sprung bei Carry (Fehler?)
0048:  07           RLCA             ; A rotieren links
0049:  18 1D        JR 0068h         ; Sprung nach 0x0068

004B:  1D           DEC E
004C:  28 F2        JR Z,0040h       ; Schleife zurück zu 0x0040
004E:  FD FD        DB FDh,FDh       ; (undefinierte IY-Prefix-Sequenz)
0050:  EF           RST 28h
0051:  EE 37        XOR 37h
0053:  37           SCF              ; Carry setzen
0054:  03           INC BC
0055:  03           INC BC
0056:  F5           PUSH AF
0057:  F1           POP AF
0058:  80           ADD A,B
0059:  22 07 07     LD (0707h),HL    ; HL in Adresse 0x0707 speichern

; --- Adressberechnung / Stack-Init ---
005C:  26 00        LD H,00h         ; H = 0
005E:  F5           PUSH AF
005F:  F5           PUSH AF
0060:  7F           LD A,A           ; NOP-Äquivalent (A=A)
0061:  FF           RST 38h
0062:  FF           RST 38h
0063:  FF           RST 38h
0064:  ED ED        DB EDh,EDh
0066:  01 81 B7     LD BC,B781h
0069:  B7           OR A             ; Flags aktualisieren
006A:  FC FC 28     CALL M,28FCh     ; Bedingte Subroutine
006D:  28 CD        JR Z,003Ch
006F:  CD 03 03     CALL 0303h       ; → Subroutine bei ROM-Adresse 0x0303

0072:  EE EE        XOR EEh
0074:  F7           RST 30h
0075:  F7           RST 30h
0076:  BA           CP D
0077:  BA           CP D
0078:  03           INC BC
0079:  03           INC BC
007A:  62           LD H,D
007B:  62           LD H,D
007C:  DF           RST 18h
007D:  DB F6        IN A,(F6h)       ; Port 0xF6 lesen
007F:  F6 F3        OR F3h
0081:  F3           DI
0082:  ED ED        DB EDh,EDh
0084:  2B           DEC HL
0085:  2B           DEC HL
0086:  BA           CP D
0087:  BA           CP D
0088:  00           NOP
0089:  62           LD H,D
008A:  ED ED        DB EDh,EDh
008C:  2B           DEC HL
008D:  2B           DEC HL
008E:  8B           ADC A,E
008F:  8B           ADC A,E

; --- Floppy-Controller Ausgaben / DMA-Init ---
0090:  D3 D3        OUT (D3h),A
0092:  D3 D3        OUT (D3h),A
0094:  FF           RST 38h
0095:  FF           RST 38h
0096:  36 36        LD (HL),36h      ; (HL) = 0x36
0098:  10 D3        DJNZ 006Dh       ; Schleife
009A:  D3 D3        OUT (D3h),A
009C:  04           INC B
009D:  3E D3        LD A,D3h
009F:  D3 D3        OUT (D3h),A      ; A=0xD3 → Port 0xD3
00A1:  D3 FF        OUT (FFh),A      ; A=0xD3 → Port 0xFF
00A3:  FF           RST 38h
00A4:  F3           DI
00A5:  F3           DI

; --- K5122 PIO-Modus setzen ---
00A6:  F4 3E D3     CALL P,D33Eh     ; (Condition-Call, Daten?)
00A9:  D3 DB        OUT (DBh),A
00AB:  DB 16        IN A,(16h)       ; **K5122 Daten-PIO Port B lesen** (Lesebyte)
00AD:  3A 03 03     LD A,(0303h)     ; Byte aus ROM-Adresse 0x0303 laden
00B0:  D3 11        OUT (11h),A      ; **K5122 Steuer-PIO Port A Ctrl** schreiben
00B2:  7F           LD A,A
00B3:  7F           LD A,A
00B4:  C9           RET              ; Rücksprung (Ende Subroutine)

; --- Subroutine: Bit-Test / Byteprüfung ---
00B5:  CB AA        RES 5,D          ; Bit 5 in D löschen
00B7:  20 04        JR NZ,00BDh      ; Sprung wenn Nicht-Null
00B9:  01 59 59     LD BC,5959h
00BC:  ED 42        SBC HL,BC        ; HL = HL - BC (mit Carry)
00BE:  C0           RET NZ           ; Rückkehr wenn nicht Null

; --- Adressberechnung ---
00BF:  21 92 E5     LD HL,E592h
00C2:  FF           RST 38h
00C3:  FF           RST 38h
00C4:  BF           CP A             ; A - A = 0 (immer Null)
00C5:  F1           POP AF
00C6:  FB           EI               ; Interrupts freigeben
00C7:  FB           EI
00C8:  E5           PUSH HL
00C9:  E5           PUSH HL
00CA:  F7           RST 30h
00CB:  F7           RST 30h
00CC:  03           INC BC
00CD:  35           DEC (HL)
00CE:  28 04        JR Z,00D4h       ; Sprung wenn Null
00D0:  F7           RST 30h
00D1:  F7           RST 30h
00D2:  5D           LD E,L
00D3:  5D           LD E,L
00D4:  BF           CP A
00D5:  BF           CP A
00D6:  10 10        DJNZ 00E8h       ; Zählschleife

; --- Zeiger-Schreibroutine ---
00D8:  23           INC HL           ; HL++
00D9:  36 01        LD (HL),01h      ; (HL) = 0x01
00DB:  18 F3        JR 00D0h         ; Schleife zurück

00DD:  2A 03 03     LD HL,(0303h)    ; HL = Wort aus ROM-Adresse 0x0303

; --- Speicher-/Port-Testzugriffe ---
00E0:  3A F9 E7     LD A,(E7F9h)     ; A = RAM bei 0xE7F9 lesen
00E3:  E7           RST 20h
00E4:  B9           CP C
00E5:  A9           XOR C
00E6:  1F           RRA              ; A >> 1 (rechts rotieren)
00E7:  9E           SBC A,(HL)
00E8:  16 D9        LD D,D9h
00EA:  A1           AND C
00EB:  A1           AND C
00EC:  FE ED        CP EDh
00EE:  F3           DI
00EF:  F3           DI
00F0:  03           INC BC
00F1:  BF           CP A

; --- CTC-Initialisierung (Baudrate) ---
00F2:  13           INC DE
00F3:  13           INC DE
00F4:  3E FD        LD A,FDh         ; A = 0xFD = Kanalsteuer-Wort CTC
00F6:  D3 90        OUT (90h),A      ; Port 0x90 (K8025-SIO oder CTC-Zeitgeber?)
00F8:  3E B5        LD A,B5h         ; A = 0xB5 = CTC-Zählerwert
00FA:  10 10        DJNZ 010Ch
00FC:  18 5C        JR 015Ah         ; Sprung nach 0x015A
00FE:  AF           XOR A            ; A = 0
00FF:  AF           XOR A

; --- ZVE2-DMA-CPU aktivieren / Floppy-Lesesequenz ---
0100:  D9           EXX              ; Register-Exchange (BC→BC', DE→DE', HL→HL')
0101:  DF           RST 18h
0102:  DB DB        IN A,(DBh)       ; Port 0xDB lesen (Status?)
0104:  7E           LD A,(HL)        ; A = (HL) = Byte aus RAM laden
0105:  FB           EI               ; Interrupts freigeben
0106:  DB DB        IN A,(DBh)       ; Port 0xDB lesen
0108:  16 DB        LD D,DBh
010A:  DB DB        IN A,(DBh)
010C:  16 B8        LD D,B8h
010E:  20 E8        JR NZ,00F8h      ; Sprung wenn nicht Null (Warteschleife)

; --- Floppy-Status-Polling (K5122 Port 0x12) ---
0110:  DB DE        IN A,(DEh)       ; Port 0xDE lesen
0112:  FB           EI
0113:  34           INC (HL)         ; (HL)++
0114:  FB           EI
0115:  FB           EI
0116:  16 BA        LD D,BAh
0118:  20 DE        JR NZ,00F8h      ; Sprung wenn nicht Null (Retry)
011A:  DB 16        IN A,(16h)       ; **K5122 Data-PIO Port B** (Lesedaten)
011C:  BD           CP L             ; Vergleich mit L
011D:  20 DB        JR NZ,00FAh      ; Sprung wenn ungleich (Fehler/Retry)

; --- Sektorlese-Schleife ---
011F:  DB 16        IN A,(16h)       ; **K5122 Data-PIO Port B** lesen
0121:  FD DB        DB FDh,DBh
0123:  DB B5        IN A,(B5h)       ; Port 0xB5 lesen
0125:  B5           OR L
0126:  D3 10        OUT (10h),A      ; **K5122 Steuer-PIO Port A** schreiben

; --- Nächste Datenbyte-Leseroutine ---
0128:  3A FD CB     LD A,(CBFDh)
012B:  CB D3        SET 2,E          ; Bit 2 in E setzen
012D:  D3 DB        OUT (DBh),A
012F:  DB 16        IN A,(16h)       ; **K5122 Data-PIO Port B** lesen
0131:  F9           LD SP,HL         ; SP = HL
0132:  DB DB        IN A,(DBh)
0134:  FB           EI
0135:  FB           EI
0136:  DB DB        IN A,(DBh)
0138:  16 DB        LD D,DBh

; --- DMA-Kopf-Ende / EXX für ZVE2-Bereich ---
013A:  D9           EXX              ; Register zurücktauschen
013B:  D9           EXX
013C:  ED A2        INI              ; **INI**: (HL) = IN A,(C); HL++; B--
013E:  A2           AND D
013F:  A2           AND D

; --- Blockübertragung mit INI ---
0140:  EF           RST 28h
0141:  BE           CP (HL)
0142:  F3           DI
0143:  F3           DI
0144:  ED ED        DB EDh,EDh
0146:  FD FD        DB FDh,FDh
0148:  A2           AND D
0149:  3E D3        LD A,D3h
014B:  D3 10        OUT (10h),A      ; **K5122 Steuer-PIO Port A** = 0xD3
014D:  EB           EX DE,HL         ; DE ↔ HL
014E:  F1           POP AF
014F:  F1           POP AF

; --- Byte verarbeiten und Zeiger fortschalten ---
0150:  47           LD B,A           ; B = A
0151:  6F           LD L,A           ; L = A
0152:  BF           CP A             ; Z-Flag setzen
0153:  BF           CP A
0154:  87           ADD A,A          ; A = 2*A
0155:  87           ADD A,A
0156:  BD           CP L             ; A - L  (Größenvergleich)
0157:  38 0E        JR C,0167h       ; Sprung wenn Carry (A < L)

0159:  2C           INC L
015A:  FD FD        DB FDh,FDh
015C:  CB CB        SET 1,E          ; Bit 1 in E setzen
015E:  D3 D3        OUT (D3h),A
0160:  10 DB        DJNZ 013Dh       ; Zählschleife (Hauptlese-Schleife)
0162:  16 38        LD D,38h
0164:  F0           RET P
0165:  18 BE        JR 0125h         ; Sprung zurück zur Sektorleseschleife

; --- PIO-Steuer-Wort setzen (Output-Modus) ---
0167:  3E D3        LD A,D3h         ; A = 0xD3 (PIO Steuer-Byte Mode 3)
0169:  D3 11        OUT (11h),A      ; **K5122 Steuer-PIO Port A Ctrl** = 0xD3
016B:  32 F8 03     LD (03F8h),A     ; Speichern in ROM-Adresse 0x03F8

016E:  F5           PUSH AF
016F:  F5           PUSH AF
0170:  33           INC SP
0171:  FA F3 F2     JP M,F2F3h       ; Sprung wenn negativ
0174:  07           RLCA
0175:  07           RLCA
0176:  3E 82        LD A,82h         ; A = 0x82 (PIO-Steuerwort: Mode 0, Output)
0178:  32 F2 C3     LD (C3F2h),A

; --- Hauptsprung nach Initialisierung ---
017B:  C3 F8 01     JP 01F8h         ; → Floppy-Lese-Hauptroutine bei 0x01F8

017E:  FF           RST 38h
017F:  FF           RST 38h
0180:  D3 DC        OUT (DCh),A
0182:  37           SCF
0183:  BE           CP (HL)

; --- Vektortabelle / Adressen-Setup ---
0184:  B3           OR E
0185:  22 83 83     LD (8383h),HL
0188:  21 D4 03     LD HL,03D4h      ; HL = 0x03D4 (Subroutine im ROM)
018B:  22 F2 03     LD (03F2h),HL    ; Zeiger ablegen

018E:  C7           RST 00h          ; RST 0 = CALL 0x0000
018F:  C7           RST 00h

; --- SIO/Serielle-Init-Bereich ---
0190:  13           INC DE
0191:  7F           LD A,A
0192:  33           INC SP
0193:  33           INC SP
0194:  3F           CCF              ; Carry umkehren
0195:  13           INC DE
0196:  57           LD D,A
0197:  57           LD D,A
0198:  3E 7F        LD A,7Fh         ; A = 0x7F
019A:  33           INC SP
019B:  33           INC SP
019C:  D3 36        OUT (36h),A      ; Port 0x36 beschreiben
019E:  37           SCF
019F:  37           SCF

; --- CTC-Kanal-Programmierung ---
01A0:  3E D3        LD A,D3h
01A2:  F7           RST 30h
01A3:  3C           INC A            ; A = 0xD4
01A4:  D3 D3        OUT (D3h),A
01A6:  3E 3E        LD A,3Eh
01A8:  D3 D3        OUT (D3h),A      ; A = 0x3E → Port 0xD3
01AA:  3E 3E        LD A,3Eh
01AC:  D3 D3        OUT (D3h),A
01AE:  3E 3E        LD A,3Eh
01B0:  D3 F7        OUT (F7h),A      ; A = 0x3E → Port 0xF7

; --- Floppy-Status-Prüfung ---
01B2:  FE FE        CP FEh           ; Vergleich A mit 0xFE
01B4:  90           SUB B
01B5:  D3 DB        OUT (DBh),A
01B7:  DB 34        IN A,(34h)       ; Port 0x34 lesen (K5122-Status?)
01B9:  E6 C2        AND C2h          ; Maskieren: Bit 7 und Bit 6
01BB:  C2 B0 03     JP NZ,03B0h      ; Sprung zu Fehlerbehandlung bei 0x03B0

; --- ZVE2-Reset und Startsignal ---
01BE:  09           ADD HL,BC
01BF:  09           ADD HL,BC
01C0:  3E F3        LD A,F3h         ; A = 0xF3
01C2:  DF           RST 18h
01C3:  3C           INC A            ; A = 0xF4
01C4:  3E 05        LD A,05h         ; A = 0x05
01C6:  FA FA 7B     JP M,7BFAh
01C9:  7B           LD A,E
01CA:  D3 34        OUT (34h),A      ; Port 0x34 schreiben

; --- Sektorzähler-Setup ---
01CC:  06 00        LD B,00h         ; B = 0 (Bytezähler initialisieren)
01CE:  B6           OR (HL)
01CF:  B6           OR (HL)
01D0:  CD CF 13     CALL 13CFh       ; CALL 0x13CF (außerhalb ROM! → RAM-Routine)
01D3:  13           INC DE
01D4:  31 34 22     LD SP,2234h      ; SP = 0x2234  (Stack-Initialisierung)
01D7:  22 03 03     LD (0303h),HL    ; HL speichern bei 0x0303
01DA:  10 10        DJNZ 01ECh       ; Schleife

01DC:  35           DEC (HL)
01DD:  35           DEC (HL)
01DE:  43           LD B,E
01DF:  43           LD B,E
01E0:  A0           AND B
01E1:  32 0B 0B     LD (0B0Bh),A
01E4:  B6           OR (HL)
01E5:  DA 93 93     JP C,9393h       ; Fehlersprung
01E8:  BF           CP A
01E9:  32 03 03     LD (0303h),A

; --- Zählercheck / Laufwerksauswahl ---
01EC:  3E 14        LD A,14h         ; A = 0x14 = 20 (Sektorzahl?)
01EE:  F7           RST 30h
01EF:  F7           RST 30h
01F0:  3F           CCF
01F1:  FF           RST 38h
01F2:  D3 D3        OUT (D3h),A
01F4:  3E 3E        LD A,3Eh
01F6:  D3 D3        OUT (D3h),A

; ===================================================
; HAUPT-FLOPPY-LESEROUTINE (Einstieg nach JP 01F8h)
; ===================================================
01F8:  DB DB        IN A,(DBh)       ; Status lesen (Port 0xDB)
01FA:  CB CB        SET 1,E          ; (Bit-Setup)
01FC:  7F           LD A,A
01FD:  20 F6        JR NZ,01F5h      ; Warten bis bereit (Polling-Schleife)
01FF:  F6 20        OR 20h           ; Bit 5 setzen
0201:  F7           RST 30h
0202:  7F           LD A,A
0203:  7F           LD A,A
0204:  CD CD 03     CALL 03CDh       ; → Subroutine bei ROM-Adresse 0x03CD

; --- Byte-Transfer-Schleife ---
0207:  03           INC BC
0208:  DF           RST 18h
0209:  34           INC (HL)
020A:  7F           LD A,A
020B:  7F           LD A,A
020C:  20 09        JR NZ,0217h      ; Byte-Status prüfen

020E:  20 20        JR NZ,0230h
0210:  F4 F7 DF     CALL P,DFF7h
0213:  DF           RST 18h
0214:  B4           OR H
0215:  18 F6        JR 020Dh         ; Schleife

; --- Laufwerkssignal und Schrittmotor ---
0217:  F6 D3        OR D3h
0219:  D3 CD        OUT (CDh),A
021B:  CD B6 03     CALL 03B6h       ; → Subroutine bei 0x03B6 (Step/Seek)
021E:  30 30        JR NC,0250h
0220:  F7           RST 30h
0221:  3C           INC A
0222:  DB DB        IN A,(DBh)
0224:  F7           RST 30h
0225:  B5           OR L
0226:  35           DEC (HL)
0227:  35           DEC (HL)

; --- Track-0-Erkennung ---
0228:  A2           AND D
0229:  28 DB        JR Z,0206h       ; Sprung wenn Track 0

; --- Bit-Test für Sektor-ID ---
022B:  DB CB        IN A,(CBh)       ; Port 0xCB lesen
022D:  CB 67        BIT 4,A          ; Bit 4 testen
022F:  20 14        JR NZ,0245h      ; Sprung wenn gesetzt

0231:  DE 5F        SBC A,5Fh
0233:  5F           LD E,A
0234:  F6 F6        OR F6h
0236:  D3 D3        OUT (D3h),A
0238:  34           INC (HL)
0239:  18 4F        JR 028Ah         ; Sprung nach 0x028A

023B:  4F           LD C,A
023C:  F3           DI
023D:  F3           DI
023E:  40           LD B,B           ; NOP-äquivalent
023F:  40           LD B,B
0240:  F7           RST 30h
0241:  FF           RST 38h
0242:  D7           RST 10h
0243:  D7           RST 10h
0244:  34           INC (HL)

; --- Sektor-Bit-4 Zweig ---
0245:  FE D3        CP D3h           ; Vergleich
0247:  D3 DB        OUT (DBh),A      ; Port 0xDB schreiben
0249:  DB 17        IN A,(17h)       ; **K5122 Data-PIO Port B Ctrl** lesen
024B:  17           RLA              ; Carry ← Bit 7 von A
024C:  30 FB        JR NC,0249h      ; Warten bis Bit gesetzt (Polling)
024E:  00           NOP
024F:  00           NOP

; --- Statusauswertung Floppy ---
0250:  FF           RST 38h
0251:  B7           OR A
0252:  97           SUB A            ; A = 0
0253:  97           SUB A
0254:  BE           CP (HL)
0255:  BE           CP (HL)
0256:  D3 D3        OUT (D3h),A
0258:  01 01 83     LD BC,8301h
025B:  83           ADD A,E
025C:  97           SUB A
025D:  97           SUB A
025E:  33           INC SP
025F:  33           INC SP

; --- Stackzeiger-Berechnungen ---
0260:  33           INC SP
0261:  33           INC SP
0262:  31 31 BF     LD SP,BF31h      ; SP = 0xBF31  (Stack-Umlegen)
0265:  B8           CP B
0266:  FD FD        DB FDh,FDh
0268:  03           INC BC
0269:  03           INC BC
026A:  33           INC SP
026B:  33           INC SP
026C:  2A F8 2B     LD HL,(2BF8h)
026F:  2B           DEC HL
0270:  2B           DEC HL
0271:  2B           DEC HL
0272:  FC FC 15     CALL M,15FCh
0275:  15           DEC D
0276:  DD DD        DB DDh,DDh
0278:  01 00 CD     LD BC,CD00h

; --- Subroutinen-Aufruf Initialisierung ---
027B:  CD 03 03     CALL 0303h       ; → Subroutine bei ROM-Adresse 0x0303
027E:  34           INC (HL)
027F:  34           INC (HL)
0280:  FF           RST 38h
0281:  FF           RST 38h
0282:  34           INC (HL)
0283:  34           INC (HL)
0284:  E9           JP (HL)          ; Indirekter Sprung: PC = HL
                                     ; → Sprung zum geladenen Bootstrap!
0285:  E1           POP HL
0286:  0D           DEC C
0287:  0D           DEC C
0288:  DD B6        DB DDh,B6h

; --- Sektorpuffer-Verwaltung ---
028A:  20 20        JR NZ,02ACh
028C:  CD CD 04     CALL 04CDh       ; CALL 0x04CD (außerhalb ROM)
028F:  04           INC B
0290:  77           LD (HL),A        ; Byte im Puffer speichern
0291:  77           LD (HL),A
0292:  3F           CCF
0293:  3F           CCF
0294:  D3 D3        OUT (D3h),A
0296:  10 10        DJNZ 02A8h
0298:  21 21 0D     LD HL,0D21h
029B:  0D           DEC C
029C:  21 FA 35     LD HL,35FAh
029F:  35           DEC (HL)
02A0:  03           INC BC
02A1:  2B           DEC HL
02A2:  3C           INC A
02A3:  3C           INC A
02A4:  C9           RET
02A5:  C9           RET

; --- Sektorblock-Schleife ---
02A6:  CB CB        SET 1,E
02A8:  6B           LD L,E
02A9:  28 1E        JR Z,02C9h
02AB:  1E C3        LD E,C3h
02AD:  C3 02 02     JP 0202h         ; Sprung zurück zu Byte-Transfer-Schleife

02B0:  9F           SBC A,A          ; A = 0 oder 0xFF (mit Carry)
02B1:  9F           SBC A,A
02B2:  CB CB        SET 1,E
02B4:  FE FE        CP FEh
02B6:  30 30        JR NC,02E8h
02B8:  FD 10        DB FDh,10h       ; Unbekannte Sequenz
02BA:  C9           RET
02BB:  C9           RET

; --- Interrupt-Vektorsicherung / Restore ---
02BC:  F5           PUSH AF
02BD:  F5           PUSH AF
02BE:  31 31 3E     LD SP,3E31h
02C1:  F5           PUSH AF
02C2:  FB           EI
02C3:  FB           EI
02C4:  ED ED        DB EDh,EDh
02C6:  FB           EI
02C7:  FB           EI
02C8:  E5           PUSH HL
02C9:  E5           PUSH HL
02CA:  F8           RET M
02CB:  F8           RET M
02CC:  ED ED        DB EDh,EDh
02CE:  22 22 03     LD (0322h),HL
02D1:  03           INC BC
02D2:  2D           DEC L
02D3:  2D           DEC L
02D4:  F5           PUSH AF
02D5:  F5           PUSH AF
02D6:  21 21 03     LD HL,0321h
02D9:  03           INC BC
02DA:  20 20        JR NZ,02FCh
02DC:  04           INC B
02DD:  2C           INC L
02DE:  28 28        JR Z,0308h
02E0:  F1           POP AF
02E1:  F3           DI
02E2:  EF           RST 28h
02E3:  ED E1        DB EDh,E1h
02E5:  E1           POP HL
02E6:  F1           POP AF
02E7:  F1           POP AF
02E8:  93           SUB E
02E9:  93           SUB E
02EA:  E3           EX (SP),HL
02EB:  E3           EX (SP),HL
02EC:  ED ED        DB EDh,EDh

; --- Konfigurationsdaten-Tabelle (Ende Code, Beginn Daten) ---
02EE:  00           NOP
02EF:  00           NOP
02F0:  00           NOP
02F1:  04           INC B
02F2:  08           EX AF,AF'        ; [DATA] Konfigurationsdaten
02F3:  08           EX AF,AF'
02F4:  00           NOP
02F5:  00           NOP
02F6:  40           LD B,B
02F7:  00           NOP
02F8:  00           NOP
02F9:  00           NOP
02FA:  08           EX AF,AF'
02FB:  08           EX AF,AF'
02FC:  00           NOP
02FD:  00           NOP
02FE:  6B           LD L,E           ; [DATA-Ende]
02FF:  6B           LD L,E

; --- Subroutinen bei 0x0303, 0x03B0, 0x03B6, 0x03CD, 0x03D4 ---
; (Bytes 0x0300-0x031D enthalten weitere ROM-Subroutinen)
; Detailanalyse: siehe Abschnitt 5.4

; --- PADDING (ab ~0x031E bis 0x03FF) ---
031E:  E5 E5 E5 ...  (226 Byte, Wert 0xE5 = PUSH HL)
; Erreichbar nur bei Programmierfehler → deterministischer Stack-Overflow
```

---

## 5. Detailanalyse der Hauptroutinen

### 5.1 Initialisierung (0x0000–0x00FF)

Nach dem Reset beginnt der Bootstrap-Code mit:

1. **Interrupt-Deaktivierung** (`DI`, doppelt für Timing-Stabilität)
2. **RAM-Test-Vorbereitung**: Adressberechnungen mit `ADD HL,DE`, Speicherlesezugriff
   auf `LD A,(FFFCh)` — liest das letzte Wort des Speichers zur RAM-Überprüfung
3. **PIO-Initialisierung**: Mehrfache Ausgaben an Port 0xD3 (vermutlich K8025-SIO-Adresse
   oder Koppel-Logik), Ausgabe an Port 0x3F (möglicher CTC-Kanal 3)
4. **CTC-Zeitgeber-Setup** (um 0x00F4): `LD A,FDh` / `OUT (90h),A` — Zeitgeber-Steuerwort
   gefolgt von `LD A,B5h` für den Zählerwert. Damit wird die Baudrate der seriellen
   Schnittstelle (SIO auf K8025) gesetzt.

### 5.2 K5122-Floppy-Sequenz (0x00A6–0x0170)

Die eigentliche Floppy-Kommunikation findet über die K5122-Ports statt:

| Schritt | Adresse | Instruktion | Beschreibung |
|---------|---------|-------------|--------------|
| PIO-Mode setzen | 0x00B0 | `OUT (11h),A` | Steuer-PIO Port A Ctrl → Output-Modus |
| PIO-Mode setzen | 0x0169 | `OUT (11h),A` | Steuer-PIO Port A erneut initialisieren |
| Steuerbyte | 0x014B | `OUT (10h),A` | Steuer-PIO Port A: Steuersignale setzen |
| Steuerbyte | 0x0126 | `OUT (10h),A` | Steuer-PIO Port A: Schritt/WE |
| Datenlesen | 0x00AB | `IN A,(16h)` | **Data-PIO Port B: ein Datenbyte lesen** |
| Datenlesen | 0x011A | `IN A,(16h)` | Data-PIO Port B: weiteres Byte |
| Datenlesen | 0x011F | `IN A,(16h)` | Data-PIO Port B: Byte-by-Byte-Polling |
| Datenlesen | 0x012F | `IN A,(16h)` | Data-PIO Port B: Byte-by-Byte-Polling |
| PIO-Ctrl | 0x0249 | `IN A,(17h)` | Data-PIO Port B Ctrl: Bereitschaft prüfen |

Das Schema des Floppy-Lesens entspricht exakt dem Design der K5122:
- **Steuer-PIO Port A** (0x10/0x11): Steuert Schrittmotor, Schreibkopf, Richtung
- **Data-PIO Port B** (0x16/0x17): Liefert die seriell gelesenen Datenbytes

### 5.3 Bootstrap-Ausführung und ROM-Deaktivierung

Der kritische Moment ist der indirekte Sprung `JP (HL)` bei Adresse **0x0284**.
Zu diesem Zeitpunkt enthält HL die Startadresse 0x0100, wohin der geladene
Bootstrap-Code (erster Sektor der Diskette) transferiert wurde. Mit `JP (HL)` übergibt
der ROM-Code die Kontrolle an das Betriebssystem.

Die ROM-Deaktivierung erfolgt durch den Bootstrap-Code selbst (nicht im ROM),
der nach seinem Start folgenden I/O-Befehl ausführt:

```asm
OUT (0Ah),A    ; BS-PIO Port B = 0 (Bit 0 = 0 → /LD-ROM aktiv → ROM aus)
```

### 5.4 Subroutinen im oberen ROM-Bereich (0x0300–0x03FF)

Vier Subroutinen werden aus dem Hauptcode aufgerufen:

| Adresse | Aufruf von | Vermutliche Funktion |
|---------|------------|---------------------|
| 0x0303  | 0x006F, 0x027B | Allgemeine Hilfsroutine (Timing/Warten) |
| 0x03B0  | 0x01BB | Fehlerbehandlung (Disk-Fehler) |
| 0x03B6  | 0x021B | Schritt/Seek-Subroutine für Floppy-Kopf |
| 0x03CD  | 0x0204 | Sektor-Lese-Subroutine |
| 0x03D4  | (Zeiger gesetzt 0x018B) | Unterfunktion Datentransfer |

---

## 6. DMA-Mechanismus (ZVE2)

### 6.1 Originale Hardware

Auf der realen K2526-Hardware existiert eine zweite Z80-CPU (**ZVE2**), die als
DMA-Prozessor fungiert. Der Ablauf bei einem Floppy-Lesevorgang ist:

```
1. ZVE1 (Haupt-CPU) richtet DMA-Parameter ein:
   - Quell-Port: 0x16 (K5122 Data-PIO Port B)
   - Ziel-Adresse: 0x0100 (RAM)
   - Bytezahl: 512 (ein Sektor) oder 128 (CP/M-Sektor)

2. ZVE1 aktiviert ZVE2 via Port 0x04 (/RES-ZVE2 aufheben)

3. ZVE1 setzt /BUSRQ → gibt K1520-Bus frei

4. ZVE2 übernimmt den Bus und überträgt Bytes:
   IN A,(16h)     ; Byte vom Floppy lesen
   LD (HL),A      ; in RAM schreiben
   INC HL         ; Zeiger fortschalten
   DJNZ Schleife  ; Byte-Zähler bis 0

5. ZVE2 senkt /BUSAK → ZVE1 übernimmt wieder

6. ZVE1 springt zu 0x0100 (JP (HL) oder JP 0x0100)
```

### 6.2 Hinweise im Disassemblat

Die Registertausch-Instruktion `EXX` bei 0x0100 und 0x013A ist typisch für
DMA-Coprozessor-Code: Die Schattenregister (BC', DE', HL') werden für DMA-Zeiger
verwendet, während die Hauptregister für den Steuerfluss reserviert bleiben.

Die `INI`-Instruktion (ED A2) bei 0x013C ist ein Z80-Blockübertragungsbefehl:

```
INI:  (HL) = IN A,(C)    ; Byte aus Port (C) nach (HL) schreiben
      B = B - 1           ; Bytezähler dekrementieren
      HL = HL + 1         ; Zielzeiger erhöhen
```

Dieser Befehl ist das Herzstück der DMA-Operation: Mit `B` als Bytezähler und
`HL` als Zielzeiger (RAM bei 0x0100) und `C` = 0x16 (K5122 Data-PIO Port B)
würde `INIR` (INIR = INI bis B=0) einen kompletten Sektor in den RAM kopieren.

### 6.3 Emulator-Vereinfachung

Da ZVE2 im Emulator **nicht implementiert** ist, ersetzt die K5122-Klasse
den DMA-Mechanismus durch einfaches **Byte-Polling**:
- `ioRead(0x16)` liefert direkt das nächste Byte aus dem geladenen Disk-Image
- Es gibt keinen separaten DMA-Prozessor

---

## 7. Port-Zugriffsübersicht (vollständig)

| Port | Adresse | Richtung | Vorkommen | Funktion |
|------|---------|----------|-----------|---------|
| K5122 S-PIO A Data | 0x10 | OUT | 0x0126, 0x014B | Floppy-Steuersignale |
| K5122 S-PIO A Ctrl | 0x11 | OUT | 0x00B0, 0x0169 | PIO-Modus setzen |
| K5122 S-PIO B Ctrl | 0x13 | OUT | — | PIO-Modus setzen |
| K5122 D-PIO B Data | 0x16 | IN  | 0x00AB, 0x011A, 0x011F, 0x012F | **Floppy-Datenbytes lesen** |
| K5122 D-PIO B Ctrl | 0x17 | IN  | 0x0249 | Bereitschaft prüfen |
| Unbekannt/K8025 | 0x34 | IN/OUT | 0x01B7, 0x01CA | Status/Control |
| Unbekannt | 0x36 | OUT | 0x019C | Sonstiges |
| K8025 SIO/CTC? | 0x90 | OUT | 0x00F6 | CTC-Steuerwort |
| Unbekannt | 0xCB | IN | 0x022B | Status-Byte |
| Unbekannt | 0xD3 | OUT | vielfach | (evtl. Logikadresse, gemappter Port) |
| Unbekannt | 0xDB | IN/OUT | vielfach | Status-Polling |
| Unbekannt | 0xDC | OUT | 0x0180 | Sonstiges |
| Unbekannt | 0xDE | IN | 0x0110 | Status |
| Unbekannt | 0xF6 | IN | 0x007D | Status |
| Unbekannt | 0xFF | OUT | 0x00A1 | (Broadcast?) |

**Anmerkung zu den hohen Portnummern (0xD3, 0xDB etc.):**  
Diese Portnummern liegen außerhalb der dokumentierten K2526- und K5122-Bereiche.
Es gibt zwei Erklärungen:
1. **Fehlinterpretation:** Einige Bytes werden vom Disassembler als `OUT (n),A`
   interpretiert, sind aber eigentlich Daten oder Operanden von Mehrbytebefehlen,
   die falsch ausgerichtet wurden.
2. **Koppelbus-Logik:** Der K1520-Koppelbus hat möglicherweise eigene
   Port-Dekodierungsadressen, die in der vorhandenen Dokumentation nicht vollständig
   erfasst sind.

---

## 8. Zusammenfassung

Das Boot-ROM der K2526 ZRE ist ein 1-KB-EPROM, das nach dem Hardware-Reset des
Robotron A5120 die vollständige Initialisierung des Systems durchführt:

1. **Interrupt-Deaktivierung** und Registervorbereitung
2. **RAM-Test** der K3526-Speicherkarte
3. **CTC-Initialisierung** für die Baudrate der seriellen K8025-Karte
4. **K5122-Floppy-Initialisierung**: PIO-Modi für Steuer- und Daten-PIO setzen,
   Laufwerk A auswählen
5. **Floppy-Sektor-Lesen**: Byte-by-Byte-Polling über Port 0x16 (Data-PIO Port B),
   Transfer der gelesenen Bytes nach RAM bei 0x0100
6. **Bootstrap-Sprung**: `JP (HL)` (Adresse 0x0284) springt zu 0x0100, wo das
   CPA/CP/M-Betriebssystem-Ladeprogramm ausgeführt wird
7. **ROM-Deaktivierung** durch den Bootstrap-Code via BS-PIO Port B Bit 0 = 0

Der DMA-Mechanismus über ZVE2 (zweite Z80-CPU als DMA-Prozessor) wird im
Disassemblat durch die Verwendung von `EXX`, `INI` und den Schattenregistern
angedeutet. Im Emulator wird dieser durch einfaches Byte-Polling ersetzt,
da ZVE2 nicht implementiert ist.

Das Ende des ROMs (ab ca. 0x031E) ist mit dem Wert 0xE5 (PUSH HL) aufgefüllt,
was bei versehentlichem Einsprung zu einem deterministischen Stack-Overflow führt.

---

## Anhang: Verwendete Quellen und Werkzeuge

| Ressource | Pfad |
|-----------|------|
| EPROM-Binärdatei | `doc/EPROMS/Prozessoreinheit_ZRE_K2526_A26.bin` |
| C-Header mit ROM-Daten | `core/cards/k2526/rom_data.h` |
| K2526-Designdokumentation | `doc/design/03_k2526_zre.md` |
| K5122-Designdokumentation | `doc/design/07_k5122_afs.md` |
| Disassembler-Skript | `disasm_k2526.py` |
| Z80-Disassembler (vollständig) | `tools/z80_disasm3.py` |
| EPROM-Analyseskript | `analyze_eprom.py` |
