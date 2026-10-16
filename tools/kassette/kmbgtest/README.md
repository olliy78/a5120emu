# KMBGTEST — Robotron-Kassettenbibliothek `KMBG.REL` unter SCPX (AP K8)

Kleines SCPX-Programm (A5120 mit AKB K5020), gebunden mit der Robotron-Bibliothek
`doc/kassette/fex/KMBG.REL` (Aufrufschnittstelle wie FEX `kassrout.mac`: C = Befehl, DE = Puffer,
HL = Länge, zurück A = Status, HL = Bytes; `doc/kassette/treiber.md` §4.9).  Ablauf an Laufwerk 1:
reservieren (15H) · 128-Byte-Block schreiben (02H) · Block zurück (32H) · lesen (01H) · vergleichen ·
freigeben (16H).  Ausgabe:

```
KMBGTEST RES=00 SCHR=00 ZURUECK=00 LESEN=00 LAENGE=0084 FREI=00
KMBGTEST OK
```

`LAENGE` zählt Präambel, CRC und Postambel mit (128 + 4).  KMBG verlangt SCPX ≥ 1.1 und
`(F7EBH) ≥ 47H` (sonst eigene Meldung und Warmstart).

```sh
python3 tools/kassette/kmbgtest/build.py           # Quelle + KMBG.REL → kmbgtest.com (M80/LINKMT, CPA_TOOLS)
python3 tools/kassette/kmbgtest/build.py --check   # passt die eingecheckte .com zur Quelle?
```

Die Kassette muss **zurückgespult** sein (Lichtschranke kurz im Vorspann, wie nach `UMS`): steht sie am
Anfang des 1-m-Vorspanns des Kernmodells, meldet KMBG beim Schreiben Status 04 — es wartet nur 2,45 s
auf das Magnetband (KMBG 048E–04A4, Adressen relativ zum Modul wie in treiber.md).  Wächter: `ScpxKmbg.SchreibtUndLiestEinenBlock`,
`cli_kmbgtest_com_passt_zur_quelle`.
