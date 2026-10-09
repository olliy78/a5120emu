"""Emulationstakt: die Maschine rechnet in einem EIGENEN Faden, nicht im GUI-Faden.

Früher lief ``emulator.run()`` aus einem 20-ms-``QTimer`` im GUI-Faden.  Schafft der
Wirt den eingestellten Takt nicht (P8000 mit Originalterminal ≈ 7× auf einem Kern,
eingestellt 10×; ein langsamer Rechner schon bei den 8-Bit-Maschinen), dauerte jeder
Bildschritt länger als sein Abstand — die Ereignisschleife kam praktisch nicht mehr
an die Reihe, Dialoge öffneten sich nicht, die Arbeitsumgebung meldete „reagiert nicht".

Jetzt rechnet der Kern hier in kurzen Scheiben (``SCHEIBE_S``); ``k1520_run`` gibt die
GIL frei, die Oberfläche läuft also auf einem anderen Prozessorkern.  Kann der Wirt den
Takt nicht halten, wird die Maschine langsamer — die Oberfläche nicht.

**Wer wann an den Kern darf** (der Kern ist nicht threadsicher, alle Aufrufe gehen über
die Kernsperre, ``k1520.kern_sperre()``): ``RLock`` ist nicht fair — ohne Weiteres nähme
sich der Faden die Sperre nach jeder Scheibe sofort wieder, und die Oberfläche verhungerte
(gemessen: 7 statt 600 Takte in 6 s).  Wer wartet, meldet sich deshalb an
(``_lib.wartende``), und der Faden lässt ihm nach seiner Scheibe den Vortritt.

Verworfen (gemessen 2026-10-09): die Sperre im GUI-Faden bis ``aboutToBlock`` zu HALTEN.
Brachte ~12 % Kerntempo, aber jede Schleife aus ``processEvents()`` + ``sleep`` (Tests,
Fortschrittsdialoge) ruht für Qt nie — die Maschine stand dann still.

Die Schnittstelle gleicht dem alten ``run_timer`` (``start``/``stop``/``isActive``),
und ``stop()`` kehrt erst zurück, wenn keine Scheibe mehr läuft: ein Test, der den
Takt anhält und die Maschine dann selbst fährt, sieht keinen zweiten Fahrer.
"""

from __future__ import annotations

import atexit
import threading
import time
import weakref
from typing import Callable, Optional

from PySide6.QtCore import QObject, QTimer, Signal

from app.core_binding import k1520 as _bindung

#: Scheibe = so viele Wanduhr-Sekunden des eingestellten Tempos.  So lange wartet die
#: ein Oberflächenaufruf höchstens (mehr, wenn der Wirt das Tempo nicht schafft).
SCHEIBE_S = 0.002
#: Scheibe im unbegrenzten Betrieb, in Nenntakt-Sekunden.
SCHEIBE_UNBEGRENZT_S = 0.02
#: Höchstens so viel Rückstand (Wanduhr) wird nachgeholt; mehr verfällt, sonst
#: raste die Maschine nach einem langsamen Abschnitt hinterher.
RUECKSTAND_S = 0.1
#: Höchstens so lange lässt der Faden nach einer Scheibe einem Wartenden den Vortritt.
VORTRITT_S = 0.01


#: Alle Takte mit Faden — beim Beenden des Interpreters werden sie angehalten, BEVOR
#: Python Daemon-Fäden einfriert: ein mitten in der Scheibe erstarrter Faden hielte die
#: Kernsperre, und das abschliessende ``k1520_destroy`` wartete ewig (Prozess endet nicht).
_LEBENDE: "weakref.WeakSet[Emulationstakt]" = weakref.WeakSet()


@atexit.register
def _alle_anhalten():
    for takt in list(_LEBENDE):
        takt._faden_anhalten()


