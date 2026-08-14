#!/usr/bin/env python3
"""SVG → Windows-Symboldatei (`.ico`) mit allen gebrauchten Größen.

    QT_QPA_PLATFORM=offscreen venv/bin/python3 tools/svg_to_ico.py \
        packaging/icon.svg packaging/icon.ico

Warum ein eigenes Skript: unter Windows braucht das Startmenü ein `.ico`, und
das Symbol liegt im Repository als SVG.  Ein Bild pro Datei kann Qt selbst
schreiben — eine `.ico` ist aber ein BEHÄLTER mit mehreren Auflösungen, und
genau darauf kommt es an: Windows nimmt 16×16 für die Titelleiste, 32×32 fürs
Startmenü, 256×256 für die große Kachelansicht.  Liegt nur eine Größe darin,
rechnet der Explorer sie herunter und das Ergebnis ist matschig.

Zwei Bildformate im selben Behälter, so wie es die üblichen Werkzeuge tun:

* **≤ 48 px als BMP** (BITMAPINFOHEADER, 32 Bit BGRA, Zeilen von unten nach
  oben, dahinter die 1-Bit-Maske).  Das ist das klassische Format, das jede
  Windows-Fassung liest.  Die Maske bleibt leer — die Durchsichtigkeit steckt
  im Alphakanal; nur die Länge muss stimmen, sonst rechnet der Explorer die
  Bildhöhe falsch.
* **≥ 64 px als PNG.**  Unkomprimiert wären allein 256×256 eine Viertelmillion
  Byte; als PNG bleibt die Datei bei rund 30 KB.  Windows liest das seit Vista.

Gerastert wird mit Qt (`QSvgRenderer`), weil PySide6 ohnehin in der
Entwicklungsumgebung liegt — kein zusätzliches Werkzeug, keine
Systembibliothek.  Braucht `QT_QPA_PLATFORM=offscreen`, sonst sucht Qt einen
Bildschirm.

Das Ergebnis wird eingecheckt: gebaut wird das Setup auf dem
Windows-Läufer, und der hat weder Qt noch dieses Skript.
"""

import struct
import sys
from pathlib import Path

#: Was in die Datei kommt.  Bis 48 klassisch, darüber PNG (siehe Kopf).
GROESSEN = (16, 24, 32, 48, 64, 128, 256)
PNG_AB = 64


def rastern(svg: Path, kante: int) -> "QImage":
    from PySide6.QtCore import QSize
    from PySide6.QtGui import QImage, QPainter
    from PySide6.QtSvg import QSvgRenderer

    bild = QImage(QSize(kante, kante), QImage.Format_ARGB32)
    bild.fill(0)
    maler = QPainter(bild)
    # Ohne Glättung wird aus einer 64er-Zeichnung bei 16 px ein Krümelbild.
    maler.setRenderHint(QPainter.Antialiasing, True)
    maler.setRenderHint(QPainter.SmoothPixmapTransform, True)
    QSvgRenderer(str(svg)).render(maler)
    maler.end()
    return bild


def als_png(bild) -> bytes:
    from PySide6.QtCore import QBuffer, QByteArray

    roh = QByteArray()
    puffer = QBuffer(roh)
    puffer.open(QBuffer.WriteOnly)
    if not bild.save(puffer, "PNG"):
        raise SystemExit("PNG liess sich nicht schreiben")
    puffer.close()
    return bytes(roh)


def als_bmp(bild) -> bytes:
    """BITMAPINFOHEADER + BGRA-Zeilen (von unten) + leere 1-Bit-Maske."""
    b = bild.width()
    h = bild.height()
    # Die Höhe im Kopf ist DOPPELT — Bild und Maske zusammen.  Das ist keine
    # Marotte des Formats, sondern woran Windows die Maske findet.
    kopf = struct.pack("<IiiHHIIiiII", 40, b, h * 2, 1, 32, 0, b * h * 4, 0, 0, 0, 0)
    zeilen = []
    for y in range(h - 1, -1, -1):
        zeile = bytearray()
        for x in range(b):
            farbe = bild.pixelColor(x, y)
            zeile += bytes((farbe.blue(), farbe.green(), farbe.red(), farbe.alpha()))
        zeilen.append(bytes(zeile))
    maske_zeile = b"\0" * (((b + 31) // 32) * 4)   # 1 Bit je Bildpunkt, auf 4 Byte
    return kopf + b"".join(zeilen) + maske_zeile * h


def ico_bauen(svg: Path, ziel: Path) -> None:
    eintraege = []
    for kante in GROESSEN:
        bild = rastern(svg, kante)
        daten = als_png(bild) if kante >= PNG_AB else als_bmp(bild)
        eintraege.append((kante, daten))

    kopf = struct.pack("<HHH", 0, 1, len(eintraege))       # Reserviert, Typ 1 = Symbol
    versatz = len(kopf) + 16 * len(eintraege)
    verzeichnis = b""
    for kante, daten in eintraege:
        # 256 wird als 0 geschrieben — das Feld ist ein Byte breit.
        n = 0 if kante == 256 else kante
        verzeichnis += struct.pack("<BBBBHHII", n, n, 0, 0, 1, 32, len(daten), versatz)
        versatz += len(daten)

    ziel.write_bytes(kopf + verzeichnis + b"".join(d for _, d in eintraege))


def main(argv) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    svg, ziel = Path(argv[1]), Path(argv[2])
    if not svg.is_file():
        print(f"Quelle fehlt: {svg}", file=sys.stderr)
        return 1
    ico_bauen(svg, ziel)
    print(f"{ziel}  ({ziel.stat().st_size / 1024:.0f} KB, "
          f"{len(GROESSEN)} Größen: {', '.join(str(g) for g in GROESSEN)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
