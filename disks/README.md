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
| `.prn` | vollständig gelinkter, kommentierter BIOS-Quelltext der jeweiligen Diskette — mit `k1520dbg -l <datei>.prn` laden, dann zeigt jede Disassembly-Zeile Label + Originalkommentar |

## Namensschema

`<system>_<diskformat>_<laufwerkskonfiguration>_<merkmale>.<ext>` — identisch zu den Fixtures,
Erklärung der Segmente in `tests/fixtures/README.md`.

## Disketten

| Datei | System | Laufwerke A: / B: / C: |
|-------|--------|------------------------|
| `cpa_cpa780_k5601_clock` | CP/A **mit Uhr** | K5601 / K5601 / K5601 |
| `cpa_cpa780_k5601_noclock` | CP/A ohne Uhr | K5601 / K5601 / K5601 |
| `cpa_cpa780_k5601_noclock-raf` | CP/A ohne Uhr, **BIOS mit eingebautem RAF-Treiber** (`raf=1`, `rafpar=1`, 88H): mit gesteckter RAM-Floppy (Einstellungen ▸ RAM-Disk) steht nach dem Kaltstart **M:** bereit (RAF 128/512/2M: 127/508/2032 KByte), Inhalt übersteht RESET; ohne Karte kein M:.  Bau: `tests/fixtures/README.md` | K5601 / K5601 / K5601 |
| `cpa_cpa780_combo5zoll_noclock` | CP/A ohne Uhr | K5601 / **K5600.10** / **K5600.20** |
| `cpa_cpa780_combo8zoll_noclock` | CP/A ohne Uhr | K5601 / **MF3200** / **K5602.10 · MF6400** |
| `scpx17_cpa780_k5601.hfe` | SCPX 1526 V1.7, 16×256-System | K5601 |
| `scpx17_5x1024_k5601_hardy.hfe` | SCPX 1526 V1.7, 5×1024-System, mit HARDY.COM | K5601 |
| `udos1715_640k_pc1715_system.hfe` | **UDOS1715/NDOS** (PC 1715), Systemdiskette „SYSTEM", 80×32×256 — dieselbe Diskette liegt als `.img` unter `tests/fixtures/disks/` | MFS 1.6 |
| `prg710_udos43_k5601_system.hfe` | **UDOS 4.3 für den PRG 710**, beidseitig, Seite 0 mit allen Kommandos (aus der MRS-Diskette, ohne das beschädigte `PROG.DOK` der Seite 1), im DiskTool gebaut (`create --boot --prg 710` + `put`), `check --full` ohne Befund | PRG 710 (K5601) |
| `prg710-1_udos43_k5601_v43_189.hfe` | **UDOS.PRG710-1 V4.3 1/89**, Abzug des Anwenders, `check --full` ohne Befund | PRG 710-1 (K5601) |
| `prg710_scpx15_cpa640_sysprg.hfe` | **SCPX V1.5 für den PRG 710** (im Emulator mit `SYSPRG` erzeugt) | PRG 710 (K5601) |
| `prg710-1_scpx17_cpa640_boot.hfe` | **SCPX 1526 V1.7 für den PRG 710-1**, Abzug des Anwenders | PRG 710-1 (K5601) |
| `pc1715_scp1715_v0006_boot.hfe` | **SCP 1715 V0006** (cpa800, 5×1024), bootfähig nach S502 | PC 1715 (K5601) |
| `pc1715_scp1715_v0007_cpa640_boot.hfe` | **SCP 1715 V0007** (cpa640, 16×256) | PC 1715 (K5601) |
| `pc1715_cpa1715_boot_4lw.hfe` | **CP/A 1715**, Bootdiskette mit Werkzeugen | PC 1715 (K5601) |
| `pc1715_cpz22_boot.hfe` | **CP/Z 2.2** (cpa640, „52K CP/Z 2.2“), bootfähig, Tastatur per SIO-Interrupt | PC 1715 (K5601) |
| `pc1715w_scp30_system.hfe` | **SCP 3.0** (CP/M 3, cpa800, 5×1024), „LOADER PC 1715W V0001", mit den `SC6xx.ZGF`-Zeichensätzen | PC 1715W (U8272) |
| `bootsec_cpa780.bin` | Bootsektor einer cpa780-Diskette (512 B) | — |

