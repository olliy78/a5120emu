# Die Oberfläche (`app/`)

PySide6/Qt6-Frontend für den K1520-Kern. Es enthält keine Emulationslogik — es lädt
`libk1520core.so` über `ctypes` und zeigt an, was der Kern liefert.

```
app/main.py
  └─ app/ui/main_window.py      Fenster, Laufschleife (Timer), Konfiguration
       ├─ ui/screen_widget.py   Bildröhre  (QOpenGLWidget + GLSL)
       ├─ ui/drive_widget.py    Laufwerksleiste
       ├─ ui/keyboard.py        Tastaturübersetzung + Bildschirmtastatur
       └─ ui/settings_widget.py Einstellungen (Reiter Allgemein / CRT)
            ↓
       app/core_binding/k1520.py   ctypes-Deklarationen
            ↓
       libk1520core.so             core/api/k1520_api.h
```

Einrichten und starten steht in **[SETUP.md](SETUP.md)**; kurz: `bash run_gui.sh`.

## Bildschirm

Der K7024 liefert einen 640×288-Bytepuffer (ein Byte je Bildpunkt). Das Widget lädt ihn
als 8-Bit-Textur hoch und rendert ihn durch einen GLSL-Fragmentshader, der das *Aussehen*
einer echten Bildröhre nachbildet: Grünphosphor-Färbung, Zeilenstruktur, Glühen/Bloom,
leichte Tonnenwölbung mit runden Ecken, Vignette, optionales Flimmern und eine
Streifenmaske. Die Rechenarbeit liegt auf der GPU — Python lädt nur eine Textur je Bild.

Das Bild wird bewusst auf **4:3** gestreckt (der sichtbare Bereich der echten Röhre),
frei skalierbar eingepasst; **F11** schaltet auf Vollbild.

Jeder Bildparameter steckt in `CRTParams` (`ui/screen_widget.py`) und ist im
Einstellungs-Dock unter **CRT** live einstellbar — Regler und Zahlenfeld je Wert,
Farbwähler für die beiden Phosphorfarben. Änderungen greifen sofort und werden
automatisch gespeichert.

> Der Kern bleibt davon unberührt: über die C-ABI gehen weiterhin nur rohe
> Bildpunktbytes. Alles „sieht aus wie eine Röhre" lebt im Frontend.

## Laufwerke

Die Zahl der Laufwerksfelder folgt der Bestückung des Laufwerksschachts
(`app/drive_types.py`) — ein auf „kein Laufwerk" gesetzter Steckplatz bekommt kein Feld.
Wählbare Typen:

| Typ | Bauart |
|---|---|
| K5601 | 5,25″ DS, 80 Spuren, MFM, 800 K |
| K5600.10 | 5,25″ SS, 40 Spuren, MFM, 200 K |
| K5600.20 | 5,25″ SS, 80 Spuren, MFM, 400 K |
| MF3200 | 8″ SS, 77 Spuren, nur FM, 300 K |
| MF6400 | 8″ SS, 77 Spuren, FM+MFM, 600 K |

Je Laufwerk: **Mount/Unmount**, **Neue Diskette** und **Speichern unter…**, dazu
Schreibschutz. Dateiformate sind `.img` (rohes Sektorabbild), **HFE v1** und **DMK**.

„Neue Diskette" mit **leerem** Formatnamen legt eine echte **unformatierte** Diskette in
der Geometrie des Laufwerks an — der Gast kann sie dann selbst mit `FORMAT.COM`
formatieren. Für `.img` geht das nicht: ein rohes Sektorabbild kennt den Zustand
„unformatiert" nicht. Aus demselben Grund lehnt „Speichern unter…" `.img` ab, sobald die
Diskette eine unformatierte Spur oder Daten hinter der Daten-CRC enthält
(`k1520_disk_raw_compatible`).

## Tastatur

Tastendrücke werden nach dem Vertrag des K7637-Kerns übersetzt (`ui/keyboard.py`):
druckbares ASCII als erzeugter Zeichencode, Sondertasten als `Qt::Key_*`-Konstante
(die `QK_*`-Werte im Kern sind damit identisch), Strg+Buchstabe als Basiscode plus
Strg-Flag. Zusätzlich gibt es eine anklickbare Bildschirmtastatur im K7637-Stil.

## Steuerung und Konfiguration

Menü **Emulator**: Reset (Strg+F5). Menü **Ansicht**: Vollbild (F11) und die Docks
(Bildschirm, Tastatur, Laufwerke, Einstellungen) ein-/ausblenden.

Die Geschwindigkeit steht im Einstellungs-Dock unter **Allgemein**: `1.0` = Echtzeit,
größer = Vorspulen (etwa um einen Boot abzukürzen), `0.0` = unbegrenzt (so schnell der
Wirt kann).

Die Konfiguration — CRT-Werte, Geschwindigkeit, Laufwerksbestückung, eingelegte Disketten —
wird automatisch nach `~/.config/k1520emu/config.yaml` geschrieben und beim Start wieder
angewandt. Über **File → Save/Load Configuration…** lassen sich zusätzlich benannte
YAML-Stände ablegen und laden.

## Tests

Die Python-Ebene liegt in `tests/python/` und läuft als Teil der Regressionsrunde
(`tools/dev.sh test-python`). Sie deckt die beiden Dinge ab, die C++-Tests nicht
erreichen: die **C-ABI** (`k1520_api.h` ↔ `libk1520core.so` ↔ die ctypes-Deklarationen —
eine Signaturänderung bricht sonst *lautlos*) und die **GUI** headless unter
`QT_QPA_PLATFORM=offscreen`. Pixel sind dort nicht prüfbar: ein `QOpenGLWidget` hat
offscreen kein FBO. Einzelheiten und Grenzen: `tests/python/README.md`.

## Wenn etwas nicht startet

**`libk1520core.so not found`** — der Kern ist nicht gebaut oder der Bibliothekspfad fehlt:

```sh
tools/dev.sh build
export LD_LIBRARY_PATH=$PWD/build:$LD_LIBRARY_PATH
```

`run_gui.sh` erledigt beides mit. Gesucht wird zuerst in `build/` der Arbeitskopie.

**`ModuleNotFoundError: No module named 'PySide6'`** — das venv ist nicht aktiv
(`source venv/bin/activate`) oder die Abhängigkeiten fehlen (`SETUP.md`, Schritt 1).

**Mehrzeilige Fehlermeldung beim Start** — Startabbrüche des Kerns (etwa ein fehlender
Diskettenformat-Katalog `data/formats.yaml`) werden unverändert durchgereicht; die
Meldung nennt die Ursache.

**Bild bleibt schwarz** — es ist keine bootfähige Diskette eingelegt oder die Maschine ist
aus. Diskette in Laufwerk 0 einlegen und den Emulator zurücksetzen.
