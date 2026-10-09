# Disketten-Images — Arbeitsverzeichnis

Dieses Verzeichnis ist für **manuelle Läufe**: GUI, `k1520dbg`, `boot_trace`, `floppy_diag`,
Formatier-Experimente. Der Inhalt darf sich jederzeit ändern.

> **Die Tests benutzen dieses Verzeichnis nicht.** Ihre unveränderlichen Kopien liegen unter
> `tests/fixtures/disks/` — dort steht auch das Namensschema und wer welche Diskette braucht
> (`tests/fixtures/README.md`). Wer hier eine Diskette ändert, beeinflusst keinen Test.

## Dateiformate

| Endung | Inhalt |
|--------|--------|
| `.hfe` | HFE-Rohbild mit Spuren/Sektoren (formatagnostisch, enthält die Bitzellen) |
| `.img` | reine Nutzdaten der Diskette (Geometrie steckt im Formatnamen) |

## Namensschema

`<system>_<diskformat>_<laufwerkskonfiguration>_<merkmale>.<ext>` — identisch zu den Fixtures,
Erklärung der Segmente in `tests/fixtures/README.md`.

## Disketten

| Datei | System | Laufwerke A: / B: / C: |
|-------|--------|------------------------|
| **`a5120_cpa_k5601_system.hfe`** | **DIE CP/A-Diskette der Auslieferung.** Bootet mit `@OS.COM` = mit Uhr, 3 × K5601; daneben die übrigen BIOS-Fassungen unter eigenem Namen, vom `A>` aus zu starten (voller BIOS-Neustart, ^C behält die Wahl): `OSNOCLK` (ohne Uhr), `OSCOMB5` (B: K5600.10, C: K5600.20), `OSCOMB8` (B: MF3200, C: MF6400), `OSRAF` (RAF als M:), `OSEM256` (A5120.16). Dazu Systemprogramme (FORMAT, CPABCGEN, PIP, STAT, POWER, DIENST …), Textverarbeitung (WordMaster mit deutscher Hilfe, TP 3.0 samt TPDRUCK und Installer TPINSCPA), Entwicklung (BASIC, Turbo Pascal, M80/LINKMT, Z1, ZSID), alle eigenen Prüfprogramme (SERTEST, ROMREAD, EM256*, RAF*, LBREAD/LBPUNCH) und `LIESMICH.TXT`; 52 Dateien, 194 KB frei. Gebaut aus `tools/cpa_a5120/` (`build.py`, Wächter `cli_cpa_a5120`); `CPABCGEN B: OSCOMB5.COM` erzeugt eine Systemdiskette auf Basis einer Variante | A: K5601 · B:/C: je nach gestarteter Variante |
| `udos1715_640k_pc1715_system.hfe` | **UDOS1715/NDOS** (PC 1715), Systemdiskette „SYSTEM", 80×32×256 — dieselbe Diskette liegt als `.img` unter `tests/fixtures/disks/` | MFS 1.6 |
| `prg710_udos43_k5601_system.hfe` | **UDOS 4.3 für den PRG 710**, beidseitig: Seite 0 Kommandos, Treiber, Assembler, Editor `SCREEN`, `PROG`; Seite 1 MRS-700-Umgebung (`MRS`, `MPSS`, `EDI`, `PROM`, E/A-Treiber) und Dokumente. Gebaut aus `tools/prg_disketten/`, `check --full` ohne Befund | PRG 710 (K5601) |
| `a5120_udos43_k5601_entwickler.hfe` | **UDOS 4.3 für den A5120** (UDOS BC.5120, 08/90), startfähig, **mit allen Entwicklerwerkzeugen** der Entwicklerdiskette (ASM/LINK, EDIT/EDI/SEDIT, SYD, BASIC, PL/Z, TRANSFER, REORG, LW, SG samt `POS_*`-Kernen). Bestand der Altdiskette `udos_boot_scp` ohne `UPRO`/`ESPRO`. Gebaut aus `tools/udos_a5120/`, `check --full` ohne Befund | A5120 (K5601) |
| `prg710-1_udos43_k5601_v43_189.hfe` | **UDOS 4.3 für den PRG 710-1**, **derselbe Inhalt** wie die 710-Diskette; verschieden nur Systemspuren und `OS`-Kennung (`UDOS PG710-1`). Gebaut aus `tools/prg_disketten/`, `check --full` ohne Befund | PRG 710-1 (K5601) |
| **`pc1715_cpa1715_system.hfe`** | **DIE CP/A-1715-Diskette der Auslieferung.** Bootet mit `@OS.COM` = BIOS 24.05.88, 4 LW; daneben `OS2LWUHR` (24.05.88, 2 LW, Uhr, ohne Monitor) und `OS0189` (03.01.89, 3 LW), vom `A>` aus zu starten. Dazu FORMATPX, CPA1715G, STAT, POWER, DIENST, DISKCOPY, UNERA, DIMA, TLC, WM mit deutscher Hilfe, TP 3.0 mit TPDRUCK/TPINSCPA, BASIC, Turbo Pascal, M80/LINKMT/Z1/ZSID und `LIESMICH.TXT`. Gebaut aus `tools/cpa_pc1715/` (Wächter `cli_cpa_pc1715`); die BIOS-Quelltexte der früheren Bootdiskette liegen als Fixture `tests/fixtures/disks/pc1715_cpa1715_boot_4lw.hfe` | PC 1715 (K5601) |
| **`a5120_scpx17_k5601_system.hfe`** | **SCPX 1526 V 1.7 (52K) für den A5120**, 5×1024 (K5601). `SYSP` erzeugt Systeme für andere Tastatur/Bildschirm/Laufwerke (alle BIOS-Module dabei), `INIT`, `MODF`, `SYSG`; dazu TP 3.0, WM (deutsche Hilfe), BASIC, Turbo Pascal, M80/LINKMT/Z1/ZSID, EM256-/RAF-/Lochband-/Serien-Prüfprogramme, `LIESMICH.TXT`. Gebaut aus `tools/scpx_a5120/` (`cli_scpx_a5120`) | A5120 (K5601) |
| **`k8915_scpx8915_v24_system.hfe`** | **SCPX 8915 V 5.3, Anpassung „V24 (XON/XOFF)“ für den K8915** (die frühere Diskette „901“), 5×1024. Mit **`RADE`** (RAM-Disk E: auf der zweiten Speicherbank, Autostart), `DISGEN`, `FORMAT`, `SOFTKEY`, `PIP`, `STAT`; dazu TP 3.0, WM, BASIC, Pascal, Entwicklungswerkzeuge, SERTEST/RAF/Lochband, `LIESMICH.TXT`. Aus `tools/scpx_k8915/` (`cli_scpx_k8915`) | K8915 |
| **`prg710_scpx15_system.hfe`** | **SCPX V 1.5 „B. Daehmlow“ für den PRG 710**, 16×256, BIOS-Module `B151V24`/`B152V24`/`B152IFSS`, `SYSPRG`; PRG-Werkzeuge: **`PROG`** (PROM-Programmierer), `CONV1` (UDOS→SCP), `ASM`/`LINK`/`LIB`/`DU`/`EDIT`; Textprogramm, Sprachen, SERTEST/RAF/Lochband, `LIESMICH.TXT`. Aus `tools/scpx_prg/` (`cli_scpx_prg`) | PRG 710 (K5601) |
| **`prg710-1_scpx17_system.hfe`** | **SCPX 1526 V 1.7 (52K) für den PRG 710-1**, 16×256, BIOS-Module `B17172xx`/`B17272xx`; sonst wie die 710-Diskette (ohne Anwenderdaten) | PRG 710-1 (K5601) |
| **`pc1715_scp1715_v0006_system.hfe`** | **SCP 1715 V0006** (03/08/87, 48 KB), 5×1024; `INIT`, `SGEN`, `INSTSCP`, `PCTEST`, Werkzeuge, TP 3.0, WM, BASIC, Pascal, Fortran-80, M80/L80, `TLC`, SERTEST. Aus `tools/scp_pc1715/` (`cli_scp_pc1715`) | PC 1715 (K5601) |
| **`pc1715_scp1715_v0007_system.hfe`** | **SCP 1715 V0007** (01/11/88, 50 KB, nachladbarer CCP), 16×256; sonst wie V0006 (ohne `PCTEST`) | PC 1715 (K5601) |
| **`pc1715_cpz22_system.hfe`** | **CP/Z 2.2** („52K CP/Z 2.2 ZOAZ MRL“ 06.12.88), 16×256, mit `CPZINIT`/`CPZGEN`/`CPZDUP`, Turbo Pascal, WordStar; dazu WM, BASIC, `TLC`, SERTEST | PC 1715 (K5601) |
| **`pc1715w_scp30_system.hfe`** | **SCP 3.0 (CP/M 3) V0003 28.03.89, Lader PC 1715W**, 5×1024; Zeichensätze `SC6xx.ZGF`, Textprogramm V1/3B + `TPINSTD`, WM, BASIC, Pascal, SERTEST V0.3. Aus `tools/scp_pc1715w/` (`cli_scp_pc1715w`) | PC 1715W (U8272) |
| `bootsec_cpa780.bin` | Bootsektor einer cpa780-Diskette (512 B) | — |

