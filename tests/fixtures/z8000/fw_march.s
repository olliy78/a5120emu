; fw_march.s - March-C DRAM-Test, phasenweise mit Fehlerakkumulation
;
; Z8001-Firmware fuer EM256 Testprogramm
; Fuehrt eine einzelne March-C Phase auf dem angegebenen Segment aus.
; Bei Fehlern wird NICHT abgebrochen, sondern eine Bitmaske der
; fehlerhaften Datenleitungen (XOR Soll/Ist) akkumuliert.
;
; March-C Algorithmus (5 Phasen, je ein Aufruf):
;   Phase 1: Aufwaerts alle Worte mit 0x0000 fuellen (nur Schreiben)
;   Phase 2: Aufwaerts lesen (soll 0x0000), dann 0xFFFF schreiben
;   Phase 3: Aufwaerts lesen (soll 0xFFFF)
;   Phase 4: Abwaerts lesen (soll 0xFFFF), dann 0x0000 schreiben
;   Phase 5: Abwaerts lesen (soll 0x0000)
;
; Erkennt: Stuck-At-Fehler, Koppelfehler zwischen benachbarten Zellen
;
; Eingabe (Mailbox):
;   MB_PARAM1 (0x000A) = Ziel-Segment im Z8001-Format:
;     0x0000=Seg0, 0x0100=Seg1, 0x0200=Seg2, 0x0300=Seg3
;   MB_PARAM2 (0x000C) = Phasennummer (1-5)
;
; Testbereich:
;   Segment 0: 0x0200 bis 0xFFFE (0x0000-0x01FF = Code+Mailbox)
;   Segment 1-3: 0x0000 bis 0xFFFE (komplett, 64 KB)
;
; Segmentierte Adressierung:
;   RR8 = {R8=Segment, R9=Offset} -> LD @RR8, Rn adressiert
;   das Ziel-Segment, waehrend Code+Mailbox in Segment 0 bleiben.
;   Mailbox-Zugriff erfolgt ueber ungerade Register (R7, R11)
;   im non-segmented Modus (= immer Segment 0).
;
; Mailbox-Ergebnis:
;   STATUS  = 0x0001 (OK) oder 0x0002 (Fehler gefunden)
;   RESULT1 = Fehlermaske (OR aller XOR Soll/Ist), 0 wenn OK
;   RESULT2 = Anzahl Wort-Fehler
;   RESULT3 = Offset der ersten Fehlerstelle (0xFFFF wenn OK)
;
; Register-Konventionen:
;   R1  = Testmuster (Soll-Wert)
;   R2  = Fehlermasken-Akkumulator (OR aller XOR Soll/Ist)
;   R3  = Startoffset
;   R4  = Endoffset (0xFFFE)
;   R5  = gelesener Wert (Scratch)
;   R6  = neues Muster (fuer Phasen 2,4)
;   R7  = Mailbox-Zeiger (ungerade -> non-segmented -> Seg 0)
;   RR8 = Segmentierte Adresse (R8=Segment, R9=Offset)
;   R10 = Phasennummer
;   R11 = Mailbox-Hilfsregister (ungerade -> Seg 0)
;   R13 = Fehleranzahl
;   R14 = Offset der ersten Fehlerstelle
;
; (c) 2026 Olaf Krieger - MIT Lizenz

    ORG  0x0000

    ; Z8001 Reset-Vektor (FCW=0xC000: Segmented + System Mode)
    DW   0x0000, 0xC000, 0x0000, 0x0040

    ; Mailbox (0x0008 - 0x003F)
    DS   56

; === Code ab 0x0040 ===

