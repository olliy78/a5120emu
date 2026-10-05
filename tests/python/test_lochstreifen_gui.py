"""Lochstreifen in der Oberfläche (doc/design/23_lochstreifen.md §7, AP-L4).

Das Kästchen *Einstellungen ▸ Allgemein ▸ Lochstreifen (SIF1000, K6022)* gibt es in
allen drei Programmen (Vorgabe aus); es steckt die K6022 über einen Neuaufbau der
Maschine.  Der Kasten „Lochstreifen" erscheint nur mit gesteckter Karte.  Datei- und
Formatdialog werden ersetzt (``lochstreifen_format_dialog.frage``) — ein ``exec()``
stünde headless bis zum Zeitüberlauf.  Der Fernschreiber bleibt im Reiter
„Schnittstellen“.
"""

from pathlib import Path

import pytest
import yaml

from conftest import requires_core

pytestmark = requires_core

MASCHINEN = ["a5120", "k8915", "prg710"]


@pytest.fixture
def konfig_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


@pytest.fixture
def formatwahl(monkeypatch):
    """Formatdialog ersetzen; merkt, wer mit welcher Vorauswahl gefragt wurde."""
    from app.ui import lochstreifen_format_dialog as fd
    gefragt = []
    antwort = {"fmt": None}             # None = die Vorauswahl übernehmen

    def frage(parent, pfad, stanzer):
        vorschlag = fd.vorauswahl(pfad)
        gefragt.append((pfad, stanzer, vorschlag))
        return vorschlag if antwort["fmt"] is None else antwort["fmt"]
    monkeypatch.setattr(fd, "frage", frage)
    return gefragt, antwort


def _fenster(qapp, maschine="prg710"):
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(None, profil=profil.profil(maschine))
    w.show()
    qapp.processEvents()
    return w


def _zu(w, qapp):
    w.close()
    qapp.processEvents()


def _menue(w, titel):
    """Die Einträge des Menüs *titel* (als Liste — die Hülle des Menüs lebt nicht lange)."""
    for a in w.menuBar().actions():
        if a.text() == titel:
            return list(a.menu().actions())
    raise AssertionError(f"kein Menü {titel}")


def _stecken(w, qapp, an=True):
    w.settings_widget.ptape_box.setChecked(an)
    qapp.processEvents()


def _datei_dialoge(monkeypatch, oeffnen=None, speichern=None):
    from PySide6.QtWidgets import QFileDialog
    if oeffnen is not None:
        monkeypatch.setattr(QFileDialog, "getOpenFileName",
                            staticmethod(lambda *a, **k: (str(oeffnen), "")))
    if speichern is not None:
        monkeypatch.setattr(QFileDialog, "getSaveFileName",
                            staticmethod(lambda *a, **k: (str(speichern), "")))


@pytest.mark.parametrize("maschine", MASCHINEN)
def test_kaestchen_in_jedem_programm_vorgabe_aus_und_dock_folgt(qapp, konfig_ordner,
                                                               maschine):
    w = _fenster(qapp, maschine)
    try:
        box = w.settings_widget.ptape_box
        assert box.text() == "Lochstreifen (SIF1000, K6022)"
        assert not box.isChecked() and not w.emulator.ptape_installed()
        assert not w.lochstreifen_dock.isVisible()
        assert not w.act_dock_lochstreifen.isVisible()
        assert w.act_dock_lochstreifen in _menue(w, "&Ansicht")

        alt = w.emulator
        _stecken(w, qapp)
        assert w.emulator is not alt and w.emulator.ptape_installed()
        assert w.lochstreifen_dock.isVisible() and w.act_dock_lochstreifen.isVisible()
        titel = [g.title() for g in w.lochstreifen_widget.findChildren(
            __import__("PySide6.QtWidgets", fromlist=["QGroupBox"]).QGroupBox)]
        assert titel == ["Lochstreifenleser", "Lochstreifenstanzer"]
        w._autosave_now()
        daten = yaml.safe_load(Path(w._konfig_pfad()).read_text(encoding="utf-8"))
        assert daten["general"]["ptape"] is True

        _stecken(w, qapp, False)
        assert not w.emulator.ptape_installed()
        assert not w.lochstreifen_dock.isVisible()
        assert not w.act_dock_lochstreifen.isVisible()
    finally:
        _zu(w, qapp)


