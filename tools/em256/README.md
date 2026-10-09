# em256 — Prüfprogramme der A5120.16 (EM064/EM256 mit U8001)

Drei CP/A-Programme (Z80, `.COM`), die die 16-Bit-Erweiterung eines **A5120.16**
prüfen — am echten Gerät wie im Emulator (`a5120emu`, Modell *A5120.16*). Entwurf und
Befunde: **`doc/design/17_a5120_16.md`** (§3 G1/S4/G2b, §7 Schaltpläne).

| Programm | Zweck | U8001-Teil |
|----------|-------|------------|
| `em256adr.com` | **G1** — Portbasis der Steuerkarte (X10/X11, erwartet A8H–AFH) und Attributspeicher A22, ohne das Gerät zu öffnen | keiner (reiner U880-Code) |
| `em16abl.com` | **S4/G2–G5** — die belegten Abläufe des 16-Bit-Mode, Zeilen A–H: Start aus Reset, Segmentweiche, VI/Status-8, INT-16, Einzelbefehlszähler→NVI, Parität, STOP, RESET16 | `src/fw16abl.s` |
| `em256ful.com` | **G2b** — Volltest v2.0, 21 Prüfungen in Gruppen A–E (PIO, Speicher, U8001-CPU, DRAM-March-C, Parität) | `src/fw_*.s` (9 Blöcke) |

Alle drei liegen auf der A5120-Systemdiskette `a5120_cpa_k5601_system.hfe` in `disks/` und damit auf den
Beispieldisketten der Installation (`tools/disketten_beigaben.py`). Am Prompt starten:
`EM256ADR`, `EM16ABL`, `EM256FUL`. Ohne Erweiterungskarte melden sie „keine Karte“ und
ändern nichts. Ausgabe seitenweise; jede Zeile endet mit `OK`/`[OK]` bzw. einem Befund mit
Soll und Ist, am Schluss eine `ERGEBNIS`- bzw. Summenzeile.

> **Wahrheit sind die Schaltpläne** (Entwurf 17 §7) und das Handbuch A5120.16, nicht
> frühere Fassungen dieser Programme: `em256tst` und `em256ful` v1.x liefen nie am Gerät.
> Am Gerät ist jede Fehlerzeile ein Befund über die Hardware **oder** das Programm —
> erst am Plan prüfen.

## Bauen

```sh
tools/dev.sh build                        # baut auch build/z8kasm
python3 tools/em256/build.py              # alle drei -> tools/em256/*.com (eingecheckt)
python3 tools/em256/build.py em16abl      # nur eines
python3 tools/em256/build.py --check      # bytegleich mit den eingecheckten .com? (= Wächter)
python3 tools/disketten_beigaben.py --tool build/k1520disktool   # Disketten + Prüflinge nachziehen
```

Die U8001-Firmware (`src/*.s`) assembliert **`z8kasm`** dieses Projekts segmentiert
(`-s`); `build.py` setzt das Abbild als `DB`-Zeilen an die Stelle `;%INCLUDE fw_….inc`
der Quelle. Danach M80 + LINKMT (Ladeadresse 0100H) über `cparun` aus der
CPA_Workbench — gemeinsamer Teil `tools/cpm_bau.py`, Pfad `CPA_TOOLS=<pfad>`, z8kasm
`Z8KASM=<pfad>`. Die `.com` sind **eingecheckt** (die CI hat die Workbench nicht).

Wächter: `cli_em256_com_passt_zur_quelle` (ohne Werkzeugkette übersprungen),
`cli_beigaben_auf_den_disketten` (Disketten und die Prüflinge
`tests/fixtures/cpm/em*.com` tragen dieselbe Fassung), im Emulator `Em256Adr.*`,
`Em16Abl.*`, `Em256Ful.*`.

## Herkunft

Übernommen am 2026-10-02 aus `CPA_Workbench/tools/16bitTest` (em256adr, em16abl v1.2,
em256ful v2.0; Workbench `2c287b5`), bytegleich nachgebaut. Nicht übernommen: `em256tst`
(älterer Test, lief nie — Befunde in Entwurf 17 §8) und `z8001asm.py` (kodiert
segmentierte Sprünge einwortig; ersetzt durch `z8kasm`). Die ausführliche
Entwurfsbeschreibung von em256ful (Mailbox-Protokoll, Testgruppen) steht weiter im
README der Workbench.
