"""
K1520-Emulator — Versionsableitung
==================================

EINE Stelle für alles, was mit der Programmversion zu tun hat
(``doc/ci_pipeline.md`` §7).  Nur Standardbibliothek: das Kommandozeilenwerkzeug
``tools/version.py`` lädt diese Datei direkt (ohne venv, auch unter Windows),
und eine Installation braucht sie für das Über-Fenster und den Beispielordner.

Zwei Begriffe:

* **Basisversion** — Inhalt der Datei ``VERSION`` im Wurzelverzeichnis:
  ``MAJOR.MINOR.PATCH`` oder ``MAJOR.MINOR.PATCH-beta`` / ``-rc`` (ohne
  laufende Nummer; die steht nur im Tag).
* **Bauversion** — was ein Bau tatsächlich meldet: das Tag auf ``HEAD`` ohne
  ``v`` (``0.3.0-beta.2``), sonst ``<Basis>+g<kurzhash>`` (bei unsauberem Baum
  mit ``.dirty``), ohne git ``<Basis>+unbekannt``.

Wer die Bauversion zur LAUFZEIT will, ruft :func:`fassung`.
"""

import re
import subprocess
from pathlib import Path
from typing import List, Optional, Tuple

#: Basisversion: Zahlen, optional ``-beta`` / ``-rc``.
_BASIS = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-(beta|rc))?$")
#: Versionstext eines Tags (ohne ``v``): Vorabversionen tragen eine laufende Nummer.
_TAG = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-(beta|rc)\.(\d+))?$")

#: Reihenfolge der Stufen: beta < rc < Endfassung.
_STUFE = {"beta": 0, "rc": 1, None: 2}


class VersionsFehler(ValueError):
    """Eine Versionsangabe verletzt die Grammatik oder die Regeln aus §7."""


# ─── Zerlegen ────────────────────────────────────────────────────────────────

def zerlege_basis(text: str) -> Tuple[int, int, int, Optional[str]]:
    """``0.3.0-beta`` → ``(0, 3, 0, 'beta')``; Endfassung: Stufe ``None``."""
    m = _BASIS.match(text.strip())
    if not m:
        raise VersionsFehler(
            f"'{text.strip()}' ist keine gültige Basisversion "
            "(MAJOR.MINOR.PATCH[-beta|-rc], ohne laufende Nummer)")
    return int(m[1]), int(m[2]), int(m[3]), m[4]


def zerlege_tag(text: str) -> Tuple[int, int, int, Optional[str], int]:
    """``0.3.0-beta.2`` → ``(0, 3, 0, 'beta', 2)``; Endfassung: ``(…, None, 0)``.

    Das führende ``v`` eines Tags wird toleriert.
    """
    roh = text.strip()
    m = _TAG.match(roh[1:] if roh.startswith("v") else roh)
    if not m:
        raise VersionsFehler(
            f"'{roh}' ist keine Versionsangabe "
            "(X.Y.Z, X.Y.Z-beta.N oder X.Y.Z-rc.N mit N ≥ 1)")
    stufe = m[4]
    n = int(m[5]) if m[5] else 0
    if stufe and n < 1:
        raise VersionsFehler(f"'{roh}': die laufende Nummer beginnt bei 1")
    return int(m[1]), int(m[2]), int(m[3]), stufe, n


def sortierschluessel(version: str) -> tuple:
    """Ordnung von Tag-Versionen: ``0.3.0-beta.2 < 0.3.0-rc.1 < 0.3.0 < 0.3.1-beta.1``."""
    a, b, c, stufe, n = zerlege_tag(version)
    return (a, b, c, _STUFE[stufe], n)


def tag_passt(tag: str, basis: str) -> bool:
    """Schreibt das Tag die Basis fort?  ``v0.3.0-beta.N`` zu ``0.3.0-beta`` usw."""
    try:
        a, b, c, stufe, n = zerlege_tag(tag)
        ba, bb, bc, bstufe = zerlege_basis(basis)
    except VersionsFehler:
        return False
    return (a, b, c) == (ba, bb, bc) and stufe == bstufe


# ─── Ableitungen aus der Bauversion ──────────────────────────────────────────

