; fw_stack.s - C6: Stack-Test (PUSH/POP)
;
; Z8001-Firmware fuer EM256 Testprogramm
; Testet die Stack-Operationen des U8001 (PUSH/POP) auf LIFO-Korrektheit.
; Pusht 0xBEEF und 0xCAFE, poppt in umgekehrter Reihenfolge zurueck.
;
; Erwartung: RESULT1 = 0xBEEF, RESULT2 = 0xCAFE, STATUS = 0x0001
;
; (c) 2026 Olaf Krieger - MIT Lizenz

    ORG  0x0000

    ; Z8001 Reset-Vektor (FCW=0xC000: Segmented + System Mode)
    DW   0x0000, 0xC000, 0x0000, 0x0040

    ; Mailbox
    DS   56

    ; Code ab 0x0040
    LD   R15, #0xFFF0       ; SP (Stack waechst nach unten)

    LD   R1, #0xBEEF
    LD   R2, #0xCAFE
    PUSH @R15, R1           ; Push 0xBEEF
    PUSH @R15, R2           ; Push 0xCAFE

    ; Pop in umgekehrter Reihenfolge
    POP  R3, @R15           ; Pop -> R3 = 0xCAFE (LIFO)
    POP  R4, @R15           ; Pop -> R4 = 0xBEEF (LIFO)

    ; Pruefen
    CP   R4, R1             ; R4 soll 0xBEEF sein
    JR   NZ, FAIL
    CP   R3, R2             ; R3 soll 0xCAFE sein
    JR   NZ, FAIL

    ; Ergebnis -> Mailbox
    LD   R5, #0x0012
    LD   @R5, R4            ; RESULT1 = 0xBEEF
    INC  R5, #2
    LD   @R5, R3            ; RESULT2 = 0xCAFE
    ; STATUS = OK
    LD   R1, #0x0001
    LD   R5, #0x0010
    LD   @R5, R1
    JR   T, $

FAIL:
    LD   R5, #0x0010
    LD   R6, #0x0002        ; STATUS = FEHLER
    LD   @R5, R6
    INC  R5, #2
    LD   @R5, R4            ; RESULT1 = was als 0xBEEF rauskam
    INC  R5, #2
    LD   @R5, R3            ; RESULT2 = was als 0xCAFE rauskam
    JR   T, $
