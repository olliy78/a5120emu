#!/usr/bin/env python3
"""cqm2img.py -- CopyQM-Abbild (.cqm) -> rohes Sektorabbild (.img) + Metadaten.

Aufruf
    python3 tools/p8000/cqm2img.py ABBILD.cqm [AUSGABE.img]   # wandeln (+ Metadaten auf stderr)
    python3 tools/p8000/cqm2img.py --info ABBILD.cqm [...]     # nur Metadaten, nichts schreiben
    python3 tools/p8000/cqm2img.py --json ABBILD.cqm [...]     # Metadaten als JSON (stdout)
    python3 tools/p8000/cqm2img.py -d ZIELDIR *.cqm            # viele Dateien nach ZIELDIR/<name>.img
Rueckgabewert 0 = alles heil, 1 = gewandelt mit Warnung (Groesse/Pruefsumme), 2 = Fehler.

Das .img ist ROH: Sektoren in Reihenfolge Zylinder -> Kopf -> Sektor (1..N), je
`sektorgroesse` Byte -- genau die Reihenfolge, die auch ein .img der Projektwerkzeuge
(k5601_16x256 usw.) hat.  Es traegt weder Sektor-IDs noch Luecken noch Spurschnitt.

Format (selbst entziffert, Gegenprobe an 17 echten Abbildern; die Feldbelegung folgt
der oeffentlichen Beschreibung des CopyQM-Kopfes, es ist kein fremder Code uebernommen):
    0x00  "CQ"                    Kennung
    0x02  Version (0x14)
    0x03  Sektorgroesse (LE16)
    0x10  Sektoren je Spur (LE16)   0x12  Seiten (LE16)
    0x1C  Bezeichnung (NUL-terminiert, "720K Double-Sided" ...)
    0x58  Dichte-Code (1 = DD/HD je nach Laufwerk; nur gemeldet)
    0x5A  gespeicherte Spuren       0x5B  Spuren insgesamt
    0x5C  Pruefwert (LE32) der entpackten Daten
    0x6F  Laenge des Kommentars (LE16)
    0x84  Kopfpruefbyte: Summe der Bytes 0x00..0x84 == 0 (mod 256)
    0x85  Kommentar (so viele Byte wie 0x6F sagt, ohne NUL), danach der Datenstrom
Datenstrom = Folge von Bloecken: int16 n (LE); n < 0 -> das EINE folgende Byte
|n|-mal wiederholen; n > 0 -> n Rohbyte; n = 0 ohne Wirkung.  Die Bloecke laufen ueber
Sektor- und Spurgrenzen hinweg (kein Neubeginn je Spur).  Der Strom muss EXAKT bis zum
Dateiende reichen und genau Spuren*Seiten*Sektoren*Sektorgroesse Byte liefern -- das ist
die Vollstaendigkeitsprobe, die wir benutzen.

OFFEN: der Pruefwert bei 0x5C ist keine der gaengigen CRC-32/CRC-32C/MSB-Varianten
(mehrfach durchprobiert) und wird deshalb nur ausgegeben, NICHT geprueft.
"""
import hashlib
import json
import os
import struct
import sys


class CqmError(Exception):
    pass


HEADER_LEN = 0x85


def parse_header(d):
    if len(d) < HEADER_LEN or d[:2] != b"CQ":
        raise CqmError("keine CopyQM-Datei (Kennung 'CQ' fehlt)")
    ssz = struct.unpack_from("<H", d, 3)[0]
    spt = struct.unpack_from("<H", d, 0x10)[0]
    heads = struct.unpack_from("<H", d, 0x12)[0]
    label = d[0x1C:0x58].split(b"\0")[0].decode("latin-1")
    clen = struct.unpack_from("<H", d, 0x6F)[0]
    if not (ssz in (128, 256, 512, 1024, 2048, 4096) and 1 <= spt <= 64 and heads in (1, 2)):
        raise CqmError("unplausible Geometrie im Kopf: %d B, %d Sektoren, %d Seiten" % (ssz, spt, heads))
    comment = d[HEADER_LEN:HEADER_LEN + clen]
    return {
        "version": d[2],
        "sektorgroesse": ssz,
        "sektoren": spt,
        "koepfe": heads,
        "spuren_gespeichert": d[0x5A],
        "spuren": d[0x5B],
        "dichte_code": d[0x58],
        "bezeichnung": label,
        "kommentar": comment.decode("latin-1"),
        "kopf_pruefbyte_ok": sum(d[:HEADER_LEN]) & 0xFF == 0,
        "pruefwert_0x5C": "%08x" % struct.unpack_from("<I", d, 0x5C)[0],
        "datenstart": HEADER_LEN + clen,
    }


