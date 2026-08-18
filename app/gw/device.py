"""Greaseweazle als Diskettenlaufwerk — dünne Hülle um die Hosttools.

Die einzige Stelle im Programm, die ``greaseweazle`` importiert.  Nach außen gibt es
nur zwei Vorgänge, und beide sprechen **HFE-Bitzellen** (LSB zuerst je Byte) — dieselbe
Darstellung, die auch in einer ``.hfe``-Datei steht und die der Kern mit demselben
``BitCodec`` liest wie ein Dateiabbild (doc/design/14_physische_diskette.md §8).

Die Hosttools liegen **nicht** auf PyPI.  In einer Installation aus dem Paket sind
sie bereits eingerichtet (der Installer spielt ein mitgeliefertes Rad ein, siehe
``packaging/gw_pins.txt``); im Quellbaum installiert man sie von Hand::

    venv/bin/python3 -m pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"

Fehlt das Paket oder der Adapter, wirft :func:`open_device` einen
:class:`GreaseweazleFehlt` bzw. :class:`KeinAdapter` — die Oberfläche sperrt dann den
Menüpunkt und nennt den Grund, sonst ändert sich nichts.
"""

from __future__ import annotations

import contextlib
import importlib
import sys
import time
from dataclasses import dataclass
from typing import Optional


class GwFehler(RuntimeError):
    """Oberbegriff aller Fehler dieser Schicht."""


class GreaseweazleFehlt(GwFehler):
    """Das Python-Paket ``greaseweazle`` ist nicht installiert."""


class KeinAdapter(GwFehler):
    """Kein Greaseweazle am USB gefunden (oder keine Rechte am Anschluss)."""


def _leise(modul: str):
    """Ein Greaseweazle-Modul einlesen, ohne dass es auf die Ausgabe schreibt.

    ``greaseweazle.optimised`` meldet beim Einlesen auf der **Standardausgabe**, ob
    seine C-Beschleunigung da ist — und im ausgelieferten Rad ist sie es bewusst
    nicht (``packaging/gw_pins.txt``: beide Aufrufstellen fallen auf Python zurück,
    das kostet ~25 ms je Spur gegenüber 500–800 ms Lesezeit).  Die Zeile stünde damit
    mitten in der Nutzlast von ``k1520disktool --physical``, wo die Standardausgabe
    das Ergebnis ist und alles Beiläufige auf die Fehlerausgabe gehört.

    Umgeleitet statt verworfen: eine Meldung, die eine künftige Fassung dort abgibt,
    soll sichtbar bleiben — nur eben am richtigen Ort.
    """
    with contextlib.redirect_stdout(sys.stderr):
        return importlib.import_module(modul)


def verfuegbar() -> bool:
    """Ist das Paket ``greaseweazle`` installiert?  (Fragt **kein** Gerät ab.)"""
    try:
        _leise("greaseweazle")
    except ImportError:
        return False
    return True


#: Vorlauf, der vor dem Sektorkopf noch zur Spur gehört (8 Byte à 16 Zellen).  Vor der
#: Sync-Gruppe steht Gap/Vorlauf, hier ist der Schnitt also folgenlos.
_VORLAUF_ZELLEN = 8 * 16


