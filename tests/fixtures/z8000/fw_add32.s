; fw_add32.s - C7: 32-Bit-Arithmetik (ADDL)
;
; Z8001-Firmware fuer EM256 Testprogramm
; Testet die 32-Bit-Addition (ADDL) mit Registerpaaren.
; RR2 = 0x00010000 + 0x0000FFFF = 0x0001FFFF
;
; Erwartung: RESULT1 = 0x0001 (High), RESULT2 = 0xFFFF (Low), STATUS = 0x0001
;
; (c) 2026 Olaf Krieger - MIT Lizenz

    ORG  0x0000

    ; Z8001 Reset-Vektor (FCW=0xC000: Segmented + System Mode)
    DW   0x0000, 0xC000, 0x0000, 0x0040

    ; Mailbox
    DS   56

    ; Code ab 0x0040
    LD   R15, #0xFFF0

    LDL  RR2, #0x00010000
    LDL  RR4, #0x0000FFFF
    ADDL RR2, RR4
    ; RR2 = 0x0001FFFF -> R2=0x0001, R3=0xFFFF

    ; Ergebnis -> Mailbox
    LD   R7, #0x0012
    LD   @R7, R2            ; RESULT1 = High (0x0001)
    INC  R7, #2
    LD   @R7, R3            ; RESULT2 = Low (0xFFFF)
    ; STATUS = OK
    LD   R1, #0x0001
    LD   R7, #0x0010
    LD   @R7, R1
    JR   T, $
