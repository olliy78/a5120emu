; fw_sub.s - C2: Subtraktion 0x8000 - 0x0001 = 0x7FFF
;
; Z8001-Firmware fuer EM256 Testprogramm
; Testet die 16-Bit-Subtraktionsinstruktion des U8001.
; Prueft insbesondere den Vorzeichenwechsel (0x8000 -> 0x7FFF).
;
; Erwartung: RESULT1 = 0x7FFF, STATUS = 0x0001
;
; (c) 2026 Olaf Krieger - MIT Lizenz

    ORG  0x0000

    ; Z8001 Reset-Vektor (FCW=0xC000: Segmented + System Mode)
    DW   0x0000, 0xC000, 0x0000, 0x0040

    ; Mailbox (0x0008 - 0x003F)
    DS   56

    ; Code ab 0x0040
    LD   R15, #0xFFF0       ; SP
    LD   R1, #0x8000
    SUB  R1, #0x0001
    ; R1 = 0x7FFF -> Mailbox
    LD   R3, #0x0012        ; RESULT1 offset
    LD   @R3, R1
    ; STATUS = OK
    LD   R1, #0x0001
    LD   R3, #0x0010
    LD   @R3, R1
    JR   T, $
