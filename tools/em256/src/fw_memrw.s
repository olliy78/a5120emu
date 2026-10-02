! fw_memrw.s - C4: Speicher R/W ueber Registerpaar
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
! Vier Muster an %0800..%0806 schreiben und sofort zuruecklesen, danach %0800
! gegenlesen.  Zeiger RR2 (R2 = Segmentwort, R3 = Offset).
! Erwartung: STATUS = %0001, RESULT1 = %A55A; sonst STATUS = %0002,
! RESULT1 = Soll, RESULT2 = Ist, RESULT3 = Offset.
! (c) 2026 Olaf Krieger - MIT Lizenz

  SEG
  ORG <<0>>%0000
  DW %0000,%C000,%0000,%0040

  ORG <<0>>%0040
START:
  LDL RR14,#<<0>>%FFF0
  LDL RR2,#<<0>>%0800               ! hinter dem Code (v1.x: %0100)
  LD R1,#%A55A
  LD @RR2,R1
  LD R4,@RR2
  CP R4,R1
  JR NZ,FAIL
  INC R3,#2
  LD R1,#%5AA5
  LD @RR2,R1
  LD R4,@RR2
  CP R4,R1
  JR NZ,FAIL
  INC R3,#2
  LD R1,#%FFFF
  LD @RR2,R1
  LD R4,@RR2
  CP R4,R1
  JR NZ,FAIL
  INC R3,#2
  LD R1,#%0000
  LD @RR2,R1
  LD R4,@RR2
  CP R4,R1
  JR NZ,FAIL
  LD R3,#%0800               ! Gegenlesen
  LD R4,@RR2
  LD R1,#%A55A
  CP R4,R1
  JR NZ,FAIL
  LD <<0>>%0012,R4           ! RESULT1 = letzter gelesener Wert
  LD R0,#%0001              ! STATUS = OK
  LD <<0>>%0010,R0
  JR FERTIG
FAIL:
  LD R0,#%0002               ! STATUS = FEHLER
  LD <<0>>%0010,R0
  LD <<0>>%0012,R1           ! Soll
  LD <<0>>%0014,R4           ! Ist
  LD <<0>>%0016,R3           ! Offset
  JR FERTIG

! --- Uebergabe an den U880: uI abwarten, MSET -> TREN -> BUSRQ/BUSAK -> A29 ---
FERTIG:
  MBIT                       ! S = 1: uI H = keine Anforderung
  JR MI,FERTIG
  MSET
HALTEN:
  JR HALTEN                  ! der U880 setzt RESET16
  END
