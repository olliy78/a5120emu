# Disketten-Images für verschiedene Tests

## Dateiformate

.hfe     raw HFE Image Format mit Spuren und Sektoren
.img     reine Payload der Diskette
.prn     volständig gelinkte BIOS Quelltext mit Kommentaren. Kann zur Fehlersuche in den Debugger geladen werden


## Disketten

cpadisk_autofs_clock_noautoexec    mit Uhr A: K5601 B: K5601 C: K5601
cpadisk_autofs_noclk_noautoexec    keine Uhr A: K5601 B: K5601 C: K5601
cpadisk_autofs_noclock_8inchCombo: keine Uhr A: K5601 B: MF3200 C: K5602.10 / MF6400
cpadisk_autofs_noclock_5inchCombo: keine Uhr A: K5601 B: K5600.10 C: K5600.20

Die beiden Combo-Disketten konfigurieren im BIOS die Laufwerke B:/C: als andere
Laufwerkstypen (DPB-Codes 10540/10580 bzw. 00877/10877). Dadurch bietet FORMAT.COM je
gewähltem Laufwerk die zugehörigen Formate an (5¼″ einseitig, 8″ SD/DD). Details, die
live abgegriffenen Formatmenüs und die Test-Pipeline: `docs/format.md` §11 und §5/§3.5.
Menüs abgreifen: `python3 tools/capture_format_menus.py --all`.