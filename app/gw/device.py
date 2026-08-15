"""Greaseweazle als Diskettenlaufwerk — dünne Hülle um die Hosttools.

Die einzige Stelle im Programm, die ``greaseweazle`` importiert.  Nach außen gibt es
nur zwei Vorgänge, und beide sprechen **HFE-Bitzellen** (LSB zuerst je Byte) — dieselbe
Darstellung, die auch in einer ``.hfe``-Datei steht und die der Kern mit demselben
``BitCodec`` liest wie ein Dateiabbild (doc/design/14_physische_diskette.md §8).

Installation der Hosttools (sie liegen **nicht** auf PyPI)::

    venv/bin/python3 -m pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"

Fehlt das Paket oder der Adapter, wirft :func:`open_device` einen
:class:`GreaseweazleFehlt` bzw. :class:`KeinAdapter` — die Oberfläche sperrt dann den
Menüpunkt und nennt den Grund, sonst ändert sich nichts.
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Optional


class GwFehler(RuntimeError):
    """Oberbegriff aller Fehler dieser Schicht."""


class GreaseweazleFehlt(GwFehler):
    """Das Python-Paket ``greaseweazle`` ist nicht installiert."""


class KeinAdapter(GwFehler):
    """Kein Greaseweazle am USB gefunden (oder keine Rechte am Anschluss)."""


def verfuegbar() -> bool:
    """Ist das Paket ``greaseweazle`` installiert?  (Fragt **kein** Gerät ab.)"""
    try:
        import greaseweazle  # noqa: F401
    except ImportError:
        return False
    return True


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

    def __init__(self, usb, drive, *, cell_rate_kbps: int = 250, revs: int = 1):
        self._usb = usb
        self._drive = drive
        self.cell_rate_kbps = cell_rate_kbps
        self.revs = revs
        self._motor_an = False
        self._letzte_arbeit = 0.0
        self._gewaehlt = False

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

    def read_track(self, cyl: int, head: int) -> tuple[bytes, int]:
        """Eine Spurseite lesen.

        Returns:
            ``(zellen, bitcells)`` — HFE-Konvention (LSB zuerst je Byte).
        """
        from bitarray import bitarray
        from greaseweazle.track import PLLTrack

        self.select()
        self.motor(True)
        self._usb.seek(cyl, head)
        # Eine Umdrehung mehr lesen, als gebraucht wird: cue_at_index() schneidet
        # anschließend genau bei Index — sonst fehlte der Anfang der Spur.
        fluss = self._usb.read_track(revs=self.revs + 1)
        fluss.cue_at_index()
        roh = PLLTrack(clock=5e-4 / self.cell_rate_kbps, data=fluss)
        bits, _ = roh.get_revolution(0)

        lsb = bitarray(endian="big")
        lsb.frombytes(bits.tobytes())
        lsb.bytereverse()                      # HFE speichert LSB zuerst
        self._letzte_arbeit = time.monotonic()
        return lsb.tobytes(), len(bits)

    def write_track(self, cyl: int, head: int, cells: bytes, bitcells: int) -> None:
        """Eine Spurseite schreiben (ganze Spur ab Index)."""
        from bitarray import bitarray
        from greaseweazle.track import MasterTrack

        self.select()
        self.motor(True)
        self._usb.seek(cyl, head)

        bits = bitarray(endian="big")
        bits.frombytes(cells)
        bits.bytereverse()                     # zurück in die interne Reihenfolge
        del bits[bitcells:]

        spur = MasterTrack(bits=bits,
                           time_per_rev=len(bits) / (2000 * self.cell_rate_kbps))
        self._usb.write_track(flux_list=spur.flux_for_writeout(cue_at_index=True).list,
                              terminate_at_index=True)
        self._letzte_arbeit = time.monotonic()


def finde_adapter() -> Adapter:
    """Adapter suchen und beschreiben — ohne ein Laufwerk anzufassen."""
    if not verfuegbar():
        raise GreaseweazleFehlt(
            "Das Paket 'greaseweazle' ist nicht installiert. Installation:\n"
            '  pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"')
    from greaseweazle.tools import util
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
                revs: int = 1, port: Optional[str] = None) -> Device:
    """Adapter öffnen und ein Laufwerk wählen.

    Args:
        drive: ``a``/``b`` (IBM-PC-Kabel mit Verdrehung) oder ``0``…``3`` (Shugart).
        cell_rate_kbps: 250 für 5,25″ DD, 500 für 8″ MFM.
        revs: Umdrehungen je Leseversuch (mehr = robuster, langsamer).
        port: Gerätedatei; ``None`` = selbst suchen.

    Raises:
        GreaseweazleFehlt: Paket nicht installiert.
        KeinAdapter: kein Gerät gefunden.
    """
    if not verfuegbar():
        raise GreaseweazleFehlt(
            "Das Paket 'greaseweazle' ist nicht installiert. Installation:\n"
            '  pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"')
    from greaseweazle.tools import util
    try:
        usb = util.usb_open(port)
    except Exception as e:                     # noqa: BLE001
        raise KeinAdapter(f"Kein Greaseweazle gefunden: {e}") from e
    return Device(usb, util.Drive()(drive), cell_rate_kbps=cell_rate_kbps, revs=revs)
