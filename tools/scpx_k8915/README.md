# tools/scpx_k8915 — SCPX-8915-Systemdiskette des K8915

`python3 tools/scpx_k8915/build.py --tool build/k1520disktool` baut `disks/k8915_scpx8915_v24_system.hfe` (Wächter `cli_scpx_k8915`).
Betriebssystem **SCPX 8915 V 5.3, Anpassung „V24 (XON/XOFF)“** (CCP/BDOS „SCPX V0/2“, REZ Zella-Mehlis 1988) — die frühere
Diskette „901“; deren Handschrift-Name steht nicht mehr im Dateinamen, die Datenträgerkennung `***901.VOL`/`-SICHERH.901` ist nicht dabei.

| Quelle | Inhalt |
|--------|--------|
| `bootabbild.bin` | Systemspuren (= `disks/boot_scpx8915_v24.bin`) |
| `inhalt/` | von 901: `DISGEN` (Systemgenerator 1.9), `FORMAT`, `POWER`, **`RADE`** (RAM-Disk E: v 1.5, nutzt die zweiten 64 KB; Autostart); von 900: `PIP`, `STAT`, `SUBM`, `DUMP`, `SOFTKEY` |
| `tools/scp_gemeinsam/` | TP 3.0, WM, DIENST, BASIC, PASCAL, M80/LINKMT/MLOAD/Z1/ZSID, RAMTEST, DIMA, UNERA, DISKCOPY |
| Beigaben | SERTEST, RAFCPM, RAF512, LBREAD, LBPUNCH, RAFTEST, RAFQUICK |

Geprüft am K8915-Emulator: alle Programme gestartet; Autostart `rade` richtet E: ein; LB* mit `--ptape`, RAF* mit `--raf raf512`.
Nicht aufgenommen: `XSUB` (im System schon aktiv), `TLC` (kennt keinen K8915-Anschluss), EM-/ROM-Programme (A5120-Hardware).
