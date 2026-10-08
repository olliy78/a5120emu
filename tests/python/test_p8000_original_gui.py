"""P21 — das ORIGINALTERMINAL im p8000emu / p8000term (doc/design/26_p8000emu_oberflaeche.md §8).

Variantenwahl (Kern-Terminal | + P8000 Terminal | P8000 Terminal), Bildschirm-Widget für das Pixelbild,
Bildschirmtastatur K7673 und Abbildung der Wirtstastatur auf die Tastenmatrix, Verbindungsdialog,
Mehrinstanz (Konfiguration, Plattensperre) und der Mehrplatz über Loopback-Telnet.

Gearbeitet wird ohne Pixelvergleich gegen das Fenster (offscreen): geprüft werden Verdrahtung und
Abbildung — das Bild selbst kommt aus Firmware + 8275 im Kern und ist dort getestet.
"""

import os
import socket
import subprocess
import sys
import time
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT, requires_core

pytestmark = requires_core


@pytest.fixture
def umgebung(tmp_path, monkeypatch):
    """Konfiguration UND Diskettenordner in tmp; keine Instanzumgebung."""
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    monkeypatch.setenv("K1520_DISKS", str(tmp_path / "disks"))
    monkeypatch.delenv("K1520_INSTANZ", raising=False)
    monkeypatch.delenv("K1520_KONFIG", raising=False)
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


def _fenster(qapp, programm="p8000", modell=None):
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(None, profil=profil.profil(programm))
    w.run_timer.stop()
    if modell and modell != w._model:
        w._on_model_selected(modell)
        w.run_timer.stop()
    w.show()
    qapp.processEvents()
    return w


def _zu(w, qapp):
    w.close()
    qapp.processEvents()


def _frei() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Aufzeichner:
    """Ersatzmaschine, die nur die Tasten der K7673 mitschreibt (Abbildung der Wirtstasten)."""

    def __init__(self):
        self.ereignisse = []
        self.gesendet = ""

    def term_matrix_key(self, i, zeile, spalte, gedrueckt):
        self.ereignisse.append(((zeile, spalte), bool(gedrueckt)))
        return True

    def term_flags(self, i):
        return 0

    def term_frame_count(self, i):
        return 0

    def term_framebuffer(self, i):
        return None

    def term_send(self, i, text):
        self.gesendet += text
        return True

    def term_text(self, i):
        return ""

    def gedrueckt(self):
        return [p for p, an in self.ereignisse if an]


# ─── Profile und Modelle ─────────────────────────────────────────────────────

def test_profil_modelle_arten_und_titel():
    from app import profil
    p = profil.profil("p8000")
    schluessel = [m[0] for m in p.modelle]
    assert schluessel == ["p8000", "p8000-16", "p8000-8", "p8000-ot", "p8000-16-ot", "p8000-8-ot", "p8000-term"]
    assert [p.modell_art(k) for k in schluessel] == [
        "kern", "kern", "kern", "original", "original", "original", "einheit"]
    assert p.modell_maschine("p8000-ot") == "p8000" and p.modell_maschine("p8000-term") == "p8000-terminal"
    assert p.modell_tastatur("p8000-ot") == "k7673" and p.modell_tastatur("p8000") == "p8000"
    assert p.modell_titel("p8000-term") == "P8000 Terminal" and p.modell_titel("p8000-ot") == "P8000 Emulator"
    assert p.modell_hat_rechner("p8000-ot") and not p.modell_hat_rechner("p8000-term")
    assert p.kern_parameter("p8000-ot")["p8000"]["terminal"] == "original"
    assert p.kern_parameter("p8000-16-ot")["p8000"]["terminal"] == "original"
    assert "wdc" not in p.kern_parameter("p8000-16-ot")["p8000"]
    assert p.kern_parameter("p8000-8-ot")["p8000"]["karte16"] == "0"
    assert p.kern_parameter("p8000-term") == {"p8000": {}}
    assert "terminal" not in p.kern_parameter("p8000")["p8000"]
    assert p.modell_hat_wdc("p8000-ot") and not p.modell_hat_wdc("p8000-16-ot")
    assert not p.modell_hat_wdc("p8000-term")
    # ROM-Wahl wirkt am Terminal nicht
    assert not any(p.hardware_wirkt(k, "p8000-term") for k, *_ in p.hardware)
    assert p.hardware_wirkt("mon16", "p8000-ot") and not p.hardware_wirkt("mon16", "p8000-8-ot")


def test_p8000term_ist_dasselbe_programm_mit_dem_terminal_vorn():
    from app import profil
    t = profil.profil("p8000term")
    p = profil.profil("p8000")
    assert (t.programm, t.titel, t.konfig_datei, t.vorgabe_datei) == (
        "p8000term", "P8000 Terminal", "p8000term.yaml", "default_config_p8000term.yaml")
    assert t.maschine == "p8000" and t.standard_modell() == "p8000-term"
    assert {m[0] for m in t.modelle} == {m[0] for m in p.modelle}
    assert p.standard_modell() == "p8000"               # das Rechnerprogramm bleibt, wie es war
    assert (PROJECT_ROOT / "data" / "default_config_p8000term.yaml").is_file()
    starter = PROJECT_ROOT / "run_p8000term.sh"
    assert starter.is_file() and os.access(starter, os.X_OK)
    assert "--machine p8000term" in starter.read_text(encoding="utf-8")


