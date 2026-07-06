; =============================================================================
; ZRE EPROM K2526 – Boot-ROM, 1 KB (0x0000–0x03FF)
; CPU: Z80 (ZVE1 = primäre CPU auf K2526), 2,45 MHz
; =============================================================================
; Speicher-Layout:
;   Phase 1 (ROM aktiv):    0x0000–0x03FF = ROM, 0x0400–0xFFFF = K3526-RAM
;   Phase 2 (nach LDIR):    0x00BA–0x03FF = ROM-Inhalt in RAM gespiegelt
;   Phase 3 (ROM disabled): 0x0000–0xFFFF = vollständiges K3526-RAM
;
; Interrupt-Modus 2 (IM 2, I=0):  ISR-Adresse = memory[vector]
;   Vektor 0xB8 → RAM[0xB8,0xB9] = {0x7A,0x00} → ISR 0x007A (BS-PIO Port A)
;   Vektor 0xBA → RAM[0xBA,0xBB] = {0xC7,0x01} → ISR 0x01C7 (K5122 Indexpuls-ISR)
;   Vektor 0xE2 → RAM[0xE2,0xE3]               → (nicht genutzt)
;
; RAM-Variablen des Boot-ROMs (werden zur Laufzeit beschrieben):
;   0x03F0  Ladeadresse für Sektor-Daten              (init: 0x0400)
;   0x03F3  Erwarteter Zylinder für IDAM-Vergleich    (init: 0x00)
;   0x03F4  Erwarteter Kopf (Head)                    (init: 0x00)
;   0x03F5  Erwartete Sektor-ID (1-basiert)           (init: 0x01)
;   0x03F6  Erwarteter Size-Code                      (init: 0x00)
;   0x03F7  Indexpuls-Zähler (ISR: 4→0)               (init: 4)
;   0x03F8  Abschluss-Flag: 0=läuft, 1=Timeout, 3=OK  (init: 0)
;   0x03FA  Seek-Retry-Counter                        (init: 5)
;   0x03FC  Aktueller Ctrl-Wert für Port 0x10         (init: 0xEE)
;   0x03FD  FM/MFM-Pfad (MK, Port 0x10 bit1): 0x87=FM, 0x85=MFM; bit7=Step-Unterdrueckung
; =============================================================================

