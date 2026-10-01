; fw_byte.s - C8: Byte-Operationen (CLR, LDB)
;
; Z8001-Firmware fuer EM256 Testprogramm
; Testet Byte-Zugriff auf Teilregister (RH/RL) des U8001.
;
; Ablauf:
;   1. CLR R1            - Ganzes 16-Bit-Register loeschen
;   2. LDB RH1, #0x75   - High-Byte (Bits 15-8) laden
;   3. LDB RL1, #0x10   - Low-Byte (Bits 7-0) laden
;   4. R1 als Wort lesen -> sollte 0x7510 sein
;
; Erwartung: RESULT1 = 0x7510, STATUS = 0x0001
;
; (c) 2026 Olaf Krieger - MIT Lizenz

    ORG  0x0000

    ; Z8001 Reset-Vektor (FCW=0xC000: Segmented + System Mode)
    DW   0x0000, 0xC000, 0x0000, 0x0040

    ; Mailbox (0x0008 - 0x003F)
    DS   56

    ; Code ab 0x0040
    LD   R15, #0xFFF0       ; Stack-Pointer

    CLR  R1                 ; R1 = 0x0000 (alle Bits loeschen)
    LDB  RH1, #0x75         ; High-Byte: R1 = 0x75xx
    LDB  RL1, #0x10         ; Low-Byte:  R1 = 0x7510

    ; Ergebnis 0x7510 in Mailbox schreiben (ueber ungerades Register)
    LD   R3, #0x0012        ; R3 ungerade -> non-segmented -> RESULT1 Offset
    LD   @R3, R1            ; RESULT1 = 0x7510

    ; STATUS = OK
    LD   R1, #0x0001
    LD   R3, #0x0010        ; STATUS Offset
    LD   @R3, R1
    JR   T, $               ; Endlosschleife (wartet auf Bus-Uebernahme)
