# BANDTEST — Kassetten-Bootprogramm für das A5120-Lade-ROM (AP K7)

Kleinstes Programm, das das Lade-ROM (`doc/EPROMS/zre.prn` 027E–03AF) ohne Diskette von der
Kassette K5020/K5200 lädt: 4 × 128 B nach 0400H, `SYL` bei 0400H (Prüfung 01B6H), Einsprung
0434H.  Es löscht den Bildspeicher (F800H), schreibt `KASSETTE OK` in die erste Zeile und bleibt
stehen — zurückkehren darf es nicht (das ROM spränge nach 0177H).

```sh
python3 tools/kassette/bandtest/build.py            # Quelle → bandtest.bin (M80/LINKMT, CPA_TOOLS)
tools/dev.sh tool k1520tape bootband tools/kassette/bandtest/bandtest.bin tests/fixtures/kassette/bandtest_boot.k52
cp tests/fixtures/kassette/bandtest_boot.k52 /tmp/b.k52     # --tape BINDET die Datei (schreibt zurück)
tools/dev.sh trace --tape /tmp/b.k52 -c 100000000                  # bootet ohne Diskette
```

`bandtest.bin` und das Band sind eingecheckt.  Wächter: `cli_bandtest_bin_passt_zur_quelle`,
`cli_bandtest_bootband`, `KassetteBoot.*`.
