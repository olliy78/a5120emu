"""prg710emu: Lochband und Fernschreiber in der Oberfläche (doc/design/20_prg710.md AP-P8d).

*Maschine ▸ Lochband* gibt es nur im PRG-Profil (``actions.NUR_FUER``), ohne Kürzel;
Einlegen/Speichern gehen über die Dateiauswahl (hier ersetzt) an die C-ABI
``k1520_ptape_*``.  Der Fernschreiber erscheint ohne eigenen Code im Reiter
„Schnittstellen“ (Namen aus dem Kern).
"""

from pathlib import Path

import pytest

from conftest import requires_core

pytestmark = requires_core


@pytest.fixture
def konfig_ordner(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    from app import config_io
    ordner = Path(config_io.default_config_dir())
    ordner.mkdir(parents=True, exist_ok=True)
    return ordner


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


def _lochband_menue(w):
    for aktion in w.menuBar().actions():
        if aktion.text() == "&Maschine":
            for a in aktion.menu().actions():
                if a.menu() is not None and a.text() == "&Lochband":
                    return a.menu()
    return None


@pytest.mark.parametrize("maschine", ["a5120", "k8915"])
def test_lochband_gibt_es_nur_am_prg(qapp, konfig_ordner, maschine):
    w = _fenster(qapp, maschine)
    try:
        assert not hasattr(w, "act_band_einlegen")
        assert _lochband_menue(w) is None
    finally:
        _zu(w, qapp)


def test_menue_ohne_kuerzel_und_vollstaendig(qapp, konfig_ordner):
    w = _fenster(qapp)
    try:
        menue = _lochband_menue(w)
        assert menue is not None
        namen = [a.text() for a in menue.actions() if not a.isSeparator()]
        assert len(namen) == 5, namen
        for a in menue.actions():
            assert a.shortcut().isEmpty(), a.text()
        assert w.act_stanzer_ein.isCheckable() and w.act_stanzer_ein.isChecked()
    finally:
        _zu(w, qapp)


def test_band_einlegen_entnehmen_und_stanzband_speichern(qapp, konfig_ordner, tmp_path,
                                                         monkeypatch):
    from PySide6.QtWidgets import QFileDialog

    band = tmp_path / "band.ptp"
    band.write_bytes(b"ZEILE\r\n")
    ziel = tmp_path / "gestanzt.ptp"
    monkeypatch.setattr(QFileDialog, "getOpenFileName", lambda *a, **k: (str(band), ""))
    monkeypatch.setattr(QFileDialog, "getSaveFileName", lambda *a, **k: (str(ziel), ""))
    w = _fenster(qapp)
    try:
        w.act_band_einlegen.trigger()
        st = w.emulator.ptape_reader_status()
        assert st["inserted"] and st["len"] == 7
        w._lochband_anzeigen()
        assert "0 von 7 Byte" in w.act_band_entnehmen.text()
        assert w.act_band_entnehmen.isEnabled()
        w.act_band_entnehmen.trigger()
        assert not w.emulator.ptape_reader_status()["inserted"]
        w._lochband_anzeigen()
        assert not w.act_band_entnehmen.isEnabled()

        w.act_stanzband_speichern.trigger()
        assert ziel.exists() and ziel.read_bytes() == b""
        w.act_stanzer_ein.trigger()                      # abhaken = aus
        assert w.emulator.ptape_punch_enabled() is False
        w.act_stanzer_ein.trigger()
        assert w.emulator.ptape_punch_enabled() is True
        w.act_stanzband_leeren.trigger()
        assert w.emulator.ptape_punch_length() == 0
    finally:
        _zu(w, qapp)


def test_unlesbares_band_meldet_sich(qapp, konfig_ordner, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QFileDialog, QMessageBox

    gemeldet = []
    monkeypatch.setattr(QFileDialog, "getOpenFileName",
                        lambda *a, **k: (str(tmp_path / "fehlt.ptp"), ""))
    monkeypatch.setattr(QMessageBox, "warning", lambda *a, **k: gemeldet.append(a[2]))
    w = _fenster(qapp)
    try:
        w.act_band_einlegen.trigger()
        assert gemeldet and "fehlt.ptp" in gemeldet[0]
        assert not w.emulator.ptape_reader_status()["inserted"]
    finally:
        _zu(w, qapp)


def test_fernschreiber_steht_im_schnittstellenreiter(qapp, konfig_ordner):
    w = _fenster(qapp)
    try:
        bloecke = w.serial_widget.bloecke()
        namen = [w.emulator.serial_info(i).name for i in range(len(bloecke))]
        assert namen[-1] == "Fernschreiber", namen
    finally:
        _zu(w, qapp)