def _feldanfaenge(zellen, bis: int):
    """Felder im Bereich ``[0, bis)`` als ``[(anfang, 'id'|'dat', ende), …]``.

    ``anfang`` ist die Zellposition der Sync-Gruppe, ``ende`` die erste Zelle NACH dem
    Feld (Marke + Inhalt + CRC).  ``ende > bis`` heisst: das Feld reicht über die Spur
    hinaus, ist also von der Naht durchschnitten.

    Erkennt beide Verfahren, weil eine echte Diskette FM- und MFM-Spuren mischen darf
    und hier niemand weiss, welche gerade unter dem Kopf liegt:

    * **MFM** — Sync-Gruppe aus 0xA1 mit ausgelassenem Taktbit (Zellwort 0x4489), danach
      ist die Marke das erste Byte, das kein 0xA1 ist (auch ein regulär kodiertes 0xA1
      gehört noch zur Gruppe, s. `BitCodec::decode`).
    * **FM** — die Marke steckt im Sync-Wort selbst (Takt 0xC7): 0xF57E = IDAM,
      0xF56F = Datenfeld, 0xF56A = gelöschtes Datenfeld.
    """
    from bitarray import bitarray

    a1 = bitarray("0100010010001001")           # MFM 0xA1 mit ausgelassenem Takt
    fm = {"1111010101111110": "id",             # FM 0xFE / Takt 0xC7
          "1111010101101111": "dat",            # FM 0xFB / Takt 0xC7
          "1111010101101010": "dat"}            # FM 0xF8 / Takt 0xC7

    def byte_bei(pos: int):
        if pos + 16 > len(zellen):
            return None
        v = 0
        for j in range(8):
            v = (v << 1) | zellen[pos + 2 * j + 1]
        return v

    #: (anfang, art, marken_position) — die Länge folgt erst unten, weil ein Datenfeld
    #: seine Länge aus dem VORANGEHENDEN Sektorkopf bezieht.
    rohe = []
    for muster, art in fm.items():
        p = 0
        m = bitarray(muster)
        while True:
            p = zellen.find(m, p, bis)
            if p < 0:
                break
            rohe.append((p, art, p))             # FM: die Marke IST das Sync-Wort
            p += 16

    p = 0
    while True:
        p = zellen.find(a1, p, bis)
        if p < 0:
            break
        q = p
        while byte_bei(q) == 0xA1:               # ganze Sync-Gruppe überspringen
            q += 16
        marke = byte_bei(q)
        if marke == 0xFE:
            rohe.append((p, "id", q))
        elif marke in (0xFB, 0xF8):
            rohe.append((p, "dat", q))
        p = max(q, p + 16)

    rohe.sort()
    felder = []
    groesse = 128                                # bis der erste Kopf etwas anderes sagt
    for anfang, art, marke_bei in rohe:
        if art == "id":
            # Marke + cyl head sec sizecode + 2 CRC = 7 Byte; sizecode steuert die
            # Länge der Datenfelder dahinter (2 Bit: 128 … 1024 B).
            code = byte_bei(marke_bei + 4 * 16)
            if code is not None:
                groesse = 128 << (code & 0x03)
            ende = marke_bei + 7 * 16
        else:
            ende = marke_bei + (1 + groesse + 2) * 16
        felder.append((anfang, art, ende))
    return felder