class Emulationstakt(QObject):
    """Faden, der ``quelle().run(n)`` im eingestellten Tempo ruft.

    Signale (im GUI-Faden, aus einem 20-ms-Bildtakt):
      * ``bild(int)`` — seit dem letzten Bild gerechnete Takte
      * ``fehler(str)`` — der Lauf ist an einer Ausnahme gestorben (Takt steht)
    """

    bild = Signal(int)
    fehler = Signal(str)

    def __init__(self, quelle: Callable[[], object], hz: float,
                 bild_ms: int = 20, parent: Optional[QObject] = None):
        super().__init__(parent)
        self._quelle = quelle
        self._hz = float(hz)
        self._faktor = 1.0
        self._cond = threading.Condition()
        self._laufen = False
        self._ende = False
        self._leerlauf = True
        self._zyklen = 0                # gerechnet, noch nicht abgeholt
        self._fehler: Optional[str] = None
        self._neu_bezug = True          # Tempo-Bezugspunkt neu setzen
        self._faden: Optional[threading.Thread] = None
        self._bildtakt = QTimer(self)
        self._bildtakt.setInterval(bild_ms)
        self._bildtakt.timeout.connect(self._abholen)
        self._sperre = _bindung._lib.sperre

    # ── Einstellung ─────────────────────────────────────────────────────────

    def set_tempo(self, hz: float, faktor: float):
        """Nenntakt und Faktor (1.0 = Echtzeit, 0.0 = unbegrenzt)."""
        with self._cond:
            self._hz = float(hz)
            self._faktor = float(faktor)
            self._neu_bezug = True

    # ── Steuerung wie beim alten QTimer ─────────────────────────────────────

    def start(self):
        with self._cond:
            self._fehler = None
            self._laufen = True
            self._neu_bezug = True
            if self._faden is None or not self._faden.is_alive():
                self._ende = False
                self._faden = threading.Thread(target=self._lauf, name="k1520-emulation",
                                               daemon=True)
                self._faden.start()
                _LEBENDE.add(self)
            self._cond.notify_all()
        self._bildtakt.start()

    def stop(self):
        """Anhalten; kehrt zurück, wenn keine Scheibe mehr rechnet."""
        with self._cond:
            self._laufen = False
            self._cond.notify_all()
        if threading.current_thread() is not self._faden:
            # Die laufende Scheibe braucht womöglich die Sperre, die WIR halten
            # (``with kern_sperre():`` um den Aufruf) — sonst warteten beide aufeinander.
            gehalten = self._sperre._release_save() if self._sperre._is_owned() else None
            try:
                with self._cond:
                    while (not self._leerlauf and self._faden is not None
                           and self._faden.is_alive()):
                        self._cond.wait(0.5)
            finally:
                if gehalten is not None:
                    self._sperre._acquire_restore(gehalten)
        self._bildtakt.stop()
        self._abholen()

    def isActive(self) -> bool:
        return self._laufen

    def beenden(self):
        """Faden auflösen (Fenster schliesst)."""
        self.stop()
        self._faden_anhalten()

    def _faden_anhalten(self):
        """Nur der Faden — ohne Qt (läuft auch aus ``atexit``)."""
        with self._cond:
            self._laufen = False
            self._cond.notify_all()
        with self._cond:
            self._ende = True
            self._cond.notify_all()
        if self._faden is not None and threading.current_thread() is not self._faden:
            self._faden.join(timeout=2.0)
        self._faden = None

    # ── GUI-Seite ───────────────────────────────────────────────────────────

    def _abholen(self):
        with self._cond:
            n, self._zyklen = self._zyklen, 0
            fehler, self._fehler = self._fehler, None
        if fehler is not None:
            self._bildtakt.stop()
            self.fehler.emit(fehler)
        self.bild.emit(n)

    # ── Faden ───────────────────────────────────────────────────────────────

    def _lauf(self):
        bib = _bindung._lib
        bezug_t = 0.0
        bezug_n = 0                     # seit bezug_t gerechnete Takte
        while True:
            with self._cond:
                while not self._laufen and not self._ende:
                    self._leerlauf = True
                    self._cond.notify_all()
                    self._cond.wait()
                if self._ende:
                    self._leerlauf = True
                    self._cond.notify_all()
                    return
                self._leerlauf = False
                hz, faktor = self._hz, self._faktor
                if self._neu_bezug:
                    self._neu_bezug = False
                    bezug_t, bezug_n = time.monotonic(), 0

            if faktor > 0.0:
                tempo = hz * faktor                       # Takte je Wanduhr-Sekunde
                scheibe = max(1000, int(tempo * SCHEIBE_S))
                soll = (time.monotonic() - bezug_t) * tempo
                if bezug_n >= soll:                       # voraus: warten
                    time.sleep(min((bezug_n - soll) / tempo + 1e-4, 0.02))
                    continue
                if soll - bezug_n > tempo * RUECKSTAND_S: # nicht unbegrenzt nachholen
                    bezug_n = int(soll - tempo * RUECKSTAND_S)
            else:
                scheibe = max(1000, int(hz * SCHEIBE_UNBEGRENZT_S))

            emu = self._quelle()
            try:
                with self._sperre:
                    if not self._laufen:
                        continue
                    n = int(emu.run(scheibe)) if emu is not None else 0
            except Exception as e:                        # an die Oberfläche melden
                with self._cond:
                    self._fehler = str(e)
                    self._laufen = False
                continue
            finally:
                del emu
            if n <= 0:                                    # nichts zu rechnen
                time.sleep(0.001)
            bezug_n += n
            with self._cond:
                self._zyklen += n

            # Vortritt: wer auf die Kernsperre wartet (ein Oberflächenaufruf), kommt
            # jetzt dran; die nächste Scheibe wartet an der Sperre, bis er fertig ist.
            if bib.wartende > 0:
                bis = time.monotonic() + VORTRITT_S
                while bib.wartende > 0 and time.monotonic() < bis:
                    time.sleep(0.0001)