**Eigene Programme auf den Bootdisketten** (Quelle und eingecheckte `.com` unter `tools/`):

| Programm | Quelle | Disketten |
|----------|--------|-----------|
| `SERTEST.COM` — Prüfprogramm der seriellen Schnittstellen | `tools/sertest/` | alle `cpa_cpa780_*`, `k8915scpx_boot1.hfe` |
| `ROMREAD.COM` — liest das Boot-EPROM der ZRE nach `ROM.BIN` | `tools/romread/` | alle `cpa_cpa780_*` |
| `EM256ADR.COM`, `EM16ABL.COM`, `EM256FUL.COM` — Prüfprogramme der A5120.16 (EM064/EM256, U8001) | `tools/em256/` | alle `cpa_cpa780_*` |
| `RAFCPM.COM` (Laufwerk M:), `RAF512.COM` (Laufwerk P:) — nachladbare Treiber der RAM-Floppy RAF (ZWG der AdW, Fremdsoftware, freigegeben; Entwurf 22) | `doc/raf512/` | alle `cpa_cpa780_*`, `k8915scpx_boot1.hfe`, `prg710-1_scpx17_cpa640_boot.hfe` |

Nach einem Neubau nachziehen mit `python3 tools/disketten_beigaben.py --tool
build/k1520disktool` (Wächter `cli_beigaben_auf_den_disketten`; er prüft auch, dass die
Prüflinge `tests/fixtures/cpm/em*.com` und `tests/fixtures/raf/RAF{CPM,512}.COM` dieselbe
Fassung tragen).

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
| `boot_cpa780.bin` | 15104 | CP/A (alle cpa780-Disketten des Projekts sind hier byte-gleich) | `cpa_cpa780_k5601_noclock.hfe` |
| `boot_scpx640.bin` | 16384 | SCPX 1526 V1.7, 16×256-System | `scpx17_cpa780_k5601.hfe` |
| `boot_scpx798.bin` | 18432 | SCPX 1526 V1.7, 5×1024-System | `scpx17_5x1024_k5601_hardy.hfe` |
| `boot_udos43.bin` | 13728 | UDOS 4.3 (Seite 0: Spuren 0–2 + Bootspur 21) | `udos_boot_scp.hfe` |
| `boot_scpx8915_55k.bin` | 20480 | SCPX 8915 V5.3, Fassung „55 K“ (K8915; Zylinder 0–1 beidseitig 5×1024, **Ladekopf mit CRC**) | `k8915scpx_cpa800_k5601_bios55k-disk900.hfe` |
| `boot_scpx8915_v24.bin` | 20480 | SCPX 8915 V5.3, Fassung „V24 XON/XOFF“ | `k8915scpx_boot1.hfe` (Diskette 901) |

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
> k1520disktool get    udos_boot_scp.hfe --to auszug     # Dateien + Beiblatt
> k1520disktool create neu.hfe --fs udos_ds77 --label UDOS.SYS.4.3 --boot disks/boot_udos43.bin
> k1520disktool put    neu.hfe auszug
> ```
>
> Ergebnis: Selbststart (`OS.INIT` → Banner, `DATE`), `%`-Prompt und laufende Befehle
> (`CAT`, `STATUS`, `PRINT`). Das **Beiblatt** `udos-dateiangaben.txt` ist dabei nicht
> optional — eine UDOS-Datei trägt Typ, Eigenschaften, Satzlänge, Startadresse und
> Speicherangaben im Kopfsektor, nicht in ihren Bytes. Die kleinste bootfähige Diskette
> ist Systemspuren + `OS` + `ZDOS`. Hintergrund: `doc/udos_diskettenformat.md` §14.

Die beiden **Combo**-Disketten konfigurieren im BIOS B:/C: als andere Laufwerkstypen
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