def naht_vor_sektorkopf(zellen, eine_umdrehung: int) -> int:
    """Wo darf die Spur aufgeschnitten werden?  Liefert die Startzelle.

    Eine ``TrackImage`` ist **eine Umdrehung**, also braucht sie einen Anfang, und der
    naheliegende ist das Indexloch.  Nur liegt der Spuranfang des schreibenden Rechners
    nicht zwangsläufig dort: schneidet man stur am Index, wird der Sektor, den die
    Index-Naht überspannt, **zerhackt** — sein Kopf landet am Spurende, sein Datenfeld am
    Spuranfang, und keins von beiden ist mehr als Sektor lesbar.  Bei UDOS reisst damit
    die Zeigerkette der Datei (an einer fremdbeschriebenen Diskette gemessen: 46 Dateien
    mit dieser Funktion, 12 ohne sie).

    Geschnitten wird deshalb **kurz vor einem Sektorkopf** — dort liegt Vorlauf, der
    Schnitt ist folgenlos.  Ob überhaupt etwas zu tun ist, sagen die Felder der Spur;
    gespalten ist ein Sektor in genau drei Lagen:

    * das **erste** Feld ist ein Datenfeld — sein Kopf steht jenseits der Naht;
    * das **letzte** Feld ist ein Kopf — sein Datenfeld steht jenseits der Naht;
    * das letzte Feld **reicht über das Spurende hinaus** — die Naht liegt mitten darin
      (dieser Fall ist der heimtückische: am Spuranfang liegen dann Datenbytes ohne
      Marke, es gibt dort also nichts zu sehen).

    Sonst bleibt es beim Index, und der Winkel im Diskeditor stimmt exakt.

    Absichtlich NICHT über die Periodizität des Füllmusters: eine Folge gleicher Bytes
    sieht im Zellstrom aus wie eine Lücke, und Datenfelder sind voll davon (0x00-Vorlauf,
    0xE5-Füllung eines frisch formatierten Sektors).  Ein darauf gestützter Schnitt landet
    mitten im Sektor — nachweisbar an genau der Diskette, um die es hier geht.

    Args:
        zellen: Bitzellen von MEHR als einer Umdrehung (MSB zuerst, ``bitarray``).
        eine_umdrehung: Zellzahl einer Umdrehung.

    Returns:
        Startzelle für den Schnitt — ``0``, wenn nichts zu verschieben ist.
    """
    if len(zellen) < eine_umdrehung + _VORLAUF_ZELLEN:
        return 0                      # nur eine Umdrehung da: nichts zu wählen

    felder = _feldanfaenge(zellen, eine_umdrehung)
    if not felder:
        return 0                      # keine Felder — nichts zu retten
    kopf = next((p for p, art, _ in felder if art == "id"), None)
    if kopf is None:
        return 0

    erste_art = felder[0][1]
    letzte_art = felder[-1][1]
    letztes_ende = felder[-1][2]
    gespalten = (erste_art == "dat"
                 or letzte_art == "id"
                 or letztes_ende > eine_umdrehung)
    if not gespalten:
        return 0                      # Index bleibt Index

    start = kopf - _VORLAUF_ZELLEN
    if start <= 0 or start + eine_umdrehung > len(zellen):
        return 0
    return start


@dataclass(frozen=True)
class Adapter:
    """Was ``gw info`` über das gefundene Gerät sagt."""

    port: str
    model: str
    firmware: str
    serial: str


