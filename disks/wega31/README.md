# WEGA 3.1 für den P8000

Eine fertig installierte Platte und die Startdiskette dazu.  **Installationsdisketten für WEGA 3.1 gibt es
hier nicht**: die einzige vorliegende Quelle ist ein Plattenabbild.  Wie bei `disks/` insgesamt gilt:
die Tests benutzen diese Dateien nicht, und der Emulator schreibt zurück.

## Inhalt

| Datei | Inhalt |
|-------|--------|
| `p8000_wega31_platte.avr.img.gz` | **Fertig installierte Platte**, Geometrie 1380/10/18 (127 MB → 5,9 MB gepackt), Typkürzel `avr` = `WEGA31-AVR` |
| `p8000_wega31_udos_boot.dmk` | UDOS-Startdiskette.  Dieselbe Datei wie `../wega30/p8000_wega30_udos_boot.dmk`: sie bringt nur UDOS und die Koppelsoftware für den 8-Bit-Teil, WEGA selbst kommt von der Platte |

## Herkunft

`WEGA_3.1.20160725.dd` aus `WEGA_3.1.20160725.zip` (pofo.de; SHA-256 des `.dd`
`a76ef89aa525f1c2fcad8419c2df22159ce5889e870a5832d559c70a06373d30`).  Das Abbild ist für den
**AVR-WDC-Emulator** gemacht (`doc/p8000/wdc_firmware.md` §11): in Sektor 0 stand statt eines
Parametersatzes die Antwort des AVR auf Kommando 28 (`"WDC_4.2\0WDC-Emulator"…`), mit der die echte
Firmware 4.2 nicht arbeitet.  **Einzige Änderung:** Sektor 0 (Z0/K0/S1) ist durch einen gültigen
PAR/BTT-Sektor ersetzt — `"DEFEKT"`, leere BTT, `"PARMTR"`, Kennung `WDC-Emulator`, 1380/10/18,
Vorkompensation 1380, Ramp 1, `ztk` 203/209, Grenzen 251/253/241/243 (Werte aus
`winchester::typen()`, `Platte::parSektor`).  Zylinder 0 liegt außerhalb des Blockraums; WEGA
bemerkt die Änderung nicht.  Damit braucht die Platte die Einstellung *Parametersatz ergänzen* nicht.

## Benutzen

Wie bei WEGA 3.0 (`../wega30/README.md`): WDC-Firmware 4.2, Platte anschließen, Startdiskette in
Laufwerk 0, *Rechner ein* → RETURN → RETURN → NMI.  Geprüft 2026-10-09: der Kern meldet
„WEGA Kernel -- Release 3.2 -- Generated 06/29/116", danach `WEGA login:` (≈ 3 min Wirtszeit).
Die Kennwörter dieser Installation sind nicht dokumentiert.
