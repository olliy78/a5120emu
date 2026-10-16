#!/usr/bin/env python3
"""Ersetzt Umlaute im Code durch Umschreibungen (ae/oe/ue/ss).

Aufruf:  python3 tools/umlaute_ersetzen.py [--trocken] [--liste] [Pfad ...]

Ohne Pfade werden alle von git verfolgten Dateien betrachtet.  Die Oberflaeche
(Menues, Dialoge, Handbuch), Dokumentation, Daten und Belege bleiben
unberuehrt; die Regeln stehen in AUSGENOMMEN_* / modus-Funktionen weiter unten.

Wortweise Ersetzung: 'für' -> 'fuer', 'Größe' -> 'Groesse', 'ÜBER' -> 'UEBER'
(ganzes Wort gross geschrieben -> AE/OE/UE/SS).  Verdaechtige Stellen
(einzelner Umlaut als Zeichen, Zeichentabellen) werden NICHT ersetzt, sondern
als AUSGELASSEN gemeldet.
"""
import io
import re
import subprocess
import sys
import tokenize
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent

UMLAUTE = "äöüßÄÖÜ"
HAT_UMLAUT = re.compile("[" + UMLAUTE + "]")
WORT = re.compile("[A-Za-z" + UMLAUTE + "]+")
KLEIN = {"ä": "ae", "ö": "oe", "ü": "ue", "ß": "ss"}
GROSS = {"Ä": "Ae", "Ö": "Oe", "Ü": "Ue"}
VERSAL = {"Ä": "AE", "Ö": "OE", "Ü": "UE", "ß": "SS"}

# Pfadpraefixe bzw. -endungen, die nie angefasst werden.
AUSGENOMMEN_PRAEFIX = (
    "doc/", "third_party/", "tests/fixtures/", "disks/", "data/", "app/help/",
    "tools/umlaute_ersetzen.py",          # enthaelt die Umlaute als Daten
    "tests/python/test_keine_umlaute.py",  # dito
)
AUSGENOMMEN_ENDUNG = (
    ".md", ".bin", ".hfe", ".img", ".rom", ".pdf", ".k52", ".com", ".png",
    ".gz", ".dmk", ".ico", ".prn", ".mac", ".hex", ".dsk", ".zip",
)
# Einzeldateien: Lizenztext / Veroeffentlichungstexte / Menueeintrag.
AUSGENOMMEN_DATEI = {
    "LIZENZ.txt",
    "packaging/release_notes.md.in",
    "packaging/a5120emu.desktop.in",
}
# Python: in app/ nur Kommentare; in tests/python/ Kommentare und Docstrings
# (Zeichenketten dort koennen gegen Oberflaechentexte pruefen); sonst auch Strings.
NUR_KOMMENTARE = ("app/",)
KOMMENTARE_UND_DOCSTRINGS = ("tests/python/",)


def ausgenommen(rel):
    return (rel.startswith(AUSGENOMMEN_PRAEFIX) or rel.endswith(AUSGENOMMEN_ENDUNG)
            or rel in AUSGENOMMEN_DATEI)


def umschreiben_wort(w):
    if not HAT_UMLAUT.search(w):
        return w
    buchst = [c for c in w if c != "ß"]
    versal = len(w) >= 2 and bool(buchst) and all(c.isupper() for c in buchst)
    out = []
    for c in w:
        if c in KLEIN:
            out.append(VERSAL[c] if versal and c == "ß" else KLEIN[c])
        elif c in GROSS:
            out.append(VERSAL[c] if versal else GROSS[c])
        else:
            out.append(c)
    return "".join(out)


def verdaechtig(w, davor, danach):
    """Wort nur aus Umlauten in Anfuehrungszeichen: Zeichen als Daten, kein Wort.
    (Ein einzelner Umlaut im Kommentar, z. B. "o. ä.", wird ersetzt.)"""
    return (davor == danach and davor in "'\"" and davor != ""
            and all(c in UMLAUTE for c in w))


def text_ersetzen(s, melde):
    n = 0

    def f(m):
        nonlocal n
        w = m.group(0)
        if not HAT_UMLAUT.search(w):
            return w
        a, b = m.start(), m.end()
        if w == "DFÜ" and s[b:b + 1] == "/":
            return w    # Geraetebeschriftung (DFÜ/V.24 ...), erscheint in der Oberflaeche
        if verdaechtig(w, s[a - 1:a], s[b:b + 1]):
            melde(w)
            return w
        n += 1
        return umschreiben_wort(w)

    return WORT.sub(f, s), n


def zeile_ist_tabelle(z):
    return bool(re.search("[" + UMLAUTE + "]{3,}", z))