START:
    ; Fehler-Tracking initialisieren
    LD   R2, #0x0000        ; Fehlermaske = leer
    LD   R13, #0x0000       ; Fehleranzahl = 0
    LD   R14, #0xFFFF       ; erste Fehleradresse = keine

    ; Segment-Parameter aus Mailbox lesen (non-segmented, Segment 0)
    LD   R11, #0x000A       ; MB_PARAM1
    LD   R8, @R11           ; R8 = Ziel-Segment

    ; Phasen-Parameter lesen
    LD   R11, #0x000C       ; MB_PARAM2
    LD   R10, @R11          ; R10 = Phase (1-5)

    ; Laufstatus in Mailbox schreiben (0xA0xx, xx=Phase)
    LD   R7, #0x0010        ; STATUS
    LD   R1, #0xA000
    OR   R1, R10
    LD   @R7, R1
    ; Initiale Debugwerte
    INC  R7, #2             ; RESULT1
    LD   R1, #0xD100        ; Debug-Marker "D1 alive"
    LD   @R7, R1
    INC  R7, #2             ; RESULT2
    LD   R1, #0xFFFF        ; Offset noch unbekannt
    LD   @R7, R1
    INC  R7, #2             ; RESULT3
    LD   @R7, R10           ; aktuelle Phase (1..5)

    ; Startadresse bestimmen
    CP   R8, #0x0000        ; Segment 0?
    JR   NE, OTHER_SEG
    LD   R3, #0x0200        ; Seg 0: ab 0x0200 (Code 332 Bytes endet bei 0x014C)
    JR   T, SETUP_OK
OTHER_SEG:
    LD   R3, #0x0000        ; Seg 1-3: ab 0x0000 (komplett)
SETUP_OK:
    LD   R4, #0xFFFE        ; Endadresse (letztes Wort)
    ; Initialen Fortschritt publizieren
    LD   R7, #0x0018        ; RESULT4 = aktueller Offset
    LD   @R7, R3

    ; Phasen-Dispatch
    CP   R10, #1
    JR   EQ, DO_P1
    CP   R10, #2
    JR   EQ, DO_P2
    CP   R10, #3
    JR   EQ, DO_P3
    CP   R10, #4
    JR   EQ, DO_P4
    CP   R10, #5
    JR   EQ, DO_P5
    JP   T, DONE            ; ungueltige Phase -> fertig

; ---------------------------------------------------------------
; Phase 1: Aufwaerts mit 0x0000 fuellen (nur Schreiben)
; ---------------------------------------------------------------
DO_P1:
    LD   R1, #0x0000
    LD   R9, R3
P1_LOOP:
    LD   R5, R9
    AND  R5, #0x0FFF        ; Heartbeat alle 4 KB
    JR   NE, P1_NHB
    LD   R7, #0x0014        ; RESULT2
    LD   @R7, R9            ; aktueller Offset
    INC  R7, #2             ; RESULT3
    LD   R11, #0x0101       ; P1: write-up
    LD   @R7, R11
    LD   R7, #0x0018
    LD   @R7, R9
P1_NHB:
    LD   @RR8, R1
    CP   R9, R4
    JP   EQ, DONE
    INC  R9, #2
    JR   T, P1_LOOP

; ---------------------------------------------------------------
; Phase 2: Aufwaerts lesen 0x0000, schreiben 0xFFFF
; ---------------------------------------------------------------
DO_P2:
    LD   R1, #0x0000        ; Erwartung
    LD   R6, #0xFFFF        ; neues Muster
    LD   R9, R3
P2_LOOP:
    LD   R5, R9
    AND  R5, #0x0FFF
    JR   NE, P2_NHB
    LD   R7, #0x0014
    LD   @R7, R9
    INC  R7, #2
    LD   R11, #0x0201       ; P2: read/write-up
    LD   @R7, R11
    LD   R7, #0x0018
    LD   @R7, R9
P2_NHB:
    LD   R5, @RR8
    XOR  R5, R1             ; R5 = Fehler-Bits (0 wenn OK)
    JR   EQ, P2_OK
    OR   R2, R5             ; Fehlermaske akkumulieren
    OR   R13, R13            ; Z-Flag: R13==0 → erster Fehler?
    JR   NE, P2_NF
    LD   R14, R9            ; erste Fehleradresse merken
P2_NF:
    INC  R13, #1
P2_OK:
    LD   @RR8, R6           ; neues Muster schreiben
    CP   R9, R4
    JP   EQ, DONE
    INC  R9, #2
    JR   T, P2_LOOP

