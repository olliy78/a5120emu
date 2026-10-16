# Demo-Bootband für den A5120 (Eigenbau)

`bandtest_boot.k52` ist **kein Robotron-Band**, sondern ein kleines selbst geschriebenes
Startprogramm (Quelle `tools/kassette/bandtest/`): das Lade-ROM des A5120 lädt es ohne Diskette von
der Kassette, das Programm schreibt `KASSETTE OK` in die erste Bildschirmzeile und bleibt stehen.

1. *Einstellungen ▸ Allgemein ▸ Kassette* einschalten (Modell A5120).
2. Im Kasten *Kassette* das Band in **Gerät 1** einlegen, **keine** Diskette in A:.
3. Kaltstart.  Das Lade-ROM probiert zuerst die Laufwerke durch und liest dann vom Band; nach gut
   zehn Sekunden steht `KASSETTE OK` da.

Das Band ist eine Kopie von `tests/fixtures/kassette/bandtest_boot.k52` (`k1520tape bootband`).