def zeilenweise(text, rel, melde, nur_kommentar=None):
    out = []
    for nr, z in enumerate(text.split("\n"), 1):
        if not HAT_UMLAUT.search(z):
            out.append(z)
            continue
        if zeile_ist_tabelle(z):
            melde(nr, z.strip(), "Tabelle")
            out.append(z)
            continue
        if nur_kommentar is not None:
            kopf, rest = nur_kommentar(z)
            if rest is None:
                out.append(z)
                continue
        else:
            kopf, rest = "", z
        gemeldet = []
        neu, _ = text_ersetzen(rest, gemeldet.append)
        for w in gemeldet:
            melde(nr, z.strip(), "Einzelzeichen " + w)
        out.append(kopf + neu)
    return "\n".join(out)


def iss_kommentar(z):
    """Inno-Setup: nur Kommentare (; // am Zeilenanfang, nachgestelltes //)."""
    s = z.lstrip()
    if s.startswith(";") or s.startswith("//"):
        return "", z
    i = z.rfind("//")
    if i >= 0 and z[:i].count("'") % 2 == 0:
        return z[:i], z[i:]
    return z, None


def python_ersetzen(text, rel, melde):
    nur_komm = rel.startswith(NUR_KOMMENTARE)
    docstr = rel.startswith(KOMMENTARE_UND_DOCSTRINGS)
    zeilen = text.split("\n")
    ersetzungen = []
    toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    fmiddle = getattr(tokenize, "FSTRING_MIDDLE", -1)
    vorher = None
    for i, t in enumerate(toks):
        tt = t.type
        nimm = False
        if tt == tokenize.COMMENT:
            nimm = True
        elif not nur_komm and tt in (tokenize.STRING, fmiddle):
            if docstr:
                nxt = toks[i + 1] if i + 1 < len(toks) else None
                nimm = (tt == tokenize.STRING
                        and vorher in (None, tokenize.NEWLINE, tokenize.NL, tokenize.INDENT,
                                       tokenize.DEDENT)
                        and nxt is not None and nxt.type in (tokenize.NEWLINE, tokenize.NL))
            else:
                nimm = True
        if tt not in (tokenize.NL, tokenize.COMMENT):
            vorher = tt
        if nimm and HAT_UMLAUT.search(t.string):
            ersetzungen.append(t)
    for t in sorted(ersetzungen, key=lambda t: t.start, reverse=True):
        (z0, c0), (z1, c1) = t.start, t.end
        if z0 != z1:
            for zi in range(z1, z0 - 1, -1):
                a = c0 if zi == z0 else 0
                b = c1 if zi == z1 else len(zeilen[zi - 1])
                _ersetze_stueck(zeilen, zi, a, b, melde)
        else:
            _ersetze_stueck(zeilen, z0, c0, c1, melde)
    return "\n".join(zeilen)


def _ersetze_stueck(zeilen, zi, a, b, melde):
    z = zeilen[zi - 1]
    stueck = z[a:b]
    if not HAT_UMLAUT.search(stueck):
        return
    if zeile_ist_tabelle(stueck):
        melde(zi, z.strip(), "Tabelle")
        return
    gemeldet = []
    neu, _ = text_ersetzen(stueck, gemeldet.append)
    for w in gemeldet:
        melde(zi, z.strip(), "Einzelzeichen " + w)
    zeilen[zi - 1] = z[:a] + neu + z[b:]


def bearbeite(rel, text, melde):
    if rel.endswith(".py"):
        return python_ersetzen(text, rel, melde)
    if rel.endswith(".iss"):
        return zeilenweise(text, rel, melde, iss_kommentar)
    return zeilenweise(text, rel, melde)


def dateien(pfade):
    if pfade:
        return [str(Path(p)) for p in pfade]
    out = subprocess.run(["git", "ls-files"], cwd=WURZEL, capture_output=True,
                         text=True, check=True).stdout
    return out.splitlines()


def main(argv):
    trocken = "--trocken" in argv or "--dry-run" in argv
    liste = "--liste" in argv
    pfade = [a for a in argv if not a.startswith("--")]
    d_geaendert = z_geaendert = 0
    for rel in dateien(pfade):
        if ausgenommen(rel):
            continue
        p = WURZEL / rel
        if not p.is_file():
            continue
        try:
            text = p.read_bytes().decode("utf-8")
        except UnicodeDecodeError:
            continue
        if not HAT_UMLAUT.search(text):
            continue
        meldungen = []
        neu = bearbeite(rel, text, lambda nr, z, w: meldungen.append((nr, z, w)))
        for nr, z, w in meldungen:
            print(f"AUSGELASSEN {rel}:{nr} [{w}] {z[:110]}")
        if neu != text:
            zl = sum(1 for a, b in zip(text.split("\n"), neu.split("\n")) if a != b)
            d_geaendert += 1
            z_geaendert += zl
            if liste:
                print(f"{rel}: {zl} Zeilen")
            if not trocken:
                p.write_bytes(neu.encode("utf-8"))
    print(f"{'(trocken) ' if trocken else ''}Dateien: {d_geaendert}, Zeilen: {z_geaendert}")


if __name__ == "__main__":
    main(sys.argv[1:])