def test_ohne_modellwahl_aendert_sich_an_den_anderen_profilen_nichts():
    from app import profil
    for name in ("a5120", "k8915", "prg710", "pc1715"):
        p = profil.profil(name)
        assert p.modell_art(p.standard_modell()) == "kern"
        assert p.modell_titel(p.standard_modell()) == p.titel
        assert p.modell_hat_rechner(p.standard_modell())


# ─── Variantenwahl im Hauptfenster ───────────────────────────────────────────

def test_variante_p8000_plus_terminal_baut_das_originalterminal(qapp, umgebung):
    from app.ui.keyboard_k7673 import KeyboardK7673Widget
    from app.ui.p8000_original import OriginalTerminalWidget
    w = _fenster(qapp, modell="p8000-ot")
    try:
        assert w.emulator.machine == "p8000" and w.emulator.term_kind(0) == 1
        assert isinstance(w.keyboard_widget, KeyboardK7673Widget)
        assert isinstance(w.screen_widget.aktuell(), OriginalTerminalWidget)
        assert w.windowTitle() == "P8000 Emulator"
        assert not w.drives_dock.isHidden() and w.platten_widget.verfuegbar()
        assert w.screen_widget.tabs.tabText(0) == "tty1 (Konsole)"
        w._autosave_now()
        from app import config_io
        cfg = config_io.load_config(str(umgebung / "p8000emu.yaml"))
        assert cfg["general"]["model"] == "p8000-ot" and cfg["terminal"] == {"skalierung": "glatt"}
        # und zurück zum Kern-Terminal
        w._on_model_selected("p8000")
        w.run_timer.stop()
        assert w.emulator.term_kind(0) == 0
        assert not isinstance(w.keyboard_widget, KeyboardK7673Widget)
        assert not isinstance(w.screen_widget.aktuell(), OriginalTerminalWidget)
    finally:
        _zu(w, qapp)


def test_variante_p8000_terminal_ist_die_einheit_ohne_rechner(qapp, umgebung):
    from app.ui.p8000_original import OriginalTerminalWidget
    w = _fenster(qapp, modell="p8000-term")
    try:
        assert w.emulator.machine == "p8000-terminal"
        assert w.emulator.term_tty(0) < 0                 # kein ttyN, die Leitung geht über den Hub
        assert isinstance(w.screen_widget.aktuell(), OriginalTerminalWidget)
        assert w.windowTitle() == "P8000 Terminal"
        assert w.drives_dock.isHidden() and not w.drives_dock.toggleViewAction().isEnabled()
        assert w.status_widget.frontplatte is not None and w.status_widget.frontplatte.isHidden()
        assert not w.platten_widget.verfuegbar()
        assert w.act_verbindung.isEnabled()
        assert w.screen_widget.tabs.tabText(0) == "P8000 Terminal"
        # zurück zu einem Rechner: Kästen und Titel kommen wieder
        w._on_model_selected("p8000")
        w.run_timer.stop()
        assert w.windowTitle() == "P8000 Emulator" and not w.drives_dock.isHidden()
        assert not w.act_verbindung.isEnabled()
        assert w.emulator.machine == "p8000"
    finally:
        _zu(w, qapp)


def test_programm_p8000term_startet_als_terminal_mit_eigener_konfiguration(qapp, umgebung):
    from app import config_io
    w = _fenster(qapp, programm="p8000term")
    try:
        assert w._model == "p8000-term" and w.windowTitle() == "P8000 Terminal"
        assert w.emulator.machine == "p8000-terminal"
        assert "verbindung" in [a.objectName() or "" for a in w.controls_bar.actions()] or \
            w.act_verbindung in w.controls_bar.actions()
        w._autosave_now()
        assert (umgebung / "p8000term.yaml").is_file() and not (umgebung / "p8000emu.yaml").exists()
        assert config_io.load_config(str(umgebung / "p8000term.yaml"))["general"]["model"] == "p8000-term"
    finally:
        _zu(w, qapp)


def test_boot_smoke_p8000_plus_terminal_einschaltmeldung_im_originalbild(qapp, umgebung):
    """Netz-Ein: die Firmware des Terminals schreibt ihre Meldung in den Bildspeicher; später
    kommt die Meldung des Rechners (Hardwaretest) über die Leitung auf dasselbe Bild."""
    w = _fenster(qapp, modell="p8000-8-ot")
    try:
        # Das Terminal läuft vor dem Rechner an (Vorlauf 1,5 s): seine Einschaltmeldung steht schon
        # im Bildspeicher, bevor der Rechner die erste Zeile schreibt — lesbar über die Zellen-API.
        text = w.emulator.term_text(0)
        assert "ADM31/9600 baud" in text, text
        t = w.screen_widget.aktuell()
        t.aktualisieren()
        assert t._bild is not None and any(t._daten[:640 * 14])          # und als Pixel im Bild
        gelaufen = 0
        while gelaufen < 250_000_000 and "Press RETURN" not in text:
            gelaufen += w.emulator.run(4_000_000)
            text = w.emulator.term_text(0)
        assert "P8000 Hardwaretest U880" in text and "Press RETURN" in text, text
        t.aktualisieren()
        assert any(t._daten)                                  # die Rechnermeldung steht im Pixelbild
        assert "Originalterminal" in w.screen_widget.modus_text()
    finally:
        _zu(w, qapp)


# ─── Bildschirm-Widget ───────────────────────────────────────────────────────

