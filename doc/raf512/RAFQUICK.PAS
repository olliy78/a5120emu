
      Program RAFQUICK (RFS);       { for TURBO V3 }

{ (C) 1986 ZWG Berlin }

   { RFS testet die RAM-Floppy-Platinen RAF 128 und RAF 512 auf defekte  }
   { Speicherchips und fehlerhafte, d.h. verbundene oder geerdete        }
   { Adressenleitungen.                                                  }

   { Der Test ist zerstoerungsfrei, d.h., der alte Speicherinhalt bleibt }
   { erhalten.                                                           }

   { Waehrend des Laufes unterbricht ein beliebiger Tastendruck; ein     }
   { weiterer setzt den Test fort. X, Q oder ^C beenden das Programm.    }

   { RFctrl und RFdata sind die Hardwareadressen der Platine. RFdata wird}
   { abgefragt. Definitionsgemaess gilt immer: RFctrl := RFdata + 1      }

   { Die moeglichen und erlaubten Daten-Port Adressen sind im Feld       }
   { RFdataPort festgehalten und muessen dort gegebenenfalls geaendert   }
   { werden. MaxPort definiert die Anzahl der Eintragungen in RFdataPort }
   { Standard-Testadresse ist $20. }

   { Mit SecInBank wird RAF 128 - Test oder RAF 512 - Test ausgewaehlt:  }

      CONST
      SecInBank  = 512;  { 128 for 16-K chips, 512 for 64-K chips        }

      MaxPort    = 5;    { Anzahl der Port Adressen in Feld RFdataPort   }
                         { Standard RAF data port addresses: }

      RFdataPort : ARRAY[1..MaxPort] OF BYTE = ($90,$88,$8A,$8C,$8E);

      adrTestRepeat = 1; { Anzahl der aufeinanderfolgenden Adressentests }
                         { jeder Test mit anderen Zufallsdaten ! }

      head1 = '                   RAF';
      head2 = ' - Test : zerstoerungsfrei !';
      head_ = '                   -----------------------------------';

      error_D : INTEGER = 0;
      error_A : INTEGER = 0;
      test_D  : INTEGER = 0;
      test_A  : INTEGER = 0;
      xc : CHAR = ' ';
      KeyFlag : BOOLEAN = false;

      D301 = $01;      { chip addresses in "bank 8" }
      D302 = $02;                 { " }
      D303 = $04;                 { " }
      D305 = $10;                 { " }
      D306 = $20;                 { " }
      D300 = $40;                 { " }
      D309 = $80;                 { " }
      D307 = $100;                { " }
      D308 = $200;                { " }

      TYPE
      iobuf = ARRAY[0 .. 127] OF BYTE;

      VAR
      Rbuf_A, Wbuf_A, Rbuf_B, Wbuf_B, Savebuf_A, Savebuf_B  : iobuf;
      bank0_7errors : ARRAY[0 .. 7] OF BYTE;
      maxbank,sector,maxsec,b8err,adr7_err : INTEGER;
      RFdata,RFctrl,errorflag,secterr,bankerr,buserr,adr0_6err,l : BYTE;
{ ******************************************************************** }

{$I RAFQUICK.INC }

      PROCEDURE TestKey;
      BEGIN
      IF KeyPressed THEN 
      BEGIN Read(Kbd,xc); KeyFlag := true; GotoXY(1,1); Write('#') END
      END;  { TestKey; }
{ -------------------------------------------------------------------- }

      PROCEDURE exmonitor;

      BEGIN
        IF xc IN ['x','X','q','Q',^C] THEN BEGIN GotoXY(01,24); halt END;
        IF KeyFlag THEN
        BEGIN
        GotoXY(01,01);
        REPEAT UNTIL KeyPressed; Read(Kbd,xc);
        IF xc IN ['x','X','q','Q',^C] THEN BEGIN GotoXY(01,24); halt END;
        KeyFlag := false; Write(' ')
        END
      END;  { exmonitor }
{ -------------------------------------------------------------------- }

      Function Basic_Tests : BYTE;

{ first test: read RAF; complement, write & check; restore old contens }

      VAR bank, l  : BYTE;
          FirstSec : INTEGER;

      BEGIN
        GotoXY(36,23); LowVideo; Write('Data Check'); NormVideo;
        test_D := test_D + 1;
        GotoXY(39,09); Write(test_D:4);
        GotoXY(05,04);

        FOR bank := 0 TO maxbank DO
          BEGIN
          bankerr  := 0;
          FirstSec := bank*SecInBank;

          FOR sector :=  FirstSec TO  FirstSec+SecInBank-1 DO

              BEGIN INLINE (
                $2A/sector/     { ld HL,(sector)    ;it iterates from 0 up to 8x512 if all chips will be tested    }
                $3A/RFctrl/     { ld A, (RFctrl)    ;RFctrl is the configured RFdata adress +1}
                $4F/            { ld C, A           ;RFctl is now in C  }
                $44/            { ld B, H           ;The high byte of sector loaded to B - should be 00h}
                $ED/$69/        { OUT (C), L        ;output the low byte of sector to the address RFctl}
                $06/$7F/        { ld B, 127d        ; load 127d to B}
                $0D/            { dec C             ;decrement C now it is RFdata}
                $AF/            { xor A             ;clear A}
                $5F/            { ld E, A           ;E now 00h}
                $ED/$78/        { L: IN A, (C)      ;input RFdata to A}
                $67/            {    ld H, A        ;load data to H}
                $2F/            {    cpl            ;invert A}
                $ED/$79/        {    OUT (C), A     ;output the inverted data to RFdata}
                $ED/$50/        {    IN  D, (C)     }
                $AA/            {    xor D   }
                $B3/            {    or  E   }
                $5F/            {    ld E, A }
                $ED/$61/        {    OUT (C), H }
                $10/$F1/        {    djnz L  }
                $ED/$78/        { IN A, (C)  }
                $67/            { ld H, A    }
                $2F/            { cpl        }
                $ED/$79/        { OUT (C), A }
                $ED/$50/        { IN  D, (C) }
                $AA/            { xor D      }
                $B3/            { or  E      }
                $5F/            { ld E, A    }
                $ED/$61/        { OUT (C), H }
                $3A/bankerr/    { ld A, (bankerr) }
                $B3/            { or E       }
                $32/bankerr)    { ld (bankerr), A }
              END;  { INLINE }

          bank0_7errors[bank] := bankerr;
          errorflag := errorflag OR bankerr;
          TestKey;
          exmonitor;
          bank_paint(bank, bankerr)
          END;  { for bank }

        buserr  := $FF;
        FOR l := 0 TO maxbank DO buserr := buserr AND bank0_7errors[l];

        IF buserr AND $0F <> 0 THEN b8err := b8err OR D302;
        IF buserr AND $F0 <> 0 THEN b8err := b8err OR D303;
        bank_paint(8, b8err);

        Basic_Tests := errorflag;

        IF errorflag <> 0 THEN  error_D := error_D + 1 ;
        GotoXY(39,11); Write(error_D:4)

      END;  { Basic_Tests }
{ ------------------------------------------------------------------- }

      PROCEDURE TestADR;

      VAR l,m,n,p : BYTE;
          i,j : INTEGER;

      BEGIN

        test_A := test_A + 1;
        GotoXY(39,14); Write(test_A:4);

        adr0_6err := 0;      { address error bits 0 .. 6       }
        adr7_err  := 0;      { address error bits 7 .. 16 (18) }
        b8err     := 0;      { bank 8  error bits }

        FOR l := 0 TO 127 DO
          BEGIN  Wbuf_A[l] := Random(128); Wbuf_B[l] := l END;

        sector := 0;   ReadSect(Savebuf_A);

        WritSect(Wbuf_B); ReadSect(Rbuf_B);

        p := 1; n := Rbuf_B[0] + 1;
        FOR l := 1 TO 127 DO         { check and count the OK-sequences }
          BEGIN                      { in one (first) sector            }
            m := Rbuf_B[l];
            IF m = n THEN p := p + 1
            ELSE BEGIN adr0_6err := adr0_6err OR p; p := 1 END;
            n := m + 1
          END;
        p := p AND $7F; adr0_6err := adr0_6err OR p;
        errorflag := adr0_6err;

        WritSect(Savebuf_A);
        TestKey;

        GotoXY(19,18);
        i := 1;
        FOR l := 0 TO 6 DO
          BEGIN
            IF (adr0_6err AND i) <> 0 THEN Write(l:3) ELSE Write('   ');
            i := 2*i
          END;

        j := 0; i := 1;
        WHILE j <= maxsec DO
          BEGIN
            sector := j;
            ReadSect(Savebuf_B); WritSect(Wbuf_B); ReadSect(Rbuf_B);
              WHILE i <= maxsec DO
              BEGIN
                sector := i;  ReadSect(Savebuf_A); WritSect(Wbuf_A);
                sector := j;  ReadSect(Rbuf_A);    WritSect(Wbuf_B);
                sector := i;  WritSect(Savebuf_A);

                FOR l := 0 TO 127 DO
                IF Rbuf_B[l] <> Rbuf_A[l] THEN adr7_err := adr7_err OR i;

                i := 2*i
              END;  { while i }

            sector := j;   WritSect(Savebuf_B);
            IF j = 0 THEN j := 1 ELSE j := 2*j;
            i := 2*j
          END;  { while j }

        IF adr7_err <> 0 THEN errorflag := $FF;
        TestKey;

        GotoXY(40,18);
        i := 1;
        FOR l := 7 TO 18 DO
          BEGIN
            IF (adr7_err AND i) <> 0 THEN Write(l:3) ELSE Write('   ');
            i := 2*i
          END;

        IF (adr0_6err AND $03) <> 0 THEN b8err := b8err OR D305;
        IF (adr0_6err AND $0C) <> 0 THEN b8err := b8err OR D306;
        IF (adr0_6err AND $30) <> 0 THEN b8err := b8err OR D307;
        IF (adr0_6err AND $40) <> 0 THEN b8err := b8err OR D308;

        IF SecInBank = 512 THEN i := adr7_err DIV 2 ELSE i := adr7_err;
        IF ((i XOR adr0_6err) AND i AND $7F) <> 0 THEN b8err := b8err OR D301;

        IF SecInBank = 512 THEN
        BEGIN
          i := adr7_err AND $101;
          IF i = $101 THEN b8err := b8err OR D308;
          IF i = $001 THEN b8err := b8err OR D301;
          IF i = $100 THEN b8err := b8err OR D300;
          IF (adr7_err AND $E00) <> 0 THEN b8err := b8err OR D300 OR D309
        END;

        IF SecInBank = 128 THEN
        BEGIN
          IF (adr7_err AND $080) <> 0 THEN b8err := b8err OR D301 OR D309;
          IF (adr7_err AND $300) <> 0 THEN b8err := b8err OR D300 OR D309
        END;

        bank_paint(8, b8err);

        IF errorflag <> 0 THEN  error_A := error_A + 1 ;
        GotoXY(39,16); Write(error_A:4);

        exmonitor

    END;  { TestADR; }
{ =================================================================== }

      BEGIN   { main }

        Writeln;Writeln; Writeln;
        Write(head1); Write(SecInBank:4); Writeln(head2); Writeln(head_);

        maxbank := 8;

        REPEAT
          maxbank := 8;
          Writeln;
          Write('Wieviel Baenke sind zu testen ? (1 .. 8), [<cr> = 8]  ');
          Readln(maxbank);
        UNTIL (maxbank > 0) AND (maxbank < 9);

        REPEAT
          Writeln; Writeln('Daten-Port Adressen:');
          Write('<cr>: ',I_Hex(RFdataPort[1]),'     ');
          FOR l := 2 TO MaxPort DO
          Write(l-1:1,': ',I_Hex(RFdataPort[l]),'     ');
          Writeln; Writeln; Write('select: ');
          l := 0; Readln(l);
        UNTIL (l >= 0) AND (l < MaxPort);
        RFdata  := RFdataPort[l+1];
        RFctrl  := RFdata + 1;

        maxsec  := maxbank * SecInBank - 1;
        maxbank := maxbank - 1;

        Frame;

        LowVideo;
        GotoXY( 4,18); Write('Adr-Fehlerbits:');
        NormVideo;
        GotoXY(18,02); Write('zerstoerungsfreier');
        GotoXY(46,02); Write('Test');

        REPEAT

          b8err := 0;
          errorflag := 0;

          IF Basic_Tests = 0 THEN

          BEGIN
          GotoXY(36,23); Write(' Test ADR ');
          FOR l := 1 TO adrTestRepeat DO TestADR     { repeat testADR n times }
          END;

        UNTIL test_D >= 9999;

      END.

                                                             