(* Function writes 4digit hexadecimal number
   call: Write(I_Hex(xxx)   => '0AF3h'
   works under TURBO 3.0  (V.2.0: CD 048D)
 *)
      Function I_Hex (datint:integer):Char;
      begin
        Inline($CD/$48D);
        I_hex:=Chr($68);
      end;
{ -------------------------------------------------------------------- }

      PROCEDURE ReadSect(VAR buf : iobuf);  { read 1 sector = 128 byte }
                                            { into array buf           }
      {$A+}  {generates absolut code}

        BEGIN INLINE (
        $2A/sector/     { ld HL, (sector) }
        $3A/RFctrl/     { ld A, (RFctrl) }
        $4F/            { ld C, A    }
        $44/            { ld B, H }
        $ED/$69/        { OUT (C), L }
        $06/$7F/        { ld B, 127d }
        $0D/            { dec C      }
        $2A/buf/        { ld HL, (buf) }
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/   {8*INI}
    $ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2/$ED/$A2)   {8*INI}
        END;   { ReadSect }
{ -------------------------------------------------------------------- }

      PROCEDURE WritSect(VAR buf : iobuf);  { write 1 sector = 128 byte }
                                            { from array buf            }
      {$A+}  {generates absolut code}

        BEGIN INLINE (
        $2A/sector/     { ld HL, (sector) }
        $3A/RFctrl/     { ld A, (RFctrl) }
        $4F/            { ld C, A    }
        $44/            { ld B, H }
        $ED/$69/        { OUT (C), L }
        $06/$80/        { ld B, 128d }
        $0D/            { dec C      }
        $2A/buf/        { ld HL, (buf) }
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/  {8*OUTI}
    $ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3/$ED/$A3)  {8*OUTI}
        END;   { WritSect }
{ -------------------------------------------------------------------- }

      PROCEDURE bank_paint(bank : BYTE; errPat : INTEGER);

        VAR
        x,y,mask : INTEGER;
        i : BYTE;
        BEGIN
          y := 4 * (bank DIV 2) + 3;
          mask := 1;

          CASE bank OF

        0..7:  BEGIN
               FOR i := 0 TO 7 DO
                 BEGIN
                 IF bank IN [1,3,5,7] THEN x := 73 - 4*i  ELSE  x := 4*i + 6;
                 IF errPat AND mask = 0 THEN
                   BEGIN
                   GotoXY(x,y);    Write('[~]');
                   GotoXY(x,y+1);  Write('|',i:1,'|');
                   GotoXY(x,y+2);  Write('[_]')
                   END
                 ELSE
                   BEGIN
                   GotoXY(x,y);    Write('XXX');
                   GotoXY(x,y+1);  Write('XXX');
                   GotoXY(x,y+2);  Write('XXX')
                   END;
                 mask := 2 * mask
                 END; { for }
               IF bank IN [0,2,4,6] THEN GotoXY(5,y+1) ELSE GotoXY(76,y+1);
               Write(bank+1:1);
               IF bank IN [1,3,5,7] THEN GotoXY(5,y+5) ELSE GotoXY(76,y+1)
               END;

           8:  FOR i := 0 TO 13 DO
                 BEGIN
                 x := 4*i + i DIV 2 + (i+1) DIV 8 + 6;
                 IF i > 12 THEN x := x + 6;
                 IF errPat AND mask = 0 THEN
                   BEGIN
                   GotoXY(x,y);    Write('[~]');
                   GotoXY(x,y+1);  Write('| |');
                   GotoXY(x,y+2);  Write('[_]')
                   END
                 ELSE
                   BEGIN
                   GotoXY(x,y);    Write('XXX');
                   GotoXY(x,y+1);  Write('XXX');
                   GotoXY(x,y+2);  Write('XXX')
                   END;
                 mask := 2 * mask
                 END;  { for }

           9:  FOR i := 0 TO 1 DO
                 BEGIN
                 x := 12*i + 52;
                 GotoXY(x,23);
                 IF errPat AND mask = 0 THEN  Write('[|||||]')
                                        ELSE  Write('XXXXXXX');
                 mask := 2 * mask
                 END  { for }
          END { case }
        END;  { bank_paint }
{ -------------------------------------------------------------------- }

      PROCEDURE empty_bank(bank : BYTE);

        VAR
        x,y : INTEGER;
        i : BYTE;
        BEGIN
          y := 4 * (bank DIV 2) + 3;

          CASE bank OF

        0..7:  FOR i := 0 TO 7 DO
                 BEGIN
                 IF bank IN [1,3,5,7] THEN x := 73 - 4*i  ELSE  x := 4*i + 6;
                 GotoXY(x,y);    Write(':::');
                 GotoXY(x,y+1);  Write(': :');
                 GotoXY(x,y+2);  Write(':::')
                 END
          END  { case }
        END; { empty_bank }

{ -------------------------------------------------------------------- }

      PROCEDURE Frame;

        VAR i: BYTE;

        BEGIN
          ClrScr;
          Write(' ------------------------------------------------------------------------------');

          FOR i := 2 TO 23 DO
            BEGIN
            GotoXY( 1,i);  Write('|');
            GotoXY(80,i);  Write('|')
            END;

          Write(' ___!!!!!!!!!!!!!!!!!!!!!!!!!!!_____^^^^^^^^___________________________________');

          FOR i := 0 TO 7 DO
          IF i>maxbank THEN  empty_bank(i) ELSE bank_paint(i, 0);
          bank_paint(8, 0);  bank_paint(9, 0);

          GotoXY(39,4); Write('(C)');
          GotoXY(39,5); Write('JWG');
          GotoXY(38,6); Write('03/86');

          LowVideo;
          GotoXY(37,02);  Write('RAF',SecInBank:5);
          GotoXY(54,02);  Write((SecInBank DIV 8):2,' K - chips');
          GotoXY(38,08);  Write('D-Lauf');
          GotoXY(38,10);  Write('Fehler');
          GotoXY(38,13);  Write('A-Lauf');
          GotoXY(38,15);  Write('Fehler');
          GotoXY(19,22);  Write(I_hex(RFctrl));
          GotoXY(16,22);  Write(I_hex(RFdata));
          GotoXY(19,23);  Write('I/O');
          NormVideo;
          GotoXY(16,22);  Write('  ');
          GotoXY(20,22);  Write(',')

        END;  { Frame }
{ -------------------------------------------------------------------- }

                                                                                                                              