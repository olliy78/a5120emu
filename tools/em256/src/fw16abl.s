! fw16abl.s - U8001-Teil von EM16ABL (A5120.16, a5120emu Plan S4 / Vorlage G2)
!
! Assembler: z8kasm aus a5120emu (NICHT z8001asm.py - der kodiert segmentierte
! Spruenge einwortig).  Segmentiert, System.  Der U880 legt das Abbild ab Zelle 0
! in ALLE VIER Segmente (Segmentweiche Mode 0/1 holt Befehle auch aus 1..3) und
! biegt in Segment 1..3 den Resetvektor auf FALSCH um.
!
! Speicher (Segment 0):
!   0000  Resetvektor (FCW %C000, PC <<0>>%0100)
!   0080  Mailbox (s. EM16ABL.MAC, MB_*)
!   0100  Programm, Phasen A..F, dazwischen Uebergabe an den U880 (UEBERG)
!   0460  SC-, VI-, NVI-Handler, 0500 FALSCH (hinter der PSA)
!   0400  Program Status Area
!   0F00  Segmentmarken (0F10 = %B0s0 vom U880 je Segment s vorbelegt)
!   0FF0  Systemstapel (abwaerts)
!
! Belegt: Plan Par. 7.3/7.4 - A33/A35 nur ueber Standard-E/A mit AD7 (hier %0080),
! Wort-OUT schreibt beide, Status-8 nur im oberen Byte eines Wort-IN; Rueckweg
! in den 8-Bit-Mode nur ueber MSET (TREN = not u0).  INT-16 = A33 Bit 4.

  SEG
  ORG <<0>>%0000
  DW %0000,%C000,%0000,%0100

  ORG <<0>>%0100
START:
  LDL RR14,#<<0>>%0FF0
  CLR R0
  LDCTL PSAPSEG,R0
  LD R0,#%0400
  LDCTL PSAPOFF,R0
  LD R0,#%5334               ! Kennung "S4": Start aus dem richtigen Vektor
  LD <<0>>%008C,R0

! --- Phase A: Rechnen, A33/A35, Status-8 -------------------------------------
  LD R0,#1
  LD <<0>>%00BE,R0
  LDCTL R0,FCW
  LD <<0>>%0080,R0           ! FCW nach Reset (erwartet %C000)
  CLR R1
  LD R2,#100
A1:
  ADD R1,R2
  DJNZ R2,A1
  LD <<0>>%0082,R1           ! Summe 1..100 = %13BA
  LD R3,#1234
  MULT RR2,#567              ! RR2 = %000AAD1E
  LDL <<0>>%0084,RR2
  LD R0,#%A505               ! A35 <- %A5, A33 <- %05 (PIOA-0..2 = 101)
  OUT %0080,R0
  IN R0,%0080                ! Status-8 im oberen Byte
  LD <<0>>%0088,R0
  CLR R0
  INB RL0,%0080              ! gerade Adresse: Byte von AD8-15 = Status-8 (am Geraet
  LD <<0>>%008A,R0           ! 2026-09-29 C3H; der U8001 waehlt die Haelfte nach A0)
  CLR R0
  INB RL0,%0081              ! ungerade: AD0-7, die niemand treibt - sie halten die
                             ! Portadresse (am Geraet 2026-09-29: 81H, a5120emu G5)
  LD <<0>>%00A6,R0
  CALL UEBERG

