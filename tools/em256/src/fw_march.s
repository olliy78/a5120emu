! fw_march.s - D1: March-C DRAM-Test, eine Phase je Aufruf, Fehlerakkumulation
!
! U8001-Firmware fuer EM256FUL (A5120.16), Gruppe D.  Assembler: z8kasm aus
! a5120emu (NICHT z8001asm.py - der kodiert segmentierte Spruenge einwortig und
! sprang so nach Segment 1).  Segmentiert, System (FCW %C000).
!
! Segmentwahl (Plan 17 Par. 7.3): die Karte sieht die Segmentnummer SN0/SN1 des
! U8001 NUR in Segmentweichen-Mode 2 (A33 Bit 7/6 = 11).  Nach RESET16 ist A33
! geloescht (Mode 0, Segment aus A33 Bit 5/6 = 0).  Die Firmware schaltet daher
! als erstes per Wort-OUT auf Mode 2 (OUT %0080 schreibt A33 <- low und
! A35 <- high gleichzeitig) - danach liegen Code und Mailbox in <<0>>, das
! Pruefsegment wird ueber das Registerpaar RR8 (R8 = Segmentwort) erreicht.
!
! Bei Fehlern wird NICHT abgebrochen, sondern eine Bitmaske der fehlerhaften
! Datenleitungen (XOR Soll/Ist) akkumuliert.
!   Phase 1: aufwaerts %0000 schreiben
!   Phase 2: aufwaerts lesen %0000, %FFFF schreiben
!   Phase 3: aufwaerts lesen %FFFF
!   Phase 4: abwaerts  lesen %FFFF, %0000 schreiben
!   Phase 5: abwaerts  lesen %0000
!
! Eingabe (Mailbox, Segment 0):
!   000A PARAM1 = Segmentwort (%0000, %0100, %0200, %0300)
!   000C PARAM2 = Phase (1..5)
! Pruefbereich: Segment 0 ab STAPEL (Code, Mailbox, Stapel bleiben), sonst 0..FFFE.
! Ausgabe:
!   0010 STATUS  = %A00p waehrend der Phase, dann %0001 (OK) / %0002 (Fehler)
!   0012 RESULT1 = Fehlermaske, 0014 RESULT2 = Anzahl Fehlworte,
!   0016 RESULT3 = Offset der ersten Fehlerstelle (%FFFF = keine),
!   0018 RESULT4 = zuletzt bearbeiteter Offset (Fortschritt, alle 4 KB)
! Ende: uI (= /TRQ8) abwarten, MSET - der U880 uebernimmt und setzt RESET16.
!
! Register: R1 Soll, R2 Fehlermaske, R3 Start, R4 Ende, R5 gelesen, R6 neues
! Muster, RR8 Pruefadresse, R10 Phase, R12 erste Fehlerstelle, R13 Fehleranzahl,
! RR14 Stapel (v1.x benutzte R14 als Datenregister - der Stapel lag dann in Segment 3).
!
! (c) 2026 Olaf Krieger - MIT Lizenz

  SEG
  ORG <<0>>%0000
  DW %0000,%C000,%0000,%0040

  ORG <<0>>%0040
START:
  LDL RR14,#STAPEL           ! Stapel (CALR) in Segment 0, hinter dem Code
  LD R0,#%00C0               ! A33 = %C0: Segmentweiche Mode 2 (SN0/SN1)
  OUT %0080,R0               ! A35 = %00
  CLR R2
  CLR R13
  LD R12,#%FFFF
  LD R8,<<0>>%000A           ! Segmentwort
  LD R10,<<0>>%000C          ! Phase
  LD R1,#%A000               ! Laufstatus %A00p
  OR R1,R10
  LD <<0>>%0010,R1
  LD R3,#%0000
  TEST R8
  JR NZ,SETUP
  LD R3,#OFF(STAPEL)         ! Segment 0: hinter Code, Mailbox und Stapel
SETUP:
  LD R4,#%FFFE
  LD <<0>>%0018,R3
  CP R10,#1
  JR EQ,DO_P1
  CP R10,#2
  JR EQ,DO_P2
  CP R10,#3
  JR EQ,DO_P3
  CP R10,#4
  JR EQ,DO_P4
  CP R10,#5
  JP EQ,DO_P5
  JP DONE                    ! ungueltige Phase

! --- Phase 1 ------------------------------------------------------------------
DO_P1:
  CLR R1
  LD R9,R3
P1_LOOP:
  CALR HERZ
  LD @RR8,R1
  CP R9,R4
  JP EQ,DONE
  INC R9,#2
  JR P1_LOOP

! --- Phase 2 ------------------------------------------------------------------
DO_P2:
  CLR R1
  LD R6,#%FFFF
  LD R9,R3
P2_LOOP:
  CALR HERZ
  LD R5,@RR8
  CALR PRUEF
  LD @RR8,R6
  CP R9,R4
  JP EQ,DONE
  INC R9,#2
  JR P2_LOOP

! --- Phase 3 ------------------------------------------------------------------
DO_P3:
  LD R1,#%FFFF
  LD R9,R3
P3_LOOP:
  CALR HERZ
  LD R5,@RR8
  CALR PRUEF
  CP R9,R4
  JP EQ,DONE
  INC R9,#2
  JR P3_LOOP

! --- Phase 4 ------------------------------------------------------------------
DO_P4:
  LD R1,#%FFFF
  CLR R6
  LD R9,R4
P4_LOOP:
  CALR HERZ
  LD R5,@RR8
  CALR PRUEF
  LD @RR8,R6
  CP R9,R3
  JP EQ,DONE
  DEC R9,#2
  JR P4_LOOP

! --- Phase 5 ------------------------------------------------------------------
DO_P5:
  CLR R1
  LD R9,R4
P5_LOOP:
  CALR HERZ
  LD R5,@RR8
  CALR PRUEF
  CP R9,R3
  JP EQ,DONE
  DEC R9,#2
  JR P5_LOOP

! PRUEF - R5 (gelesen) gegen R1 (Soll); Fehler in R2/R13/R14 verbuchen
PRUEF:
  XOR R5,R1
  RET Z
  OR R2,R5
  TEST R13
  JR NZ,PR1
  LD R12,R9                  ! erste Fehlerstelle
PR1:
  INC R13,#1
  RET

! HERZ - Fortschritt alle 4 KB nach RESULT4
HERZ:
  LD R5,R9
  AND R5,#%0FFF
  RET NZ
  LD <<0>>%0018,R9
  RET

! --- Ergebnis -----------------------------------------------------------------
DONE:
  LD R1,#%0001
  TEST R13
  JR Z,WR_STATUS
  LD R1,#%0002
WR_STATUS:
  LD <<0>>%0012,R2           ! RESULT1 = Fehlermaske
  LD <<0>>%0014,R13          ! RESULT2 = Fehleranzahl
  LD <<0>>%0016,R12          ! RESULT3 = erste Fehlerstelle
  LD <<0>>%0018,R9           ! RESULT4 = letzter Offset
  LD <<0>>%0010,R1           ! STATUS zuletzt (der U880 wertet ihn aus)

! --- Uebergabe an den U880: uI abwarten, MSET -> TREN -> BUSRQ/BUSAK -> A29 ---
FERTIG:
  MBIT                       ! S = 1: uI H = keine Anforderung
  JR MI,FERTIG
  MSET
HALTEN:
  JR HALTEN                  ! der U880 setzt RESET16

  EVEN
  DS 64                      ! Stapelbereich (Nullen, mitgeladen)
STAPEL:
  END
