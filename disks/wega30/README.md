# WEGA 3.0 für den P8000

Vollständiger Satz zum Installieren und eine fertig installierte Platte.  Arbeitsverzeichnis wie
`disks/` insgesamt: **die Tests benutzen diese Dateien nicht**, und der Emulator schreibt in jede
eingelegte Diskette bzw. angeschlossene Platte zurück. Für Experimente also vorher kopieren.

## Inhalt

| Datei | Inhalt |
|-------|--------|
| `p8000_wega30_platte.k5504.img.gz` | **Fertig installierte Platte** K5504.50 (1024/5/18), gzip-gepackt (47 MB → 3,6 MB).  Stand `p15_7_sync` der Installation (`P8000WegaInstall`, `~/.cache/k1520emu/p8000_wega/`): WEGA auf der Platte, Urlader in Block 0, Mehrbenutzerbetrieb eingerichtet |
| `p8000_wega30_udos_boot.dmk` | UDOS-Startdiskette (`w30start`): Urlader, `boot`, `wega` (Kern 3.2 vom 12.02.88), `sa.*` |
| `p8000_wega30_udos_boot2.dmk` | zweiter Stand der Startdiskette (`w30strt2`, andere README/INHALT) |
| `p8000_wega30_root1…5.dmk` | Wurzeldateisystem: `/`, `/bin`, `/etc`, `/lib` |
| `p8000_wega30_usr1…9.dmk` | `/usr`: Programme, Bibliotheken, Handbuchseiten, SCCS … |
| `p8000_wega30_doc1.dmk` | Dokumentation (`*.z`) |

Inhalt je Diskette: `doc/p8000/wega_datentraeger.md` §3.

## Herkunft

Die Disketten sind die 17 CopyQM-Abbilder der Anwender-Sicherung (`~/projects/robotron/P8000/discs/WEGA3.0/*.cqm`),
mit `tools/p8000/cqm2img.py` entpackt und über den Emulatorkern (`mount_disk` + `save_disk_as`) nach
DMK gewandelt.  **Rückprobe:** jede `.dmk` wieder als `.img` ausgelesen ergibt das Ausgangsabbild bytegleich
(SHA-256 der `.img` in `wega_datentraeger.md` §3).  Geometrie: Startdisketten 80×2×16×256, alle übrigen
80×2×9×512 (`k5601_9x512`), MFM.  DMK statt HFE, weil halb so groß (1,0 statt 2,0 MB).

## Benutzen

**Von der Platte starten** (geprüft 2026-10-09: bis `WEGA login:` in ≈ 2 min Wirtszeit):
`p8000emu`, Modell mit Winchester, WDC-Firmware 4.2.  Platte über *Anschließen…* wählen, Startdiskette
in Laufwerk 0.  Dann *Rechner ein* → RETURN (MON8) → RETURN (UDOS mit Koppelsoftware) → bei „Press NMI" den
NMI-Taster.  Der Hardwaretest läuft, danach startet WEGA von selbst.  Datum/Uhrzeit bestätigen, anmelden
als `wega`, Kennwort `root`.

**Neu installieren:** leere Platte über *Neue Platte…* anlegen, Startdiskette in Laufwerk 0, die
Dateisystemdisketten der Reihe nach in Laufwerk 1, wenn `sa.install` fragt.  Ablauf im Handbuch, Kapitel
WEGA-Installation, und in `tests/system/test_p8000_wega_install.cpp`.
