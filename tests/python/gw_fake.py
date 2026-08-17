"""Ersatzlaufwerk für die Tests: eine ``.hfe``-Datei statt eines Greaseweazle.

Liefert dieselben Bitzellen, die der Adapter liefern würde — eine HFE-Aufnahme *ist*
genau das, samt Phasenversatz und Jitter, wenn man eine echte Aufnahme nimmt.  Damit
liegt der ganze Weg (Zustände, Warteschlange, Decodierung, Oberfläche) in der
Regression, und die Hardware fügt nur noch USB hinzu
(doc/design/14_physische_diskette.md §14).

**Importiert ``greaseweazle`` nicht** — das Paket ist freiwillig und in der CI nie da.
"""

from __future__ import annotations

import struct
import threading
import time


class HfeDevice:
    """Steht für „Greaseweazle + Laufwerk", liest aber aus einer ``.hfe``.

    Genügt dem :class:`app.gw.TrackWorker`-Vertrag (``read_track``/``write_track``
    mit HFE-Bitzellen) — mehr braucht der Arbeitsfaden nicht.  Der HFE-v1-Aufbau ist
    hier von Hand gelesen, damit der Test **keine** Fremdpakete braucht.

    Attributes:
        gelesen: Reihenfolge der gelesenen ``(cyl, head)`` — der Beleg dafür, dass nur
            geholt wird, was gebraucht wird.
        geschrieben: dasselbe für Schreibvorgänge.
        verzoegerung: künstliche Lesedauer in Sekunden (Kopfweg nachstellen).
    """

    #: Spuren, die nichts annehmen — Schadstelle einer echten Diskette.  Geschrieben
    #: wird scheinbar, gelesen wird weiter der ALTE Inhalt.
    schadhaft: set

    def __init__(self, pfad, verzoegerung: float = 0.0, double_step: bool = False):
        roh = pfad.read_bytes()
        assert roh[:8] == b"HXCPICFE", f"keine HFE-v1-Datei: {pfad}"
        self.num_cyls = roh[9]
        self.num_heads = roh[10]
        self.bitrate = struct.unpack_from("<H", roh, 12)[0]
        lut_off = struct.unpack_from("<H", roh, 18)[0] * 512

        self._spuren: dict[tuple[int, int], tuple[bytes, int]] = {}
        for c in range(self.num_cyls):
            off, laenge = struct.unpack_from("<2H", roh, lut_off + c * 4)
            start, seitenlaenge = off * 512, laenge // self.num_heads
            for h in range(self.num_heads):
                # Spurdaten liegen seitenverschränkt in 256-B-Blöcken.
                seite = bytearray()
                blocks = (seitenlaenge + 255) // 256
                for b in range(blocks):
                    a = start + b * 256 * self.num_heads + h * 256
                    seite += roh[a:a + 256]
                self._spuren[(c, h)] = (bytes(seite[:seitenlaenge]), seitenlaenge * 8)

        self.gelesen: list[tuple[int, int]] = []
        self.geschrieben: list[tuple[int, int]] = []
        self.schadhaft = set()
        self.verzoegerung = verzoegerung
        #: Wie am echten Gerät: Spur n liegt auf dem physischen Zylinder 2n.
        self.double_step = double_step
        self._sperre = threading.Lock()

    def _position(self, cyl: int) -> int:
        return cyl * 2 if self.double_step else cyl

    def read_track(self, cyl: int, head: int) -> tuple[bytes, int]:
        if self.verzoegerung:
            time.sleep(self.verzoegerung)
        wo = (self._position(cyl), head)
        with self._sperre:
            self.gelesen.append(wo)
        return self._spuren.get(wo, (b"", 0))

    def write_track(self, cyl: int, head: int, cells: bytes, bitcells: int) -> None:
        wo = (self._position(cyl), head)
        with self._sperre:
            self.geschrieben.append(wo)
            # Auf einer Schadstelle bleibt der alte Inhalt stehen; der Schreibvorgang
            # selbst meldet trotzdem Erfolg — genau wie am echten Laufwerk.
            if wo not in self.schadhaft:
                self._spuren[wo] = (cells, bitcells)


def fake_session(hfe, *, writable=False, read_ahead=True, for_emulator=False,
                 verzoegerung=0.0):
    """Eine :class:`app.ui.physical_disk.PhysicalSession` über einer ``.hfe``.

    Gleiche Bauteile wie im Betrieb (echter ``Sync``, echter ``TrackWorker``), nur
    das Gerät ist die Datei.  Der Arbeitsfaden läuft bereits.
    """
    from app.gw import Sync, TrackWorker
    from app.ui.physical_disk import PhysicalSession

    geraet = HfeDevice(hfe, verzoegerung=verzoegerung)
    sync = Sync(num_cyls=geraet.num_cyls, num_heads=geraet.num_heads,
                writable=writable, read_ahead=read_ahead, for_emulator=for_emulator)
    worker = TrackWorker(sync, geraet, poll_ms=50)
    worker.start()
    return PhysicalSession(sync, worker, geraet, drive="a", writable=writable,
                           cell_rate_kbps=250)