Alle Systemdisketten werden von Skripten gebaut (`tools/<…>/build.py`, Wächter `cli_*`) und tragen eine `LIESMICH.TXT`; Inventur und
Gründe: `doc/scp_inventur.md`, `doc/disketten_bestand.md`. Die früheren Einzeldisketten (CP/A clock, noclock, combo5zoll, combo8zoll, `-raf`;
SCPX/SCP-Bootdisketten) gibt es nur noch als Testdaten unter `tests/fixtures/disks/`; ihre BIOS-Fassungen liegen als `@OS.COM`/`OS*.COM` auf der Systemdiskette. Dort liegen auch die
`.prn`-Listings der Fassungen (`cpa_cpa780_*.prn`, für `k1520dbg -l`).

**Eigene Programme auf den Systemdisketten** (Quelle und eingecheckte `.com` unter `tools/`; die Bauskripte `tools/<…>/build.py`
spielen sie über `tools/disketten_beigaben.py` und `tools/diskbau.py` ein):

| Programm | Quelle | Disketten |
|----------|--------|-----------|
| `SERTEST.COM` V0.3 — serielle Schnittstellen | `tools/sertest/` | alle Systemdisketten außer UDOS |
| `ROMREAD.COM` — liest das Boot-EPROM der ZRE nach `ROM.BIN` | `tools/romread/` | A5120 (CP/A, SCPX) |
| `EM256ADR.COM`, `EM16ABL.COM`, `EM256FUL.COM`, `EM256TST.COM` — Prüfprogramme der A5120.16 (EM064/EM256, U8001) | `tools/em256/` | A5120 (CP/A, SCPX) |
| `RAFCPM.COM` (M:), `RAF512.COM` (P:), `RAFTEST.COM`, `RAFQUICK.COM` — RAM-Floppy RAF (ZWG der AdW, Fremdsoftware, freigegeben; Entwurf 22) | `doc/raf512/`, `tests/fixtures/raf/` | A5120, K8915, PRG 710/710-1 |
| `LBREAD.COM`, `LBPUNCH.COM` — Lochband lesen/stanzen (K6022/SIF1000) | `tools/lochband/` | A5120 (CP/A, SCPX), K8915, PRG 710/710-1 |

