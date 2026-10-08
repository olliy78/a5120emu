"""p8000emu — der P8000 Emulator als fünftes Gesicht des Hauptfensters (AP P16).

doc/design/26_p8000emu_oberflaeche.md: Programmprofil (Titel, Konfiguration, 4 MHz, Modellwahl,
ROM-Fassungen/Platinenindex als Hardwarewahl), Terminal-Widget (Zeichensatz aus den EPROM-Abzügen,
Attribute, Reiter je Kern-Terminal), Tastenabbildung und Funktionstastenleiste, Kasten für die
Winchesterplatte, Statuszeile, Zwischenstand (P8KS), Auslieferungskonfiguration, Boot-Rauchtest
(Banner „P8000 Hardwaretest U880" im Terminal, Taste → Echo).

Gearbeitet wird ohne Pixelvergleich gegen das Fenster: gezeichnet wird in einen eigenen
640 × 288-Puffer, und der ist prüfbar.
"""

import os
from pathlib import Path

import pytest

from conftest import requires_core

pytestmark = requires_core

PLATTE_BYTES = 1024 * 5 * 18 * 512          # K5504.50
HARDWARETEST = "P8000 Hardwaretest U880"


@pytest.fixture
def umgebung(tmp_path, monkeypatch):
    """Konfiguration UND Diskettenordner in tmp — die Standardplatte entsteht dort, nicht im Heim."""
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    monkeypatch.setenv("K1520_DISKS", str(tmp_path / "disks"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


def _fenster(qapp, modell=None):
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(None, profil=profil.profil("p8000"))
    w.run_timer.stop()                        # die Tests schalten die Zeit selbst
    if modell and modell != w._model:
        w._on_model_selected(modell)
        w.run_timer.stop()
    w.show()
    qapp.processEvents()
    return w


def _zu(w, qapp):
    w.close()
    qapp.processEvents()


def _bis(w, text, budget=150_000_000, schritt=2_000_000):
    """Laufen, bis *text* im Terminal steht (Takte als Budget)."""
    gelaufen = 0
    while gelaufen < budget:
        gelaufen += w.emulator.run(schritt)
        if text in w.emulator.term_text(0):
            return True
    return False


def _menue(w, titel):
    for aktion in w.menuBar().actions():
        if aktion.text() == titel:
            return list(aktion.menu().actions())
    raise AssertionError(f"kein Menü {titel}")


# ─── Profil, Modell, Hardwarewahl ────────────────────────────────────────────

def test_profil_und_titel(qapp, umgebung):
    from app import profil
    p = profil.profil("p8000")
    assert (p.programm, p.titel, p.konfig_datei, p.vorgabe_datei) == (
        "p8000emu", "P8000 Emulator", "p8000emu.yaml", "default_config_p8000.yaml")
    assert p.nenntakt_hz == 4_000_000 and p.nenntakt_text == "4 MHz"
    assert p.terminal and p.platte and p.frontplatte and not p.eprommer
    assert p.tastatur == "p8000" and not p.ptape_wahl
    assert "nmi" in p.eigene_aktionen
    w = _fenster(qapp)
    try:
        assert w.windowTitle() == "P8000 Emulator"
        assert w.emulator.machine == "p8000"
        assert w.CPU_HZ == 4_000_000
        assert w._drive_types[:2] == ["K5601", "K5601"]
        assert hasattr(w, "act_nmi") and hasattr(w, "act_stand_speichern")
        assert not hasattr(w, "act_p8000emu")      # man startet sich nicht selbst
        assert w.emulator.term_count() == 1
        assert w.status_widget.frontplatte is not None
    finally:
        _zu(w, qapp)


def test_vorgaben_des_vollgeraets(qapp, umgebung):
    """karte16 an, Index 3/4, MON 3.1, 1 MB, WDC 4.2, Platte = Datei (Vorgabe des Auftrags)."""
    from app import profil
    p = profil.profil("p8000")
    assert p.standard_modell() == "p8000"
    kern = p.kern_parameter("p8000", {})["p8000"]
    assert kern == {"karte16": "1", "wdc": "4.2", "index8": "3", "index16": "4",
                    "mon16": "3.1", "dram": "1M@0", "mon8": "3.1"}
    # ohne Winchester: keine wdc-Zeile (der Kern lehnt wdc ohne … nicht ab, aber es soll aus sein)
    k16 = p.kern_parameter("p8000-16", {})["p8000"]
    assert k16["karte16"] == "1" and "wdc" not in k16
    k8 = p.kern_parameter("p8000-8", {})["p8000"]
    assert k8 == {"karte16": "0", "index8": "3", "mon8": "3.1"}
    assert p.modell_hat_wdc("p8000") and not p.modell_hat_wdc("p8000-16")
    # unbekanntes Modell → Vorgabe
    assert p.modell_normalisieren("quatsch") == "p8000"


def test_jede_hardwarewahl_baut_eine_maschine(qapp, umgebung):
    """Jeder angebotene Wert (je Modell, an dem er wirkt) ergibt eine startende Maschine."""
    from app import profil
    from app.core_binding.k1520 import K1520Emulator
    p = profil.profil("p8000")
    assert [h[0] for h in p.hardware] == ["index", "mon8", "mon16", "dram", "wdc"]
    for schluessel, _b, _t, werte, modelle in p.hardware:
        for modell in (modelle or ("p8000", "p8000-16", "p8000-8")):
            for wert, _anzeige in werte:
                hw = {**p.hardware_standard(), schluessel: wert}
                emu = K1520Emulator(["K5601", "K5601", "none", "none"], machine="p8000",
                                    **p.kern_parameter(modell, hw))
                emu.power_on()
                assert emu.run(200_000) > 0, (schluessel, wert, modell)
                assert emu.term_count() == 1
                del emu


def test_hardwarewahl_normalisiert_gross_klein_und_fehlendes(qapp, umgebung):
    from app import profil
    p = profil.profil("p8000")
    assert p.hardware_normalisieren({}) == {"index": "34", "mon8": "3.1", "mon16": "3.1",
                                            "dram": "1M@0", "wdc": "4.2"}
    n = p.hardware_normalisieren({"dram": "1m@0+1m@1", "mon8": 3.0, "wdc": "9.9", "index": "11"})
    assert n["dram"] == "1M@0+1M@1" and n["mon8"] == "3.0" and n["wdc"] == "4.2"
    assert n["index"] == "11"


def test_rom_fassung_und_modell_in_den_einstellungen(qapp, umgebung):
    """ROM-Wahl in *Allgemein*: wirkt auf die Maschine, wird gemerkt, graut aus, wo sie nichts tut."""
    from app import config_io
    w = _fenster(qapp)
    try:
        sw = w.settings_widget
        assert set(sw.hardware_combos) == {"index", "mon8", "mon16", "dram", "wdc"}
        assert sw.model_combo.count() == 7 and not sw.model_combo.isHidden()
        assert not sw.ptape_box.isVisibleTo(sw)           # am P8000 keine Lochstreifenkarte
        for k in ("mon16", "dram", "wdc", "mon8", "index"):
            assert sw.hardware_combos[k].isEnabled()

        # MON8 3.0: Banner der Maschine ändert sich, Konfiguration merkt es
        w._on_hardware_selected("mon8", "3.0")
        assert w._hardware["mon8"] == "3.0"
        assert _bis(w, HARDWARETEST + " - Version 3.0", 20_000_000)
        w._autosave_now()
        cfg = config_io.load_config(str(umgebung / "p8000emu.yaml"))
        assert cfg["general"]["mon8"] == "3.0" and cfg["general"]["model"] == "p8000"

        # nur 8-Bit-Teil: 16-Bit-Felder und WDC ausgegraut, Platte gesperrt
        w._on_model_selected("p8000-8")
        w.run_timer.stop()
        assert sw.model_value() == "p8000-8"
        assert not sw.hardware_combos["mon16"].isEnabled()
        assert not sw.hardware_combos["dram"].isEnabled()
        assert not sw.hardware_combos["wdc"].isEnabled()
        assert sw.hardware_combos["mon8"].isEnabled()
        assert not w.platten_widget.verfuegbar()
        assert not w.platten_widget.knopf_neu.isEnabled()
        assert w.emulator.panel_lamps() == 0

        # ohne Winchester: 16-Bit an, WDC aus
        w._on_model_selected("p8000-16")
        w.run_timer.stop()
        assert sw.hardware_combos["mon16"].isEnabled()
        assert not sw.hardware_combos["wdc"].isEnabled()
        assert not w.platten_widget.verfuegbar()
        w._on_model_selected("p8000")
        assert w.platten_widget.verfuegbar()
    finally:
        _zu(w, qapp)


# ─── Boot, Terminal-Widget, Tasten ───────────────────────────────────────────

def test_boot_zeigt_das_banner_im_terminal_und_die_taste_hallt(qapp, umgebung):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    w = _fenster(qapp, "p8000-8")                 # nur 8-Bit: der kürzeste Weg zu „Press RETURN"
    try:
        assert _bis(w, "Press RETURN")
        t = w.screen_widget.aktuell()
        t.aktualisieren()
        assert HARDWARETEST in t.text() and "U880-Softwaremonitor Version 3.1" in t.text()
        # Puffer ist gezeichnet: es gibt Pixel in der Farbe „hell/normal", nicht nur Hintergrund
        bg = t._farben()[0].rgb()
        hell = {t._puffer.pixel(x, y) for x in range(640) for y in range(0, 288, 3)}
        assert len(hell) > 1 and bg in hell
        # RETURN, dann ein Zeichen: das Echo steht im Terminal
        # (MON8 liest nach einer Eingabe erst nach Millionen Takten wieder: großzügig laufen lassen)
        t.setFocus()
        QTest.keyClick(t, Qt.Key_Return)
        w.emulator.run(20_000_000)
        QTest.keyClick(t, Qt.Key_A)
        QTest.keyClicks(t, "xyz")
        assert _bis(w, ">axyz", 80_000_000), w.emulator.term_text(0)
        # Strg+C geht als Steuerzeichen durch (kein Kürzel des Fensters), ohne Absturz
        QTest.keyClick(t, Qt.Key_C, Qt.ControlModifier)
        w.emulator.run(1_000_000)
    finally:
        _zu(w, qapp)


def test_die_zeile_unter_dem_bild_nennt_die_betriebsart(qapp, umgebung):
    w = _fenster(qapp, "p8000-8")
    try:
        text = w.screen_widget.modus_text()
        assert text.startswith("ADM31") and "Zeichensatz 1" in text and "On-Line" in text
        assert w.screen_widget.info.text() == text
        # VT100: der Gast schaltet um (ESC [ ? 2 l ist die Folge zum VT100-Modus — hier genügt der Kern)
        w.emulator.term_key(0, 0x02000000 + 22, False, False)       # SI/SO
        w.emulator.run(200_000)
        assert "Zeichensatz 2" in w.screen_widget.modus_text()
        w.emulator.term_key(0, 0x02000100, False, False)            # Caps an
        w.emulator.run(200_000)
        assert "Caps lock" in w.screen_widget.modus_text()
    finally:
        _zu(w, qapp)


class _Attrappe:
    """Minimale Maschine für das Terminal-Widget: ein vorgegebenes Bild."""

    def __init__(self):
        self.snap = bytearray(b" \x00\x00" * (80 * 24))
        self.cursor = (79, 23)
        self.flags = 3
        self.tasten = []

    def zelle(self, z, s, zeichen, attr=0, flags=0):
        o = (z * 80 + s) * 3
        self.snap[o:o + 3] = bytes((ord(zeichen), attr, flags))

    def term_snapshot(self, i):
        return bytes(self.snap)

    def term_cursor(self, i):
        return self.cursor

    def term_flags(self, i):
        return self.flags

    def term_mode(self, i):
        return 0

    def term_text(self, i):
        return "\n".join("".join(chr(self.snap[(z * 80 + s) * 3]) for s in range(80))
                         for z in range(24))

    def term_key(self, i, kode, shift, ctrl):
        self.tasten.append((i, kode, shift, ctrl))
        return True

    def term_send(self, i, text):
        self.tasten.append((i, "text", text))
        return True


def _widget(qapp):
    from app.ui.p8000_terminal import TerminalWidget
    from app.ui.screen_widget import CRTParams
    emu = _Attrappe()
    t = TerminalWidget(CRTParams(), 0)
    t.resize(800, 400)
    t.set_emulator(emu)
    return t, emu


def _pixel(t, z, s, x, y):
    return t._puffer.pixel(s * 8 + x, z * 12 + y)


def test_zeichensatz_aus_dem_abzug_und_attribute(qapp, umgebung):
    """„A" aus P8TEZS (Zeile 1 = ...###..); Invers/Leer/Unterstrich/Hell/Blinken/ZG2 wirken im Puffer."""
    from app.ui.p8000_terminal import (ATTR_BLINK, ATTR_HELL, ATTR_INVERS, ATTR_LEER,
                                       ATTR_UNTERSTRICH, FLAG_ZG2)
    t, emu = _widget(qapp)
    bg, normal, hell = [c.rgb() for c in t._farben()]
    emu.zelle(0, 0, "A")
    emu.zelle(0, 1, "A", ATTR_INVERS)
    emu.zelle(0, 2, "A", ATTR_LEER)
    emu.zelle(0, 3, "A", ATTR_UNTERSTRICH)
    emu.zelle(0, 4, "A", ATTR_HELL)
    emu.zelle(0, 5, "A", ATTR_BLINK)
    emu.zelle(0, 6, "{")
    emu.zelle(0, 7, "{", 0, FLAG_ZG2)
    emu.zelle(0, 8, "\x00")
    t.set_emulator(emu)
    t.aktualisieren()
    # normal: Pixel (3,1) gesetzt, (0,0) Hintergrund
    assert _pixel(t, 0, 0, 3, 1) == normal and _pixel(t, 0, 0, 0, 0) == bg
    # invers: vertauscht
    assert _pixel(t, 0, 1, 3, 1) == bg and _pixel(t, 0, 1, 0, 0) == normal
    # leer: unsichtbar
    assert _pixel(t, 0, 2, 3, 1) == bg
    # unterstrichen: letzte Zeile der Zelle durchgehend
    assert all(_pixel(t, 0, 3, x, 11) == normal for x in range(8))
    # hell leuchtet stärker als normal
    assert _pixel(t, 0, 4, 3, 1) == hell != normal
    # blinken: in der Aus-Phase unsichtbar, in der An-Phase sichtbar
    assert _pixel(t, 0, 5, 3, 1) == normal
    t.blinken()
    assert _pixel(t, 0, 5, 3, 1) == bg
    t.blinken()
    assert _pixel(t, 0, 5, 3, 1) == normal
    # ZG2: „{" ist dort „ä" — ein anderes Bitmuster als das ASCII-Zeichen
    zg1 = [[_pixel(t, 0, 6, x, y) for x in range(8)] for y in range(12)]
    zg2 = [[_pixel(t, 0, 7, x, y) for x in range(8)] for y in range(12)]
    assert zg1 != zg2
    # Zeichen 00H (Programm-Mode-Symbol) ist im Satz vorhanden (nicht leer)
    assert any(_pixel(t, 0, 8, x, y) != bg for x in range(8) for y in range(12))


def test_blockcursor_invertiert_die_zelle_und_blinkt(qapp, umgebung):
    t, emu = _widget(qapp)
    bg, normal, _hell = [c.rgb() for c in t._farben()]
    emu.cursor = (2, 1)
    emu.zelle(1, 2, "A")
    t.set_emulator(emu)
    t.aktualisieren()
    assert _pixel(t, 1, 2, 0, 0) == normal              # Block: Hintergrund der Zelle = Vordergrund
    assert _pixel(t, 1, 2, 3, 1) == bg                   # Glyphe in Hintergrundfarbe
    t.blinken()                                           # Aus-Phase: Cursor verschwindet
    assert _pixel(t, 1, 2, 0, 0) == bg and _pixel(t, 1, 2, 3, 1) == normal
    emu.cursor = (3, 1)                                   # Cursor wandert: alte Zelle normal
    t.blinken()
    t.aktualisieren()
    assert _pixel(t, 1, 2, 0, 0) == bg


def test_aenderungen_zeichnen_nur_geaenderte_zellen(qapp, umgebung):
    t, emu = _widget(qapp)
    gezeichnet = []
    orig = t._zeichne_zelle
    t._zeichne_zelle = lambda z, s, c: (gezeichnet.append((z, s)), orig(z, s, c))[1]
    emu.zelle(5, 7, "X")
    t.aktualisieren()
    assert {z for z, _s in gezeichnet} == {5, 23}      # Zeile 5 und die Cursorzelle unten rechts
    gezeichnet.clear()
    t.aktualisieren()                                    # nichts geändert → nichts gezeichnet
    assert gezeichnet == []


def test_host_tasten_abbildung(qapp, umgebung):
    from PySide6.QtCore import Qt
    from app.ui.p8000_terminal import host_taste, taste_kode
    NM, SH, CT = Qt.NoModifier, Qt.ShiftModifier, Qt.ControlModifier
    assert host_taste(Qt.Key_A, "a", NM) == (ord("a"), False, False)
    assert host_taste(Qt.Key_A, "A", SH) == (ord("A"), True, False)
    assert host_taste(Qt.Key_C, "\x03", CT) == (ord("C"), False, True)      # Strg+C
    assert host_taste(Qt.Key_BracketLeft, "\x1b", CT) == (0x1B, False, False)
    assert host_taste(Qt.Key_Return, "\r", NM)[0] == 0x01000004
    assert host_taste(Qt.Key_Tab, "\t", NM)[0] == 0x01000001
    assert host_taste(Qt.Key_Backtab, "", SH)[0] == 0x01000002
    assert host_taste(Qt.Key_Backspace, "\x08", NM)[0] == 0x01000003
    assert host_taste(Qt.Key_Delete, "", NM)[0] == 0x01000007
    for taste, kode in ((Qt.Key_Left, 0x01000012), (Qt.Key_Up, 0x01000013),
                        (Qt.Key_Right, 0x01000014), (Qt.Key_Down, 0x01000015),
                        (Qt.Key_Home, 0x01000010)):
        assert host_taste(taste, "", NM)[0] == kode
    for taste, name in ((Qt.Key_F1, "LINE_ERASE"), (Qt.Key_F2, "PAGE_ERASE"),
                        (Qt.Key_F3, "LINE_INSERT"), (Qt.Key_F4, "CHAR_INSERT"),
                        (Qt.Key_F5, "LINE_DELETE"), (Qt.Key_F6, "CHAR_DELETE"),
                        (Qt.Key_F7, "BREAK"), (Qt.Key_F8, "SI_SO"), (Qt.Key_Pause, "BREAK")):
        assert host_taste(taste, "", NM)[0] == taste_kode(name)
    assert host_taste(Qt.Key_Adiaeresis, "ä", NM)[0] == 0x7B               # ä → ZG2-Stelle
    assert host_taste(Qt.Key_Odiaeresis, "Ö", SH)[0] == 0x5C
    # Umschalter allein und F11 (Vollbild) ergeben nichts
    assert host_taste(Qt.Key_Shift, "", SH) is None
    assert host_taste(Qt.Key_Control, "", CT) is None
    assert host_taste(0, "€", NM) is None
    # Reihenfolge der Tastennamen = enum class TerminalTaste des Kerns
    from app.ui.p8000_terminal import TASTEN
    assert TASTEN[0] == "VT" and TASTEN[18] == "BREAK" and TASTEN[-1] == "SI_SO"


def test_terminal_widget_gibt_tasten_und_einfuegen_an_den_kern(qapp, umgebung):
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    from app.ui.p8000_terminal import taste_kode
    t, emu = _widget(qapp)
    QTest.keyClick(t, Qt.Key_F3)
    QTest.keyClick(t, Qt.Key_Up)
    QTest.keyClick(t, Qt.Key_X)
    assert [k[1] for k in emu.tasten] == [taste_kode("LINE_INSERT"), 0x01000013, ord("x")]
    emu.tasten.clear()
    t.einfuegen("ls -l\näb\r\nx")
    assert emu.tasten == [(0, "text", "ls -l\nb\nx")]
    # Feststeller rastet im Terminal: Zustand kommt aus term_flags
    emu.tasten.clear()
    QTest.keyClick(t, Qt.Key_CapsLock)
    assert emu.tasten[-1][1] == 0x02000100
    emu.flags |= 16
    QTest.keyClick(t, Qt.Key_CapsLock)
    assert emu.tasten[-1][1] == 0x02000101
    # F11 geht ans Fenster, nicht an den Gast
    gerufen = []
    t.toggleFullscreenRequested.connect(lambda: gerufen.append(True))
    emu.tasten.clear()
    QTest.keyClick(t, Qt.Key_F11)
    assert gerufen == [True] and emu.tasten == []


def test_funktionstastenleiste_schickt_kodes_und_zeigt_den_zustand(qapp, umgebung):
    from app.ui.p8000_terminal import KeyboardP8000Widget, taste_kode
    w = _fenster(qapp, "p8000-8")
    try:
        kw = w.keyboard_widget
        assert isinstance(kw, KeyboardP8000Widget)
        assert w.screen_widget.key_sink is kw
        gesendet = []
        w.emulator.term_key = lambda i, k, s, c: gesendet.append((i, k)) or True
        kw.knopf("LINE_ERASE").click()
        kw.knopf("BREAK").click()
        kw.knopf("MODE").click()
        assert gesendet == [(0, taste_kode("LINE_ERASE")), (0, taste_kode("BREAK")),
                            (0, taste_kode("MODE"))]
        del w.emulator.term_key
        # Rasttasten folgen dem Zustand des Terminals, auch wenn der Gast ihn ändert
        assert not kw.si_so.isChecked() and not kw.caps.isChecked()
        kw.si_so.click()
        w.emulator.run(500_000)
        w.screen_widget.aktuell().aktualisieren()
        assert w.emulator.term_flags(0) & 8 and kw.si_so.isChecked()
        kw.caps.click()
        w.emulator.run(500_000)
        w.screen_widget.aktuell().aktualisieren()
        assert w.emulator.term_flags(0) & 16 and kw.caps.isChecked()
        kw.caps.click()                                    # wieder aus
        w.emulator.run(500_000)
        w.screen_widget.aktuell().aktualisieren()
        assert not w.emulator.term_flags(0) & 16 and not kw.caps.isChecked()
        # Knöpfe nehmen keinen Fokus
        from PySide6.QtCore import Qt
        assert kw.knopf("BREAK").focusPolicy() == Qt.NoFocus
    finally:
        _zu(w, qapp)


def test_reiter_folgen_term_count(qapp, umgebung):
    """Die Oberfläche zeigt `term_count()` Terminals als Reiter (heute eines, ohne Reiterleiste)."""
    from app.ui.p8000_terminal import TerminalTabs

    class Zwei(_Attrappe):
        def term_count(self):
            return 2

        def term_kind(self, i):
            return 0

        def term_tty(self, i):
            return (1, 4)[i]

    class Eins(Zwei):
        def term_count(self):
            return 1

    tabs = TerminalTabs()
    tabs.set_emulator(Zwei())
    assert tabs.tabs.count() == 2 and not tabs.tabs.tabBar().isHidden()
    assert [tabs.tabs.tabText(i) for i in range(2)] == ["tty1 (Konsole)", "tty4"]
    tabs.sende_taste(0x41)
    tabs.tabs.setCurrentIndex(1)
    tabs.sende_taste(0x42)
    e = tabs.emulator
    assert [(i, k) for i, k, *_ in e.tasten] == [(0, 0x41), (1, 0x42)]
    tabs.set_emulator(Eins())
    assert tabs.tabs.count() == 1 and tabs.tabs.tabBar().isHidden()
    assert tabs.focusProxy() is tabs.aktuell()


# ─── Platte, Laufwerke, Statuszeile ──────────────────────────────────────────

def test_erster_start_legt_keine_platte_an_und_der_monitor_meldet_sich(qapp, umgebung, tmp_path):
    """Anwenderentscheid (P21): das Vollgerät startet OHNE Platte.  Eine von selbst angelegte leere (E5)
    Platte schickte MON16 in den AUTOBOOT (Merkposten 26a); angelegt wird nur über den Plattenkasten."""
    from app import config_io
    w = _fenster(qapp)
    try:
        assert not (tmp_path / "disks" / "p8000_platte.img").exists()
        assert w.platten_widget.pfad() == "" and w.emulator.hd_path(0) == ""
        assert w.platten_widget.verfuegbar() and w.platten_widget.knopf_neu.isEnabled()
        assert "Neue Platte" in w.platten_widget.hinweis.text()
        assert w.platten_widget.name.text() == "keine Platte angeschlossen"
        assert _bis(w, "Press RETURN", 160_000_000, 4_000_000)       # Hardwaretest + Monitor, kein AUTOBOOT
        w._autosave_now()
        cfg = config_io.load_config(str(umgebung / "p8000emu.yaml"))
        assert "platte" not in cfg or cfg["platte"] == {"path": ""}
    finally:
        _zu(w, qapp)


def test_platte_entsteht_nur_ueber_den_plattenkasten_und_wird_gemerkt(qapp, umgebung, tmp_path):
    from app import config_io
    w = _fenster(qapp)
    pfad = str(tmp_path / "meine_platte.img")
    try:
        assert w.platten_widget.neu_anlegen(pfad)                    # = „Neue Platte…“ im Kasten
        assert os.path.getsize(pfad) == PLATTE_BYTES and w.emulator.hd_path(0) == pfad
        # PAR-Sektor auf Z0/K0/S1: Kenntext „PARMTR" (Platte::neu)
        assert b"PARMTR" in Path(pfad).read_bytes()[:18 * 512]
        w._autosave_now()
        cfg = config_io.load_config(str(umgebung / "p8000emu.yaml"))
        assert cfg["platte"] == {"path": pfad}
    finally:
        _zu(w, qapp)
    w = _fenster(qapp)                                               # zweiter Start: dieselbe Platte
    try:
        assert w.platten_widget.pfad() == pfad and w.emulator.hd_path(0) == pfad
    finally:
        _zu(w, qapp)


def test_abtrennen_ist_eine_entscheidung_und_wird_gemerkt(qapp, umgebung, tmp_path):
    from app import config_io
    w = _fenster(qapp)
    pfad = str(tmp_path / "erste.img")
    try:
        assert w.platten_widget.neu_anlegen(pfad)
        w.platten_widget.abtrennen()
        assert w.platten_widget.pfad() == "" and w.emulator.hd_path(0) == ""
        w._autosave_now()
        assert config_io.load_config(str(umgebung / "p8000emu.yaml"))["platte"] == {"path": ""}
    finally:
        _zu(w, qapp)
    w = _fenster(qapp)                               # neu starten: bleibt leer
    try:
        assert w.platten_widget.pfad() == "" and w.emulator.hd_path(0) == ""
        # Wieder anschließen (dieselbe Datei) und neu anlegen mit anderem Typ
        assert w.platten_widget.anschliessen(pfad)
        assert w.emulator.hd_path(0) == pfad
        neu = str(tmp_path / "klein.img")
        assert w.platten_widget.neu_anlegen(neu, "D5126")
        assert os.path.getsize(neu) == 615 * 4 * 18 * 512
        assert w.emulator.hd_path(0) == neu
        # eine gemerkte Platte folgt der neu gebauten Maschine (Hardwarewechsel)
        w._on_hardware_selected("mon8", "3.0")
        assert w.emulator.hd_path(0) == neu
    finally:
        _zu(w, qapp)


def test_fehlende_platte_meldet_statt_zu_scheitern(qapp, umgebung, tmp_path):
    w = _fenster(qapp)
    try:
        meldungen = []
        w.platten_widget.meldung.connect(meldungen.append)
        w.platten_widget.zustand_anwenden({"path": str(tmp_path / "gibt_es_nicht.img")})
        assert w.platten_widget.pfad() == "" and meldungen
        assert not w.platten_widget.anschliessen(str(tmp_path / "auch_nicht.img"))
        assert w.emulator.hd_path(0) == ""
    finally:
        _zu(w, qapp)


def test_laufwerkskasten_zwei_disketten_und_der_plattenkasten_darunter(qapp, umgebung, temp_disk):
    w = _fenster(qapp, "p8000-8")
    try:
        assert w.drives_widget.present_drives() == [0, 1]
        # eine Diskette einlegen (.hfe braucht keinen Formatdialog)
        pfad = temp_disk("udos_ds77_k5601_fremdsync.hfe") if (
            Path(__file__).parent.parent / "fixtures" / "disks" / "udos_ds77_k5601_fremdsync.hfe"
        ).exists() else None
        if pfad:
            assert w.drives_widget.mount_path(0, pfad)
            assert w.emulator.disk_path(0) == pfad
        # Reihenfolge im Kasten: Disketten, darunter die Platte
        kasten = w.drives_dock.widget().widget()
        lay = kasten.layout()
        assert lay.itemAt(0).widget() is w.drives_widget
        assert lay.itemAt(1).widget() is w.platten_widget
        assert [f.drive for f in w.status_widget.felder()] == [0, 1]
    finally:
        _zu(w, qapp)


def test_frontplatte_zeigt_run_unit16_platte_und_power(qapp, umgebung):
    from app.ui import status_bar
    w = _fenster(qapp)
    try:
        platte = w.status_widget.frontplatte
        assert [l.name for l in platte.lampen()] == ["Run", "Unit16", "Platte", "Power"]
        assert platte.beschriftungen() == ["Run", "16-Bit", "Platte", "Power"]
        w._update_frontplatte()
        assert platte.zustand()["Power"] is True
        # aktiv HIGH: Bit 0/1/2 des Kerns
        platte.zeige(0b101, laeuft=True, eingeschaltet=True)
        assert platte.zustand() == {"Run": True, "Unit16": False, "Platte": True, "Power": True}
        platte.zeige(0b010, laeuft=True, eingeschaltet=True)
        assert platte.zustand() == {"Run": False, "Unit16": True, "Platte": False, "Power": True}
        platte.zeige(0b111, laeuft=True, eingeschaltet=False)       # aus: alles dunkel
        assert not any(platte.zustand().values())
        # die K8915-Frontplatte ist unverändert (aktiv low, sechs Lampen)
        k = status_bar.Frontplatte()
        assert len(k.lampen()) == 6
        k.zeige(0xFF, laeuft=True, eingeschaltet=True)
        assert k.zustand()["ERROR"] is False
        k.zeige(0x7F, laeuft=True, eingeschaltet=True)
        assert k.zustand()["ERROR"] is False or True
        # Takt in der Statuszeile: eingestellter Takt (Vorgabe 5 × 4 MHz)
        assert "4 MHz" in w.status_widget.takt.text()
    finally:
        _zu(w, qapp)


def test_16bit_lampen_ueber_die_maschine(qapp, umgebung):
    """Vollgerät am MON16: nach dem Hardwaretest der 16-Bit-Karte leuchtet UNIT16 in der Frontplatte."""
    w = _fenster(qapp, "p8000-16")
    try:
        assert w.emulator.panel_lamps() in (0, 1)       # U8001 im Reset bis zur Kopplung
        w._update_frontplatte()
        assert w.status_widget.frontplatte.zustand()["Unit16"] is False
    finally:
        _zu(w, qapp)


# ─── Aktionen, Menü, Kürzel ──────────────────────────────────────────────────

def test_aktionen_menue_und_kein_neues_kuerzel(qapp, umgebung, monkeypatch):
    from PySide6.QtGui import QAction
    from test_gui_smoke import _als_kuerzel, _handbuch_kuerzel
    from app import programme
    w = _fenster(qapp)
    try:
        maschine = _menue(w, "&Maschine")
        for name in ("power", "reset", "nmi", "stand_speichern", "stand_laden"):
            assert getattr(w, f"act_{name}") in maschine
            assert getattr(w, f"act_{name}").shortcut().isEmpty() or name in ("power", "reset")
        for name in ("nmi", "stand_speichern", "stand_laden"):
            assert getattr(w, f"act_{name}").shortcut().isEmpty()
        leiste = [a for a in w.controls_bar.actions() if not a.isSeparator()]
        assert leiste.index(w.act_nmi) == leiste.index(w.act_reset) + 1
        # Kürzel-Wächter wie bei den anderen Programmen
        fremd = [a.shortcut().toString() for a in w.findChildren(QAction)
                 if a.shortcut().toString() and a.shortcut().toString() != "F11"
                 and not a.shortcut().toString().startswith("Ctrl+Shift+")]
        assert fremd == []
        im_handbuch = {_als_kuerzel(k).toString() for k in _handbuch_kuerzel()}
        assert [a.shortcut().toString() for a in w.findChildren(QAction)
                if not a.shortcut().isEmpty()
                and a.shortcut().toString() not in im_handbuch] == []
        # Werkzeuge: die anderen Emulatoren, nicht der eigene
        gerufen = []
        monkeypatch.setattr(programme, "programm_starten", lambda k, *a, **kw: gerufen.append(k))
        assert w.act_a5120emu in _menue(w, "&Werkzeuge")
        assert w.act_disktool in _menue(w, "&Werkzeuge")
        w.act_a5120emu.trigger()
        assert gerufen == [programme.EMULATOR]
    finally:
        _zu(w, qapp)
    # und umgekehrt: der A5120 kann den P8000 starten
    from app import profil
    from app.ui.main_window import MainWindow
    a = MainWindow(None, profil=profil.profil("a5120"))
    try:
        a.run_timer.stop()
        gerufen.clear()
        assert a.act_p8000emu in _menue(a, "&Werkzeuge")
        a.act_p8000emu.trigger()
        assert gerufen == [programme.P8000EMU]
        assert not hasattr(a, "act_stand_speichern") and not hasattr(a, "act_nmi")
    finally:
        a.close()
        qapp.processEvents()


def test_nmi_taste_erreicht_die_maschine(qapp, umgebung, monkeypatch):
    w = _fenster(qapp, "p8000-8")
    try:
        gerufen = []
        monkeypatch.setattr(w.emulator, "nmi", lambda: gerufen.append(True))
        w.act_nmi.trigger()
        assert gerufen == [True]
    finally:
        _zu(w, qapp)


def test_zwischenstand_rundreise_am_prompt(qapp, umgebung, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    w = _fenster(qapp, "p8000-8")
    try:
        assert _bis(w, "Press RETURN")
        w.emulator.run(20_000_000)                    # bis der Monitor den Prompt „>" geschrieben hat
        assert w.emulator.term_text(0).rstrip().endswith(">")
        w.emulator.term_send(0, "ab\r")
        w.emulator.run(20_000_000)
        vorher = w.emulator.term_text(0)
        stand = str(tmp_path / "p8000.p8ks")
        assert w.stand_sichern(stand) and os.path.getsize(stand) > 1000
        w.emulator.term_send(0, "cd\r")
        w.emulator.run(20_000_000)
        assert w.emulator.term_text(0) != vorher
        assert w.stand_laden(stand)
        assert w.emulator.term_text(0) == vorher
        # weiterlaufen und tippen geht
        w.emulator.term_send(0, "q\r")
        w.emulator.run(20_000_000)
        assert ">q" in w.emulator.term_text(0)
    finally:
        _zu(w, qapp)
    # Konfiguration weicht ab (Vollgerät statt nur 8-Bit): Laden scheitert mit Begründung, nichts ändert sich
    w = _fenster(qapp, "p8000")
    try:
        warnungen = []
        monkeypatch.setattr(QMessageBox, "warning",
                            staticmethod(lambda *a, **k: warnungen.append(a[-1]) or 0))
        w.emulator.run(1_000_000)
        text = w.emulator.term_text(0)
        assert not w.stand_laden(stand)
        assert warnungen and "passt nicht" in warnungen[0]
        assert w.emulator.state_error()
        assert w.emulator.term_text(0) == text
    finally:
        _zu(w, qapp)


# ─── Auslieferungskonfiguration, Kommandozeile ───────────────────────────────

def test_auslieferungskonfiguration_ist_gueltig_und_ohne_rechnerpfade(qapp, umgebung):
    from app import config_io, profil
    p = profil.profil("p8000")
    cfg = config_io.standard_konfiguration(p)
    assert cfg, "data/default_config_p8000.yaml nicht gefunden"
    assert "disks" not in cfg and "platte" not in cfg
    assert "geometry" not in cfg["window"]
    g = cfg["general"]
    assert p.modell_normalisieren(g["model"]) == g["model"] == "p8000"
    # jeder Hardwarewert der Vorgabe ist ein bekannter Wert des Profils
    assert p.hardware_normalisieren(g) == {k: str(g[k]) for k in p.hardware_standard()}
    assert cfg["drive_types"][:2] == ["K5601", "K5601"]
    # jede Leistenaktion der Vorgabe gibt es in diesem Programm
    from app.ui import actions
    erlaubt = set(n for n in actions.reihenfolge("p8000") if n)
    assert [n for n in cfg["window"]["toolbar"] if n and n not in erlaubt] == []


def test_standard_zuruecksetzen_laesst_die_platte_liegen(qapp, umgebung, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    w = _fenster(qapp, "p8000-8")
    try:
        monkeypatch.setattr(QMessageBox, "question",
                            staticmethod(lambda *a, **k: QMessageBox.Yes))
        w.platten_widget.zustand_anwenden({"path": ""})       # entschieden: keine Platte
        w._standard_zuruecksetzen()
        assert w._model == "p8000" and w.platten_widget.pfad() == ""
    finally:
        _zu(w, qapp)


def test_kommandozeile_hilfe_und_pfade(umgebung):
    import subprocess
    import sys
    from conftest import PROJECT_ROOT
    env = {**os.environ, "QT_QPA_PLATFORM": "offscreen"}
    r = subprocess.run([sys.executable, str(PROJECT_ROOT / "app" / "main.py"),
                        "--machine", "p8000", "--help"], capture_output=True, text=True,
                       encoding="utf-8", env=env, timeout=60)
    assert r.returncode == 0 and r.stdout.startswith("p8000emu")
    assert "P8000" in r.stdout
    r = subprocess.run([sys.executable, str(PROJECT_ROOT / "app" / "main.py"),
                        "--machine", "p8000", "--paths"], capture_output=True, text=True,
                       encoding="utf-8", env=env, timeout=60)
    assert r.returncode == 0 and "Vorgabe (P8000):" in r.stdout
    assert "NICHT GEFUNDEN" not in r.stdout.split("Vorgabe (P8000):")[1].splitlines()[0]


def test_kern_schnittstelle_additiv(qapp, umgebung, tmp_path):
    """Schnappschuss, Flags, Klingel und Save-State der Bindung — und Ruhewerte an anderen Maschinen."""
    from app.core_binding.k1520 import K1520Emulator
    e = K1520Emulator(["K5601", "K5601", "none", "none"], machine="p8000",
                      p8000={"karte16": "0"})
    e.power_on()
    e.run(20_000_000)
    s = e.term_snapshot(0)
    assert len(s) == 24 * 80 * 3
    zeilen = ["".join(chr(s[(z * 80 + c) * 3]) for c in range(80)).rstrip() for z in range(24)]
    assert any(z.startswith(HARDWARETEST) for z in zeilen)
    assert e.term_flags(0) & 1 and e.term_flags(0) & 2 and not e.term_flags(0) & 8
    assert e.term_snapshot(1) == b"" and e.term_flags(1) == -1 and e.term_bell_count(1) == 0
    assert e.term_bell_count(0) >= 0
    pfad = str(tmp_path / "s.p8ks")
    assert e.state_save(pfad) and os.path.getsize(pfad) > 100
    assert e.state_load(pfad) and e.state_error() == ""
    assert not e.state_load(str(tmp_path / "gibt_es_nicht"))
    assert e.state_error()
    a = K1520Emulator()
    assert not a.state_save(pfad) and a.term_snapshot(0) == b""
    assert a.term_flags(0) == -1 and not a.state_load(pfad) and a.state_error()