! --- Phase B: Segmentweiche, alle drei Modi -----------------------------------
  LD R0,#2
  LD <<0>>%00BE,R0
  ! Mode 0: Segment aus A33 Bit 5/6 - fuer ALLE Zugriffe (Befehle aus den Kopien)
  LD R1,#%A001
  LD R0,#%0020
  OUT %0080,R0
  LD <<0>>%0F00,R1
  LD R1,#%A002
  LD R0,#%0040
  OUT %0080,R0
  LD <<0>>%0F00,R1
  LD R1,#%A003
  LD R0,#%0060
  OUT %0080,R0
  LD <<0>>%0F00,R1
  ! Mode 1: SG0 = INSTR, SG1 = N/S.  LD = Daten, LDR = Programmspeicher.
  LD R0,#%0080
  OUT %0080,R0
  LD R2,<<0>>%0F10           ! System-Daten    -> Segment 0
  LDR R3,<<0>>%0F10          ! System-Befehl   -> Segment 1
  LD <<0>>%0098,R3
  LD <<0>>%009A,R2
  LD R0,#%8000               ! Normalmodus (segmentiert)
  LDCTL FCW,R0
  LD R4,<<0>>%0F10           ! Normal-Daten    -> Segment 2
  LDR R5,<<0>>%0F10          ! Normal-Befehl   -> Segment 3
  LD R6,#%B222
  LD <<0>>%0F02,R6           ! -> Segment 2
  SC #1                      ! zurueck in den Systemmodus (PSA-Lesen = Befehlsraum)
BWEIT:
  LD <<0>>%009C,R4
  LD <<0>>%009E,R5
  ! Mode 2: SG0/SG1 = SN0/SN1
  LD R0,#%00C0
  OUT %0080,R0
  LD R1,#%C002
  LD <<2>>%0F04,R1
  LD R1,#%C003
  LD <<3>>%0F04,R1
  LD R1,#%C005
  LD <<5>>%0F04,R1           ! SN = 5: SN0 = 1, SN1 = 0 -> Segment 1
  ! Mode 0, Segment 0 - und die G2-Messung: OUTB legt das Byte auf AD0-7 UND
  ! (vermutlich) AD8-15.  A33 <- %01 (harmlos), A35 <- ? (U880 liest AEH)
  LDB RL0,#%01
  OUTB %0081,RL0
  CALL UEBERG

! --- Phase C: VI vom U880 -----------------------------------------------------
  LD R0,#3
  LD <<0>>%00BE,R0
  CLR R9
  LD <<0>>%0090,R9
  EI VI
C1:
  LD R0,<<0>>%0090
  CP R0,#2
  JR ULT,C1
  DI VI
  CALL UEBERG

! --- Phase D: INT-16 zum U880 -------------------------------------------------
  LD R0,#4
  LD <<0>>%00BE,R0
  LD R0,#%0010               ! A33 Bit 4 = INT-16 -> PIO A4
  OUT %0080,R0
  CALL UEBERG
  CLR R0                     ! INT-16 zuruecknehmen
  OUT %0080,R0

! --- Phase E: Einzelbefehlszaehler A53 -> NVI ---------------------------------
  LD R0,#5
  LD <<0>>%00BE,R0
  CLR R0
  LD <<0>>%00A0,R0
  LD <<0>>%00A2,R0
  LD <<0>>%00A4,R0
  LD <<0>>%00A8,R0
  LDL RR12,RR14              ! Stapelzeiger merken
  ! Vorlauf (R7 = 0): A54 kann von einem frueheren Lauf noch freigeben (A53
  ! zaehlt dann jeden Stapelzugriff).  Ein NVI hier wird quittiert, waehrend
  ! A33 Bit 3 = 0 ist - das laedt A53 und haelt ihn geladen (Handbuch 1.12).
  ! v1.2: R4 VOR dem EI loeschen - am Geraet kam der NVI 2026-09-29 gleich beim
  ! EI (A54 gab nach Netz-Ein frei, QD war schon L), und R4 stand noch auf %B020
  ! aus Phase B.  Der Handler legt %8000 + Zahl der PUSH ab, 0 = kein NVI.
  CLR R7
  CLR R4
  EI NVI
E0:
  INC R4
  PUSH @RR14,R4
  CP R4,#20
  JR ULT,E0
