# Frühe A5120-Ausführungen — Quellen und Arbeitsunterlagen

Planung: `doc/design/30_a5120_fruehe_varianten.md`, Arbeitspakete: `doc/design/32_arbeitspakete_kassette_a5120alt_mc80.md` §5.
Belege nicht verändern; Analyseergebnisse als eigene Dateien daneben (`laderom.md`, `k7023.md`, `k7636.md`,
`k7636_01_codes.csv`, `k5120.md`).

| Ordner/Datei | Inhalt | Herkunft |
|---|---|---|
| `bcu/<satz>/disk.img`, `disk.td0`, `DT-INFO.TXT` | **BCU 880** V1.2/2.1 (FZW Karl-Marx-Stadt), 26×128×40×1 (OhneSYL.3: 80 Spuren); bootfähig sind die „SYL_auf_Spur1“-Sätze; Kassettentreiber (LW F:) auf `satz2_OhneSYL.1` | Archiv des Anwenders (`~/projects/robotron/sonst/StartROMs/BCUStartdisketten`), urspr. VEB Halbleiterwerk Frankfurt/Oder; Abzüge 2003–2019 |
| `cpa_quellen/CPA_890925_Quellen_A5120.zip` | CP/A-Quellen 25.09.89: BIOS mit **BAB1 16×64 (K7023) und BAB2 24×80 (K7024)**, Umschaltung Port 0AH Bit 6; Tastaturen K7604/06/34/36/37; Floppy K5120/K5122; Bau mit M80/LINKMT (`make_@OS.cmd`) | `~/projects/CPA/` des Anwenders |

Weitere Belege:
- Lade-ROMs `doc/EPROMS/K2526/`: `laderom_v06.bin` (+ `laderom_v06.mac`, `laderom_orirom.mac`, Analyse des
  Anwenders 2019), `laderom_v09_1.bin` (= MAME `a5120_v2`), `../zre.rom` (= V0.9_2 = MAME `a5120_v1`),
  `k8924_laderom_v0.9a.bin` (tiffe.de); A5130 `doc/EPROMS/A5130/dzr5130.rom` (MAME)
- K7023: `doc/EPROMS/K7023/BAB1-46-2.bin`, `BAB1-47-1.bin`; `doc/original_docs/ASS_K7023_*`, `K7023_Serviceschaltung.pdf`, `ABS_K7023.pdf`
- K7636: `doc/original_docs/K7634-36-Serviceschaltplaene.pdf` (**S. 8 Belegungsplan K7636.01, Codetabellen**),
  `Tastaturen_K7632_34_35_36_Betriebsdokumentation.pdf`
- K5120: `doc/EPROMS/K5120/`, `doc/original_docs/AMF_K5120_Betriebsdokumentation.pdf`
- K3525: `doc/original_docs/Speichersteckeinheiten_K3520_3521_3525_K3820_Betriebsdokumentation.pdf`
- Gerät des Anwenders (A5120 Kassettenausführung, K7221.11 von 1984): Fotos nur lokal
  `~/projects/robotron/sonst/A5120Kassette/Bilder/`
