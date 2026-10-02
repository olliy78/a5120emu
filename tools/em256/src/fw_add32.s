! fw_add32.s - C7: 32-Bit-Addition (ADDL)
!
! U8001-Firmware fuer EM256FUL (A5120.16), Gruppe C.  Assembler: z8kasm aus
! a5120emu (NICHT z8001asm.py - der kodiert segmentierte Spruenge einwortig).
! Segmentiert, System (FCW %C000).  Der U880 laedt das Abbild ab Zelle 0 in
! Segment 0; RESET16 = 0 loescht A33 (Mode 0, Segment 0), der U8001 holt FCW/PC
! aus Segment 0 (Plan 17 Par. 7.4).
!
! Speicher (Segment 0):
!   0000  Resetvektor (FCW %C000, PC <<0>>%0040)
!   0008  Mailbox: 000A PARAM1, 000C PARAM2, 0010 STATUS, 0012..0018 RESULT1..4
!   0040  Programm;  FFF0 Stapel (RR14, abwaerts)
!
! Zeiger im segmentierten Modus nur als Registerpaar (@RRn), Mailbox mit
! direkter Adresse <<0>>.  Ende: auf uI (= /TRQ8) warten, MSET (TREN) - der U880
! uebernimmt im naechsten M1 den Bus und setzt danach RESET16.
!
! RR2 = %00010000 + %0000FFFF = %0001FFFF.
! Erwartung: RESULT1 = %0001 (oben), RESULT2 = %FFFF (unten), STATUS = %0001
! (c) 2026 Olaf Krieger - MIT Lizenz

  SEG
  ORG <<0>>%0000
  DW %0000,%C000,%0000,%0040

  ORG <<0>>%0040
START:
  LDL RR14,#<<0>>%FFF0
  LDL RR2,#%00010000
  LDL RR4,#%0000FFFF
  ADDL RR2,RR4
  LD <<0>>%0012,R2           ! RESULT1 = oberes Wort
  LD <<0>>%0014,R3           ! RESULT2 = unteres Wort
  LD R0,#%0001              ! STATUS = OK
  LD <<0>>%0010,R0
  JR FERTIG

! --- Uebergabe an den U880: uI abwarten, MSET -> TREN -> BUSRQ/BUSAK -> A29 ---
FERTIG:
  MBIT                       ! S = 1: uI H = keine Anforderung
  JR MI,FERTIG
  MSET
HALTEN:
  JR HALTEN                  ! der U880 setzt RESET16
  END
