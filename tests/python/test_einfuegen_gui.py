"""Rechtsklick auf das Bild (alle Maschinen): Bildschirminhalt als Text kopieren und
Zwischenablage ZEICHENWEISE über die Tastatur einfügen (120 Zeichen/s, Zeichen ohne Taste entfallen).

Der Emulator ist ein Aufzeichner: geprüft wird die Verdrahtung (Menü, Tempo, Auslassen, Abbildung
durch die Bildschirmtastatur), nicht der Gast.  Gegen den echten Kern: ``test_p8000_original_gui``.
"""

import time

import pytest
from PySide6.QtCore import Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import QApplication


@pytest.fixture(scope="module")
def qapp():
    return QApplication.instance() or QApplication([])


class Aufzeichner:
    def __init__(self, machine="a5120", text="HALLO   \n  zweite\x01Zeile  \n"):
        self.machine = machine
        self.ereignisse = []                   # (art, code, shift, ctrl, Zeit)
        self._text = text
        self.zeilen_angefragt = None

    def key_press(self, code, shift=False, ctrl=False):
        self.ereignisse.append(("an", code, shift, ctrl, time.monotonic()))

    def key_release(self, code):
        self.ereignisse.append(("aus", code, False, False, time.monotonic()))

    def screen_text(self, zeilen=24):
        self.zeilen_angefragt = zeilen
        return self._text

    def is_framebuffer_dirty(self):
        return False


def _widget(emu=None):
    from app.ui.screen_widget import ScreenWidget
    w = ScreenWidget()
    w.set_emulator(emu or Aufzeichner())
    return w


def _warte(qapp, bedingung, frist=5.0):
    ende = time.monotonic() + frist
    while time.monotonic() < ende and not bedingung():
        qapp.processEvents()
        time.sleep(0.001)
    assert bedingung()


def test_kontextmenue_bietet_kopieren_und_einfuegen_ohne_kuerzel(qapp):
    w = _widget()
    QGuiApplication.clipboard().setText("x")
    menue = w.kontextmenue_bauen()
    assert [a.text() for a in menue.actions()] == ["Bildschirminhalt als Text kopieren",
                                                   "Zwischenablage über Tastatur einfügen"]
    assert all(a.shortcut().isEmpty() for a in menue.actions())      # ^C/^V gehören dem Gast
    assert all(a.isEnabled() for a in menue.actions())


def test_einfuegen_ist_ohne_text_in_der_zwischenablage_grau(qapp):
    w = _widget()
    QGuiApplication.clipboard().setText("")
    assert not w.kontextmenue_bauen().actions()[1].isEnabled()


def test_text_kopieren_zeilen_ohne_schlussleerzeichen_und_steuerzeichen_als_leerzeichen(qapp):
    emu = Aufzeichner()
    w = _widget(emu)
    QGuiApplication.clipboard().setText("vorher")
    w.kontextmenue_bauen().actions()[0].trigger()
    assert QGuiApplication.clipboard().text() == "HALLO\n  zweite Zeile\n"
    assert emu.zeilen_angefragt == 24


def test_pc1715_kopiert_25_zeilen_mit_der_statuszeile(qapp):
    emu = Aufzeichner(machine="pc1715w")
    w = _widget(emu)
    w.text_kopieren()
    assert emu.zeilen_angefragt == 25


def test_einfuegen_tippt_mit_120_zeichen_je_sekunde_und_laesst_zeichen_ohne_taste_aus(qapp):
    emu = Aufzeichner()
    w = _widget(emu)
    fertig = []
    w.einfuegenFertig.connect(lambda e, u: fertig.append((e, u)))
    text = "Ab 1!\täö\r\n" + "x" * 114                 # 6 + 1 + 2 übersprungen … s. u.
    w.einfuegen(text)
    assert w.einfuegen_laeuft()
    _warte(qapp, lambda: fertig)
    an = [e for e in emu.ereignisse if e[0] == "an"]
    aus = [e for e in emu.ereignisse if e[0] == "aus"]
    codes = [e[1] for e in an]
    assert codes[:5] == [ord("A"), ord("b"), 0x20, ord("1"), ord("!")]
    assert codes[5] == int(Qt.Key_Tab) and codes[6] == int(Qt.Key_Return)   # ä/ö entfallen
    assert fertig == [(len(an), 2)] and len(an) == len(aus)            # alles wieder losgelassen
    assert an[0][2] is True and an[1][2] is False                       # 'A' mit, 'b' ohne Umschalt
    # Tempo: 120/s — 121 Tasten brauchen rund eine Sekunde, weder 10 ms noch 5 s.
    dauer = an[-1][4] - an[0][4]
    assert 0.6 < dauer < 2.0, dauer


def test_jedes_zeichen_wird_vor_dem_naechsten_losgelassen(qapp):
    emu = Aufzeichner()
    w = _widget(emu)
    w.einfuegen("llll")
    _warte(qapp, lambda: not w.einfuegen_laeuft())
    arten = [e[0] for e in emu.ereignisse]
    assert arten == ["an", "aus"] * 4


def test_abbrechen_laesst_die_gedrueckte_taste_los_und_hoert_auf(qapp):
    emu = Aufzeichner()
    w = _widget(emu)
    w.einfuegen("x" * 1000)
    _warte(qapp, lambda: len(emu.ereignisse) >= 4)
    assert w.kontextmenue_bauen().actions()[1].text() == "Einfügen abbrechen"
    w.einfuegen_abbrechen()
    n = len(emu.ereignisse)
    assert not w.einfuegen_laeuft() and emu.ereignisse[-1][0] == "aus"
    for _ in range(30):
        qapp.processEvents()
        time.sleep(0.002)
    assert len(emu.ereignisse) == n


def test_ausgeschaltet_oder_ohne_maschine_wird_nichts_getippt(qapp):
    emu = Aufzeichner()
    w = _widget(emu)
    w.set_powered(False)
    w.einfuegen("abc")
    assert not w.einfuegen_laeuft() and emu.ereignisse == []
    w.set_powered(True)
    w.set_emulator(None)
    w.einfuegen("abc")
    assert not w.einfuegen_laeuft()


def test_einfuegen_geht_durch_die_bildschirmtastatur(qapp):
    """Mit Bildschirmtastatur gilt ihre Abbildung (PC 1715: Return = physische Taste), nicht die rohe."""
    emu = Aufzeichner(machine="pc1715")
    w = _widget(emu)

    class Sink:
        def host_key_press(self, ev):
            return (0x100 + ev.key() % 256, False, False)

        def host_key_release(self, ev):
            return (0x100 + ev.key() % 256, False, False)

    w.key_sink = Sink()
    w.einfuegen("a\n")
    _warte(qapp, lambda: not w.einfuegen_laeuft())
    assert [e[1] for e in emu.ereignisse if e[0] == "an"] == [0x100 + ord("A") % 256, 0x100 + int(Qt.Key_Return) % 256]
