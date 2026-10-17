"""Herkunftsangaben und Beschreibungsdateien fuer ein Archiv — Pruefung an EINER Stelle.

`create_archive(..., provenance=, beilagen=)` nimmt freie Angaben; hier steht, was als
gueltig gilt und wie sie aus Kommandozeile und Oberflaeche entstehen.  Wer sie befuellt
(`archive.main`, `physical_cli`, der Archiv-Dialog, `tools/p8000/pofo_archive.py`),
ruft diese Funktionen — es gibt keine zweite Regel.

Ohne Qt, ohne Kern: reine Python-Standardbibliothek (+ yaml fuer ``--herkunft-datei``).
"""

from __future__ import annotations

import codecs
import json
import re
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

#: Groessengrenze je Beilage (Bytes).  Beschreibungstexte sind kurz; die Grenze fangt
#: vor allem die versehentlich gewaehlte Binaerdatei/Abbilddatei ab.
MAX_BEILAGE = 4 * 1024 * 1024

#: Schluessel von ``provenance``: Kleinbuchstaben, Ziffern, Unterstrich; Buchstabe vorn.
SCHLUESSEL_RE = re.compile(r"^[a-z][a-z0-9_]{0,63}$")

#: Die festen Felder der Oberflaeche und ihr Schluessel in ``provenance``.
FESTE_FELDER = (
    ("source_url", "Quelle / URL"),
    ("set_name", "Satz / Titel"),
    ("version", "Version"),
    ("web_label", "Etikett"),
    ("note", "Bemerkung"),
)


class ArchivMetaFehler(ValueError):
    """Eine Angabe ist unzulaessig; der Text ist fuer den Anwender gedacht."""


# ─── Provenance ──────────────────────────────────────────────────────────────

def pruefe_schluessel(schluessel: str) -> str:
    if not isinstance(schluessel, str) or not SCHLUESSEL_RE.match(schluessel):
        raise ArchivMetaFehler(
            f"Ungueltiger Schluessel '{schluessel}': erlaubt sind Kleinbuchstaben, "
            "Ziffern und Unterstrich, vorn ein Buchstabe (hoechstens 64 Zeichen)")
    return schluessel


def _skalar(wert):
    """Ein Wert fuer die YAML-Ausgabe: Text, Zahl, Wahrheitswert; Datum als Text."""
    if wert is None or isinstance(wert, (str, bool, int, float)):
        return wert
    if hasattr(wert, "isoformat"):                       # datetime.date / datetime
        return wert.isoformat()
    raise ArchivMetaFehler(f"Wert vom Typ {type(wert).__name__} ist nicht zulaessig "
                           "(erlaubt: Text, Zahl, ja/nein, Liste davon)")


def bereinige_provenance(angaben: Optional[dict]) -> dict:
    """Schluessel pruefen, Werte normieren, LEERE Angaben weglassen.

    ``hinweise`` muss eine Liste sein (das Inhaltsverzeichnis druckt sie Zeile fuer
    Zeile); ein Einzeltext wird zur Ein-Element-Liste.
    """
    aus: Dict[str, object] = {}
    for k, v in (angaben or {}).items():
        pruefe_schluessel(k)
        if isinstance(v, (list, tuple)):
            v = [_skalar(x) for x in v if _skalar(x) not in ("", None)]
        else:
            v = _skalar(v)
            if k == "hinweise" and v not in ("", None):
                v = [v]
        if isinstance(v, str):
            v = v.strip("\r\n") if "\n" in v else v.strip()
        if v in ("", None, []):
            continue
        aus[k] = v
    return aus


def herkunft_aus_datei(pfad) -> dict:
    """``--herkunft-datei``: eine YAML- oder JSON-Datei, deren Wurzel ein Dict ist."""
    p = Path(pfad)
    if not p.is_file():
        raise ArchivMetaFehler(f"Herkunftsdatei '{p}' gibt es nicht")
    try:
        text = p.read_bytes().decode("utf-8-sig")
        if p.suffix.lower() == ".json":
            daten = json.loads(text)
        else:                                  # YAML ist die Obermenge von JSON
            import yaml
            daten = yaml.safe_load(text)
    except UnicodeDecodeError:
        raise ArchivMetaFehler(f"Herkunftsdatei '{p}' ist nicht UTF-8") from None
    except Exception as e:                                           # noqa: BLE001
        raise ArchivMetaFehler(f"Herkunftsdatei '{p}' ist nicht lesbar: {e}") from None
    if daten is None:
        return {}
    if not isinstance(daten, dict):
        raise ArchivMetaFehler(f"Herkunftsdatei '{p}': die Wurzel muss eine "
                               "Schluessel/Wert-Tabelle sein")
    return daten