class Device:
    """Ein gewähltes Laufwerk an einem Greaseweazle.

    Das Laufwerk bleibt für die Dauer der Sitzung **gewählt**; der Motor läuft mit
    Nachlauf (:attr:`motor_idle_s`), weil ein Anlauf rund eine halbe Sekunde kostet und
    ihn je Spur zu bezahlen das Vorauslesen sinnlos machte.

    Alle Methoden sind für **einen** Faden gedacht — den Arbeitsfaden.
    """

    #: Sekunden ohne Auftrag, nach denen der Motor abgeschaltet wird.
    motor_idle_s: float = 3.0

    def __init__(self, usb, drive, *, cell_rate_kbps: int = 250, revs: int = 1,
                 double_step: bool = False):
        self._usb = usb
        self._drive = drive
        self.cell_rate_kbps = cell_rate_kbps
        self.revs = revs
        #: **Doppelschritt**: die Spur *n* liegt auf dem physischen Zylinder *2n*.
        #:
        #: So schreibt ein 40-Spur-Laufwerk (K5600.10) auf eine 5,25″-Diskette; ein
        #: 80-Spur-Laufwerk (K5601) fährt dieselben Spuren mit doppeltem Schritt an.
        #: Der Umrechnung wegen bleibt ALLES darüber — Medium, Erkennung, Abbild —
        #: in LOGISCHEN Spuren: ein so gelesenes Abbild hat 40 Spuren, keine 80 mit
        #: Lücken.  Das ist der Unterschied zu `DiskFormat::step`, das eine
        #: Eigenschaft der Diskette beschreibt; hier geht es um den Kopfweg.
        self.double_step = double_step
        self._motor_an = False
        self._letzte_arbeit = 0.0
        self._gewaehlt = False
        self._ticks_per_rev = None

    # ── Auswahl / Motor ─────────────────────────────────────────────────────

    def select(self) -> None:
        """Bus einstellen und Laufwerk wählen (einmal je Sitzung)."""
        if self._gewaehlt:
            return
        self._usb.set_bus_type(self._drive.bus.value)
        self._usb.drive_select(self._drive.unit_id)
        self._gewaehlt = True

    def deselect(self) -> None:
        if not self._gewaehlt:
            return
        try:
            self.motor(False)
        finally:
            self._usb.drive_deselect()
            self._gewaehlt = False

    def motor(self, an: bool) -> None:
        if an == self._motor_an:
            return
        self._usb.drive_motor(self._drive.unit_id, an)
        self._motor_an = an
        if an:
            time.sleep(0.5)      # Anlauf: die erste Umdrehung wäre sonst unbrauchbar

    def tick(self) -> None:
        """Nachlauf abarbeiten — regelmäßig aus der Leerlaufschleife rufen."""
        if (self._motor_an and self._letzte_arbeit
                and time.monotonic() - self._letzte_arbeit > self.motor_idle_s):
            self.motor(False)

    # ── Die beiden Vorgänge ─────────────────────────────────────────────────

    def _position(self, cyl: int) -> int:
        """Logische Spur → physischer Zylinder (§12.5)."""
        return cyl * 2 if self.double_step else cyl

    def read_track(self, cyl: int, head: int) -> tuple[bytes, int]:
        """Eine Spurseite lesen.

        Args:
            cyl: **logische** Spur — bei Doppelschritt fährt der Kopf auf 2·cyl.

        Returns:
            ``(zellen, bitcells)`` — HFE-Konvention (LSB zuerst je Byte).
        """
        from bitarray import bitarray
        PLLTrack = _leise("greaseweazle.track").PLLTrack

        self.select()
        self.motor(True)
        self._usb.seek(self._position(cyl), head)
        # Eine Umdrehung mehr lesen, als gebraucht wird: cue_at_index() schneidet
        # anschließend genau bei Index — sonst fehlte der Anfang der Spur.
        fluss = self._usb.read_track(revs=self.revs + 1)
        fluss.cue_at_index()
        roh = PLLTrack(clock=5e-4 / self.cell_rate_kbps, data=fluss)
        bits, _ = roh.get_revolution(0)
        eine = len(bits)

        # Die zweite Umdrehung dranhängen, damit der Schnitt vor einen Sektorkopf gelegt
        # werden kann (s. naht_vor_sektorkopf) — sonst zerhackt der Index-Schnitt den
        # Sektor, den die Naht überspannt.
        try:
            weiter, _ = roh.get_revolution(1)
        except Exception:                      # nur eine Umdrehung gelesen
            weiter = None
        if weiter:
            ganz = bitarray(endian="big")
            ganz += bits
            ganz += weiter
            start = naht_vor_sektorkopf(ganz, eine)
            if start:
                bits = ganz[start:start + eine]

        lsb = bitarray(endian="big")
        lsb.frombytes(bits.tobytes())
        lsb.bytereverse()                      # HFE speichert LSB zuerst
        self._letzte_arbeit = time.monotonic()
        return lsb.tobytes(), len(bits)

    def ticks_per_rev(self) -> float:
        """Umdrehungsdauer des Laufwerks in Adapter-Takten (einmal gemessen).

        Ohne diese Messung lässt sich **nicht schreiben**: die Bitzellen kommen mit
        der *nominellen* Zellrate herein, das Laufwerk dreht aber mit seiner eigenen
        Drehzahl.  Die Flusszeiten müssen daher auf die **gemessene** Umdrehung
        gestreckt werden — sonst ist der Datenstrom vor dem Indexloch zu Ende und
        der Adapter meldet `Flux Underflow`.
        """
        if self._ticks_per_rev is None:
            self.select()
            self.motor(True)
            self._ticks_per_rev = self._usb.read_track(2).ticks_per_rev
        return self._ticks_per_rev

    def write_track(self, cyl: int, head: int, cells: bytes, bitcells: int) -> None:
        """Eine Spurseite schreiben — ganze Spur, ab Index bis Index."""
        from bitarray import bitarray
        MasterTrack = _leise("greaseweazle.track").MasterTrack

        self.select()
        self.motor(True)
        takte_je_umdrehung = self.ticks_per_rev()
        self._usb.seek(self._position(cyl), head)

        bits = bitarray(endian="big")
        bits.frombytes(cells)
        bits.bytereverse()                     # zurück in die interne Reihenfolge
        del bits[bitcells:]

        spur = MasterTrack(bits=bits,
                           time_per_rev=len(bits) / (2000 * self.cell_rate_kbps))
        fluss = spur.flux_for_writeout(cue_at_index=True)

        # Zeitbasis der Vorlage auf die des LAUFWERKS umrechnen; der Rest wird
        # mitgeschleppt, damit sich Rundungsfehler nicht über die Spur summieren
        # (dasselbe Verfahren wie `gw write`, tools/write.py).
        faktor = takte_je_umdrehung / fluss.ticks_to_index
        rest = 0.0
        liste = []
        for x in fluss.list:
            y = x * faktor + rest
            v = round(y)
            rest = y - v
            liste.append(v)

        self._usb.write_track(flux_list=liste,
                              cue_at_index=fluss.index_cued,
                              terminate_at_index=fluss.terminate_at_index)
        self._letzte_arbeit = time.monotonic()


