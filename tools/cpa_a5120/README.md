# tools/cpa_a5120 — Bau der CP/A-Systemdiskette des A5120

`python3 tools/cpa_a5120/build.py --tool build/k1520disktool` baut `disks/cpa_cpa780_k5601_system.hfe`
(`--check` = Wächter `cli_cpa_a5120`). Beschreibung des Inhalts für den Anwender: `inhalt/LIESMICH.TXT`;
Entscheidungen und Versuche: `doc/disketten_bestand.md`.

| Ordner | Inhalt |
|--------|--------|
| `varianten/` | die sechs BIOS-Fassungen (`@OS.COM` = mit Uhr, dazu `OSNOCLK`, `OSCOMB5`, `OSCOMB8`, `OSRAF`, `OSEM256`) — aus den Fixtures `tests/fixtures/disks/cpa_cpa780_*.img` |
| `inhalt/` | Systemprogramme, TP, BASIC, Pascal, STAT, `LIESMICH.TXT` |
| `quellen/` | `WM_HLP_de.txt` (deutsche Hilfe, daraus wird `WM.HLP` gebaut), `WM.HLP.russisch` (Original) |
| (Quellordner der Werkzeuge) | SERTEST, ROMREAD, EM256*, RAFCPM/RAF512, LBREAD/LBPUNCH über `tools/disketten_beigaben.py`; RAFTEST (gepatcht)/RAFQUICK aus `tests/fixtures/raf/` |

## Herkunft der Programme in `inhalt/`

| Programm | Herkunft | Geprüft am A5120 (CP/A, Emulator) |
|----------|----------|-----------------------------------|
| FORMAT, FORMATB, CPABCGEN, PIP, POWER, DIENST, SUBM, TLC*, WM, ZSID, M80, LINKMT, MLOAD, Z1, MSDOSCPA, BRUN, RAMTEST, HARDY, EM256TST | die bisherigen CP/A-Disketten (`cpa_cpa780_k5601_clock`) | Matrix/Boot-Tests |
| `STAT.COM` | SCPX-1.7-Diskette `scpx17_cpa780_k5601` | `STAT`, `STAT *.COM`, `STAT DSK:` |
| `TP.COM`, `TPHT.OVR`, `TPOVLY1.OVR`, `TPDRUCK.OVR`, `TPINSCPA.COM` | K8915-Diskette `k8915scpx_boot1` (Text-Programm **3.0**, „Anpassung an CP/A 20.04.88“, Schirm 24×80 über BIOS) | Anfangsmenü; `TPINSCPA` zeigt sein Menü |
| `BASIC.COM` | PC-1715-Softwarediskette `SOFT1715` (BASIC „R-BWS“ 12/1984) | `PRINT 7*6` |
| `PASCAL.COM`, `PASCAL.TXT`, `PASCAL.RES`, `PASSAVE.COM`, `PASINST.COM` | `SOFT1715` bzw. `SUPPLIED` (Turbo Pascal „TURBO/+“ 1987) | `E`, `C`, `R`: `writeln(6*7)` → 42; `PASINST` zeigt sein Menü |

**Nicht aufgenommen, bewusst:**
- **TP 1.3** (`A5120_TP_Power_Spiele`, `prg710-1_scpx17`): SCPX-Fassung, **hängt unter CP/A** (nach dem Banner,
  ZVE2 läuft ins Leere — die Nullseite wird überschrieben). Dessen `INSTALL.COM` gehört dazu.
- **BASCOM/L80/BASLIB** (BASIC-Compiler der 1715): übersetzt (0 Fehler), das gelinkte Programm bricht aber mit
  `Bdos Err On T: Select` ab.
- **TURBO.COM** (Turbo Pascal 2.00A) läuft ebenfalls, liegt aber nicht dabei (eine Pascal-Fassung genügt).
- **MBASIC.COM** läuft, ist aber vom `BASIC.COM` nicht zu unterscheiden.