def ist_vorabversion(bau: str) -> bool:
    """Trägt die Bauversion ``-beta``/``-rc`` (vor einem etwaigen ``+…``)?"""
    return "-" in bau.split("+", 1)[0]


def windows_zahl(bau: str) -> str:
    """Vierstellige Zahl ``X.Y.Z.B`` für ``VersionInfoVersion`` des Installers.

    B = N bei ``beta.N`` (1–49), 50+N bei ``rc.N``, 100 bei der Endfassung,
    0 bei einem Bau ohne Tag (``+g…``, ``+dev``, ``+unbekannt``).  Damit liegt
    0.3.0 in den Dateieigenschaften über allen Betas.
    """
    if "+" in bau:
        a, b, c, _ = zerlege_basis(bau.split("+", 1)[0])
        return f"{a}.{b}.{c}.0"
    a, b, c, stufe, n = zerlege_tag(bau)
    if stufe is None:
        zusatz = 100
    elif stufe == "beta":
        zusatz = min(n, 49)
    else:
        zusatz = 50 + min(n, 49)
    return f"{a}.{b}.{c}.{zusatz}"


def beispielordner(bau: str) -> str:
    """Name des Beispieldiskettenordners dieser Fassung (§7.5).

    * Endfassung ``0.3.x``          → ``Beispieldisketten_v03`` (ein Patch-Release
      legt keinen neuen Ordner an)
    * Vorabversion ``0.3.0-beta.2`` → ``Beispieldisketten_v03-beta.2``
    * Bau ohne Tag                 → ``Beispieldisketten_v03-test`` (kein Ordner je Commit)

    Das Kürzel ``v<MAJOR><MINOR>`` nennt MINOR zweistellig und lässt ein
    MAJOR von 0 weg: ``0.3`` → ``v03``, ``1.2`` → ``v102``.
    """
    ohne_zusatz = bau.split("+", 1)[0]
    try:
        zerlege_tag(ohne_zusatz) if "+" not in bau else zerlege_basis(ohne_zusatz)
    except VersionsFehler:
        return "Beispieldisketten_unbekannt"     # z. B. „unbekannt" ohne VERSION-Datei
    if "+" in bau:
        a, b, _c, _s = zerlege_basis(ohne_zusatz)
        suffix = "-test"
    else:
        a, b, _c, stufe, n = zerlege_tag(ohne_zusatz)
        suffix = f"-{stufe}.{n}" if stufe else ""
    kuerzel = f"v{a if a else ''}{b:02d}"
    return f"Beispieldisketten_{kuerzel}{suffix}"


# ─── Quellbaum: Basis und git ────────────────────────────────────────────────

def wurzel() -> Path:
    """Das Elternverzeichnis von ``app/`` (Quellbaum bzw. Installationswurzel)."""
    return Path(__file__).resolve().parents[1]


def lies_basis(root: Optional[Path] = None) -> str:
    """Inhalt von ``<root>/VERSION`` (Quellbaum), geprüft gegen die Grammatik."""
    datei = (root or wurzel()) / "VERSION"
    try:
        zeile = datei.read_text(encoding="utf-8").strip().splitlines()[0]
    except (OSError, IndexError):
        raise VersionsFehler(f"{datei} fehlt oder ist leer")
    zerlege_basis(zeile)
    return zeile


def _git(root: Path, *args: str) -> Optional[str]:
    try:
        r = subprocess.run(["git", "-C", str(root), *args], capture_output=True,
                           text=True, timeout=30)
    except (OSError, subprocess.SubprocessError):
        return None
    return r.stdout if r.returncode == 0 else None


def tags_auf_head(root: Path) -> List[str]:
    """Alle Tags auf ``HEAD`` (leer ohne git)."""
    aus = _git(root, "tag", "--points-at", "HEAD")
    return aus.split() if aus else []


def alle_tags(root: Path) -> List[str]:
    """Alle Tags des Repos (leer ohne git)."""
    aus = _git(root, "tag", "--list")
    return aus.split() if aus else []


