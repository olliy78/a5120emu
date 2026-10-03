# PC 1715 — EPROM-/PROM-Abzüge

Stand 2026-10-03 (AP-0b, `doc/design/21_pc1715.md` §6). Alle CRC32 stimmen mit der Tabelle in §6
überein. Abkürzungen: **sax** = `~/projects/robotron/sax_de/www.sax.de/_zander/pc1715/eprom/`
(Spiegel von sax.de/~zander/pc1715), **tiffe** = `~/projects/robotron/PC1715/EPROM/`
(tiffe.de/robotron/PC1715/EPROM/, dort `ROMs-PC1715.zip` mit `PC_S502/S602/S619.EPR`,
`TAST_600/618.EPR`). Alle Dateien sind rohe Abzüge ohne Kopf.

| Datei | Baustein / Sockel | Größe | CRC32 | Herkunft | Bedeutung |
|-------|-------------------|-------|-------|----------|-----------|
| `pc1715_s502_urlader.bin` | S502, A25.3 | 2 K | 7b6302e1 | sax `s502.bin` = tiffe `r1715bt.bin` = `PC_S502.EPR` | Urlader 0000–07FF (Floppy-Boot, dann V.24); Listing `r1715bt.lst` |
| `pc1715_s619_zg1_deutsch.bin` | S619, A25.2 | 2 K | 98647763 | sax `s619.bin` = tiffe `r1715cg.bin` = `PC_S619.EPR` | Zeichengenerator 1: ASCII-Satz (siehe unten) |
| `pc1715_s602_zg2.bin` | S602, A25.1 | 2 K | ca6cb4b0 | tiffe `ROMs-PC1715.zip` → `PC_S602.EPR` | Zeichengenerator 2: DIN-66003-Variante mit Umlauten und Unterlängen (MAME: NO_DUMP) |
| `pc1715_s641_zg1_polnisch.bin` | S641, A25.2 | 2 K | 9bd9de4a | sax `s641.bin` | ZG 1 polnisch |
| `pc1715_s643_zg1_kyrillisch.bin` | S643, A25.2 | 2 K | ea37f0e6 | sax `s643.bin` | ZG 1 kyrillisch |
| `pc1715_s605_zg2_kyrillisch.bin` | S605, A25.1 | 2 K | 38062024 | sax `s605.bin` | ZG 2 kyrillisch |
| `pc1715_s600_tastatur.bin` | S600, IC8 der Tastaturplatine | 2 K | b7070122 | sax `s600.bin` = `TAST_600.EPR` | Programm der Tastatur-U880 |
| `pc1715_tast618_tastatur.bin` | TAST_618 | 2 K | 6052a81e | tiffe `ROMs-PC1715.zip` | zweites Tastatur-ROM, Zugehörigkeit unbekannt [?] |
| `pc1715_068_fdc_lese.bin` | 068, A8.2 (Karte A302) | 1 K | 5306d57b | sax `068.bin` = tiffe `r1715lr.bin` | FDC Lese-ROM (Markenerkennung) |
| `pc1715_069_fdc_schreib.bin` | 069, A8.1 (Karte A302) | 1 K | 319fa72c | sax `069.bin` = tiffe `r1715sr.bin` | FDC Schreib-ROM |
| `zg_organisation_corti.txt` | — | — | — | tiffe `r1715cg.txt` | Beschreibung der ZG-Anordnung (Corti) |

Nicht vorhanden: **098/099** (FDC-Karte A301) — die sax-Dateien haben 0 Byte. Nichts fehlte sonst.

## Zeichengenerator (S619, S602 und die übrigen ZG)

Zeilenweise abgelegt, wie bei `zg_organisation_corti.txt`: **Byte = ROM[Zeile·0x80 + Code]**,
128 Zeichen (Code 0–127) × 16 Zeilen = 2 K. Gemessen an den Abzügen:

- **MSB = linkes Pixel**, Bit gesetzt = hell. Genutzt sind Bit 7..1, also **7 Pixel Breite**;
  Bit 0 ist in S619/S641/S643/S605 nie gesetzt (S602: in einem einzigen Byte — wohl Abzugsfehler
  oder Zufall, ohne Folge). Der Block 0x7F ist `#######.`.
- Genutzt sind **Zeilen 0–11**; die Zeilen 12–15 (ROM 0x600–0x7FF) sind in allen Abzügen 0.
  Für 8×12 gilt also Zeile 0–11 direkt; ein 8×15-Raster (Zellenhöhe laut 8275) bekommt dieselben
  12 Zeilen, Rest leer/Unterstreichung durch die Hardware. Zeile 0 ist praktisch leer
  (Ausnahme ein Zeichen), Versalien stehen in Zeile 1–9, Unterlängen (g, p, q, y) in S619 bis Zeile 10,
  in S602 bis Zeile 11.
- Beispiel `A` (0x41), S619 und S602 gleich: Zeilen 1–9 = `...##...`, `..#..#..`, `.#....#.` ×3,
  `.######.`, `.#....#.` ×3.

### S602 gegen S619

**Fast gleich: 121 von 2048 Bytes unterscheiden sich, nur 16 Codes.** Ziffern, Versalien und
Kleinbuchstaben a–z sind bis auf `W` (0x57) identisch. S602 ist **kein** zweiter Satz (keine
Semigrafik, keine Zweitbelegung), sondern die **deutsche Zeichensatzvariante**; S619 ist der
reine ASCII-Satz (`[ \ ] ^ { | } ~` als Klammern/Backslash/Zirkumflex/Tilde):

| Code | S619 | S602 |
|------|------|------|
| 0x24 | `$` | `$` mit Längsstrich, anderes Bild (Währungszeichen) |
| 0x40 | `@` | `§` (zweiteilig, Paragraph) |
| 0x5B 0x5C 0x5D | `[ \ ]` | `Ä Ö Ü` |
| 0x7B 0x7C 0x7D 0x7E | `{ \| } ~` | `ä ö ü ß` |
| 0x5E | `^` | anderes Zeichen mit Unterlänge (nicht gedeutet) |
| 0x13, 0x2F, 0x32, 0x3C, 0x3E, 0x57 | | abweichende Zeilenlage/Form (z. B. 0x13 mit Linie in Zeile 10; `/`, `2`, `<`, `>` um eine Zeile nach oben, S602-Glyphen sind 10–11 Zeilen hoch) |

Die Beschriftung „deutsch" im Planungsdokument gehört demnach eher zu **S602** (Umlaute) als
zu S619; welcher Satz an A25.1 bzw. A25.2 in welcher Gerätefassung steckt, ist noch ungeklärt
(Frage an den Anwender, §9.1). S641 weicht von S619 in 143, S643 von S605 in 264 Bytes ab.

## C-Arrays

Erzeugt mit `tools/eprom_to_h.py <bin> <Symbol> <Header>`; von keinem Code eingebunden:

- `core/cards/pc1715_zre/rom_s502.h` (`PC1715_S502_URLADER`), `chargen_s619.h` (`PC1715_S619_ZG1`),
  `chargen_s602.h` (`PC1715_S602_ZG2`), `rom_068.h`, `rom_069.h`
- `core/peripherals/tastatur1715/rom_s600.h` (`PC1715_S600_TASTATUR`)

Ablage nach Vorbild PRG710 (`core/cards/k2521/rom_prg710.h`: ROM-Header beim besitzenden
Kartenordner) und dem Plan §7: ZRE-Karte, Tastatur-Peripherie, 1715W-Speicherkarte.