def test_bildschirm_zieht_frames_und_pausiert_bei_unveraendertem_zaehler(qapp, umgebung):
    w = _fenster(qapp, modell="p8000-term")
    try:
        emu = w.emulator
        t = w.screen_widget.aktuell()
        emu.run(3_000_000)
        aufrufe = []
        original = emu.term_framebuffer
        emu.term_framebuffer = lambda i=0: (aufrufe.append(1), original(i))[1]
        t.aktualisieren()
        assert len(aufrufe) == 1                             # neues Bild geholt
        bild = t._bild
        zaehler = emu.term_frame_count(0)
        for _ in range(5):
            t.aktualisieren()
        assert len(aufrufe) == 1 and t._bild is bild          # Zähler unverändert: weder geholt noch neu
        assert emu.term_frame_count(0) == zaehler
        emu.run(4_000_000)                                    # ≈ 1 s Z8-Zeit ⇒ viele Bilder
        assert emu.term_frame_count(0) > zaehler
        t.aktualisieren()
        assert len(aufrufe) == 2 and t._bild is not bild
    finally:
        _zu(w, qapp)


def test_skalierung_ganzzahlig_oder_glatt(qapp, umgebung):
    from app.ui.p8000_original import BILD_H, BILD_W
    w = _fenster(qapp, modell="p8000-term")
    try:
        t = w.screen_widget.aktuell()
        t.resize(1300, 700)
        t.set_skalierung("ganzzahlig")
        r = t.bild_rechteck()
        assert (r.width(), r.height()) == (2 * BILD_W, 2 * BILD_H)              # Faktor 2, Rand dunkel
        assert r.center().x() in range(648, 653)
        t.set_skalierung("glatt")
        r = t.bild_rechteck()
        assert r.width() == 1300 and abs(r.height() - 1300 * BILD_H // BILD_W) <= 1
        # die Wahl geht an alle Reiter, wird gemerkt und zurückgeladen
        w.screen_widget.set_skalierung("ganzzahlig")
        assert t.skalierung == "ganzzahlig"
        w._autosave_now()
        from app import config_io
        cfg = config_io.load_config(str(umgebung / "p8000emu.yaml"))
        assert cfg["terminal"] == {"skalierung": "ganzzahlig"}
    finally:
        _zu(w, qapp)
    w = _fenster(qapp, modell="p8000-term")
    try:
        assert w.screen_widget.skalierung == "ganzzahlig"
        assert w.screen_widget.aktuell().skalierung == "ganzzahlig"
    finally:
        _zu(w, qapp)


def test_zeichenfarbe_gruen_weiss_bernstein(qapp, umgebung):
    from app.ui.p8000_original import FARBEN
    w = _fenster(qapp, modell="p8000-term")
    try:
        t = w.screen_widget.aktuell()
        t.aktualisieren()
        for schluessel, _anzeige, an, _aus in FARBEN:
            t.set_farbe(schluessel)
            assert tuple(w.screen_widget.params.phosphor_on) == an
            assert t._bild is None or t._bild.colorTable()[1] != t._bild.colorTable()[0]
    finally:
        _zu(w, qapp)


# ─── Tastatur K7673: Tastenbild ──────────────────────────────────────────────

def test_layout_105_positionen_scancodes_wie_im_eprom_abzug():
    from app.core_binding.k1520 import K1520Emulator
    from app.ui import k7673_layout as L
    e = K1520Emulator(machine="p8000-terminal")
    try:
        belegt = {}
        for z in range(8):
            for s in range(16):
                sc = e.term_matrix_scancode(0, z, s)
                if sc:
                    belegt[(z, s)] = sc
        assert len(belegt) == 105
        assert {p: L.scancode(p) for p in L.TASTEN} == belegt     # Tabelle des Layouts = Kern (EPROM)
    finally:
        del e
    assert sorted(p for p, *_ in L.BILD) == sorted(L.TASTEN)      # jede Position genau einmal im Bild
    assert len(L.BILD) == 105
    # Keine zwei Tasten überdecken sich
    felder = [(x, y, x + b, y + 1) for _, x, y, b in L.BILD]
    for i, a in enumerate(felder):
        for b in felder[i + 1:]:
            assert a[2] <= b[0] + 1e-6 or b[2] <= a[0] + 1e-6 or a[3] <= b[1] + 1e-6 or b[3] <= a[1] + 1e-6, (a, b)
    assert max(f[2] for f in felder) <= L.BILD_BREITE and max(f[3] for f in felder) <= L.BILD_HOEHE


def test_jede_matrixposition_ist_ueber_die_bildschirmtastatur_erreichbar(qapp):
    from PySide6.QtCore import QPointF, Qt
    from PySide6.QtTest import QTest
    from app.ui import k7673_layout as L
    from app.ui.keyboard_k7673 import KeyboardK7673Widget
    kw = KeyboardK7673Widget()
    kw.resize(1000, 340)
    gedrueckt, losgelassen = [], []
    kw.keyPressed.connect(lambda k, s, c: gedrueckt.append(k))
    kw.keyReleased.connect(lambda k: losgelassen.append(k))
    for pos in L.positionen():
        if pos in L.UMSCHALTER:
            continue
        mitte = kw.taste_rechteck(pos).center().toPoint()
        assert kw.taste_bei(QPointF(mitte)) == pos
        QTest.mouseClick(kw, Qt.LeftButton, Qt.NoModifier, mitte)
    erwartet = [L.matrix_kode(*p) for p in L.positionen() if p not in L.UMSCHALTER]
    assert gedrueckt == erwartet and losgelassen == erwartet       # Drücken UND Loslassen je Taste
    assert len(set(gedrueckt)) == 105 - len(L.UMSCHALTER)
    # Kodierung: dieselbe wie im Kern (`0x04000000 | Zeile << 8 | Spalte`)
    assert L.matrix_kode(3, 8) == 0x04000308 and L.kode_matrix(0x04000308) == (3, 8)
    assert L.kode_matrix(0x41) is None


def test_umschalter_rasten_und_die_rechte_maustaste_haelt(qapp):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    from app.ui import k7673_layout as L
    from app.ui.keyboard_k7673 import KeyboardK7673Widget
    kw = KeyboardK7673Widget()
    kw.resize(1000, 340)
    ereignisse = []
    kw.keyPressed.connect(lambda k, s, c: ereignisse.append(("an", L.kode_matrix(k))))
    kw.keyReleased.connect(lambda k: ereignisse.append(("aus", L.kode_matrix(k))))
    shift, ctrl, a = (1, 6), (6, 13), (2, 0)
    QTest.mouseClick(kw, Qt.LeftButton, Qt.NoModifier, kw.taste_rechteck(shift).center().toPoint())
    assert ereignisse == [("an", shift)] and kw.gehalten() == {shift}      # SHIFT bleibt gedrückt
    QTest.mouseClick(kw, Qt.LeftButton, Qt.NoModifier, kw.taste_rechteck(a).center().toPoint())
    assert ereignisse[1:] == [("an", a), ("aus", a)] and kw.gehalten() == {shift}
    QTest.mouseClick(kw, Qt.LeftButton, Qt.NoModifier, kw.taste_rechteck(shift).center().toPoint())
    assert ereignisse[-1] == ("aus", shift) and kw.gehalten() == set()
    QTest.mouseClick(kw, Qt.LeftButton, Qt.NoModifier, kw.taste_rechteck(ctrl).center().toPoint())
    assert kw.gehalten() == {ctrl}
    QTest.mouseClick(kw, Qt.RightButton, Qt.NoModifier, kw.taste_rechteck(a).center().toPoint())
    assert kw.gehalten() == {ctrl, a}                                       # rechte Taste hält jede Taste
    kw.clear_host_keys()                                                    # Ausschalten löst alles
    assert kw.gehalten() == set() and ("aus", ctrl) in ereignisse and ("aus", a) in ereignisse


def test_leds_der_k7673(qapp):
    from app.ui.keyboard_k7673 import KeyboardK7673Widget
    kw = KeyboardK7673Widget()
    kw.set_leds(0b010)
    assert kw.leds() == 2
    kw.set_leds(-1)                                          # ohne Originalterminal: aus
    assert kw.leds() == 0


def test_tastatur_im_fenster_geht_an_die_matrix_des_kerns(qapp, umgebung):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    from app.ui import k7673_layout as L
    w = _fenster(qapp, modell="p8000-term")
    try:
        gemerkt = []
        w.emulator.term_matrix_key = lambda i, z, s, an: gemerkt.append((i, z, s, an)) or True
        kw = w.keyboard_widget
        kw.resize(1000, 340)
        QTest.mouseClick(kw, Qt.LeftButton, Qt.NoModifier, kw.taste_rechteck((3, 8)).center().toPoint())
        assert gemerkt == [(0, 3, 8, True), (0, 3, 8, False)]
    finally:
        _zu(w, qapp)


# ─── Wirtstastatur → Matrix ──────────────────────────────────────────────────

def _widget(qapp, emu=None):
    from app.ui.p8000_original import OriginalTerminalWidget
    from app.ui.screen_widget import CRTParams
    t = OriginalTerminalWidget(CRTParams())
    t.set_emulator(emu or Aufzeichner())
    return t


def test_zeichen_auf_tasten_nach_den_firmwaretabellen():
    from app.ui import k7673_layout as L
    assert L.zeichen_taste("a") == ((2, 0), False) and L.zeichen_taste("A") == ((2, 0), True)
    assert L.zeichen_taste("z") == ((5, 2), False) and L.zeichen_taste("y") == ((3, 0), False)   # QWERTZ
    assert L.zeichen_taste("1") == ((0, 0), False) and L.zeichen_taste("!") == ((0, 0), True)
    assert L.zeichen_taste(" ") == ((7, 7), False)
    assert L.zeichen_taste(">") == ((4, 6), True) and L.zeichen_taste("<") == ((4, 6), False)
    assert L.zeichen_taste("@") == ((0, 1), True) and L.zeichen_taste("#") == ((6, 5), False)
    assert L.zeichen_taste("{") == ((2, 5), True) and L.zeichen_taste("[") == ((2, 5), False)
    assert L.zeichen_taste("ä") == ((2, 5), False) and L.zeichen_taste("Ä") == ((2, 5), True)
    assert L.zeichen_taste("ö") == ((6, 4), False) and L.zeichen_taste("ü") == ((1, 5), False)
    assert L.zeichen_taste("ß") == L.zeichen_taste("~") == ((0, 5), False)
    # Hauptblock vor Ziffernblock: die Ziffer 9 liegt auf (0,4), nicht auf einer der beiden anderen 9
    assert L.zeichen_taste("9") == ((0, 4), False)
    # Jedes druckbare ASCII-Zeichen ist erreichbar — außer denen, die die Firmware nicht liefert.
    fehlt = [chr(c) for c in range(0x20, 0x7F) if L.zeichen_taste(chr(c)) is None]
    assert fehlt == [], fehlt
    # Sondertasten
    assert L.sondertaste(0x01000004) == (3, 8)               # Return
    assert L.sondertaste(0x01000012) == (2, 8)               # Pfeil links
    assert L.sondertaste(0x01000036) == (3, 14)              # F7 = BREAK


def _ev(t, an, key, mod=None, text=""):
    """Wirtstastenereignis unmittelbar an das Widget (QTest.keyPress mit Modifikator erzeugt von sich
    aus Shift/Strg-Ereignisse hinzu, das wäre ein anderer Test)."""
    from PySide6.QtCore import QEvent, Qt
    from PySide6.QtGui import QKeyEvent
    mod = Qt.NoModifier if mod is None else mod
    e = QKeyEvent(QEvent.KeyPress if an else QEvent.KeyRelease, key, mod, text)
    (t.keyPressEvent if an else t.keyReleaseEvent)(e)


def test_wirtstaste_kleinbuchstabe_und_loslassen(qapp):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    emu = Aufzeichner()
    t = _widget(qapp, emu)
    QTest.keyPress(t, Qt.Key_A, Qt.NoModifier)
    assert emu.ereignisse == [((2, 0), True)]
    QTest.keyRelease(t, Qt.Key_A, Qt.NoModifier)
    assert emu.ereignisse == [((2, 0), True), ((2, 0), False)]


def test_wirts_shift_wird_als_shift_der_k7673_weitergegeben(qapp):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    emu = Aufzeichner()
    t = _widget(qapp, emu)
    _ev(t, True, Qt.Key_Shift, Qt.ShiftModifier)
    _ev(t, True, Qt.Key_A, Qt.ShiftModifier, "A")
    _ev(t, False, Qt.Key_A, Qt.ShiftModifier, "A")
    _ev(t, False, Qt.Key_Shift)
    assert emu.ereignisse == [((1, 6), True), ((2, 0), True), ((2, 0), False), ((1, 6), False)]


def test_zeichen_mit_shift_ohne_wirts_shift_drueckt_shift_vorher(qapp):
    """AltGr+Q = „@" am Wirtsrechner: das Terminal braucht SHIFT + (0,1) — SHIFT zuerst und mit Vorlauf,
    sonst käme „3" vor dem SHIFT (die K7673 sendet gleichzeitig erkannte Tasten zeilenweise)."""
    from PySide6.QtCore import QEvent, Qt
    from PySide6.QtGui import QKeyEvent
    from PySide6.QtTest import QTest
    from app.ui import p8000_original as O
    emu = Aufzeichner()
    t = _widget(qapp, emu)
    t.keyPressEvent(QKeyEvent(QEvent.KeyPress, Qt.Key_Q, Qt.NoModifier, "@"))
    assert emu.ereignisse == [((1, 6), True)]                   # erst nur SHIFT
    QTest.qWait(O.SHIFT_VORLAUF_MS + 80)
    assert emu.ereignisse == [((1, 6), True), ((0, 1), True)]
    t.keyReleaseEvent(QKeyEvent(QEvent.KeyRelease, Qt.Key_Q, Qt.NoModifier, "@"))
    assert emu.ereignisse[2:] == [((0, 1), False), ((1, 6), False)]
    # Kurz getippt: losgelassen, bevor der Vorlauf um ist — das Zeichen kommt trotzdem
    emu.ereignisse.clear()
    t.keyPressEvent(QKeyEvent(QEvent.KeyPress, Qt.Key_Q, Qt.NoModifier, "@"))
    t.keyReleaseEvent(QKeyEvent(QEvent.KeyRelease, Qt.Key_Q, Qt.NoModifier, "@"))
    QTest.qWait(O.MINDEST_HALTEN_MS + 100)
    assert ((0, 1), True) in emu.ereignisse and ((0, 1), False) in emu.ereignisse
    assert emu.ereignisse[-1] == ((1, 6), False)
    assert emu.ereignisse.index(((1, 6), True)) < emu.ereignisse.index(((0, 1), True))


def test_strg_buchstabe_und_sondertasten(qapp):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    emu = Aufzeichner()
    t = _widget(qapp, emu)
    _ev(t, True, Qt.Key_Control, Qt.ControlModifier)
    _ev(t, True, Qt.Key_C, Qt.ControlModifier, "\x03")
    _ev(t, False, Qt.Key_C, Qt.ControlModifier, "\x03")
    _ev(t, False, Qt.Key_Control)
    assert emu.ereignisse == [((6, 13), True), ((3, 1), True), ((3, 1), False), ((6, 13), False)]
    emu.ereignisse.clear()
    for taste, pos in ((Qt.Key_Return, (3, 8)), (Qt.Key_Escape, (5, 6)), (Qt.Key_Tab, (0, 6)),
                       (Qt.Key_Backspace, (6, 7)), (Qt.Key_Delete, (4, 7)), (Qt.Key_Up, (7, 9)),
                       (Qt.Key_Down, (1, 8)), (Qt.Key_Left, (2, 8)), (Qt.Key_Right, (0, 8)),
                       (Qt.Key_Home, (5, 9)), (Qt.Key_F7, (3, 14)), (Qt.Key_F8, (5, 12)),
                       (Qt.Key_F9, (7, 14)), (Qt.Key_F10, (7, 15)), (Qt.Key_F12, (7, 12)),
                       (Qt.Key_CapsLock, (2, 6))):
        emu.ereignisse.clear()
        QTest.keyClick(t, taste)
        assert emu.ereignisse == [(pos, True), (pos, False)], (taste, emu.ereignisse)


def test_automatische_wiederholung_des_wirts_wird_verworfen(qapp):
    from PySide6.QtCore import QEvent, Qt
    from PySide6.QtGui import QKeyEvent
    emu = Aufzeichner()
    t = _widget(qapp, emu)
    t.keyPressEvent(QKeyEvent(QEvent.KeyPress, Qt.Key_A, Qt.NoModifier, "a"))
    for _ in range(5):
        t.keyPressEvent(QKeyEvent(QEvent.KeyPress, Qt.Key_A, Qt.NoModifier, "a", True))   # Auto-Repeat
        t.keyReleaseEvent(QKeyEvent(QEvent.KeyRelease, Qt.Key_A, Qt.NoModifier, "a", True))
    assert emu.ereignisse == [((2, 0), True)]                 # die K7673 wiederholt selbst
    t.keyReleaseEvent(QKeyEvent(QEvent.KeyRelease, Qt.Key_A, Qt.NoModifier, "a"))
    assert emu.ereignisse[-1] == ((2, 0), False)


def test_fokusverlust_laesst_gehaltene_tasten_los(qapp):
    from PySide6.QtCore import QEvent, Qt
    from PySide6.QtGui import QFocusEvent, QKeyEvent
    emu = Aufzeichner()
    t = _widget(qapp, emu)
    t.keyPressEvent(QKeyEvent(QEvent.KeyPress, Qt.Key_A, Qt.NoModifier, "a"))
    t.focusOutEvent(QFocusEvent(QEvent.FocusOut))
    assert emu.ereignisse == [((2, 0), True), ((2, 0), False)]


def test_f11_und_kuerzel_regel_bleiben(qapp):
    """F11 = Vollbild wird nicht an das Terminal gegeben; Strg+Umschalt-Kürzel gehören dem Fenster."""
    from PySide6.QtCore import QEvent, Qt
    from PySide6.QtGui import QKeyEvent
    emu = Aufzeichner()
    t = _widget(qapp, emu)
    erhalten = []
    t.toggleFullscreenRequested.connect(lambda: erhalten.append(1))
    t.keyPressEvent(QKeyEvent(QEvent.KeyPress, Qt.Key_F11, Qt.NoModifier))
    assert erhalten == [1] and emu.ereignisse == []


def test_einfuegen_geht_ueber_term_send(qapp):
    emu = Aufzeichner()
    t = _widget(qapp, emu)
    t.einfuegen("ls\r\nÄ")
    assert emu.gesendet == "ls\n"


# ─── Verbindung ──────────────────────────────────────────────────────────────

def test_verbindungsdialog_stellt_die_leitung_zum_rechner_her(qapp, umgebung):
    from app.core_binding import k1520 as K
    from app.core_binding.k1520 import K1520Emulator
    from app.ui.verbindung_dialog import VerbindungDialog, zustand_text
    rechner = K1520Emulator(machine="p8000", p8000={"karte16": "0"})
    terminal = K1520Emulator(machine="p8000-terminal")
    port = _frei()
    try:
        assert rechner.serial_configure(1, betriebsart=K.SER_TELNET, rolle=K.SER_SERVER, port=port)
        assert rechner.serial_start(1)                                        # tty2 als Server
        dlg = VerbindungDialog(terminal, 0)
        assert dlg.art.currentData() == K.SER_TELNET and dlg.rolle.currentData() == K.SER_CLIENT
        assert dlg.knopf.text() == "Verbinden" and "getrennt" in dlg.zustand.text()
        dlg.host.setText("127.0.0.1")
        dlg.port.setValue(port)
        dlg.knopf.click()
        for _ in range(300):
            if terminal.serial_status(0).zustand == K.SER_VERBUNDEN:
                break
            time.sleep(0.01)
        dlg.aktualisieren()
        assert terminal.serial_status(0).zustand == K.SER_VERBUNDEN
        assert dlg.knopf.text() == "Trennen" and "verbunden" in dlg.zustand.text()
        assert not dlg.port.isEnabled() and not dlg.host.isEnabled()          # im Betrieb gesperrt
        assert terminal.serial_config(0).port == port
        assert zustand_text(terminal.serial_status(0)).startswith("verbunden mit ")
        dlg.knopf.click()                                                     # Trennen
        assert terminal.serial_status(0).zustand == K.SER_AUS
        assert dlg.knopf.text() == "Verbinden" and dlg.port.isEnabled()
        # Datei als Betriebsart sperrt Rechner/Port
        dlg.art.setCurrentIndex(dlg.art.findData(K.SER_DATEI))
        assert not dlg.port.isEnabled() and dlg.datei.isEnabled()
        dlg._timer.stop()
    finally:
        terminal.serial_stop(0)
        rechner.serial_stop(1)


def test_statuszeile_zeigt_die_verbindung_der_terminaleinheit(qapp, umgebung):
    from app.core_binding import k1520 as K
    from app.core_binding.k1520 import K1520Emulator
    rechner = K1520Emulator(machine="p8000", p8000={"karte16": "0"})
    port = _frei()
    w = _fenster(qapp, modell="p8000-term")
    try:
        w._update_verbindung()
        feld = w.status_widget.verbindung
        assert "Rechner: getrennt" in feld.text.text() and not feld.isHidden()
        assert rechner.serial_configure(1, betriebsart=K.SER_TELNET, rolle=K.SER_SERVER, port=port)
        assert rechner.serial_start(1)
        assert w.emulator.serial_configure(0, betriebsart=K.SER_TELNET, rolle=K.SER_CLIENT,
                                           host="127.0.0.1", port=port)
        assert w.emulator.serial_start(0)
        for _ in range(300):
            if w.emulator.serial_status(0).zustand == K.SER_VERBUNDEN:
                break
            time.sleep(0.01)
        w._update_verbindung()
        assert "Rechner: verbunden" in feld.text.text()
        # Reiter „Schnittstellen": der Hinweis auf die Gegenstelle, Vorschlagsport 5004
        assert w.serial_widget.block("Terminal (XB5)") is not None
        # nur die Einheit hat das Feld
        w._on_model_selected("p8000")
        w.run_timer.stop()
        assert feld.isHidden()
    finally:
        rechner.serial_stop(1)
        _zu(w, qapp)


def test_verbindung_im_rechnermodell_ist_nicht_wahlbar(qapp, umgebung):
    w = _fenster(qapp, modell="p8000-ot")
    try:
        assert not w.act_verbindung.isEnabled()
    finally:
        _zu(w, qapp)


def test_schnittstellenreiter_bietet_ttys_als_server_mit_portvorschlaegen(qapp, umgebung):
    from app.core_binding import k1520 as K
    w = _fenster(qapp, modell="p8000-ot")
    try:
        namen = [b.info.name for b in w.serial_widget.bloecke()]
        assert namen == ["tty0", "tty2", "tty3", "tty4", "tty5", "tty6", "tty7"]
        ports = {n: w.emulator.serial_config(i).port for i, n in enumerate(namen)}
        assert ports == {"tty0": 5000, "tty2": 5002, "tty3": 5003, "tty4": 5004,
                         "tty5": 5005, "tty6": 5006, "tty7": 5007}
        assert all(w.emulator.serial_config(i).rolle == K.SER_SERVER for i in range(7))
        assert "Mehrplatzbetrieb" in w.serial_widget.hinweis and "p8000term" in w.serial_widget.hinweis
    finally:
        _zu(w, qapp)
    w = _fenster(qapp, modell="p8000-term")
    try:
        assert w.emulator.serial_config(0).port == 5004 and w.emulator.serial_config(0).rolle == K.SER_CLIENT
    finally:
        _zu(w, qapp)


# ─── Mehrinstanz ─────────────────────────────────────────────────────────────

def test_instanzname_und_config_bestimmen_die_konfigurationsdatei(umgebung, monkeypatch, tmp_path):
    from app import config_io, instanz, profil
    t = profil.profil("p8000term")
    assert Path(config_io.default_config_path(t)).name == "p8000term.yaml"
    monkeypatch.setenv("K1520_INSTANZ", "platz 2/../x")
    assert instanz.name() == "platz2x"                                       # bereinigt
    assert Path(config_io.default_config_path(t)).name == "p8000term-platz2x.yaml"
    assert Path(config_io.default_config_path(profil.profil("p8000"))).name == "p8000emu-platz2x.yaml"
    eigene = str(tmp_path / "meine.yaml")
    monkeypatch.setenv("K1520_KONFIG", eigene)
    assert config_io.default_config_path(t) == eigene


def test_zwei_instanzen_schreiben_getrennte_konfigurationen(qapp, umgebung, monkeypatch):
    monkeypatch.setenv("K1520_INSTANZ", "a")
    w1 = _fenster(qapp, programm="p8000term")
    monkeypatch.setenv("K1520_INSTANZ", "b")
    w2 = _fenster(qapp, programm="p8000term")
    try:
        assert w1.windowTitle() == "P8000 Terminal [a]" and w2.windowTitle() == "P8000 Terminal [b]"
        w1._autosave_now()
        w2._autosave_now()
        assert (umgebung / "p8000term-a.yaml").is_file() and (umgebung / "p8000term-b.yaml").is_file()
    finally:
        _zu(w1, qapp)
        _zu(w2, qapp)


def test_main_kennt_instance_und_config():
    r = subprocess.run([sys.executable, str(PROJECT_ROOT / "app" / "main.py"), "--machine", "p8000term",
                        "--instance", "platz2", "--help"], capture_output=True, text=True, timeout=60,
                       encoding="utf-8")
    assert r.returncode == 0 and "--instance NAME" in r.stdout and "p8000term-NAME.yaml" in r.stdout
    r = subprocess.run([sys.executable, str(PROJECT_ROOT / "app" / "main.py"), "--machine", "p8000term",
                        "--instance"], capture_output=True, text=True, timeout=60, encoding="utf-8")
    assert r.returncode == 2


def test_plattensperre_sperrt_die_zweite_instanz_aus(qapp, umgebung, tmp_path):
    from app import instanz
    pfad = str(tmp_path / "gemeinsam.img")
    # eine fremde, lebende Instanz hält die Platte (der Elternprozess des Tests lebt)
    Path(pfad + ".lock").write_text(str(os.getppid()), encoding="ascii")
    assert instanz.sperre_nehmen(pfad) == os.getppid()
    w = _fenster(qapp)
    try:
        meldungen = []
        w.platten_widget.meldung.connect(meldungen.append)
        assert not w.platten_widget.neu_anlegen(pfad)
        assert w.platten_widget.pfad() == "" and w.emulator.hd_path(0) == ""
        assert meldungen and "gehört schon dem Prozess" in meldungen[-1]
        # verwaiste Sperre (Prozess tot) wird übernommen
        tot = subprocess.Popen([sys.executable, "-c", "pass"])
        tot.wait()
        Path(pfad + ".lock").write_text(str(tot.pid), encoding="ascii")
        assert w.platten_widget.neu_anlegen(pfad)
        assert Path(pfad + ".lock").read_text(encoding="ascii") == str(os.getpid())
        w.platten_widget.abtrennen()
        assert not Path(pfad + ".lock").exists()                               # eigene Sperre weg
    finally:
        _zu(w, qapp)


# ─── Mehrplatz über Loopback-Telnet ──────────────────────────────────────────

def test_mehrplatz_taste_im_terminal_erreicht_den_rechner_und_ausgabe_das_bild(qapp):
    """Rechner (8-Bit-Teil + Originalterminal als Konsole) bietet tty0 als Telnet-Server an; eine zweite
    Terminaleinheit verbindet sich.  Hin: `pw 24 54/30` am Monitor des Rechners sendet „T0" — es steht im
    Bild des Arbeitsplatzes.  Zurück: Taste „x" im Widget des Arbeitsplatzes → K7673 → Firmware →
    Telnet → SIO0-A; `pr 24` zeigt 78.  (Ablauf wie `P8000TerminalOriginal.ZweiArbeitsplaetze…`.)"""
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    from app.core_binding import k1520 as K
    from app.core_binding.k1520 import K1520Emulator
    from app.ui.p8000_original import OriginalTerminalWidget
    from app.ui.screen_widget import CRTParams
    m = K1520Emulator(machine="p8000", p8000={"terminal": "original", "karte16": "0"})
    t = K1520Emulator(machine="p8000-terminal")
    port = _frei()
    try:
        assert m.serial_configure(0, betriebsart=K.SER_TELNET, rolle=K.SER_SERVER, port=port)
        assert m.serial_start(0)
        assert t.serial_configure(0, betriebsart=K.SER_TELNET, rolle=K.SER_CLIENT, host="127.0.0.1", port=port)
        assert t.serial_start(0)
        for _ in range(500):
            if (t.serial_status(0).zustand == K.SER_VERBUNDEN
                    and m.serial_status(0).zustand == K.SER_VERBUNDEN):
                break
            time.sleep(0.01)
        assert t.serial_status(0).zustand == K.SER_VERBUNDEN
        m.power_on()
        t.power_on()

        def schritt():
            m.run(5000)
            t.run(4608)                            # 5 000 Rechnertakte ≙ 4 608 Z8-Takte

        def bis(text_von, nadel, grenze):
            z = 0
            while z < grenze:
                schritt()
                z += 5000
                if z % 100_000 == 0 and nadel in text_von():
                    return True
            return nadel in text_von()

        def tippe(text, warte=700_000):
            for c in text:
                m.key_press(ord(c), False, False)
                for _ in range(warte // 5000):
                    schritt()

        konsole = lambda: m.term_text(0)
        arbeitsplatz = lambda: t.term_text(0)
        assert bis(konsole, "Press RETURN", 200_000_000), konsole()
        assert "ADM31/9600 baud" in arbeitsplatz()           # Einschaltmeldung der Firmware am Arbeitsplatz
        tippe("\r", 900_000)
        assert bis(konsole, "\n>", 20_000_000)
        tippe("\r", 900_000)                                  # die erste Eingabe nach dem Hardwaretest bootet
        assert bis(konsole, "DISK ERROR", 80_000_000)
        for _ in range(2000):
            schritt()
        for z in ("pw 25 05\r", "pw 25 68\r", "pw 24 54\r", "pw 24 30\r"):
            tippe(z)
            for _ in range(400):
                schritt()
        for _ in range(20):
            time.sleep(0.005)
            schritt()
        assert bis(arbeitsplatz, "T0", 4_000_000), arbeitsplatz()      # Ausgabe des Rechners → Terminalbild
        # Der Arbeitsplatz zeigt es auch im Widget (Pixelbild geholt, Zähler gelaufen)
        widget = OriginalTerminalWidget(CRTParams())
        widget.set_emulator(t)
        widget.aktualisieren()
        assert widget._bild is not None and any(widget._daten)

        # Rückweg: Taste „x" im Widget des Arbeitsplatzes
        QTest.keyPress(widget, Qt.Key_X)                      # gehalten wie von einer Hand: die K7673
        for _ in range(1_500_000 // 5000):                    # entprellt über viele Abtastungen
            schritt()
        QTest.keyRelease(widget, Qt.Key_X)
        for _ in range(8_000_000 // 5000):
            schritt()
        for _ in range(40):
            time.sleep(0.005)
            schritt()
        tippe("pr 24\r")
        for _ in range(4_000_000 // 5000):
            schritt()
        assert bis(konsole, "\n78", 4_000_000), konsole()
    finally:
        t.serial_stop(0)
        m.serial_stop(0)
