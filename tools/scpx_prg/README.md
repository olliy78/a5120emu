# tools/scpx_prg — SCPX-Systemdisketten des PRG 710 und PRG 710-1

`python3 tools/scpx_prg/build.py --tool build/k1520disktool` baut `disks/prg710_scpx15/prg710_scpx15_system.hfe` (SCPX V 1.5 „B. Daehmlow“) und
`disks/prg710-1_scpx17/prg710-1_scpx17_system.hfe` (SCPX 1526 V 1.7); Wächter `cli_scpx_prg`. Format 16 × 256 (`scpx640`, wie die bisherigen PRG-Disketten).

| Quelle | Inhalt |
|--------|--------|
| `bootabbild_710.bin`, `bootabbild_710-1.bin` | Systemspuren aus den bisherigen Disketten |
| `inhalt710/`, `inhalt710-1/` | BIOS-Module (710: `B151V24`, `B152V24`, `B152IFSS`; 710-1: `B17172/B17272` × `V2,ZI,FS,SD`) |
| `inhalt/` | `SYL17`, `CCPBD17`, `SYSPRG`, `SYSG`, `FORMAT`, `PIP`, `STAT`, `SUBM`, `MODF`, `POWER`, `SDIR`, **`PROG`** (PROM-Programmierer V1.1), `CONV1` (UDOS → SCP), `CODP`, `EDIT`, `ASM`, `LINK`, `LIB`, `DU` (+`HIST.UTL`, `TRACE.UTL`) |
| `tools/scp_gemeinsam/`, Beigaben | wie K8915 (TP 3.0, WM, BASIC, PASCAL, M80 …; SERTEST V0.3, RAF*, LB*) |

Geprüft an beiden PRG-Emulatoren unter SCPX (alle Programme gestartet; `PROG` zeigt Menü, SERTEST erkennt 710 bzw. 710-1; RAF/LB* mit
`--raf`/`--ptape`). **Weggelassen:** die Anwenderdaten der alten 710-1-Diskette (`KLINGEL.DAT`, `RITE.DAT`, `ROM0.DAT`, `ROM01/02.001`,
`POM02.001`, `0001.DAT`, `001`, `LC80PRG.DAT`, `TEXT.TXT`), `DIT`/`DIT.001`/`DIT.DAT`/`ALCP.000/001` (Zweck ohne Programmteil nicht
feststellbar), `TP 1.3` mit `INSTALL`/`TPOVLY*` (A5120-Fassung; TP 3.0 läuft über das BIOS), `TURBO` (PASCAL genügt), `FORLIB.LIB` (leer).
