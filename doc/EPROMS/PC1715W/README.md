# PC 1715W — EPROM-/PROM-Abzüge

Stand 2026-10-03 (AP-0b). Quelle beider Dateien: sax-Spiegel
`~/projects/robotron/sax_de/www.sax.de/_zander/pc1715/eprom/` (`s550.bin`, `s287.zip`, dort
`287.BIN`/`287.PLD`/`287.SI`/`inhalt.txt`, 2007); xepb.org/robotron `docs/1715w.zip` wurde nicht geladen.
Der Zeichensatz des 1715W kommt von Diskette (`SC602.ZGF` …), kein ROM.

| Datei | Baustein / Sockel | Größe | CRC32 | Bedeutung |
|-------|-------------------|-------|-------|-----------|
| `pc1715w_s550_urlader.bin` | S550 (U2716), A6 | 2 K | 0a96c754 | Urlader 1715W, Aufschrift „111715W070887"; läuft in Bank 0 ab 0000 |
| `pc1715w_74s287_cas.bin` | 74S287 (PROM 256×4), A41 | 256 B | 8508360c | /CAS-Dekoder der 4 × 64-K-RAM-Blöcke; Adresse A7..A2 (Bank/A15–A12 laut Plan), A1/A0 ohne Wirkung |
| `pc1715w_74s287_cas.pld` | — | 1,2 K | — | rückgerechnete PLD-Gleichung (XCPLD) aus der `s287.zip` |

CRC32 stimmen mit §6 und MAME (8508360c) überein. Das PROM enthält nur die Werte
0x07/0x0B/0x0D/0x0E/0x0F (Nibble, D4–D7 = 0): ein aktiv-niedriger Block-Select, 0x0F = kein Block.
Die Gleichung in der `.pld` ist die Auswertungsgrundlage (AP-W0/W1).

Die C-Arrays liegen in `core/cards/pc1715w_speicher/` (`rom_s550.h` `PC1715W_S550_URLADER`,
`rom_74s287.h` `PC1715W_74S287_CAS`), erzeugt mit `tools/eprom_to_h.py`, noch nicht eingebunden.