def bauversion(root: Optional[Path] = None) -> str:
    """Die Bauversion des Quellbaums (Tabelle in §7.1)."""
    root = root or wurzel()
    basis = lies_basis(root)
    passende = [t for t in tags_auf_head(root) if tag_passt(t, basis)]
    if passende:
        return max(passende, key=sortierschluessel).lstrip("v")
    hash_ = _git(root, "rev-parse", "--short=7", "HEAD")
    if not hash_ or not hash_.strip():
        return f"{basis}+unbekannt"
    unsauber = _git(root, "status", "--porcelain")
    return f"{basis}+g{hash_.strip()}" + (".dirty" if unsauber and unsauber.strip() else "")


def pruefe_neue_version(version: str, vorhandene_tags: List[str]) -> None:
    """Für ``tools/dev.sh release``: Grammatik, Tag frei, größer als jedes Tag.

    Raises:
        VersionsFehler: mit einer Meldung, die der Anwender lesen kann.
    """
    zerlege_tag(version)
    schluessel = sortierschluessel(version)
    for t in vorhandene_tags:
        try:
            vorhanden = sortierschluessel(t)
        except VersionsFehler:
            continue            # fremde Tags (z. B. „vor-merge-…") zählen nicht
        if vorhanden == schluessel:
            raise VersionsFehler(f"Tag v{version} existiert bereits")
        if vorhanden > schluessel:
            raise VersionsFehler(
                f"v{version} ist nicht größer als das vorhandene Tag {t}")


def basis_zu(version: str) -> str:
    """Basisversion, die ``VERSION`` zu einer Release-Version tragen muss.

    ``0.3.0-beta.1`` → ``0.3.0-beta``; ``0.3.0`` → ``0.3.0``.
    """
    a, b, c, stufe, _n = zerlege_tag(version)
    return f"{a}.{b}.{c}" + (f"-{stufe}" if stufe else "")


def naechste_basis(version: str) -> str:
    """Basis nach einer Endfassung: ``0.3.0`` → ``0.4.0-beta``; ``0.3.1`` → ``0.3.2-beta``."""
    a, b, c, stufe, _n = zerlege_tag(version)
    if stufe:
        raise VersionsFehler("eine Folgefassung gibt es nur nach einer Endfassung")
    return f"{a}.{b + 1}.0-beta" if c == 0 else f"{a}.{b}.{c + 1}-beta"


# ─── Laufzeit ────────────────────────────────────────────────────────────────

def _installierte_version(root: Path) -> Optional[str]:
    """Erste Angabe der mitgelieferten ``VERSION`` einer Installation.

    Das Paket schreibt ``<Bauversion> (<Plattform>, <Datum>, Python x.y)``;
    gültig ist nur die erste Angabe.  Die Datei im Quellbaum (nur die Basis)
    erkennt man daran, dass daneben ``tools/version.py`` liegt — dort gilt
    :func:`bauversion`.
    """
    if (root / "tools" / "version.py").is_file():
        return None
    try:
        erste = (root / "VERSION").read_text(encoding="utf-8").split()[0]
    except (OSError, IndexError):
        return None
    return erste


def fassung(root: Optional[Path] = None) -> str:
    """Die Bauversion des laufenden Programms (Über-Fenster, Handbuch, Konfiguration).

    Installation: erste Angabe der mitgelieferten Datei ``VERSION``.  Quellbaum:
    :func:`bauversion` (ruft git; Ergebnis wird je Prozess gemerkt).  Lässt sich
    nichts ermitteln, kommt ``unbekannt`` — die Oberfläche startet trotzdem.
    """
    if root is None:
        return _fassung_gemerkt()
    return _ermittle(root)


def _ermittle(root: Path) -> str:
    inst = _installierte_version(root)
    if inst:
        return inst
    try:
        return bauversion(root)
    except VersionsFehler:
        return "unbekannt"


_gemerkt: dict = {}


def _fassung_gemerkt() -> str:
    """Je Wurzel einmal ermitteln — der Quellbaum fragt dafür git (3 Prozesse)."""
    from app import paths
    try:
        wurzel_ = paths.base_dir()
    except Exception:
        wurzel_ = wurzel()
    if wurzel_ not in _gemerkt:
        _gemerkt[wurzel_] = _ermittle(wurzel_)
    return _gemerkt[wurzel_]
