# MC80 — Arbeitsunterlagen

Planung: `doc/design/31_mc80.md`, Arbeitspakete: `doc/design/32_arbeitspakete_kassette_a5120alt_mc80.md` §6.
EPROMs mit Herkunft und CRC: `doc/EPROMS/MC80/README.md`.  Originaldokumentation: `doc/original_docs/mc80/`
(ycdt.net/mc80.3x: EGOS 30.1, Bedienungsanleitung, Systemkassette, KdT-Lehrgang, SPE2-/ZVE-Pläne, PK-3,
BASIC, GRAB, OCED, RAM-Modus, MC80.22-Betriebssystem).

| Datei | Inhalt |
|---|---|
| `spiele/*.bin` | Speicherabzüge (ycdt.net): PHARAO ab 6400H (`RKET 01,4000,6400`, dann `Z`), WALL ab 8000H (`RKET 01,4000,8000`, `Z`), TETRIS ab 4000H (`TE`), HASI ab 4000H (`HASI`) |
| `mc80_2x_handbuch_ocr.txt` | Textebene (OCR, fehlerhaft) des MC80.2x-Handbuchs (Original 43 MB, tiffe.de `Robotron/MC80.2X/MC80.2x-Handbuch.pdf`) — Technische Daten, MBI §3.1.8, Bedienung |
| `hardware_2x.md` | MC80.21/.22: Hardware (DPL, Tastatur, MBI, PPE, E/A, Speicher), ROM-Fassungen (AP M10) |
| `bsys_2x.prn` | Betriebssystem MC80.21/22 kommentiert, ERZEUGT von `tools/gen_mc8020_bsys_prn.py` |

Analyseergebnisse kommen hierher: `hardware_3x.md`, `egos.prn`, `hardware_2x.md`, `bsys_2x.prn`.