@pytest.mark.parametrize("maschine", MASCHINEN)
def test_keine_alten_lochband_aktionen_und_keine_kuerzel(qapp, konfig_ordner, maschine):
    from PySide6.QtGui import QAction
    w = _fenster(qapp, maschine)
    try:
        for name in ("band_einlegen", "band_entnehmen", "stanzband_speichern",
                     "stanzband_leeren", "stanzer_ein"):
            assert not hasattr(w, f"act_{name}"), name
        from PySide6.QtWidgets import QMenu
        titel = [m.title() for m in w.findChildren(QMenu)]
        assert "&Lochband" not in titel, titel
        _stecken(w, qapp)
        for a in w.lochstreifen_widget.findChildren(QAction):
            assert a.shortcut().isEmpty(), a.text()
        assert w.act_dock_lochstreifen.shortcut().isEmpty()
    finally:
        _zu(w, qapp)


def test_leser_oeffnen_mit_formatdialog_und_entnehmen(qapp, konfig_ordner, tmp_path,
                                                     monkeypatch, formatwahl):
    from app.core_binding.k1520 import PTAPE_FMT_IHEX
    gefragt, _ = formatwahl
    band = tmp_path / "band.hex"
    band.write_text(":0300000041424337\n:00000001FF\n", encoding="ascii")
    _datei_dialoge(monkeypatch, oeffnen=band)
    w = _fenster(qapp, "a5120")
    try:
        _stecken(w, qapp)
        lw = w.lochstreifen_widget
        lw.leser_oeffnen_knopf.click()
        assert gefragt == [(str(band), False, PTAPE_FMT_IHEX)]
        st = w.emulator.ptape_reader_status()
        assert st["inserted"] and st["format"] == PTAPE_FMT_IHEX and st["file"] == str(band)
        assert lw.leser_datei.text() == str(band)
        assert lw.leser_format.text() == "Intel HEX"
        assert "von" in lw.leser_stand.text() and lw.leser_entnehmen_knopf.isEnabled()
        lw.leser_entnehmen_knopf.click()
        assert not w.emulator.ptape_reader_status()["inserted"]
        assert not lw.leser_entnehmen_knopf.isEnabled()
    finally:
        _zu(w, qapp)


def test_abgebrochener_formatdialog_legt_nichts_ein(qapp, konfig_ordner, tmp_path,
                                                    monkeypatch):
    from app.ui import lochstreifen_format_dialog as fd
    band = tmp_path / "band.ptp"
    band.write_bytes(b"X")
    _datei_dialoge(monkeypatch, oeffnen=band)
    monkeypatch.setattr(fd, "frage", lambda *a, **k: None)
    w = _fenster(qapp)
    try:
        _stecken(w, qapp)
        w.lochstreifen_widget.leser_oeffnen_knopf.click()
        assert not w.emulator.ptape_reader_status()["inserted"]
    finally:
        _zu(w, qapp)


def test_unlesbares_band_meldet_sich(qapp, konfig_ordner, tmp_path, monkeypatch, formatwahl):
    from PySide6.QtWidgets import QMessageBox
    from app.core_binding.k1520 import PTAPE_FMT_IHEX
    _, antwort = formatwahl
    antwort["fmt"] = PTAPE_FMT_IHEX                  # Rohdaten als Intel HEX → Fehler
    band = tmp_path / "kaputt.hex"
    band.write_bytes(b"kein hex\n")
    _datei_dialoge(monkeypatch, oeffnen=band)
    gemeldet = []
    monkeypatch.setattr(QMessageBox, "warning",
                        staticmethod(lambda *a, **k: gemeldet.append(a[2])))
    w = _fenster(qapp)
    try:
        _stecken(w, qapp)
        w.lochstreifen_widget.leser_oeffnen_knopf.click()
        assert gemeldet and "kaputt.hex" in gemeldet[0]
        assert not w.emulator.ptape_reader_status()["inserted"]
    finally:
        _zu(w, qapp)


