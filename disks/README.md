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
| `cpa_cpa780_combo5zoll_noclock` | CP/A ohne Uhr | K5601 / **K5600.10** / **K5600.20** |
| `cpa_cpa780_combo8zoll_noclock` | CP/A ohne Uhr | K5601 / **MF3200** / **K5602.10 · MF6400** |
| `scpx17_cpa780_k5601.hfe` | SCPX 1526 V1.7, 16×256-System | K5601 |
| `scpx17_5x1024_k5601_hardy.hfe` | SCPX 1526 V1.7, 5×1024-System, mit HARDY.COM | K5601 |
| `bootsec_cpa780.bin` | Bootsektor einer cpa780-Diskette (512 B) | — |

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