def unpack_stream(d, start, limit):
    """Entpackt ab `start`; bricht bei `limit` Byte Ausgabe ab. Rueckgabe (daten, naechste_position)."""
    out = bytearray()
    p = start
    n_d = len(d)
    while p + 2 <= n_d and len(out) < limit:
        n = struct.unpack_from("<h", d, p)[0]
        p += 2
        if n < 0:
            if p >= n_d:
                break  # abgeschnitten: Sollmenge wird in decode() gemeldet
            out += d[p:p + 1] * (-n)
            p += 1
        elif n:
            out += d[p:p + n]  # bei Abschneiden kuerzer als n; decode() meldet es
            p += n
    return out, p


def decode(d):
    """-> (meta, rohdaten). meta['warnungen'] listet Unstimmigkeiten."""
    meta = parse_header(d)
    warn = []
    if not meta["kopf_pruefbyte_ok"]:
        warn.append("Kopfpruefbyte stimmt nicht")
    used = meta["spuren_gespeichert"] or meta["spuren"]
    spur_b = meta["sektorgroesse"] * meta["sektoren"] * meta["koepfe"]
    erwartet = used * spur_b
    out, pos = unpack_stream(d, meta["datenstart"], erwartet + 1)
    if len(out) != erwartet:
        warn.append("entpackt %d B, erwartet %d B" % (len(out), erwartet))
        out = out[:erwartet] + bytes(max(0, erwartet - len(out)))
    elif pos != len(d):
        # Der Strom hat die Sollmenge erreicht, aber es folgt noch etwas.
        warn.append("%d Byte Rest hinter dem Datenstrom" % (len(d) - pos))
    if meta["spuren"] > used:
        out = bytes(out) + bytes((meta["spuren"] - used) * spur_b)
        warn.append("%d Spuren nicht gespeichert, mit 00 aufgefuellt" % (meta["spuren"] - used))
    meta["bytes"] = len(out)
    meta["sha256"] = hashlib.sha256(out).hexdigest()
    meta["warnungen"] = warn
    return meta, bytes(out)


def convert_file(src, dst=None):
    with open(src, "rb") as f:
        d = f.read()
    meta, raw = decode(d)
    if dst:
        with open(dst, "wb") as f:
            f.write(raw)
    return meta


def _fmt(meta, name):
    g = "%d Spuren x %d Kopf x %d Sektoren x %d B" % (
        meta["spuren"], meta["koepfe"], meta["sektoren"], meta["sektorgroesse"])
    s = "%s: %s = %d B, sha256 %s\n  Bezeichnung %r, Kommentar %r" % (
        name, g, meta["bytes"], meta["sha256"][:16], meta["bezeichnung"], meta["kommentar"])
    for w in meta["warnungen"]:
        s += "\n  WARNUNG: " + w
    return s


def main(argv):
    info = js = False
    zdir = None
    files = []
    it = iter(argv)
    for a in it:
        if a == "--info":
            info = True
        elif a == "--json":
            js = info = True
        elif a == "-d":
            zdir = next(it, None)
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            files.append(a)
    if not files:
        print(__doc__, file=sys.stderr)
        return 2
    out_names = {}
    if zdir is None and not info and len(files) == 2 and not files[1].lower().endswith(".cqm"):
        out_names[files[0]] = files[1]
        files = files[:1]
    if zdir:
        os.makedirs(zdir, exist_ok=True)
    rc = 0
    allmeta = {}
    for f in files:
        try:
            if info:
                meta, _ = decode(open(f, "rb").read())
            else:
                dst = out_names.get(f) or os.path.join(
                    zdir or os.path.dirname(f) or ".", os.path.splitext(os.path.basename(f))[0] + ".img")
                meta = convert_file(f, dst)
            allmeta[os.path.basename(f)] = meta
            if not js:
                print(_fmt(meta, os.path.basename(f)), file=sys.stderr if not info else sys.stdout)
            if meta["warnungen"]:
                rc = max(rc, 1)
        except (CqmError, OSError) as e:
            print("%s: FEHLER: %s" % (f, e), file=sys.stderr)
            rc = 2
    if js:
        print(json.dumps(allmeta, indent=1))
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