; ===== RESET ENTRY POINT =====
  RESET_ENTRY:           0000  NOP                            
                        ; BC=0x0800=2048: Byte-Zähler für LDI-Schleife (löscht 2 KB RAM)
                         0001  LD BC,0800h
                        ; DE=HL=0x0000: Quelle und Ziel zeigen beide auf Adresse 0
                         0004  LD D,C
                         0005  LD E,C
                         0006  LD H,C
                         0007  LD L,C
                        ; LDI-Trick: ROM[0x0000]=NOP=0x00 wird nach RAM[DE++] geschrieben.
                        ; DEC HL haelt HL=0x0000 (Quelle bleibt immer bei NOP).
                        ; JP PE: PV-Flag von LDI zeigt BC!=0 (noch Bytes uebrig).
  CLEAR_LOOP:            0008  LDI
                         000A  DEC HL
                         000B  JP PE,0008h                    
                        ; ===== HARDWARE-GRUNDINITIALISIERUNG =====
  INIT_PORTS:            000E  XOR A
                        ; Port 0x02 = Bank-Select: 0 → Bank 0 aktiv
                         000F  OUT (02h),A
                        ; SP = 0x07E0: Stack unterhalb des 2 KB RAM-Bereichs
                         0011  LD SP,07E0h
                        ; IM 2: ISR-Adresse = memory[(I<<8) | device_vector]
                         0014  IM 2
                        ; I = 0x00 → Vektor-Tabelle beginnt bei RAM[0x0000]
                         0016  LD A,00h
                         0018  LD I,A                         
                        ; ===== BS-PIO (K2526-interne PIO, Ports 0x08/09/0A/0B) INIT =====
                        ; BS-PIO steuert RAM-Banken, ROM-Enable und Drive-Select.
                        ; Port 0x08=Port-A-Daten, 0x09=Port-A-Ctrl
                        ; Port 0x0A=Port-B-Daten, 0x0B=Port-B-Ctrl
  INIT_BS_PIO:           001A  LD A,7Fh
                        ; Port A ctrl: 0x7F = Modus 1 (Input)
                         001C  OUT (09h),A
                        ; Port B ctrl: 0x7F = Modus 1 (Input)
                         001E  OUT (0Bh),A
                         0020  LD A,FFh
                        ; Port A Daten = 0xFF (alle Leitungen high)
                         0022  OUT (08h),A
                        ; Port B Daten = 0xFF (alle Leitungen high)
                         0024  OUT (0Ah),A
                         0026  LD A,B8h
                        ; Port A: Interrupt-Vektor = 0xB8 → ISR-Adresse aus RAM[0xB8,0xB9]
                         0028  OUT (09h),A
                         002A  LD A,FFh
                        ; Port A: Modus 3 (Bit-Control: einzelne Bits konfigurierbar)
                         002C  OUT (09h),A
                         002E  LD A,7Fh
                        ; Modus-3 I/O-Maske: Bit7=Output (ROM-Disable/Drive-Ctrl), Bits6..0=Input
                         0030  OUT (09h),A                    
                        ; ===== RAM-TEST / BANK-WALK (62 Durchlaeufe, je 1 KB) =====
                        ; Testet 62 x 1KB-Bloecke (IX=0x0800, 0x0C00, ..., 0xFC00).
                        ; Waehrend EI/DI-Fenster: falls /ASTB feuert → ISR_BS_PIO_A (0x007A).
  RAM_TEST:              0032  LD IX,0800h
                        ; IX (Startadresse erste RAM-Bank) in RAM[0x0462] sichern
                         0036  LD (0462h),IX
                        ; HL → Config-Flag bei 0x044E (nach RAM-Clear = 0x00)
                         003A  LD HL,044Eh
                        ; DE = 0x0400 = IX-Inkrement (1 KB pro Bankschritt)
                         003D  LD DE,0400h
                        ; B = 62 Iterationen (0x3E)
                         0040  LD B,3Eh
                        ; C = aktueller RAM-Inhalt (wird nach ISR-Aufruf wiederhergestellt)
  RAM_TEST_LOOP:         0042  LD C,(IX+0)
                        ; BS-PIO Port A Int-Ctrl: 0xD7 = INTENA=1, OR-Modus, H-Level, Maske folgt
                         0045  LD A,D7h
                         0047  OUT (09h),A
                        ; Interrupt-Maske = 0x9F
                         0049  LD A,9Fh
                         004B  OUT (09h),A
                        ; EI: Interrupt-Fenster oeffnen
                         004D  EI
                        ; Schreibe 0xFF → RAM[IX+0]: RAM-Test-Markierung; ISR prueft ob Schreiben geklappt
                         004E  LD (IX+0),FFh
                         0052  NOP
                        ; DI: Interrupt-Fenster schliessen
                         0053  DI
                        ; BS-PIO Port A: INTENA=0 (kein weiterer Interrupt von Port A)
                         0054  LD A,47h
                         0056  OUT (09h),A
                        ; IX += 0x0400 → naechster 1 KB-Block
                         0058  ADD IX,DE
                         005A  DJNZ 0042h                     
                        ; ===== DISK-PARAMETER-TABELLE KOPIEREN (ROM → RAM) =====
                        ; Quelle 0x046F liegt ausserhalb des 1 KB ROM (endet bei 0x03FF),
                        ; daher wird tatsaechlich RAM[0x046F..] gelesen (= 0x00 nach Clear).
  COPY_DISK_TBL:         005C  LD HL,046Fh
                         005F  LD DE,006Fh
                        ; 36 Byte rueckwaerts kopieren (LDDR)
                         0062  LD BC,0024h
                         0065  LDDR
                        ; ===== BOOT-FLAG PRUEFEN =====
                        ; RAM[0x044E] Bit0: 0=erster Boot (ROM aktiv), 1=ROM bereits deaktiviert
  CHECK_BOOT_FLAG:       0067  LD HL,044Eh
                        ; Bit0 testen
                         006A  BIT 0,(HL)
                        ; Z (Bit0=0): erster Boot → springe zu ROM_COPY (0x00BC)
                         006C  JR Z,00BCh
                        ; Bit0=1: ROM-Inhalt liegt bereits in RAM → direkt zum Hauptprogramm
  SKIP_ROM_COPY:         006E  LD HL,0800h
                        ; [0x004C] = 0x0800 (Ladeadresse fuer naechste Stufe)
                         0071  LD (004Ch),HL
                        ; HL = gesicherter IX-Wert (erste gueltige RAM-Bank aus RAM-Test)
                         0074  LD HL,(0462h)
                        ; L = 0x9D: Offset innerhalb der Bank → Sprung zu HL|0x9D
                         0077  LD L,9Dh
                         0079  JP (HL)                        
                        ; ===== ISR: BS-PIO PORT A (Vektor 0xB8 → ISR-Adresse RAM[0xB8,0xB9]={0x7A,0x00}=0x007A) =====
                        ; Aufgerufen wenn /ASTB waehrend RAM-Test feuert.
                        ; Prueft ob RAM-Block korrekt beschrieben ist und protokolliert
                        ; erste/letzte gueltige RAM-Bank in RAM[0x0460]/RAM[0x0464].
  ISR_BS_PIO_A:          007A  LD A,47h
                        ; BS-PIO Port A: INTENA=0 (kein weiterer Interrupt waehrend ISR)
                         007C  OUT (09h),A
                         007E  LD A,FFh
                        ; CP (IX+0): ist RAM-Block korrekt mit 0xFF beschrieben?
                         0080  CP (IX+0)
                        ; NZ: RAM nicht beschreibbar → Fehler-Pfad bei 0x00A7
                         0083  JR NZ,00A7h
                        ; Originalwert (C) wiederherstellen
                         0085  LD (IX+0),C
                        ; HL zeigt auf Config-Flag 0x044E; Bit0='erste Bank bereits gefunden'
                         0088  BIT 0,(HL)
                        ; Bit0 gesetzt → nicht erste gueltige Bank → bei 0x0092
                         008A  JR NZ,0092h
                        ; Erste gueltige Bank: speichere IX in RAM[0x0460]
                         008C  LD (0460h),IX
                         0090  RETI
                        ; Nicht erste Bank: speichere IX als letzte valide Bank in RAM[0x0464]
                         0092  LD (0464h),IX
                        ; Bit6: 'bevorzugte Boot-Bank bereits gefunden'?
                         0096  BIT 6,(HL)
                         0098  JR NZ,00A5h
                        ; Bit6 noch nicht gesetzt: markiere diese Bank als Boot-Bank
                         009A  SET 6,(HL)
                        ; Speichere IX in RAM[0x046C] (Boot-Bank-Adresse)
                         009C  LD (046Ch),IX
                         00A0  LD A,05h
                        ; [0x046C] = 5 (Bank-Qualitaets-Code / Retry-Zaehler)
                         00A2  LD (046Ch),A
                         00A5  RETI
                        ; Fehler-Pfad: RAM-Block nicht beschreibbar
                         00A7  BIT 0,(HL)
                         00A9  JR NZ,00B1h
                        ; Erster Fehler: Bit0 setzen, IX als erste Fehler-Bank sichern
                         00AB  SET 0,(HL)
                         00AD  LD (0462h),IX
                        ; Immer: IX als letzte gesehene Bank sichern
                         00B1  LD (0464h),IX
                         00B5  RETI                           
                        ; ===== ROM-DATEN: ISR-VEKTOR-EINTRAEGE (kein ausfuehrbarer Code!) =====
                        ; Diese Bytes werden durch LDIR (bei 0x00C4) 1:1 in RAM kopiert
                        ; und bilden Eintraege in der IM-2-Vektor-Tabelle:
                        ;   RAM[0xB8,0xB9] = {0x7A, 0x00} → Vektor 0xB8 → ISR 0x007A (BS-PIO)
                        ;   RAM[0xBA,0xBB] = {0xC7, 0x01} → Vektor 0xBA → ISR 0x01C7 (K5122!)
                        ; ACHTUNG: Disassembler-Fehlausrichtung! ROM[0xBB]=0x01 (ISR-High-Byte)
                        ; wird als Beginn von 'LD BC,nn' fehlinterpretiert. ROM[0xBC]=0x21 ist
                        ; tatsaechlich der 'LD HL'-Opcode des ROM_COPY-Einstiegs (JR Z,00BCh).
  ROM_DATA_BA_BB:        00B7  NOP
                        ; [DATA] ROM[0xB8]=0x7A → ISR-Adresse-Low fuer Vektor 0xB8
                         00B8  LD A,D
                        ; [DATA] ROM[0xB9]=0x00 → ISR-Adresse-High → ISR=0x007A (BS-PIO)
                         00B9  NOP
                        ; [DATA] ROM[0xBA]=0xC7 → ISR-Adresse-Low fuer Vektor 0xBA
                         00BA  RST 00h
                        ; [DATA+CODE] ROM[0xBB]=0x01 (ISR-High) | ROM[0xBC]=0x21 (LD HL Opcode)
                        ; Disassembler liest 0x01,0x21,0xBA faelschlich als 'LD BC,0xBA21'
                        ; Echter ROM_COPY-Einstieg: 0xBC=LD HL, 0xBD=0xBA, 0xBE=0x00 → HL=0x00BA
                         00BB  LD BC,BA21h
                        ; [LD HL operand high = 0x00]; ROM_COPY-Code beginnt hier korrekt geparst
                         00BE  NOP
                        ; ===== ROM → RAM KOPIEREN (erster Boot, Einstieg via JR Z von 0x006C) =====
                        ; Kopiert 838 Byte ROM[0x00BA..0x03FF] → RAM[0x00BA..0x03FF]:
                        ;   Enthaelt ISR-Vektor-Daten (0xBA/0xBB) und alle Laufzeit-Routinen.
                        ; DE = HL = 0x00BA: Quelle und Ziel identisch (ROM und RAM gleiche Adresse)
                         00BF  LD D,H
                         00C0  LD E,L
                        ; BC = 0x0346 = 838 Byte (0x00BA bis 0x03FF inkl.)
                         00C1  LD BC,0346h
                        ; Nach LDIR: RAM[0xBA]=0xC7, RAM[0xBB]=0x01 → ISR-Vektor 0xBA fertig
                         00C4  LDIR                           
                        ; ===== POST-COPY HARDWARE-INITIALISIERUNG =====
                        ; ROM-Kopie abgeschlossen. Alle Laufzeit-Routinen jetzt in RAM.
                        ; Naechster Schritt: ROM deaktivieren, K5122 (Floppy-Controller) konfigurieren.
  POST_COPY_INIT:        00C6  LD A,FFh
                        ; BS-PIO Port B ctrl (0x0B): 0xFF = Modus 3 (Bit-Control)
                         00C8  OUT (0Bh),A
                        ; K5122 ctrl PIO Port B ctrl (Port 0x13): 0xFF = Modus 3
                         00CA  OUT (13h),A
                         00CC  LD A,E2h
                        ; BS-PIO Port B: Interrupt-Vektor = 0xE2 (nicht weiter genutzt)
                         00CE  OUT (0Bh),A
                        ; Lese BS-PIO Port B Daten (Hardware-Konfigurations-Bits)
                         00D0  IN A,(0Ah)
                        ; AND 0xF6 = ~0x09: loescht Bit0 (ROM-Enable) und Bit3
                         00D2  AND F6h
                        ; Schreibe zurueck: Bit0=0 → ROM DEAKTIVIERT (laeuft jetzt aus RAM)
                         00D4  OUT (0Ah),A
                        ; EI: Interrupts freigeben (ROM aus, ISR-Vektoren in RAM bereit)
  ENABLE_INTS_INIT:      00D6  EI
                         00D7  LD A,F3h
                        ; K5122 ctrl PIO Port B ctrl (Port 0x13): 0xF3 = E/A-Richtungsmaske (nach Modus 3): b2=/HF, b3=PRE = Ausgang
                         00D9  OUT (13h),A
                         00DB  LD A,7Fh
                        ; K5122 ctrl PIO Port B Daten (Port 0x12): 0x7F
                         00DD  OUT (12h),A
                        ; K5122 ctrl PIO Port A ctrl (Port 0x11): 0x7F = Modus 1 (Input)
                         00DF  OUT (11h),A
                         00E1  LD A,A9h
                        ; K5122 ctrl PIO Port A Daten (Port 0x10): 0xA9 = Drive-Steuer-Byte
                        ; Bit7=1 (kein Step), Bit3=1 (/STR high)
                         00E3  OUT (10h),A
                         00E5  LD A,3Fh
                        ; K5122 ctrl PIO Port A ctrl: 0x3F = MODUS 0 (Output)!
                        ; In Modus 0 loest /ASTB-Flanke (=Indexpuls) Interrupt aus.
                         00E7  OUT (11h),A
                        ; K5122 data PIO Port A ctrl (Port 0x15): 0x3F = Modus 0 (Output)
                         00E9  OUT (15h),A
                        ; *** K5122 ctrl PIO Port A: INTERRUPT-VEKTOR = 0xBA ***
                        ; Indexpuls (/ASTB fallend) → CPU-Interrupt → ISR = RAM[0xBA,0xBB] = 0x01C7
                         00EB  LD A,BAh
                         00ED  OUT (11h),A
                        ; [0x07F2] = 4: Seek-Retry-Zaehler
                         00EF  LD A,04h
                         00F1  LD (07F2h),A
                        ; RAM[0x0000] = 0xC3 (JP-Opcode): ueberschreibt die geloopten NOPs
                         00F4  LD A,C3h
                         00F6  LD (0000h),A
                        ; RAM[0x0001,0x0002] = 0x01DD → JP 0x01DDh (STROBE_LOOP-Einstieg)
                        ; ZVE2 startet bei PC=0x0000 und springt damit sofort zu STROBE_LOOP
                         00F9  LD HL,01DDh
                         00FC  LD (0001h),HL
                        ; [0x03FC] = 0xEE: initialer Ctrl-Wert fuer Port 0x10
                        ; 0xEE: Bit0=0 (Read-Modus), Bit3=1 (/STR high), kein Drive aktiv
                         00FF  LD A,EEh
                         0101  LD (03FCh),A
                        ; Port 0x18 (8212-Latch Drive-Select): 0xEE → kein Drive selektiert
                         0104  OUT (18h),A
                        ; HL = 0x0400: Ladeadresse fuer Bootsektor-Daten
                         0106  LD HL,0400h
                        ; B = 0x00 (= Low-Byte von HL)
                         0109  LD B,L
                        ; [0x03F0] = 0x0400: Ladeadresse sichern
                         010A  LD (03F0h),HL
                        ; D=0x50 (Outer-Retry-Zaehler), E=0x06
                         010D  LD DE,5006h                    
                        ; ===== LAUFWERK-ERKENNUNG =====
                        ; Liest Drive-Status aus K5122 ctrl PIO Port B (Port 0x12).
                        ; RLCA rotiert Bit7 in Carry: Carry=0 → Bit7 war 0 → Drive bereit.
  DRIVE_DETECT_LOOP:     0110  IN A,(12h)
                        ; altes Bit7 → Carry
                         0112  RLCA
                        ; Carry=0 (Bit7=0) → Drive bereit → WAIT_INDEX_SETUP
                         0113  JR NC,0128h
                        ; kein Drive bereit: Outer-Retry--
                         0115  DEC D
                        ; Retry erschoepft → BOOT_FAIL
                         0116  JR Z,0140h
                        ; Seek-to-Track-0: Step-Pulse senden um Kopf auf Track 0 zu setzen
  SEND_SEEK_TRACK0:      0118  LD A,09h
                         011A  LD B,A
                        ; Port 0x10 Ctrl: Step-Puls ausloesen (Bit3 faellt)
                         011B  OUT (10h),A
                         011D  LD A,89h
                         011F  OUT (10h),A
                        ; Delay-Schleife (innerer Zaehler C)
                         0121  DEC C
                         0122  JR NZ,0121h
                        ; Delay-Schleife (aeusserer Zaehler B)
                         0124  DJNZ 0121h
                         0126  JR 0110h
                        ; ===== 4 INDEX-PULSE ABWARTEN =====
                        ; Jeder Indexpuls → ISR 0x01C7 → [0x03F7]--.
                        ; Nach 4 Pulsen (= 4 Umdrehungen x 200 ms = 800 ms) → STORE_03FD.
  WAIT_INDEX_SETUP:      0128  LD HL,03F7h
                        ; [0x03F7] = 4: Puls-Zaehler (ISR dekrementiert auf 0)
                         012B  LD (HL),04h
                        ; Port 0x11 Int-Ctrl: 0x97 = INTENA=1, OR-Modus, H-Level, Maske folgt
                         012D  LD A,97h
                         012F  OUT (11h),A
                        ; Interrupt-Maske = 0xFF (in Modus 0 ignoriert; IE wird durch 0x97 gesetzt)
                         0131  LD A,FFh
                         0133  OUT (11h),A
                        ; A = [0x03F7] lesen; A=0x87 vorladen fuer [0x03FD] (NZ-Pfad + kein Step)
  WAIT_INDEX_LOOP:       0135  LD A,(HL)
                        ; Z-Flag setzen wenn [0x03F7]=0
                         0136  OR A
                        ; A = 0x87 vorladen: Bit7=1 (kein Step), Bit1=1 (NZ-Pfad)
                         0137  LD A,87h
                        ; [0x03F7]=0 → 4 Pulse empfangen → springe zu STORE_03FD
                         0139  JR Z,0153h
                        ; Timeout-Zaehler dekrementieren
                         013B  DEC C
  WAIT_INDEX_INNER:      013C  JR NZ,0135h
                         013E  DJNZ 0135h
                        ; ===== BOOT-FEHLER / RETRY-LOGIK =====
                        ; Kein Indexpuls nach Timeout. [0x03FC] enthaelt aktuellen Ctrl-Wert.
                        ; 0xEE, 0xDD, ... bis 0x77 = letzter moeglicher Wert → alternativer Boot.
  BOOT_FAIL:             0140  LD A,(03FCh)
                        ; letzter moeglicher Ctrl-Wert? → zu alternativer Boot-Quelle (0x027E)
                         0143  CP 77h
                         0145  JP Z,027Eh
                        ; naechsten Ctrl-Wert durch Rotation berechnen und Drive-Init wiederholen
                         0148  RLCA
                         0149  JR 0101h
                         014B  DEC E
                         014C  JR Z,0140h
  WAIT_INDEX_OK:         014E  LD A,(03FDh)
                         0151  XOR 02h
                        ; ===== [0x03FD] SETZEN: FM/MFM-Pfadbyte (Default 0x87=FM, vorgeladen @0137) =====
                        ; A=0x87 (bit7=1 → kein Step) wurde bei 0x0137 vorgeladen. Bei [0x03FD]=0x00
                        ; (bit7=0) wuerde OUT(10h) faelschlich einen STEP-Puls erzeugen.
  STORE_03FD:            0153  LD (03FDh),A
                        ; [0x07F0,0x07F1] = 0x8001 (Sektor-Count-Init fuer ZVE2)
                         0156  LD HL,8001h
                         0159  LD (07F0h),HL
                        ; Erwartetes IDAM fuer 1. Sektor: Zyl=0, Kopf=0, Sektor-ID=1, Size=0
  SEEK_SETUP:            015C  LD H,00h
                        ; HL=0x0001 (aus 0x8001, H:=0): [0x03F5]=1 (Sektor-ID, 1-basiert!), [0x03F6]=0 (Size, 0=128B)
                         015E  LD (03F5h),HL
                         0161  LD L,H
                        ; [0x03F3]=0 (Zylinder), [0x03F4]=0 (Kopf)
                         0162  LD (03F3h),HL
                        ; Laufwerks-Init: setzt [0x03F8]=0, gibt ZVE2 aus Reset frei
                         0165  CALL 0194h
                        ; HL zeigt nach CALL 0194h auf [0x03F8] (Abschluss-Flag)
                         0168  LD A,(HL)
                         0169  OR A
                        ; warte solange [0x03F8]=0 (ZVE2 laeuft noch)
                         016A  JR Z,0168h
                        ; [0x03F8]=1: ISR-Timeout → Retry; [0x03F8]=3: ZVE2 fertig → weiter
  STROBE_INIT:           016C  DEC A
                        ; A=1 (d.h. [0x03F8] war 1 = Timeout) → Retry-Pfad
                         016D  JR Z,014Bh
                        ; [0x03F8]=3 (nach DEC: A=2): Bootsektor-Signatur pruefen
                         016F  CALL 01B6h                     
                        ; NZ: Signatur falsch → BOOT_FAIL
                         0172  JR NZ,0140h
                        ; Signatur korrekt: Bootsektor-Code ausfuehren
                         0174  CALL 0437h
                        ; Nach Rueckkehr: Ladeadresse und Vektor-Tabelle aktualisieren (Neustart-Pfad)
                         0177  LD HL,(03FAh)
                         017A  LD (0462h),HL
                        ; ROM-Enable Bit setzen: OR 0x01 → Bit0=1
                         017D  IN A,(0Ah)
                         017F  OR 01h
                         0181  OUT (0Ah),A
                        ; Nochmalige LDI-Kopie (wie CLEAR_LOOP-Trick) fuer Neustart-Pfad
                         0183  LD BC,0346h
                         0186  LD DE,00BAh
                         0189  LD H,D
                         018A  LD L,H
                         018B  LDI
                         018D  DEC HL
                         018E  JP PE,018Bh
                         0191  JP 0074h
                        ; ===== LAUFWERKS-INITIALISIERUNG (CALL-Ziel von 0x0165) =====
                        ; Bereitet ZVE2 fuer Sektor-Lesen vor:
                        ;   [0x03F7] = 16 (ISR-Timeout-Zaehler)
                        ;   OUT(04h): gibt ZVE2 aus Reset frei → ZVE2 startet bei PC=0x0000 → JP 0x01DD
                        ;   OUT(10h,0xA5): /STR fallende Flanke → doReadSector()
                        ;   INC HL: HL → [0x03F8]; [0x03F8] = 0 (ZVE2 laeuft)
                         0194  LD HL,03F7h
                        ; Timeout-Zaehler auf 16 Indexpulse
                         0197  LD (HL),10h
                        ; Port 0x14: unbekanntes Steuersignal (evtl. ZVE2-Vorbereitung)
                         0199  OUT (14h),A
                        ; Port 0x04: ZVE2 aus Reset freigeben (ZVE2 startet bei JP 0x01DD)
                         019B  OUT (04h),A
                         019D  LD A,FFh
                        ; Port 0x17 = K5122 Daten-Port (Puffer-Reset oder Sync)
                         019F  OUT (17h),A
                         01A1  OUT (17h),A
                         01A3  LD A,A5h
                        ; OUT(10h,0xA5): /STR fallende Flanke → doReadSector() ausloesen
                         01A5  OUT (10h),A
                         01A7  LD A,7Fh
                         01A9  OUT (17h),A
                        ; Dummy-Lese aus Sektor-Puffer (Port 0x16)
                         01AB  IN A,(16h)
                        ; [0x03FD] in Port 0x10: konfiguriert /STR-Verhalten (NZ-Pfad / Step)
                         01AD  LD A,(03FDh)
                         01B0  OUT (10h),A
                        ; INC HL: HL → [0x03F8] (Abschluss-Flag)
                         01B2  INC HL
                        ; [0x03F8] = 0: ZVE2 noch nicht fertig (ZVE1 wartet in Schleife 0x0168)
                         01B3  LD (HL),00h
                         01B5  RET
                        ; ===== BOOTSEKTOR-SIGNATUR PRUEFEN (CALL-Ziel von 0x016F) =====
                        ; Prueft RAM[0x0400,0x0401]="YS" (0x59,0x53) und RAM[0x0402]='L' (0x4C).
                        ; RET NZ wenn Signatur falsch, RET mit Z wenn korrekt.
                         01B6  LD HL,(0400h)
                        ; BC = 0x5953 = "SY" (little-endian: "YS")
                         01B9  LD BC,5953h
                        ; SBC HL,BC: NZ wenn HL != 0x5953
                         01BC  SBC HL,BC
                         01BE  RET NZ
                         01BF  LD HL,0402h
                        ; A = 0x4C = 'L'
                         01C2  LD A,4Ch
                        ; CP (HL): [0x0402] = 'L'?
                         01C4  CP (HL)
                         01C5  RET NZ
                         01C6  RET                            
                        ; ===== ISR: INDEXPULS-ZAEHLER DEKREMENTIEREN =====
                        ; Vektor 0xBA → RAM[0xBA,0xBB]={0xC7,0x01} → ISR-Adresse 0x01C7
                        ; Bedingung: K5122 ctrl PIO Port A Modus 0, /ASTB feuert pro Indexpuls, IE=1
                        ; Ablauf: jeder Indexpuls dekrementiert [0x03F7]; nach 4 Pulsen /STR assertieren.
  ISR_DECREMENT_03F7:    01C7  EI
                        ; verschachtelte Interrupts erlauben (naechster Puls nicht blockiert)
                         01C8  PUSH AF
                         01C9  PUSH HL
                         01CA  LD HL,03F7h
                        ; [0x03F7]--: Puls-Zaehler dekrementieren
                         01CD  DEC (HL)
                        ; 0 erreicht → nach 4 Pulsen → /STR-Strobe ausloesen
                         01CE  JR Z,01D4h
                         01D0  POP HL
                         01D1  POP AF
                         01D2  RETI
                        ; [0x03F7]=0: 4 Indexpulse gezaehlt → /STR assertieren, [0x03F8]=1 setzen.
                        ; Dies ist der ISR-Timeout-Pfad; normaler Abschluss setzt [0x03F8]=3 durch ZVE2.
  ISR_ASSERT_STR:        01D4  LD A,BDh
                        ; Port 0x10: 0xBD = Bit3=1 → /STR high (Vorbereitungs-Phase)
                         01D6  OUT (10h),A
                        ; INC HL: HL → 0x03F8
                         01D8  INC HL
                        ; [0x03F8] = 1: ISR-Timeout-Flag (ZVE2 hat keinen Sektor geladen)
                         01D9  LD (HL),01h
                         01DB  JR 01D0h
                        ; ===== HAUPT-STROBE/IDAM-SUCHSCHLEIFE =====
                        ; ZVE2 (zweite Z80-CPU) startet bei PC=0x0000 → JP 0x01DD.
                        ; Sucht IDAM-Marker (0xFE) im K5122-Sektorpuffer (Port 0x16).
                        ; Nach IDAM-Treffer: Sektor-Daten per INI/INIR in RAM-Ladeadresse.
                        ; Puffer-Layout: buf[0]=0x00, buf[1]=0xFE(IDAM), buf[2]=cyl, buf[3]=head,
                        ;                buf[4]=sector_id(1-bas.), buf[5]=size_code, buf[6..]=Daten
  STROBE_LOOP:           01DD  LD HL,(03F0h)
                        ; HL = aktuelle Ladeadresse (zu Beginn 0x0400)
                         01E0  LD A,(07F1h)
                         01E3  LD B,A
                         01E4  LD DE,0700h
                        ; C = 0x16 (K5122 Daten-Lese-Port fuer INI/INIR)
                         01E7  LD C,16h
                        ; Lade IDAM-Vergleichswerte in Alternativ-Registersatz
  LOAD_IDAM_REGS:        01E9  EXX
                        ; B'=0xFE (IDAM-Marker), C'=0xA1
                         01EA  LD BC,FEA1h
                        ; D'=[0x03F4]=Kopf, E'=[0x03F3]=Zylinder
                         01ED  LD DE,(03F3h)
                        ; H'=[0x03F6]=Size-Code, L'=[0x03F5]=Sektor-ID (erwartet, 1-basiert)
                         01F1  LD HL,(03F5h)
                        ; Port 0x10: 0xBD = Bit3=1 → /STR high (Ruecksetzen)
                         01F4  LD A,BDh
                         01F6  OUT (10h),A
                        ; /STR assertieren: fallende Flanke Bit3 → K5122 doReadSector()
  ASSERT_STR:            01F8  LD A,B5h
                        ; Port 0x10: 0xB5 = Bit3=0 → /STR low (Sektor-Lese-Strobe)
                         01FA  OUT (10h),A
                        ; springe zu STROBE_LOOP_BODY (liest [0x03FD] und prueft Pfad)
                         01FC  JR 025Ah
                        ; Sektor-Nummern-Fehler-Handling (von 0x0222 JR NZ,01FEh)
                         01FE  LD H,A
                         01FF  XOR A
                         0200  EXX
                         0201  LD B,A
                         0202  EXX
                         0203  JR 0273h
                        ; ===== IDAM-SUCHE: Z-PFAD = MFM-Lesepfad ([0x03FD] bit1=0, 0x85) =====
                        ; Liest mehr Bytes vor dem FE-Vergleich (ueberspringt den A1-Sync vor FE).
                        ; Passt die Spur nicht (z.B. FM-Spur) → kein FE-Treffer → Re-Strobe/Timeout
                        ; → ROM toggelt MK (FM<->MFM). Welcher Pfad passt, haengt von der Spur ab.
  READ_IDAM_Z:           0205  IN A,(16h)
                         0207  IN A,(16h)
                         0209  IN A,(16h)
                        ; ===== IDAM-SUCHE: NZ-PFAD = FM-Lesepfad ([0x03FD] bit1=1, 0x87) =====
                        ; Liest wenige Bytes vor dem FE-Vergleich (Marke FE liegt direkt, kein A1).
                        ; buf[1]=0xFE == B'=0xFE → Treffer auf einer FM-Spur.
  READ_IDAM_NZ:          020B  IN A,(16h)
                        ; liest buf[1] (NZ-Pfad) oder buf[4] (Z-Pfad-Fall-through)
                        ; IDAM-Marker vergleichen (B'=0xFE)
  CMP_IDAM_MARK:         020D  CP B
                        ; kein Treffer → neuen /STR-Strobe (Puffer neu lesen)
                         020E  JR NZ,01F8h
                        ; Zylinder vergleichen (E'=erwarteter Zylinder)
                         0210  IN A,(16h)
                         0212  CP E
                         0213  JR NZ,01F8h
                        ; Kopf vergleichen (D'=erwarteter Kopf)
                         0215  IN A,(16h)
                         0217  CP D
                         0218  JR NZ,01F8h
                        ; Sektor-ID vergleichen (L'=erwarteter Sektor, 1-basiert)
  IDAM_MATCH:            021A  IN A,(16h)
                         021C  CP L
                         021D  JR NZ,01F8h
                        ; Size-Code vergleichen (H'=erwarteter Size-Code)
                         021F  IN A,(16h)
                         0221  CP H
                        ; Size-Code falsch → Sektor-Nummern-Fehler-Pfad (0x01FE)
                         0222  JR NZ,01FEh
                        ; IDAM vollstaendig gematcht! Nutzdaten folgen direkt im Puffer.
                         0224  LD A,B5h
                         0226  OUT (10h),A
                        ; [0x03FD] lesen um NZ/Z-Bit fuer Timing-Anpassung zu pruefen
                         0228  LD A,(03FDh)
                         022B  BIT 1,A
                        ; /STR nochmal pulsieren (Daten-Transfer-Modus vorbereiten)
                         022D  OUT (10h),A
                        ; Dummy-Reads fuer Timing (Z-Pfad: 4 Bytes, NZ-Pfad: 1 Byte)
                         022F  IN A,(16h)
                         0231  JR NZ,0239h
                         0233  IN A,(16h)
                         0235  IN A,(16h)
                         0237  IN A,(16h)
                        ; Erstes Datenbyte einlesen
                         0239  IN A,(16h)
                        ; Wechsle zum Hauptregistersatz (HL=Ladeadresse, BC=Byte-Zaehler)
                         023B  EXX
                        ; INI (ED A2): 1 Byte Port C → (HL++), B--
                         023C  DB EDh,A2h
                         023E  DB EDh,A2h
                         0240  DB EDh,A2h
                        ; INIR (ED B2): verbleibende B Bytes Port C → (HL++), bis B=0
                         0242  DB EDh,B2h
                         0244  EX DE,HL
                        ; weitere INI-Transfers fuer Header-Abschluss
                         0245  DB EDh,A2h
                         0247  DB EDh,A2h
                        ; /STR high
                         0249  LD A,B5h
                         024B  OUT (10h),A
                         024D  EX DE,HL
                        ; Sektor-Zaehler aktualisieren
                         024E  LD A,(07F1h)
                         0251  LD B,A
                         0252  EXX
                        ; [0x07F2] = max Sektor-Retry-Zaehler
                         0253  LD A,(07F2h)
                         0256  CP L
                        ; alle Sektoren geladen? → ZVE2 Fertigstellung
                         0257  JR Z,0267h
                         0259  INC L
                        ; ===== STROBE-LOOP KOERPER (Einstieg von JR 025Ah bei 0x01FC) =====
                        ; Liest [0x03FD] und waehlt FM/MFM-Lesepfad (MK = Port-0x10 bit1):
                        ;   0x87 FM:  Bit1=1 → NZ-Pfad (Marke FE direkt), Bit7=1 → kein Step
                        ;   0x85 MFM: Bit1=0 → Z-Pfad (A1-Sync vor FE).  (0x00: Bit7=0 → STEP-PULS!)
  STROBE_LOOP_BODY:      025A  LD A,(03FDh)
                        ; Bit1: NZ-Pfad-Flag testen
                         025D  BIT 1,A
                        ; ACHTUNG: wenn Bit7=0 → Step-Puls auf Port 0x10!
                         025F  OUT (10h),A
                        ; Lese buf[0] aus Sektorpuffer
                         0261  IN A,(16h)
                        ; Z (Bit1=0) → Z-Pfad = MFM (A1-Sync vor FE)
                         0263  JR Z,0205h
                        ; NZ (Bit1=1) → NZ-Pfad = FM (Marke FE direkt)
                         0265  JR 020Bh
                        ; ===== ZVE2 ABSCHLUSS: alle Sektoren geladen =====
                        ; ZVE2 setzt [0x03F8]=3 → ZVE1 verlässt Warte-Schleife bei 0x016A.
                         0267  LD A,03h
                        ; K5122 ctrl PIO Port A: INTENA=0 (kein weiterer Indexpuls-Interrupt)
                         0269  OUT (11h),A
                        ; [0x03F8] = 3: ZVE2 fertig → ZVE1 wird geweckt
                         026B  LD (03F8h),A
                         026E  JR 0265h
                         0270  LD BC,1EDAh
                        ; [0x07F1] = A (Sektor-Zaehler update)
                         0273  LD (07F1h),A
                        ; [0x07F2] = 2 (Retry auf 2 setzen)
                         0276  LD A,02h
                         0278  LD (07F2h),A
                         027B  JP 01F8h
                        ; ===== ALTERNATIVER BOOT-PFAD (zweite Boot-Quelle) =====
                        ; Erreicht wenn alle Floppy-Ctrl-Werte versucht wurden (CP 77h bei 0x0143).
                        ; Schaltet auf Sekundaer-Controller um (Ports 0x30-0x37: evtl. K2521 Seriell).
                         027E  LD A,FFh
                        ; Port 0x18 (Drive-Select): 0xFF = alle Drives deselektiert
                         0280  OUT (18h),A
                        ; ISR-Vektoren fuer alternativen Boot umbiegen:
                        ; [0x03F0] → 0x03BC (ISR Kanal 0 des Sekundaer-Controllers)
                         0282  LD HL,03BCh
                         0285  LD (03F0h),HL
                        ; [0x03F2] → 0x03D4 (ISR Kanal 1: Block-Zaehler)
                         0288  LD HL,03D4h
                         028B  LD (03F2h),HL
                        ; [0x03F4] → 0x03C7 (ISR: Byte-Transfer per INI in RAM)
                         028E  LD HL,03C7h
                         0291  LD (03F4h),HL
                        ; I = 3: Interrupt-Vektor-Tabelle jetzt bei 0x0300
                         0294  LD A,03h
                         0296  LD I,A
                        ; Init Sekundaer-Controller Ports (0x33-0x37):
                        ; Port 0x33=Ctrl, 0x34=Cmd/Status, 0x35=Daten, 0x36/37=weitere Ctrl
                         0298  LD A,7Fh
                         029A  OUT (33h),A
                         029C  OUT (36h),A
                         029E  OUT (37h),A
                         02A0  LD A,03h
                         02A2  OUT (34h),A
                         02A4  INC A
                        ; A = 4
                         02A5  OUT (35h),A
                         02A7  LD A,FFh
                         02A9  OUT (37h),A
                         02AB  LD A,FDh
                         02AD  OUT (37h),A
                         02AF  LD A,FFh
                         02B1  OUT (36h),A
                         02B3  LD A,80h
                         02B5  OUT (36h),A
                        ; Status lesen und pruefen ob Controller bereit
                         02B7  IN A,(34h)
                        ; Maske Bits 6..2: Fehler-Bits
                         02B9  AND 7Ch
                        ; Fehler → HALT-Schleife (0x03B0)
                         02BB  JP NZ,03B0h
                         02BE  LD E,09h
                         02C0  LD A,F2h
                        ; ===== K2526 CTC-INITIALISIERUNG (nur im alternativen Boot-Pfad) =====
                        ; Port 0x0C = K2526 CTC Kanal 0, Port 0x0D = CTC Kanal 1
  INIT_CTC:              02C2  OUT (0Ch),A
                        ; CTC Kanal 0: 0xF2 konfigurieren (Timer-Modus, Prescaler 16)
                         02C4  LD A,05h
                        ; [0x03FA] = 5 (Retry-Counter fuer Sekundaer-Boot)
                         02C6  LD (03FAh),A
                         02C9  LD A,E
                        ; Port 0x34: Sekundaer-Controller Cmd-Register
                         02CA  OUT (34h),A
                        ; BC=0: maximale Delay-Schleife
                         02CC  LD B,00h
                         02CE  CALL 03B6h
                         02D1  CALL 03B6h
                        ; HL = 0x0400: Ladeadresse fuer Sekundaer-Boot
                         02D4  LD HL,0400h
                         02D7  LD (03F8h),HL
                         02DA  LD D,10h
                        ; Status Port 0x35 lesen
                         02DC  IN A,(35h)
                        ; E Bit0: Laufwerks-Typ-Unterscheidung
                         02DE  BIT 0,E
                         02E0  JR NZ,02E4h
                         02E2  LD D,08h
                         02E4  AND D
                        ; Status-Bit fehlt → HALT bei 0x0397
                         02E5  JP Z,0397h
                         02E8  XOR A
                        ; [0x03F6] = 0 (Byte-Zaehler reset)
                         02E9  LD (03F6h),A
                         02EC  LD A,14h
                        ; [0x03F7] = 0x14 = 20 (Block-Zaehler)
                         02EE  LD (03F7h),A
                         02F1  LD A,A7h
                        ; CTC Kanal 1: 0xA7 = Timer-Modus, Prescaler 16, kein Interrupt
  INIT_CTC2:             02F3  OUT (0Dh),A
                        ; Zeitkonstante = 0xFF (maximaler Timeout)
                         02F5  LD A,FFh
                         02F7  OUT (0Dh),A
                        ; Sekundaer-Controller Status pruefen (Bit7 = Bereit-Signal)
                         02F9  IN A,(34h)
                         02FB  BIT 7,A
                        ; Bit7=1: bereit → Konfiguration abschliessen
                         02FD  JR NZ,0317h
                        ; Bit7=0: noch nicht bereit → Bit5 setzen und warten
                         02FF  OR 20h
                         0301  OUT (34h),A
                         0303  LD L,03h
                         0305  CALL 03B6h
                         0308  IN A,(34h)
                         030A  BIT 7,A
                         030C  JR NZ,0317h
                         030E  DEC L
                         030F  JR NZ,0305h
                        ; Bit5 ruecksetzen
                         0311  AND DFh
                         0313  OUT (34h),A
                         0315  JR 033Bh
                        ; Bit7=1 Pfad: Konfiguration abschliessen
                         0317  AND DFh
                         0319  OUT (34h),A
                         031B  CALL 03B6h
                        ; Transfer-Modus: Bits 5 und 4 setzen
                         031E  OR 30h
                         0320  OUT (34h),A
                        ; Nur Bits 7,6 behalten
                         0322  AND C3h
                         0324  OUT (34h),A
                        ; warte auf Status-Bit
                         0326  IN A,(35h)
                         0328  AND D
                         0329  JR Z,0326h
                         032B  IN A,(34h)
                        ; Bit0: Laufwerks-Typ-Unterscheidung
                         032D  BIT 0,A
                         032F  JR NZ,0335h
                        ; Typ A: +4
                         0331  ADD A,04h
                         0333  JR 0337h
                        ; Typ B: +8
                         0335  ADD A,08h
                         0337  OUT (34h),A
                         0339  JR 02F9h
                        ; Bereit: Transfer-Parameter einstellen
                         033B  LD C,A
                        ; Bits 1,0 loeschen, Bit6 setzen
                         033C  AND F3h
                         033E  OR 40h
                         0340  OUT (34h),A
                         0342  LD A,C
                         0343  OUT (34h),A
                        ; Bit4 setzen (Transfer Start)
                         0345  OR 10h
                         0347  OUT (34h),A
                        ; warte bis Bit7 gesetzt (Transfer laeuft)
                         0349  IN A,(34h)
                         034B  RLA
                         034C  JR NC,0349h
                         034E  LD B,00h
                         0350  CALL 03B6h
                         0353  LD D,04h
                         0355  LD A,F0h
                        ; Port 0x33: Reset/Init-Sequenz fuer Sekundaer-Controller
                         0357  OUT (33h),A
                         0359  LD BC,8331h
                         035C  LD A,97h
                         035E  OUT (33h),A
                         0360  OUT (33h),A
                        ; warte auf Sync-Byte von Port 0x31
                         0362  IN A,(31h)
                         0364  XOR A
                        ; warte bis B-Zaehler 0 (Sync)
                         0365  CP B
                         0366  JR NZ,0365h
                        ; Reset Port 0x33
                         0368  LD A,03h
                         036A  OUT (33h),A
                        ; Ladeadresse um 3 dekrementieren (je Sync-Schleife)
                         036C  LD HL,(03F8h)
                         036F  DEC HL
                         0370  DEC HL
                         0371  DEC HL
                         0372  LD (03F8h),HL
                         0375  DEC D
                        ; D Zyklen wiederholen
                         0376  JR NZ,0355h
                         0378  LD BC,0D00h
                         037B  CALL 03B6h
                        ; Bit4 ruecksetzen (Transfer Ende)
                         037E  IN A,(34h)
                         0380  AND EFh
                         0382  OUT (34h),A
                        ; CTC Kanal 1: 0x21 = Timer-Stop
                         0384  LD A,21h
                         0386  OUT (0Dh),A
                        ; Bootsektor-Signatur pruefen (wie bei Floppy-Boot)
                         0388  CALL 01B6h
                        ; NZ: Signatur falsch → Fehler-Pfad
                         038B  JR NZ,0398h
                        ; Signatur OK: Bootsektor-Code ausfuehren
                         038D  CALL 0434h
                         0390  JP 0177h
                        ; Hilfs-Reset Port 0x33 (Rueckkehr-Ziel von ISR 0x03D4 via Stack-Trick)
                         0393  LD A,03h
                         0395  OUT (33h),A
                         0397  NOP
                        ; Fehler: Signature-Mismatch oder Transfer-Fehler → Retry
                         0398  LD A,21h
                         039A  OUT (0Dh),A
                        ; [0x03FA]-- (Retry-Counter)
                         039C  LD HL,03FAh
                         039F  DEC (HL)
                         03A0  LD A,03h
                         03A2  OUT (34h),A
                        ; NZ: noch Retries uebrig → von vorn
                         03A4  JP NZ,02C9h
                        ; Bit0 von E: weiteren Laufwerks-Typ versuchen
                         03A7  BIT 0,E
                         03A9  JR Z,03B0h
                         03AB  LD E,06h
                         03AD  JP 02C4h
                        ; ===== HALT-SCHLEIFE (kein Boot moeglich) =====
                        ; Alle Boot-Versuche erschoepft: Fehler-Code ausgeben und anhalten.
                         03B0  LD A,9Fh
                        ; Port 0x03: Fehler-/Status-Ausgabe (evtl. Frontplatten-LED oder Panel)
                         03B2  OUT (03h),A
                        ; Endlosschleife: System bleibt stehen
                         03B4  JR 03B4h
                        ; ===== DELAY-SUBROUTINE (CALL-Ziel von vielen Stellen) =====
                        ; Zaehlt C runter, dann B x C - Dauer: ca. (B+1)x(C+1)x13 Takte
                         03B6  DEC C
                         03B7  JR NZ,03B6h
                         03B9  DJNZ 03B6h
                         03BB  RET
                        ; ===== ISR: SEKUNDAERER CONTROLLER KANAL 0 =====
                        ; Liest Byte von Port 0x31, bestaetigt Empfang via Port 0x33.
                        ; Adresse in [0x03F0] (gesetzt bei 0x0285).
                         03BC  EI
                         03BD  PUSH AF
                        ; Daten-Byte vom Sekundaer-Controller lesen
                         03BE  IN A,(31h)
                         03C0  LD A,F4h
                        ; Empfangs-Bestaetigung an Port 0x33
                         03C2  OUT (33h),A
                         03C4  POP AF
                         03C5  RETI
                        ; ===== ISR: BYTE-TRANSFER (Software-DMA via INI) =====
                        ; Schreibt ein Byte vom Sekundaer-Controller (Port C) in RAM bei [0x03F8].
                        ; Adresse in [0x03F4] (gesetzt bei 0x0291).
                         03C7  EI
                         03C8  PUSH AF
                         03C9  PUSH HL
                        ; HL = aktuelle Schreib-Adresse im RAM
                         03CA  LD HL,(03F8h)
                        ; INI (ED A2): 1 Byte aus Port C → (HL++), B--
                         03CD  DB EDh,A2h
                        ; neue Schreib-Adresse sichern
                         03CF  LD (03F8h),HL
                         03D2  JR 03E1h
                        ; ===== ISR: SEKUNDAERER CONTROLLER KANAL 1 (Block-Zaehler) =====
                        ; Dekrementiert Byte-Zaehler [0x03F6] und Block-Zaehler [0x03F7].
                        ; Wenn beide 0: Bootvorgang abgeschlossen → springe zu 0x0393.
                        ; Adresse in [0x03F2] (gesetzt bei 0x028B).
                         03D4  EI
                         03D5  PUSH AF
                         03D6  PUSH HL
                        ; HL → [0x03F6] = Byte-Zaehler
                         03D7  LD HL,03F6h
                        ; Byte-Zaehler--
                         03DA  DEC (HL)
                        ; noch Bytes im Block → normal RETI
                         03DB  JR NZ,03E1h
                        ; INC L: HL → [0x03F7] = Block-Zaehler
                         03DD  INC L
                        ; Block-Zaehler--
                         03DE  DEC (HL)
                        ; alle Bloecke geladen → Abschluss-Pfad
                         03DF  JR Z,03E5h
                         03E1  POP HL
                         03E2  POP AF
                         03E3  RETI
                        ; ===== BOOT-ABSCHLUSS: Ruecksprung-Adresse manipulieren =====
                        ; Ersetzt normale RETI-Ruecksprung-Adresse durch 0x0393 (Reset Port 0x33).
                        ; EX (SP),HL tauscht Ruecksprung-Adresse auf dem Stack.
                         03E5  POP HL
                         03E6  POP AF
                        ; zweites POP AF: rauemt zusaetzlichen Stack-Frame ab
                         03E7  POP AF
                        ; neues Sprungziel: 0x0393 (Reset-Code nach erfolgreichem Transfer)
                         03E8  LD HL,0393h
                        ; tausche [SP] mit HL: Ruecksprung geht jetzt nach 0x0393
                         03EB  EX (SP),HL
                         03EC  PUSH AF
                         03ED  RETI
                        ; ===== RAM-VARIABLEN-BEREICH (0x03EF-0x03FF) =====
                        ; Diese Adressen enthalten zur Laufzeit Boot-Variablen.
                        ; Der Disassembler zeigt zufaellige Opcodes, da die Bytes
                        ; nach dem Booten ueberschrieben werden.
                         03EF  NOP
                        ; 0x03F0-0x03F1: Ladeadresse Sektor-Daten (2 Byte, init: 0x0400)
                         03F0  NOP
                         03F1  INC B
                        ; 0x03F2-0x03F3: ISR-Zeiger Kanal 1 (2 Byte) / erwarteter Zylinder
                         03F2  CP 00h
                        ; 0x03F4-0x03F5: ISR-Zeiger Kanal 2 (2 Byte) / erwarteter Kopf + Sektor-ID
                         03F4  NOP
                         03F5  NOP
                        ; 0x03F6: Byte-Zaehler (Sek.-Boot) / Size-Code (Floppy)
                         03F6  NOP
                        ; 0x03F7: Indexpuls-Zaehler (4→0) / Block-Zaehler (Sek.-Boot)
                         03F7  NOP
                        ; 0x03F8-0x03F9: Abschluss-Flag (0/1/3) oder Schreib-Adresse (2 Byte)
                         03F8  NOP
                         03F9  NOP
                        ; 0x03FA: Seek-/Retry-Counter (init: 5)
                         03FA  NOP
                        ; 0x03FB: (unbenutzt)
                         03FB  EX AF,AF'
                        ; 0x03FC: Ctrl-Wert fuer Port 0x10 (init: 0xEE, rotiert bei Retry)
                         03FC  NOP
                        ; 0x03FD: FM/MFM-Pfad (MK, Port 0x10 bit1): 0x87=FM, 0x85=MFM; bit7=Step-Unterdr.
                         03FD  NOP
                        ; 0x03FE-0x03FF: Puffer-Ende / unbenutzt
                         03FE  RST 30h
                         03FF  LD L,E                         
