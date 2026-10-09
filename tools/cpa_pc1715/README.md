# tools/cpa_pc1715 — Bau der CP/A-Systemdiskette des PC 1715

`python3 tools/cpa_pc1715/build.py --tool build/k1520disktool` baut `disks/pc1715_cpa1715_system.hfe`
(`--check` = Wächter `cli_cpa_pc1715`). Gegenstück zu `tools/cpa_a5120/`; Beschreibung für den Anwender:
`inhalt/LIESMICH.TXT`, Entscheidungen: `doc/disketten_bestand.md` §7.

| Ordner/Datei | Inhalt |
|--------------|--------|
| `bootabbild.bin` | Bootkopf (128 B): Platz 0 + F0-Platz mit den Laufwerks-Parametersätzen; CP/A 1715 hat keine Systemspuren. Aus `pc1715_cpa1715_boot_4lw`, Platz 1/2 mit E5 geleert |
| `varianten/` | `@OS.COM` (24.05.88, 4 LW), `OS2LWUHR` (24.05.88, 2 LW, Uhr; `tests/fixtures/disks/pc1715_cpa1715_workbench.hfe`), `OS0189` (03.01.89, 3 LW; Gotek-Abzug `CPA_PC1715`) |
| `inhalt/` | Programme (s. u.) und `LIESMICH.TXT` |
| (Beigabe) | `SERTEST.COM` (V0.2, aus `tools/sertest/sertest.com` über `disketten_beigaben.PROGRAMME`) |
| (gemeinsam) | `WM.HLP` deutsch aus `tools/cpa_a5120/quellen/WM_HLP_de.txt` |

## Herkunft in `inhalt/`

| Programm | Herkunft |
|----------|----------|
| PIP, FORMATPX, CPA1715G, M80, LINKMT, MLOAD, Z1, ZSID, WM | die bisherige Bootdiskette `pc1715_cpa1715_boot_4lw` |
| STAT, POWER (3.08), CLS, DIMA, DISKCOPY, UNERA | Gotek-Abzug `CPA_PC1715` (CP/A 03.01.89 am PC 1715) |
| DIENST, TLC (+TLC.PAR, TLCX.PMA), MSDOSCPA, RAMTEST | wie bei `tools/cpa_a5120/` (byte-gleich mit der Workbench-Diskette des 1715) |
| TP.COM, TPHT/TPOVLY1/TPDRUCK.OVR, TPINSCPA | TP 3.0 „Anpassung an CP/A“ (K8915-Diskette 901), siehe `tools/cpa_a5120/README.md` |
| BASIC.COM, PASCAL.COM/.TXT/.RES, PASSAVE, PASINST | PC-1715-Softwarediskette `SOFT1715` bzw. `SUPPLIED` — Turbo Pascal meldet „PC ROBOTRON 1715“ |

Geprüft am 1715-Emulator (`k1520dbg --machine pc1715`): Start von STAT, POWER, DIENST, DIMA, UNERA, DISKCOPY, CLS,
TLC (zeigt „PC 1715 - V.24“), RAMTEST, MSDOSCPA; BASIC `PRINT 7*6`; Pascal `writeln(6*7)` → 42; TP-Anfangsmenü;
WM-Hilfe deutsch. Nicht aufgenommen: BIOS-Quellen (Fixture), PCTEST, A5120-Prüfprogramme (ROMREAD, EM256*), RAF/Lochband, BASCOM/L80. SERTEST V0.2 ist dabei (Drucker + V.24, `Sertest.Pc1715_*`, `SertestKopplung.Pc1715_*`).
