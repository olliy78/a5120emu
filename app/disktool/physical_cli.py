"""`k1520disktool --physical` — die Kommandozeile für ein **echtes** Laufwerk.

Warum das hier steht und nicht im C++-Werkzeug: `k1520disktool-cli` ist der
Dateiaustausch mit **Abbildern**, und der Kern kennt Greaseweazle bewusst nicht
(`doc/design/14_physische_diskette.md` §1).  Der Arbeitsfaden, der Aufträge abholt
und Bitzellen zurückgibt, ist Python — also liegt auch die physische Kommandozeile
auf der Python-Seite.  Sie benutzt genau dieselben Stücke wie die Oberfläche:
:class:`app.gw.PhysicalSession` für die Sitzung und
:class:`app.core_binding.k1520disk.DiskTool` für alles Weitere.

Aufruf::

    k1520disktool --physical [Sitzungsschalter] <befehl> [args]
    python3 app/disktool/main.py --physical ls

Der Grundsatz aus §13 gilt hier besonders, weil niemand nachfragt:
**physisch heißt schreibgeschützt, bis jemand widerspricht.**  Ohne ``--write``
lehnt jeder verändernde Befehl ab, bevor der Motor überhaupt anläuft.

Siehe doc/design/14_physische_diskette.md §12.3.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from typing import List, Optional

BEFEHLE = ("ls", "info", "check", "get", "put", "rm", "save-as", "rewrite")


# ─── Sitzung ─────────────────────────────────────────────────────────────────

def _parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="k1520disktool --physical",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Dateiaustausch mit der Diskette in einem echten Laufwerk "
                    "(Greaseweazle).",
        epilog="""Befehle:
  ls [-l]                     Verzeichnis (ohne -l nur die Namen)
  info                        Belegung und Erkennung
  check                       Pruefbericht
  get <muster…> --to <ordner> Dateien herausholen (ohne Muster: alles)
  put <datei|ordner…>         Dateien einfuegen            [--write]
  rm  <muster…>               Dateien loeschen             [--write]
  save-as <ziel.hfe>          die ganze Diskette als Abbild sichern
  rewrite                     Diskette neu beschreiben     [--write]

Ohne --write ist die Diskette schreibgeschuetzt — ein Original ist meist ein
Einzelstueck.  Vor dem ersten Schreibversuch lohnt `save-as`.

Beispiele:
  k1520disktool --physical ls -l
  k1520disktool --physical save-as sicherung.hfe
  k1520disktool --physical --write put NEU.TXT
  k1520disktool --physical --drive 0 --cyls 40 --double-step ls
