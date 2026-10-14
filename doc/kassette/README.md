# Kassette (K5020/K5200) — Quellen und Arbeitsunterlagen

Planung: `doc/design/29_kassette_k5020_k5200.md`, Arbeitspakete: `doc/design/32_arbeitspakete_kassette_a5120alt_mc80.md`.
Die Dateien unter `fex/` sind **Belege** (nicht verändern); Ergebnisse der Analyse-APs kommen als eigene Dateien
daneben (`k5020.md`, `treiber.md`, `k5200.md`).

## `fex/` — Kassettenprogramme aus dem Archiv des Anwenders (2012–2015)

Herkunft: `~/projects/robotron/sonst/Fex`, `FexMitKMBG`, `FEXohneKMBG` (Rechner des Anwenders, kopiert 2026-10-13).
Unter SCPX entstanden, getestet am realen A5120/K8915 mit K5221.

| Datei | Inhalt |
|---|---|
| `LesenOhneKMBG.mac` | Kassette lesen **ohne** `KMBG.REL`, mit der vollständigen **Portbelegung der K5020** (Daten-PIO 30H–33H, Steuer-PIO 34H–37H, jede Leitung) |
| `kmbrev.mac` | K5200 auf „Wiedergabe rückwärts“ schalten; PIO-Init „wie in ASS K5020 beschrieben“ |
| `PIO-Initialisierung.txt`, `Adressen.txt` | Steuerworte der PIOs; Merkzellen/Einsprünge von `KMBG.REL` (rückübersetzt) |
| `KMBG.REL` | **Robotron-Kassettenbibliothek für SCPX** (Microsoft-REL) |
| `fexmain.mac`, `kassrout.mac`, `kasstest.mac`, `startkass.mac`, `routins.mac`, `menue.mac`, `hmenue.mac`, `estatus.mac` | FEX: Kassettenleser/-schreiber (Basis für `padr` = 30H A5120, D0H K8915) |
| `befehl.txt`, `Formate.txt`, `Version.txt` | KMBG-Befehle A–Q, Kassettenformate (Basis-/Kompakt-/Ausbaustufe, FLX K1600, frei), Erprobungsnotizen |
| `dirk2014/ki5120.asm`, `*.inc`, `images/tape000[0-3].bin` | Leseprogramm eines Mitstreiters (2014) und damit gelesene Testblöcke (Status + Daten) |

## Weitere Belege

- `doc/original_docs/AKB_K5020_Betriebsdokumentation.pdf`, `AKB_K5020_Serviceschaltplaene.pdf` (tiffe.de)
- `doc/original_docs/Kassettenmagnetbandgeraet_K5200_Betriebsdokumentation.pdf`, `…_Serviceschaltplaene.pdf`
- Lade-ROM-Kassettenweg: `doc/EPROMS/zre.prn` 027E–03AF; K5261-Firmware `doc/EPROMS/K5261/`;
  MC80-EGOS-Treiber `doc/EPROMS/MC80/mc8030_mame/zve_3.rom`, `zve_4.rom`
- BCU 880 mit Kassettenlaufwerk F: `doc/a5120_alt/bcu/`