def herkunft_aus_paaren(paare: Iterable[str], basis: Optional[dict] = None) -> dict:
    """``--herkunft schluessel=wert`` (mehrfach) ueber ``basis`` legen.

    ``hinweise`` sammelt: jede Angabe haengt eine Zeile an die Liste.  Alles andere
    ueberschreibt den Wert der Datei.
    """
    aus = dict(basis or {})
    for p in paare or ():
        k, gleich, v = p.partition("=")
        if not gleich:
            raise ArchivMetaFehler(f"--herkunft erwartet schluessel=wert, nicht '{p}'")
        pruefe_schluessel(k.strip())
        k = k.strip()
        if k == "hinweise":
            alt = aus.get(k) or []
            aus[k] = (list(alt) if isinstance(alt, (list, tuple)) else [alt]) + [v]
        else:
            aus[k] = v
    return aus


# ─── Beilagen ────────────────────────────────────────────────────────────────

def _reserviert(stamm: str) -> set:
    from app.disktool.archive import DATEI_ORDNER, INVENTAR_DATEI
    namen = {INVENTAR_DATEI.lower(), DATEI_ORDNER.lower()}
    if stamm:
        namen |= {f"{stamm}.txt".lower(), f"{stamm}.hfe".lower()}
    return namen


def pruefe_beilagenname(name: str, *, stamm: str = "", unterordner: bool = False) -> str:
    """Der Name einer Beilage an der Wurzel der `.zip`.

    Ohne Pfadanteile (``unterordner=False``) — sonst keine ``..``, kein absoluter Pfad,
    kein Laufwerksbuchstabe, kein Steuerzeichen, nichts unter ``dateien/`` (das waere
    Diskettenbestand: `put` des Ordners fuegte es ein) und kein Name eines Mitglieds,
    das das Archiv selbst anlegt.
    """
    from app.disktool.archive import DATEI_ORDNER
    if not isinstance(name, str) or not name.strip():
        raise ArchivMetaFehler("Der Name der Beilage ist leer")
    if len(name) > 200:
        raise ArchivMetaFehler(f"Name der Beilage zu lang ({len(name)} Zeichen)")
    if any(ord(c) < 32 or c == "\x7f" for c in name):
        raise ArchivMetaFehler(f"Name der Beilage enthaelt Steuerzeichen: {name!r}")
    if "\\" in name or ":" in name or name.startswith("/"):
        raise ArchivMetaFehler(f"Name der Beilage '{name}': kein Pfad, Laufwerk oder "
                               "Rueckwaerts-Schraegstrich erlaubt")
    teile = name.split("/")
    if len(teile) > 1 and not unterordner:
        raise ArchivMetaFehler(f"Name der Beilage '{name}' darf keine Pfadanteile haben")
    for t in teile:
        if t in ("", ".", ".."):
            raise ArchivMetaFehler(f"Name der Beilage '{name}': '{t or '(leer)'}' "
                                   "ist als Pfadteil nicht erlaubt")
    if teile[0].lower() == DATEI_ORDNER.lower():
        raise ArchivMetaFehler(f"Name der Beilage '{name}' kollidiert mit dem Ordner "
                               f"'{DATEI_ORDNER}/' (dort liegen die Dateien der Diskette)")
    if name.lower() in _reserviert(stamm):
        raise ArchivMetaFehler(f"Name der Beilage '{name}' kollidiert mit einem Namen, "
                               "den das Archiv selbst vergibt")
    return name


def pruefe_beilagen(beilagen: Optional[dict], *, stamm: str = "",
                    unterordner: bool = False) -> None:
    """Alle Namen, Doppelte (ohne Beachtung der Gross-/Kleinschreibung) und die Groesse."""
    gesehen = set()
    for name, inhalt in (beilagen or {}).items():
        pruefe_beilagenname(name, stamm=stamm, unterordner=unterordner)
        if name.lower() in gesehen:
            raise ArchivMetaFehler(f"Beilage '{name}' kommt doppelt vor")
        gesehen.add(name.lower())
        n = len(inhalt.encode("utf-8") if isinstance(inhalt, str) else inhalt)
        if n > MAX_BEILAGE:
            raise ArchivMetaFehler(f"Beilage '{name}' ist {n} Byte gross "
                                   f"(Grenze {MAX_BEILAGE // (1024 * 1024)} MiB)")


