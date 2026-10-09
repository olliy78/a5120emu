"""Emulationstakt (``app/emulationstakt.py``): die Maschine rechnet im eigenen Faden.

Wächter für den Fehler „Oberfläche friert ein, wenn der Wirt den eingestellten Takt
nicht schafft" (P8000 mit Originalterminal bei 10×).  Die Ersatzmaschine rechnet
absichtlich LANGSAMER als verlangt und hält dabei die Kernsperre wie ``k1520_run``;
die Oberfläche greift wie die echten Kästen in jedem Takt mehrmals auf den Kern zu.
Geprüft wird, dass sie trotzdem im Takt bleibt UND die Maschine weiterrechnet — das
erste ohne das zweite hiesse nur, das Einfrieren an den Kern weitergereicht zu haben.
"""

import time

import pytest
from PySide6.QtCore import QEventLoop, QTimer
from PySide6.QtWidgets import QApplication

from app.core_binding import k1520 as B
from app.emulationstakt import Emulationstakt

HZ = 4_000_000


@pytest.fixture(scope="module")
def qapp():
    return QApplication.instance() or QApplication([])


class LangsameMaschine:
    """``run(n)`` braucht dreimal so lange, wie das Tempo erlaubt (Wirt zu langsam)."""

    def __init__(self, tempo):
        self.tempo = tempo
        self.laeufe = 0
        self.gleichzeitig = 0
        self.max_gleichzeitig = 0

    def run(self, n):
        with B.kern_sperre():               # wie jeder echte Aufruf über _lib
            self.gleichzeitig += 1
            self.max_gleichzeitig = max(self.max_gleichzeitig, self.gleichzeitig)
            time.sleep(3 * n / self.tempo)
            self.gleichzeitig -= 1
            self.laeufe += 1
        return n

    def abfrage(self):
        with B.kern_sperre():
            assert self.gleichzeitig == 0, "Oberfläche und Faden gleichzeitig im Kern"


def _schleife(dauer_s):
    loop = QEventLoop()
    QTimer.singleShot(int(dauer_s * 1000), loop.quit)
    loop.exec()


def _abstaende(qapp, takt, emu, dauer_s=1.5):
    luecken, letzte = [], [time.monotonic()]

    def tick():
        jetzt = time.monotonic()
        luecken.append(jetzt - letzte[0])
        letzte[0] = jetzt
        for _ in range(8):                  # Lampen, Laufwerke, Statuszeile …
            emu.abfrage()
            bis = time.perf_counter() + 0.0003
            while time.perf_counter() < bis:  # Oberflächenarbeit zwischen den Abfragen
                pass

    t = QTimer()
    t.timeout.connect(tick)
    t.start(10)
    _schleife(dauer_s)                      # echte Schleife: sie RUHT zwischen den Takten
    t.stop()
    return sorted(luecken)


def test_oberflaeche_bleibt_im_takt_wenn_der_wirt_zu_langsam_ist(qapp):
    emu = LangsameMaschine(HZ * 10)
    takt = Emulationstakt(lambda: emu, HZ)
    takt.set_tempo(HZ, 10.0)
    gerechnet = []
    takt.bild.connect(gerechnet.append)
    takt.start()
    try:
        luecken = _abstaende(qapp, takt, emu)
    finally:
        takt.beenden()
    p95 = luecken[int(len(luecken) * 0.95)]
    assert p95 < 0.05, f"Oberfläche stockt: 95 % der Takte unter {p95 * 1000:.0f} ms"
    assert emu.laeufe > 50, f"die Maschine rechnet nicht weiter ({emu.laeufe} Scheiben)"
    assert sum(gerechnet) > 0
    assert emu.max_gleichzeitig == 1


def test_stop_kehrt_erst_nach_der_laufenden_scheibe_zurueck(qapp):
    emu = LangsameMaschine(HZ)
    takt = Emulationstakt(lambda: emu, HZ)
    takt.set_tempo(HZ, 0.0)                 # unbegrenzt: rechnet ohne Pause
    takt.start()
    time.sleep(0.05)
    with B.kern_sperre():                   # Oberfläche hält die Sperre (Ereignis läuft)
        takt.stop()                         # darf sich nicht mit dem Faden verklemmen
        n = emu.laeufe
    time.sleep(0.1)
    assert emu.laeufe == n, "nach stop() rechnet nichts mehr"
    assert not takt.isActive()
    takt.start()
    _schleife(0.1)
    assert emu.laeufe > n, "start() setzt fort"
    takt.beenden()


def test_eine_ausnahme_im_lauf_haelt_an_und_wird_gemeldet(qapp):
    class Kaputt:
        def run(self, n):
            raise RuntimeError("kaputt")

    takt = Emulationstakt(lambda: Kaputt(), HZ)
    meldungen = []
    takt.fehler.connect(meldungen.append)
    takt.start()
    _schleife(0.3)
    takt.beenden()
    assert meldungen == ["kaputt"]
    assert not takt.isActive()


def test_p8000_unbegrenzt_oberflaeche_bleibt_bedienbar(qapp, tmp_path, monkeypatch):
    """Der gemeldete Fall am echten Kern: P8000 mit Originalterminal, Tempo „unbegrenzt"
    (= auf JEDEM Wirt überlastet).  Vor dem Umbau stand die Ereignisschleife; mit
    Einzelaufrufen statt gehaltener Sperre stockte sie um Hunderte Millisekunden.
    Die Schwellen sind weit, weil ctest parallel fährt — ein Einfrieren fängt das."""
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    monkeypatch.setenv("K1520_DISKS", str(tmp_path / "disks"))
    monkeypatch.delenv("K1520_INSTANZ", raising=False)
    monkeypatch.delenv("K1520_KONFIG", raising=False)
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(None, profil=profil.profil("p8000"))
    try:
        w._apply_speed(0.0)
        w.show()
        luecken, letzte = [], [time.monotonic()]

        def tick():
            jetzt = time.monotonic()
            luecken.append(jetzt - letzte[0])
            letzte[0] = jetzt

        t = QTimer()
        t.timeout.connect(tick)
        t.start(10)
        c0, t0 = w.cycles, time.monotonic()
        _schleife(2.0)
        t.stop()
        tempo = (w.cycles - c0) / (time.monotonic() - t0) / w.CPU_HZ
    finally:
        w.run_timer.stop()
        w.close()
    luecken.sort()
    assert len(luecken) > 60, f"nur {len(luecken)} von ~200 Takten der Oberfläche"
    p90 = luecken[int(len(luecken) * 0.9)]
    assert p90 < 0.1, f"Oberfläche stockt: 90 % der Takte unter {p90 * 1000:.0f} ms"
    assert tempo > 0.3, f"die Maschine rechnet kaum ({tempo:.2f}× Nenntakt)"