def test_stanzer_binden_neues_band_loesen_und_schalten(qapp, konfig_ordner, tmp_path,
                                                      monkeypatch, formatwahl):
    from PySide6.QtWidgets import QMessageBox
    from app.core_binding.k1520 import PTAPE_FMT_ASCII, PTAPE_FMT_RAW
    gefragt, _ = formatwahl
    ziel = tmp_path / "stanz.tape"                    # neu: Vorauswahl nach der Endung
    _datei_dialoge(monkeypatch, speichern=ziel)
    w = _fenster(qapp, "k8915")
    try:
        _stecken(w, qapp)
        lw = w.lochstreifen_widget
        assert not lw.stanzer_loesen_knopf.isEnabled()
        lw.stanzer_oeffnen_knopf.click()
        assert gefragt == [(str(ziel), True, PTAPE_FMT_ASCII)]
        ps = w.emulator.ptape_punch_status()
        assert ps["file"] == str(ziel) and ps["format"] == PTAPE_FMT_ASCII
        assert ziel.exists()                          # Binden legt die Datei an
        assert lw.stanzer_format.text() == "ASCII-Art" and lw.stanzer_loesen_knopf.isEnabled()

        # Vorhandene Datei: Vorauswahl aus dem INHALT, nicht der Endung.
        roh = tmp_path / "alt.txt"
        roh.write_bytes(b"\x00\x01\x02\xff")
        _datei_dialoge(monkeypatch, speichern=roh)
        lw.stanzer_oeffnen_knopf.click()
        assert gefragt[-1] == (str(roh), True, PTAPE_FMT_RAW)
        assert w.emulator.ptape_punch_status()["len"] == 4
        assert "4 Byte" in lw.stanzer_stand.text()

        # Neues Band fragt bei Inhalt nach und leert die Datei.
        gefragt_box = []
        monkeypatch.setattr(QMessageBox, "question", staticmethod(
            lambda *a, **k: gefragt_box.append(a[2]) or QMessageBox.Yes))
        lw.neues_band_knopf.click()
        assert gefragt_box and w.emulator.ptape_punch_status()["len"] == 0
        assert roh.read_bytes() == b""

        lw.stanzer_ein.setChecked(False)
        assert w.emulator.ptape_punch_enabled() is False
        lw.stanzer_ein.setChecked(True)
        assert w.emulator.ptape_punch_enabled() is True

        lw.stanzer_loesen_knopf.click()
        assert w.emulator.ptape_punch_status()["file"] == ""
        assert not lw.stanzer_loesen_knopf.isEnabled()
    finally:
        _zu(w, qapp)


def test_wiederherstellung_aus_der_konfiguration(qapp, konfig_ordner, tmp_path, monkeypatch,
                                                 formatwahl):
    from app.core_binding.k1520 import PTAPE_FMT_ASCII, PTAPE_FMT_RAW
    band = tmp_path / "leser.ptp"
    band.write_bytes(b"ABC")
    ziel = tmp_path / "stanz.txt"
    _datei_dialoge(monkeypatch, oeffnen=band, speichern=ziel)
    w = _fenster(qapp)
    try:
        _stecken(w, qapp)
        w.lochstreifen_widget.leser_oeffnen_knopf.click()
        w.lochstreifen_widget.stanzer_oeffnen_knopf.click()
    finally:
        _zu(w, qapp)                                   # schreibt die Konfiguration
    daten = yaml.safe_load(Path(w._konfig_pfad()).read_text(encoding="utf-8"))
    assert daten["general"]["ptape"] is True
    assert daten["lochstreifen"]["leser"] == {"datei": str(band), "format": PTAPE_FMT_RAW}
    assert daten["lochstreifen"]["stanzer"] == {"datei": str(ziel), "format": PTAPE_FMT_ASCII}

    w = _fenster(qapp)
    try:
        assert w.emulator.ptape_installed() and w.settings_widget.ptape_box.isChecked()
        assert w.lochstreifen_dock.isVisible()
        st = w.emulator.ptape_reader_status()
        assert st["inserted"] and st["file"] == str(band) and st["len"] >= 3
        ps = w.emulator.ptape_punch_status()
        assert ps["file"] == str(ziel) and ps["format"] == PTAPE_FMT_ASCII
        # Ausstecken und wieder stecken: die Dateien sind nicht vergessen.
        _stecken(w, qapp, False)
        _stecken(w, qapp, True)
        assert w.emulator.ptape_reader_status()["file"] == str(band)
        assert w.emulator.ptape_punch_status()["file"] == str(ziel)
    finally:
        _zu(w, qapp)

    # Fehlende Dateien beim Start: Hinweis in der Statuszeile, kein Meldungsfenster.
    band.unlink()
    ziel.unlink()
    w = _fenster(qapp)
    try:
        assert w.emulator.ptape_installed()
        assert not w.emulator.ptape_reader_status()["inserted"]
        assert w.emulator.ptape_punch_status()["file"] == ""
        meldung = w.statusBar().currentMessage()
        assert "leser.ptp" in meldung and "stanz.txt" in meldung, meldung
    finally:
        _zu(w, qapp)


@pytest.mark.parametrize("maschine", MASCHINEN)
def test_auslieferung_hat_die_karte_aus(qapp, konfig_ordner, maschine):
    from app import config_io, profil
    vorgabe = config_io.standard_konfiguration(profil.profil(maschine))
    assert vorgabe["general"]["ptape"] is False
    assert "lochstreifen" not in vorgabe


def test_fernschreiber_steht_im_schnittstellenreiter(qapp, konfig_ordner):
    w = _fenster(qapp)
    try:
        bloecke = w.serial_widget.bloecke()
        namen = [w.emulator.serial_info(i).name for i in range(len(bloecke))]
        assert namen[-1] == "Fernschreiber", namen
    finally:
        _zu(w, qapp)