EV:
  LDL RR14,RR12
  EI NVI
  LD R7,#1                   ! a) ohne A33 Bit 3: darf nie ausloesen
  CLR R4
E1:
  INC R4
  PUSH @RR14,R4
  CP R4,#20
  JR ULT,E1
  LDL RR14,RR12
  LD R7,#2                   ! b) mit A33 Bit 3
  LD R0,#%0008
  OUT %0080,R0
  CLR R4
E2:
  INC R4
  PUSH @RR14,R4              ! je PUSH ein Stapelzugriff (Status 9)
  CP R4,#40
  JR ULT,E2
  DI NVI                     ! kein NVI nach 40 Zugriffen
  CLR R0
  OUT %0080,R0
  LDL RR14,RR12
  LD R0,#%FFFF
  LD <<0>>%00A0,R0
E9:
  CALL UEBERG

! --- Phase F: STOP - Zaehler in A35, bis RESET16 ------------------------------
  LD R0,#6
  LD <<0>>%00BE,R0
  CLR R5
F1:
  INC R5
  LDB RH6,RL5
  CLRB RL6                   ! A33 = 0 mitfuehren
  OUT %0080,R6
  JR F1

! --- Uebergabe an den U880 ----------------------------------------------------
! Warten auf uI (= /TRQ8, aktiv L); MSET setzt u0 -> TREN -> BUSRQ -> BUSAK ->
! A29 kippt im naechsten M1 des U880.  Weiter geht es erst, wenn der U880 TRQ8
! zuruecknimmt (dann faellt BUSRQ und A29 sofort).
UEBERG:
U1:
  MBIT                       ! S = 1: uI H = keine Anforderung
  JR MI,U1
  MSET
U2:
  MBIT
  JR PL,U2
  MRES
  RET

! --- Program Status Area (Z8001: je Eintrag res, FCW, Seg, Off) ----------------
  ORG <<0>>%0400+%1A
  DW %C000,%0000,SCH&%FFFF
  ORG <<0>>%0400+%32
  DW %C000,%0000,NVIH&%FFFF
  ORG <<0>>%0400+%3A
  DW %C000
  ORG <<0>>%0400+%3C+2*4     ! Vektor 4
  DW %0000,VIH&%FFFF
  ORG <<0>>%0400+%3C+2*6     ! Vektor 6
  DW %0000,VIH&%FFFF
  ORG <<0>>%0400+%3C+2*8     ! Ende der Tabelle
  DW %0000
! --- Handler ------------------------------------------------------------------
  ORG <<0>>%0460             ! hinter der PSA (v1.1: Hauptteil reicht ueber %0300)
SCH:                         ! SC: Rahmen (4 Worte) verwerfen
  ADD R15,#8
  JP BWEIT

VIH:                         ! VI: Kennung (Vektor low + Status-8 high) ablegen
  LD R10,@RR14
  LD R11,<<0>>%0090
  ADD R11,R11
  LD <<0>>%0092(R11),R10
  LD R11,<<0>>%0090
  INC R11
  LD <<0>>%0090,R11
  IRET

NVIH:                        ! NVI: A33 Bit 3 aus, Zaehlerstand ablegen
  CP R7,#0
  JR NE,NVI1
  SET R4,#15                 ! Vorlauf: %8000 + Stapelzugriffe vermerken,
  LD <<0>>%00A8,R4           ! weiter mit Teil a
  JP EV
NVI1:
  CLR R0
  OUT %0080,R0
  LD <<0>>%00A0,R4
  LD R0,<<0>>%00A2
  INC R0
  LD <<0>>%00A2,R0
  LD <<0>>%00A4,R7
  LDL RR14,RR12
  JP E9

  ORG <<0>>%0500             ! em16abl.mac: FALSCH EQU 0500H
FALSCH:                      ! Resetvektor der Segmente 1..3 zeigt hierher
  LD R0,#%DEAD
  LD <<0>>%008C,R0
  HALT

  END
