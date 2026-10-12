# tools/scp_pc1715w — SCP-3.0-Systemdiskette des PC 1715W

`python3 tools/scp_pc1715w/build.py --tool build/k1520disktool` baut `disks/pc1715w_scp30/pc1715w_scp30_system.hfe` (Wächter `cli_scp_pc1715w`).
SCP 3.0 (R-BWS) V0003 vom 28.03.89 (CP/M 3), Lader „PC 1715W“ V0001. `bootabbild.bin` = vier Systemspuren; `inhalt/` = die bisherige
Systemdiskette plus das auf SCP 3 zugeschnittene **Textprogramm V1/3B (12/87)** (`TP.COM`, `TPHT`, `TPOVLY0`, `TPDRUCK.OVR`) und der
Installer `TPINSTD` (+ `.000/.001/.002/.011/.012`) aus der SCP-3.0-Programmdiskette (`real_discs_2/disk_V.hfe`). Gemeinsam: WM (+deutsche
Hilfe), BASIC, PASCAL; dazu SERTEST V0.3 (PC 1715W).

Geprüft am 1715W-Emulator: Boot mit `PROFILE.SUB`, `TYPE LIESMICH.TXT`, TP (Menü), TPINSTD (Menü), WM, BASIC, PASCAL, SERTEST (erkennt den 1715W),
ZSID/M80 (liefen, passen aber nicht mehr auf die Diskette: 68 KB frei; `DIENST` meldet sich unter SCP 3.0 nicht). Weggelassen: `TP120`/`TPG`/
`TPDRUCK.COM` (Textprogramm-Varianten), `DRUCK`, Anwenderdaten der Programmdisketten.
