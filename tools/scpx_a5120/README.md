# tools/scpx_a5120 — SCPX-Systemdiskette des A5120

`python3 tools/scpx_a5120/build.py --tool build/k1520disktool` baut `disks/a5120_scpx17/a5120_scpx17_k5601_system.hfe`
(`--check` = Wächter `cli_scpx_a5120`). Betriebssystem **SCPX 1526 V 1.7 (52K)**, Format **5 × 1024** (`scpx798`, K5601 — wie die
anderen Systemdisketten des Pakets). Anwenderbeschreibung: `inhalt/LIESMICH.TXT`; Inventur und Entscheidungen: `doc/scp_inventur.md`.

| Quelle | Inhalt |
|--------|--------|
| `bootabbild.bin` | Systemspuren (= `disks/bootsektoren/boot_scpx798.bin`, aus der Hardy-Diskette) |
| `inhalt/` | System und Dienste der Hardy-Diskette: `SYL17`/`CCPBD17`/`BIOSG617,G717,K617,K717.SYS`, `SYSP`, `SYSG`, `INIT`, `MODF`, `MODX`, `SEPR`, `PIP`, `POWER`, `STAT`, `HARDY` |
| `tools/scp_gemeinsam/` | TP 3.0 (+TPINSCPA), WM, DIENST, BASIC, PASCAL, M80/LINKMT/MLOAD/Z1/ZSID, RAMTEST, DIMA, UNERA, DISKCOPY, TLC (+ `TLC.PAR` für K8025 A, 9600 Bd — im Emulator mit dem TLC-Installationsmenü erzeugt), EM256TST |
| Beigaben | SERTEST, ROMREAD, EM256ADR, EM16ABL, EM256FUL, RAFCPM, RAF512, LBREAD, LBPUNCH (`tools/disketten_beigaben.py`), RAFTEST (Adresse 88H/89H), RAFQUICK |

**Geprüft am A5120-Emulator unter SCPX 1.7** (Programm gestartet, Bildschirm gelesen): alle oben genannten; die **EM-Programme
mit `--em em256`** (EM256ADR: 16 Seiten, EM16ABL: Zeilen A–H ok, EM256FUL, EM256TST: „EM256 OK“); RAF-Programme mit `--raf raf512`,
LB* mit `--ptape`. Eine 16×256-Systemdiskette entsteht mit `INIT` (Format 0) + `MODF` (Format 0) + `SYSP` (Format 0, Ausgabe-Laufwerk B:) —
durchgespielt, die Diskette bootet; `SYSG` allein genügt nicht (kopiert die Spuren im Format des Quelllaufwerks).
Nicht aufgenommen: `MSDOSCPA` (meldet sich unter SCPX nicht), die TLC-Voreinstellung der Workbench (stand auf PC 1715).
