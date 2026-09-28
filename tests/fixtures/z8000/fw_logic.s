; fw_logic.s - C3: Logik-Test AND/OR/XOR
;
; Z8001-Firmware fuer EM256 Testprogramm
; Testet alle drei logischen Operationen des U8001 in einer Kette:
;   AND 0xFF00 & 0x0F0F = 0x0F00
;   OR  0x0F00 | 0x00F0 = 0x0FF0
;   XOR 0x0FF0 ^ 0x0FF0 = 0x0000
;   XOR 0x0000 ^ 0x1234 = 0x1234
;
; Erwartung: RESULT1 = 0x1234, STATUS = 0x0001
;
; (c) 2026 Olaf Krieger - MIT Lizenz

    ORG  0x0000

    ; Z8001 Reset-Vektor (FCW=0xC000: Segmented + System Mode)
    DW   0x0000, 0xC000, 0x0000, 0x0040

    ; Mailbox
    DS   56

    ; Code ab 0x0040
    LD   R15, #0xFFF0
    LD   R1, #0xFF00
    AND  R1, #0x0F0F        ; -> 0x0F00
    OR   R1, #0x00F0        ; -> 0x0FF0
    XOR  R1, #0x0FF0        ; -> 0x0000
    XOR  R1, #0x1234        ; -> 0x1234
    ; Ergebnis -> Mailbox
    LD   R3, #0x0012
    LD   @R3, R1
    ; STATUS = OK
    LD   R1, #0x0001
    LD   R3, #0x0010
    LD   @R3, R1
    JR   T, $
