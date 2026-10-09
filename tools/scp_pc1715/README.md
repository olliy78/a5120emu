# tools/scp_pc1715 — SCP-/CP/Z-Systemdisketten des PC 1715

`python3 tools/scp_pc1715/build.py --tool build/k1520disktool` baut drei Disketten (Wächter `cli_scp_pc1715`):
`pc1715_scp1715_v0006_system.hfe` (SCP 1715 V0006, 5 × 1024), `pc1715_scp1715_v0007_system.hfe` (V0007, 16 × 256, nachladbarer CCP),
`pc1715_cpz22_system.hfe` (CP/Z 2.2, 16 × 256). Die CP/A-Diskette des 1715 baut `tools/cpa_pc1715/`.

Basis: die bisherigen Bootdisketten (`bootabbild_*.bin`, `inhalt_v0006/`, `inhalt_v0007/`, `inhalt_cpz22/`). Dazu `inhalt_scp/`
(aus den 1715-Softwaredisketten `SOFT1715`/`SUPPLIED`: `XDIR`, `XSUB`, `SUBM`, `PRINT`, `D`, `L80`, `LIB`, `F80`, `FORLIB.REL`),
`tools/scp_gemeinsam/` und SERTEST V0.3. **Geprüft** am 1715-Emulator unter V0006, V0007 und CP/Z (Programme gestartet; Pascal, BASIC,
TP, WM-Hilfe, SERTEST; TLC meldet „PC 1715 - V.24“). Nicht aufgenommen: Spiele und Anwendungen der Softwaredisketten (Anwenderdaten),
der BASIC-Compiler (`BASCOM`/`L80`/`BCLOAD` übersetzt, das gelinkte Programm bricht ab — wie am A5120), `SID` (Fremdprogramm von 1990),
PCTEST auf V0007 (Speicherfehler, Gastverhalten), RAF/Lochband (am 1715 nur mechanisch).