def text_dekodieren(roh: bytes, kodierung: str = "auto", name: str = "") -> Tuple[str, str]:
    """Bytes -> (Text, benutzte Kodierung).

    ``auto``: UTF-8 (auch mit BOM), sonst Latin-1.  Bei ``auto`` ist eine Datei mit
    NUL-Bytes keine Textdatei und wird abgelehnt (UTF-16 braucht die ausdrueckliche
    Kodierung).  Zeilenenden bleiben unveraendert.
    """
    wer = f"'{name}' " if name else ""
    if kodierung in ("", "auto"):
        if b"\x00" in roh:
            raise ArchivMetaFehler(f"{wer}ist keine Textdatei (enthaelt NUL-Bytes)")
        try:
            return roh.decode("utf-8-sig"), "utf-8"
        except UnicodeDecodeError:
            return roh.decode("latin-1"), "latin-1"
    try:
        codecs.lookup(kodierung)
    except LookupError:
        raise ArchivMetaFehler(f"Unbekannte Kodierung '{kodierung}'") from None
    try:
        return roh.decode(kodierung), kodierung
    except UnicodeDecodeError as e:
        raise ArchivMetaFehler(f"{wer}laesst sich nicht als {kodierung} lesen: {e}") from None


def beilage_lesen(pfad, name: str = "", kodierung: str = "auto",
                  *, stamm: str = "") -> Tuple[str, str, str]:
    """Eine Beschreibungsdatei laden: ``(Name im Archiv, Text, Kodierung)``."""
    p = Path(pfad)
    if not p.is_file():
        raise ArchivMetaFehler(f"Beilage '{p}' gibt es nicht")
    n = p.stat().st_size
    if n > MAX_BEILAGE:
        raise ArchivMetaFehler(f"Beilage '{p.name}' ist {n} Byte gross "
                               f"(Grenze {MAX_BEILAGE // (1024 * 1024)} MiB)")
    text, kod = text_dekodieren(p.read_bytes(), kodierung, p.name)
    return pruefe_beilagenname(name or p.name, stamm=stamm), text, kod


def beilagen_aus_angaben(angaben: Iterable[str], kodierung: str = "auto",
                         *, stamm: str = "") -> Dict[str, str]:
    """``--beilage datei[=name]`` (mehrfach) -> ``{Name im Archiv: Text}``."""
    aus: Dict[str, str] = {}
    for a in angaben or ():
        pfad, name = a, ""
        if not Path(a).is_file() and "=" in a:
            pfad, _, name = a.rpartition("=")
        n, text, _ = beilage_lesen(pfad, name, kodierung, stamm=stamm)
        if n.lower() in {k.lower() for k in aus}:
            raise ArchivMetaFehler(f"Beilage '{n}' kommt doppelt vor")
        aus[n] = text
    return aus


# ─── Kommandozeile ───────────────────────────────────────────────────────────

def add_argumente(gruppe) -> None:
    """Die gemeinsamen Schalter an einen argparse-Parser/eine Gruppe haengen."""
    gruppe.add_argument(
        "--herkunft-datei", metavar="DATEI",
        help="YAML- oder JSON-Datei mit Herkunftsangaben (Schluessel/Wert); landet "
             "als `provenance:` in diskarchive.yaml")
    gruppe.add_argument(
        "--herkunft", metavar="SCHLUESSEL=WERT", action="append", default=[],
        help="einzelne Herkunftsangabe (mehrfach; ueberschreibt die Datei; Schluessel "
             "nur a-z, 0-9, _; `hinweise=` haengt eine Zeile an)")
    gruppe.add_argument(
        "--beilage", metavar="DATEI[=NAME]", action="append", default=[],
        help="Beschreibungsdatei an die Wurzel der .zip (mehrfach; Text, hoechstens "
             "4 MiB; wird nach UTF-8 uebernommen; NAME = Name im Archiv). Sie liegt "
             "NICHT unter dateien/ und kommt nie auf die Diskette")
    gruppe.add_argument(
        "--beilage-kodierung", metavar="KODIERUNG", default="auto",
        help="Kodierung der Beilagen (Vorgabe auto: UTF-8, sonst Latin-1)")


def aus_optionen(o, *, stamm: str = "") -> Tuple[dict, Dict[str, str]]:
    """``(provenance, beilagen)`` aus den Schaltern; leer = ``({}, {})``.

    Wirft :class:`ArchivMetaFehler` — der Aufrufer meldet es und beendet mit 2.
    """
    basis = herkunft_aus_datei(o.herkunft_datei) if getattr(o, "herkunft_datei", None) else {}
    prov = bereinige_provenance(herkunft_aus_paaren(getattr(o, "herkunft", ()), basis))
    beil = beilagen_aus_angaben(getattr(o, "beilage", ()),
                                getattr(o, "beilage_kodierung", "auto"), stamm=stamm)
    return prov, beil
