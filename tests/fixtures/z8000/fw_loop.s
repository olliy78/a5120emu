; fw_loop.s - C5: Schleifentest (256x INC)
;
; Z8001-Firmware fuer EM256 Testprogramm
; Testet Schleifen-Steuerung: INC, DEC, CP und bedingten Sprung.
; Zaehlt R1 in 256 Iterationen hoch, Ergebnis R1 = 0x0100.
;
; Erwartung: RESULT1 = 0x0100, STATUS = 0x0001
;
; (c) 2026 Olaf Krieger - MIT Lizenz

    ORG  0x0000

    ; Z8001 Reset-Vektor (FCW=0xC000: Segmented + System Mode)
    DW   0x0000, 0xC000, 0x0000, 0x0040

    ; Mailbox
    DS   56

    ; Code ab 0x0040
    LD   R15, #0xFFF0
    LD   R1, #0x0000        ; Summe
    LD   R2, #0x0100        ; Zaehler = 256

LOOP:
    INC  R1, #1
    DEC  R2, #1
    CP   R2, #0x0000
    JR   NZ, LOOP

    ; Ergebnis -> Mailbox
    LD   R3, #0x0012        ; RESULT1
    LD   @R3, R1
    ; STATUS = OK
    LD   R1, #0x0001
    LD   R3, #0x0010
    LD   @R3, R1
    JR   T, $