; ---------------------------------------------------------------
; Phase 3: Aufwaerts lesen 0xFFFF
; ---------------------------------------------------------------
DO_P3:
    LD   R1, #0xFFFF
    LD   R9, R3
P3_LOOP:
    LD   R5, R9
    AND  R5, #0x0FFF
    JR   NE, P3_NHB
    LD   R7, #0x0014
    LD   @R7, R9
    INC  R7, #2
    LD   R11, #0x0301       ; P3: read-up
    LD   @R7, R11
    LD   R7, #0x0018
    LD   @R7, R9
P3_NHB:
    LD   R5, @RR8
    XOR  R5, R1
    JR   EQ, P3_OK
    OR   R2, R5
    OR   R13, R13
    JR   NE, P3_NF
    LD   R14, R9
P3_NF:
    INC  R13, #1
P3_OK:
    CP   R9, R4
    JP   EQ, DONE
    INC  R9, #2
    JR   T, P3_LOOP

; ---------------------------------------------------------------
; Phase 4: Abwaerts lesen 0xFFFF, schreiben 0x0000
; ---------------------------------------------------------------
DO_P4:
    LD   R1, #0xFFFF
    LD   R6, #0x0000
    LD   R9, R4             ; von Ende
P4_LOOP:
    LD   R5, R9
    AND  R5, #0x0FFF
    JR   NE, P4_NHB
    LD   R7, #0x0014
    LD   @R7, R9
    INC  R7, #2
    LD   R11, #0x0401       ; P4: read/write-down
    LD   @R7, R11
    LD   R7, #0x0018
    LD   @R7, R9
P4_NHB:
    LD   R5, @RR8
    XOR  R5, R1
    JR   EQ, P4_OK
    OR   R2, R5
    OR   R13, R13
    JR   NE, P4_NF
    LD   R14, R9
P4_NF:
    INC  R13, #1
P4_OK:
    LD   @RR8, R6
    CP   R9, R3
    JP   EQ, DONE
    DEC  R9, #2
    JR   T, P4_LOOP

; ---------------------------------------------------------------
; Phase 5: Abwaerts lesen 0x0000
; ---------------------------------------------------------------
DO_P5:
    LD   R1, #0x0000
    LD   R9, R4
P5_LOOP:
    LD   R5, R9
    AND  R5, #0x0FFF
    JR   NE, P5_NHB
    LD   R7, #0x0014
    LD   @R7, R9
    INC  R7, #2
    LD   R11, #0x0501       ; P5: read-down
    LD   @R7, R11
    LD   R7, #0x0018
    LD   @R7, R9
P5_NHB:
    LD   R5, @RR8
    XOR  R5, R1
    JR   EQ, P5_OK
    OR   R2, R5
    OR   R13, R13
    JR   NE, P5_NF
    LD   R14, R9
P5_NF:
    INC  R13, #1
P5_OK:
    CP   R9, R3
    JP   EQ, DONE
    DEC  R9, #2
    JR   T, P5_LOOP

; ---------------------------------------------------------------
; Ergebnis in Mailbox schreiben (non-segmented -> Segment 0)
; ---------------------------------------------------------------
DONE:
    LD   R7, #0x0010        ; STATUS-Offset
    OR   R13, R13
    JR   NE, HAS_ERRORS
    LD   R1, #0x0001        ; STATUS = OK
    JR   T, WR_STATUS
HAS_ERRORS:
    LD   R1, #0x0002        ; STATUS = FEHLER
WR_STATUS:
    LD   @R7, R1            ; STATUS
    INC  R7, #2
    LD   @R7, R2            ; RESULT1 = Fehlermaske
    INC  R7, #2
    LD   @R7, R13           ; RESULT2 = Fehleranzahl
    INC  R7, #2
    LD   @R7, R14           ; RESULT3 = erste Fehleradresse
    LD   R7, #0x0018
    LD   @R7, R9            ; RESULT4 = letzter Offset
    JR   T, $               ; Endlosschleife -> wartet auf TRQ8