def finde_adapter() -> Adapter:
    """Adapter suchen und beschreiben — ohne ein Laufwerk anzufassen."""
    if not verfuegbar():
        raise GreaseweazleFehlt(
            "Das Paket 'greaseweazle' ist nicht installiert.\n"
            "In einer Installation aus dem Paket sollte es da sein — dann ist beim "
            "Einrichten etwas schiefgegangen (siehe bootstrap.log bzw. die Meldung "
            "des Installers).\n"
            "Im Quellbaum von Hand:\n"
            '  pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"')
    util = _leise("greaseweazle.tools.util")
    try:
        usb = util.usb_open(None)
    except Exception as e:                     # noqa: BLE001 — gw wirft vielerlei
        raise KeinAdapter(f"Kein Greaseweazle gefunden: {e}") from e
    try:
        return Adapter(port=getattr(usb, "port_info", None) and usb.port_info.device or "",
                       model=str(usb.hw_model), firmware=f"{usb.major}.{usb.minor}",
                       serial=str(getattr(usb, "serial", "")))
    finally:
        pass


def open_device(drive: str = "a", *, cell_rate_kbps: int = 250,
                revs: int = 1, port: Optional[str] = None,
                double_step: bool = False) -> Device:
    """Adapter öffnen und ein Laufwerk wählen.

    Args:
        drive: ``a``/``b`` (IBM-PC-Kabel mit Verdrehung) oder ``0``…``3`` (Shugart).
        cell_rate_kbps: 250 für 5,25″ DD, 500 für 8″ MFM.
        revs: Umdrehungen je Leseversuch (mehr = robuster, langsamer).
        port: Gerätedatei; ``None`` = selbst suchen.
        double_step: Spur *n* auf physischem Zylinder *2n* (40-Spur-Diskette im
            80-Spur-Laufwerk).

    Raises:
        GreaseweazleFehlt: Paket nicht installiert.
        KeinAdapter: kein Gerät gefunden.
    """
    if not verfuegbar():
        raise GreaseweazleFehlt(
            "Das Paket 'greaseweazle' ist nicht installiert.\n"
            "In einer Installation aus dem Paket sollte es da sein — dann ist beim "
            "Einrichten etwas schiefgegangen (siehe bootstrap.log bzw. die Meldung "
            "des Installers).\n"
            "Im Quellbaum von Hand:\n"
            '  pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"')
    util = _leise("greaseweazle.tools.util")
    try:
        usb = util.usb_open(port)
    except Exception as e:                     # noqa: BLE001
        raise KeinAdapter(f"Kein Greaseweazle gefunden: {e}") from e
    return Device(usb, util.Drive()(drive), cell_rate_kbps=cell_rate_kbps, revs=revs,
                  double_step=double_step)