""")
    p.add_argument("--physical", action="store_true", help=argparse.SUPPRESS)

    s = p.add_argument_group("Laufwerk und Diskette")
    s.add_argument("--drive", default="a",
                   help="Laufwerk am Kabel: a|b (PC) oder 0…3 (Shugart); Vorgabe a")
    s.add_argument("--cyls", type=int, default=80, help="Zylinder (Vorgabe 80)")
    s.add_argument("--heads", type=int, default=2, help="Koepfe (Vorgabe 2)")
    s.add_argument("--rate", type=int, default=250,
                   help="Zellrate in kbit/s: 250 = 5,25″ DD, 500 = 8″/HD")
    s.add_argument("--rpm", type=int, default=300, help="Drehzahl (Vorgabe 300)")
    s.add_argument("--double-step", action="store_true",
                   help="40-Spur-Diskette in einem 80-Spur-Laufwerk")

    z = p.add_argument_group("Zugriff")
    z.add_argument("--write", action="store_true",
                   help="Schreiben erlauben (ohne das: nur lesen)")
    z.add_argument("--no-verify", action="store_true",
                   help="Pruef-Lesen nach dem Schreiben abschalten (nicht empfohlen)")
    z.add_argument("--fs", metavar="NAME", help="Erkennung uebersteuern")
    z.add_argument("--raw", action="store_true",
                   help="roh oeffnen — ohne Dateisystem (nur save-as/rewrite)")
    z.add_argument("--text", action="store_true", help="Zeilenenden umsetzen")
    z.add_argument("--force", action="store_true", help="vorhandene ersetzen")
    z.add_argument("--to", metavar="ORDNER", help="Zielordner fuer get")
    z.add_argument("-l", "--long", action="store_true", help="ausfuehrliches ls")
    z.add_argument("-q", "--quiet", action="store_true",
                   help="keine Fortschrittsanzeige")

    p.add_argument("befehl", nargs="?", help=argparse.SUPPRESS)
    p.add_argument("rest", nargs="*", help=argparse.SUPPRESS)
    return p


class _Fortschritt:
    """Zaehlt mit, waehrend im Hintergrund Spuren geholt werden.

    Am Laufwerk dauert alles: eine Spur 0,5–0,8 s, die ganze Diskette zwei
    Minuten.  Eine Kommandozeile, die dabei nichts sagt, sieht aus wie eine, die
    haengt — deshalb eine Zeile auf **stderr** (stdout bleibt maschinenlesbar).
    """

    def __init__(self, sitzung, aus: bool):
        self.sitzung = sitzung
        self.aus = aus or not sys.stderr.isatty()
        self._letzte = -1.0

    def tick(self, was: str = "") -> None:
        if self.aus:
            return
        jetzt = time.monotonic()
        if jetzt - self._letzte < 0.3:
            return
        self._letzte = jetzt
        st = self.sitzung.stats()
        if st is None:
            return
        wo = f" c{st.busy_cyl}h{st.busy_head}" if st.busy else ""
        sys.stderr.write(f"\r{was}{st.tracks_known}/{st.tracks_total} Spuren{wo}   ")
        sys.stderr.flush()

    def fertig(self) -> None:
        if not self.aus:
            sys.stderr.write("\r" + " " * 60 + "\r")
            sys.stderr.flush()


def _mit_ticker(sitzung, arbeit, ruhig: bool, text: str = ""):
    """@p arbeit in einem Faden laufen lassen und derweil den Fortschritt zeigen."""
    import threading

    ergebnis: list = []
    fehler: list = []

    def lauf():
        try:
            ergebnis.append(arbeit())
        except BaseException as e:                      # noqa: BLE001
            fehler.append(e)

    f = _Fortschritt(sitzung, ruhig)
    t = threading.Thread(target=lauf, daemon=True)
    t.start()
    while t.is_alive():
        f.tick(text)
        t.join(0.2)
    f.fertig()
    if fehler:
        raise fehler[0]
    return ergebnis[0] if ergebnis else None


# ─── Befehle ─────────────────────────────────────────────────────────────────

def _passt(name: str, muster: List[str]) -> bool:
    import fnmatch
    if not muster:
        return True
    kurz = name.split("/")[-1]
    return any(fnmatch.fnmatch(name, m) or fnmatch.fnmatch(kurz, m) for m in muster)


def _cmd_ls(d, o) -> int:
    eintraege = d.list() if o.long else d.list_names()
    if not o.long:
        for e in eintraege:
            print(e.side_prefix + e.name)
        return 0

    print(f"Dateisystem: {d.filesystem} (erkannt) · "
          f"{d.volume_count} {'Seite' if d.volume_count == 1 else 'Seiten'}")
    print(f"{'Name':22s} {'Typ':4s} {'Groesse':>9s} {'Eigensch.':10s} {'Datum':7s}")
    for e in eintraege:
        print(f"{e.side_prefix + e.name:22s} {e.type or '-':4s} {e.size:9d} "
              f"{e.attrs or '-':10s} {e.date:7s}")
    for v in d.volumes():
        print(f"{v.dir or 'Diskette'}: {v.free} Byte frei von {v.total}")
    return 0


def _cmd_info(d, o) -> int:
    print(f"Herkunft:    echtes Laufwerk")
    print(f"Format:      {d.format}")
    print(f"Dateisystem: {d.filesystem or '(nicht erkannt)'}")
    if d.remarks:
        print(f"Befund:      {d.remarks}")
    for v in d.volumes():
        kopf = f"Datentraeger '{v.label}'" if not v.dir else f"{v.dir} '{v.label}'"
        print(f"{kopf}: {v.used} Byte belegt, {v.free} Byte frei")
    return 0


def _cmd_get(d, o) -> int:
    ziel = Path(o.to or ".")
    ziel.mkdir(parents=True, exist_ok=True)
    if not o.rest:
        d.extract_all(ziel, text=o.text)
        n = len(d.list_names())
        print(f"{n} Dateien nach {ziel}")
        return 0
    n = 0
    for e in d.list_names():
        name = e.side_prefix + e.name
        if not _passt(name, o.rest):
            continue
        d.extract(e.ref, ziel / name.replace("/", "_"), text=o.text)
        print(f"{name} → {ziel / name.replace('/', '_')}")
        n += 1
    if n == 0:
        print("Fehler: kein Eintrag passt", file=sys.stderr)
        return 1
    return 0


def _cmd_put(d, o) -> int:
    if not o.rest:
        print("Fehler: put braucht Dateien oder einen Ordner", file=sys.stderr)
        return 1
    for pfad in o.rest:
        q = Path(pfad)
        if q.is_dir():
            d.insert_all(q, text=o.text, overwrite=o.force)
            print(f"eingefuegt aus {q}")
        else:
            d.insert(q, q.name, text=o.text, overwrite=o.force)
            print(f"{q} → {q.name}")
    return 0


def _cmd_rm(d, o) -> int:
    if not o.rest:
        print("Fehler: rm braucht ein Muster", file=sys.stderr)
        return 1
    n = 0
    for e in d.list_names():
        name = e.side_prefix + e.name
        if not _passt(name, o.rest):
            continue
        d.erase(e.ref)
        print(f"geloescht: {name}")
        n += 1
    if n == 0:
        print("Fehler: kein Eintrag passt", file=sys.stderr)
        return 1
    return 0


def _cmd_save_as(d, o) -> int:
    if not o.rest:
        print("Fehler: save-as braucht einen Zieldateinamen", file=sys.stderr)
        return 1
    ziel = Path(o.rest[0])
    # export_image schreibt eine Kopie und laesst die Bindung, wie sie ist — bei
    # einer physischen Diskette gibt es keine Datei, an die man sich neu binden
    # koennte (§12.2).
    d.export_image(ziel)
    print(f"gesichert: {ziel} ({ziel.stat().st_size} Byte)")
    return 0


# ─── Einstieg ────────────────────────────────────────────────────────────────

def main(argv: Optional[List[str]] = None) -> int:
    o = _parser().parse_args(argv if argv is not None else sys.argv[1:])

    if not o.befehl:
        _parser().print_help()
        return 1
    if o.befehl not in BEFEHLE:
        print(f"Fehler: unbekannter Befehl '{o.befehl}' "
              f"(bekannt: {', '.join(BEFEHLE)})", file=sys.stderr)
        return 1

    # ERST die Argumente pruefen, DANN das Laufwerk anfassen (Entwurf §12.3): ein
    # Tippfehler soll nicht erst nach zwei Minuten Einlesen auffallen — und er soll
    # auch dann gemeldet werden, wenn die Hosttools gar nicht installiert sind.
    # Frueher stand die Verfuegbarkeitspruefung hier davor; damit haing die Aussage
    # „dafuer braucht es --write" an einer freiwilligen Abhaengigkeit.
    veraendernd = o.befehl in ("put", "rm", "rewrite")
    if veraendernd and not o.write:
        print(f"Fehler: '{o.befehl}' veraendert die eingelegte Diskette — dafuer "
              f"braucht es --write.\nEine echte Diskette ist meist ein Einzelstueck; "
              f"vorher lohnt `--physical save-as sicherung.hfe`.", file=sys.stderr)
        return 1
    if o.raw and o.befehl not in ("save-as", "rewrite", "info"):
        print(f"Fehler: --raw gibt es ohne Dateisystem — '{o.befehl}' braucht eines.",
              file=sys.stderr)
        return 1

    from app.core_binding.k1520disk import DiskTool, K1520DiskError
    from app.gw import GreaseweazleFehlt, PhysicalSession

    try:
        sitzung = PhysicalSession.start(
            drive=o.drive, cell_rate_kbps=o.rate, num_cyls=o.cyls,
            num_heads=o.heads, writable=o.write, rpm=o.rpm,
            read_ahead=True, verify_writes=not o.no_verify,
            double_step=o.double_step)
    except GreaseweazleFehlt:
        # Die ausfuehrliche Fassung nennt DIESEN Interpreter — genau die Auskunft,
        # die fehlt, wenn man im venv eines anderen Projekts steckt.
        from app.gw import verfuegbarkeit
        print(verfuegbarkeit()[1], file=sys.stderr)
        return 1
    except Exception as e:                                   # noqa: BLE001
        print(f"Fehler: {e}", file=sys.stderr)
        return 1

    rc = 1
    try:
        oeffnen = (DiskTool.open_physical_raw if o.raw else DiskTool.open_physical)
        try:
            d = _mit_ticker(sitzung,
                            lambda: oeffnen(sitzung, o.fs, read_only=not o.write),
                            o.quiet, "erkennen: ")
        except K1520DiskError as e:
            print(f"Fehler: {e}", file=sys.stderr)
            return 2

        with d:
            if not o.quiet:
                print(f"erkannt: {d.format} / {d.filesystem or '(roh)'}",
                      file=sys.stderr)
            try:
                if o.befehl == "rewrite":
                    n = sitzung.rewrite_all()
                    print(f"{n} Spuren zum Neubeschreiben vorgemerkt")
                elif o.befehl == "check":
                    print(_mit_ticker(sitzung, d.check, o.quiet, "pruefen: "))
                else:
                    tabelle = {"ls": _cmd_ls, "info": _cmd_info, "get": _cmd_get,
                               "put": _cmd_put, "rm": _cmd_rm, "save-as": _cmd_save_as}
                    rc = _mit_ticker(sitzung, lambda: tabelle[o.befehl](d, o),
                                     o.quiet, f"{o.befehl}: ")
                    if rc:
                        return rc
                rc = 0
            except K1520DiskError as e:
                print(f"Fehler: {e}", file=sys.stderr)
                return 1

            # Rueckfuehrung: erst hier steht die Aenderung auf der Scheibe (§7.1).
            # Eine Schadstelle meldet sich hier — und nur hier: bis dahin sah alles
            # nach Erfolg aus, weil das Abbild im Speicher ja stimmt.
            if o.write and veraendernd:
                gelungen = True
                grund = ""
                try:
                    d.flush()
                    gelungen = _mit_ticker(sitzung,
                                           lambda: sitzung.sync.flush(180_000),
                                           o.quiet, "zurueckschreiben: ")
                    if not gelungen:
                        grund = sitzung.sync.last_error
                except K1520DiskError as e:
                    gelungen, grund = False, str(e)

                st = sitzung.stats()
                if st:
                    print(f"{st.writes_done} Spuren geschrieben, "
                          f"{st.verifies_done} geprueft, "
                          f"{st.verify_failed} Vergleiche misslungen", file=sys.stderr)
                if not gelungen:
                    print(f"Fehler beim Zurueckschreiben: {grund}", file=sys.stderr)
                if st and st.tracks_defect:
                    print(f"Schadstelle auf {st.tracks_defect} Spur(en): "
                          f"{sitzung.defect_tracks}\n"
                          f"Das ist die Diskette, nicht das Programm — zweimal "
                          f"geschrieben, zweimal anders zurueckgelesen.  Das Abbild "
                          f"im Speicher ist heil: mit `--physical save-as datei.hfe` "
                          f"retten, oder eine fehlerfreie Diskette einlegen und "
                          f"`--physical --write rewrite` fahren.", file=sys.stderr)
                    return 1
                if not gelungen:
                    return 1
    finally:
        # Lesewiederholungen sind eine Aussage ueber die DISKETTE, nicht ueber den
        # Lauf — sie gehoeren auch unter ein blosses `ls` oder `get`.  Nur melden,
        # wenn es welche gab; sonst waere es Rauschen.
        _st = sitzung.stats()
        if _st and _st.read_retries:
            print(f"{_st.read_retries} Lesewiederholung(en) wegen fehlerhafter "
                  f"Pruefsumme"
                  + (f"; {_st.read_crc_bad} Spur(en) blieben fehlerhaft"
                     if _st.read_crc_bad else " — danach fehlerfrei"),
                  file=sys.stderr)
        sitzung.close()
    return rc


if __name__ == "__main__":       # pragma: no cover
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    sys.exit(main())
