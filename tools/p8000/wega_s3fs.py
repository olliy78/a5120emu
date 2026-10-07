#!/usr/bin/env python3
"""Dateien aus einem WEGA-Dateisystem (System III, 512-B-Blöcke, Z8000 = big endian) auf
einem P8000-Plattenabbild lesen — nur lesend, ohne Abhängigkeiten.

Gebraucht für die Testdaten `tests/fixtures/p8000/sa.format`/`sa.verify` (Fassung 4.1 aus dem
WEGA-3.1-Abbild von pofo.de, `doc/p8000/wdc_firmware.md` §11): auf der WEGA-3.0-Startdiskette
steht nur `sa.format` V1.4, das die Parameterblöcke der Firmware 3.x erwartet.

    wega_s3fs.py <abbild> <fs-block> ls [verzeichnis]
    wega_s3fs.py <abbild> <fs-block> get <pfad> <ziel>

<fs-block> = Blocknummer des Dateisystems im Blockraum des WDC (Block 0 = Zylinder 1); im
WEGA-3.1-Abbild /usr = 0, / = 16000.  `--vorlauf` = Sektoren vor Block 0 (Vorgabe 180 =
1 Zylinder × 10 Köpfe × 18 Sektoren des AVR-Abbilds).
"""
import argparse
import struct
import sys


class S3fs:
    def __init__(self, pfad, fs_block, vorlauf):
        self.f = open(pfad, "rb")
        self.basis = (vorlauf + fs_block) * 512

    def block(self, n):
        self.f.seek(self.basis + n * 512)
        return self.f.read(512)

    def inode(self, i):
        b = self.block(2 + (i - 1) // 8)
        d = b[((i - 1) % 8) * 64:][:64]
        mode, _nl, _uid, _gid, groesse = struct.unpack(">HHHHI", d[:12])
        adr = [int.from_bytes(d[12 + 3 * k:15 + 3 * k], "big") for k in range(13)]
        return mode, groesse, adr

    def daten(self, i):
        _mode, groesse, adr = self.inode(i)
        bloecke = adr[:10]
        if adr[10]:   # einfach indirekt (reicht für Dateien bis 69 KB)
            ib = self.block(adr[10])
            bloecke += [struct.unpack(">I", ib[4 * k:4 * k + 4])[0] for k in range(128)]
        if adr[11] or adr[12]:
            sys.exit("Datei zu groß (doppelt indirekt nicht umgesetzt)")
        out = bytearray()
        for a in bloecke:
            if len(out) >= groesse:
                break
            out += self.block(a) if a else bytes(512)
        return bytes(out[:groesse])

    def verzeichnis(self, i):
        d = self.daten(i)
        r = {}
        for k in range(0, len(d), 16):
            ino = struct.unpack(">H", d[k:k + 2])[0]
            if ino:
                r[d[k + 2:k + 16].split(b"\0")[0].decode("latin-1")] = ino
        return r

    def suche(self, pfad):
        i = 2
        for teil in [t for t in pfad.split("/") if t]:
            i = self.verzeichnis(i).get(teil)
            if i is None:
                sys.exit(f"nicht gefunden: {pfad}")
        return i


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("abbild")
    ap.add_argument("fs_block", type=int)
    ap.add_argument("befehl", choices=["ls", "get"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--vorlauf", type=int, default=180)
    a = ap.parse_args()
    fs = S3fs(a.abbild, a.fs_block, a.vorlauf)
    if a.befehl == "ls":
        for name in sorted(fs.verzeichnis(fs.suche(a.args[0] if a.args else "/"))):
            print(name)
    else:
        if len(a.args) != 2:
            sys.exit("get <pfad> <ziel>")
        with open(a.args[1], "wb") as z:
            z.write(fs.daten(fs.suche(a.args[0])))


if __name__ == "__main__":
    main()