Nach einem Neubau einer dieser Dateien alle Systemdisketten nachbauen (`python3 tools/<ordner>/build.py --tool build/k1520disktool`;
die Wächter `cli_*` melden eine alte Fassung). `python3 tools/disketten_beigaben.py --tool build/k1520disktool --check`
(Wächter `cli_beigaben_auf_den_disketten`) prüft, dass die Prüflinge `tests/fixtures/cpm/em*.com` und
`tests/fixtures/raf/RAF{CPM,512}.COM` dieselbe Fassung tragen.

## P8000 / WEGA (Unterordner)

| Ordner | Inhalt |
|--------|--------|
| `wega30/` | WEGA 3.0: UDOS-Startdiskette, 15 Installationsdisketten (`p8000_wega30_root1…5`, `usr1…9`, `doc1`, als `.dmk`) und eine **fertig installierte Platte** `p8000_wega30_platte.k5504.img.gz` |
| `wega31/` | WEGA 3.1: fertig installierte Platte `p8000_wega31_platte.avr.img.gz` (Abbild von pofo.de, Sektor 0 mit Parametersatz) + Startdiskette; keine Installationsdisketten vorhanden |

Beide Platten starten im `p8000emu` bis `WEGA login:` — Bedienung und Herkunft in der README des Ordners.
Platten sind **gzip-gepackt** (`.img.gz`); der Emulator liest und schreibt sie so (Merkposten p8000 Nr. 56).

## Bootabbilder (`boot_*.bin`) — Systemspuren zum Wiedereinspielen

Bootfähig wird eine Diskette durch die **Systemspuren** vor dem Dateisystem; das
Lade-ROM liest Spur 0 blind ein, lange bevor es ein Dateisystem gibt. Diese Bytebänder
liegen hier als `.bin` — damit legt das DiskTool eine **bootfähige** Diskette an:

```sh
tools/dev.sh tool k1520disktool create neu.hfe --fs cpa780 --boot disks/boot_cpa780.bin
tools/dev.sh tool k1520disktool put    neu.hfe auszug/          # @OS.COM und der Rest
```

