; fw_add.s - C1: Addition 0x1234 + 0x5678 = 0x68AC
;
; Z8001-Firmware fuer EM256 Testprogramm
; Testet die 16-Bit-Additionsinstruktion des U8001.
;
; Ablauf:
;   1. LD R1, #0x1234   - Ersten Operanden laden
;   2. ADD R1, #0x5678  - Addition ausfuehren (Ergebnis in R1)
;   3. Ergebnis (R1=0x68AC) in Mailbox schreiben
;
; Erwartung: RESULT1 = 0x68AC, STATUS = 0x0001
;
; (c) 2026 Olaf Krieger - MIT Lizenz

    ORG  0x0000

    ; Z8001 Reset-Vektor (FCW=0xC000: Segmented + System Mode)
    DW   0x0000, 0xC000, 0x0000, 0x0040

    ; Mailbox (0x0008 - 0x003F)
    DS   56

    ; Code ab 0x0040
    LD   R15, #0xFFF0       ; SP
    LD   R1, #0x1234
    ADD  R1, #0x5678
    ; R1 = 0x68AC -> Mailbox
    LD   R3, #0x0012        ; RESULT1 offset
    LD   @R3, R1
    ; STATUS = OK
    LD   R1, #0x0001
    LD   R3, #0x0010
    LD   @R3, R1
    JR   T, $