| Datei | Größe | System | Herkunft |
|-------|-------|--------|----------|
| `boot_cpa780.bin` | 15104 | CP/A (alle cpa780-Disketten des Projekts sind hier byte-gleich) | `tests/fixtures/disks/cpa_cpa780_k5601_noclock.hfe` |
| `boot_scpx640.bin` | 16384 | SCPX 1526 V1.7, 16×256-System | `tests/fixtures/disks/scpx17_cpa780_k5601.hfe` |
| `boot_scpx798.bin` | 18432 | SCPX 1526 V1.7, 5×1024-System | `tests/fixtures/disks/scpx17_5x1024_k5601_hardy_norm.hfe` |
| `boot_udos43.bin` | 13728 | UDOS 4.3 (Seite 0: Spuren 0–2 + Bootspur 21) | `a5120_udos43_k5601_entwickler.hfe` (Altbestand `udos_boot_scp`, jetzt unter `tests/fixtures/disks/`) |
| `boot_scpx8915_55k.bin` | 20480 | SCPX 8915 V5.3, Fassung „55 K“ (K8915; Zylinder 0–1 beidseitig 5×1024, **Ladekopf mit CRC**) | `k8915scpx_cpa800_k5601_bios55k-disk900.hfe` |
| `boot_scpx8915_v24.bin` | 20480 | SCPX 8915 V5.3, Fassung „V24 XON/XOFF“ | `tests/fixtures/disks/k8915scpx_boot1.hfe` (Diskette 901) |

K8915: `create neu.hfe --fs scpx8915 --boot disks/boot_scpx8915_55k.bin` — der Name `scpx8915`
ist nötig (seine Systemspuren = Zylinder 0–1), und das Abbild muss einen gültigen
K8915-Ladekopf tragen (ein CP/A-Abbild des A5120 wird abgewiesen).

Ein eigenes Abbild holt man sich mit `k1520disktool boot-get <diskette> <datei.bin>`
(in der Oberfläche: „Bootabbild sichern…"). Die Systemspuren **allein** machen noch
kein laufendes System — die Betriebssystemdateien (`@OS.COM` …) müssen zusätzlich auf
die Diskette.

> **UDOS braucht die Dateien dazu.** Mit den Systemspuren allein meldet der Urlader
> `OS NOT FOUND` — er sucht die Systemdateien **über das Verzeichnis**. Der ganze
> Datenträger wandert so:
>
> ```sh
> k1520disktool get    a5120_udos43_k5601_entwickler.hfe --to auszug     # Dateien + Beiblatt
> k1520disktool create neu.hfe --fs udos_ds77 --label UDOS.SYS.4.3 --boot disks/boot_udos43.bin
> k1520disktool put    neu.hfe auszug
> ```
>
> Ergebnis: Selbststart (`OS.INIT` → Banner, `DATE`), `%`-Prompt und laufende Befehle
> (`CAT`, `STATUS`, `PRINT`). Das **Beiblatt** `udos-dateiangaben.txt` ist dabei nicht
> optional — eine UDOS-Datei trägt Typ, Eigenschaften, Satzlänge, Startadresse und
> Speicherangaben im Kopfsektor, nicht in ihren Bytes. Die kleinste bootfähige Diskette
> ist Systemspuren + `OS` + `ZDOS`. Hintergrund: `doc/udos_diskettenformat.md` §14.

Die beiden **Combo**-Fassungen (`OSCOMB5`/`OSCOMB8`, Fixtures `combo5zoll`/`combo8zoll`) konfigurieren im BIOS B:/C: als andere Laufwerkstypen
(DPB-Codes 10540/10580 bzw. 00877/10877). FORMAT.COM bietet dadurch je gewähltem Laufwerk
die zugehörigen Formate an (5¼″ einseitig, 8″ SD/DD). Details, live abgegriffene Formatmenüs
und die Test-Pipeline: `doc/format.md` §11 und §5/§3.5.
Menüs abgreifen: `python3 tools/capture_format_menus.py --all`.

## Leere Disketten

Hier liegen **keine** Leerdisketten mehr. Eine gültig formatierte Leerdiskette erzeugt der
Kern selbst — `DiskImage::create` schreibt echte IDAM/DATA/CRC-Strukturen (Daten 0xE5):

```sh
tools/dev.sh tool mk_disk_template …      # einseitige Vorlagen (8″-FM/MFM, 5¼″-SS)
```

In der GUI legt `k1520_create_disk` (Menü „Diskette anlegen") eine leere Diskette im
laufwerkstyp-spezifischen Standardformat an. Auch die Boot-Disk-Pipeline unter
`tests/system/drivers/` erzeugt ihre Vorlagen zur Laufzeit — es ist keine
Leerdiskette mehr eingecheckt.
