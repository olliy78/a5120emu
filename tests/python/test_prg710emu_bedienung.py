"""prg710emu — durchgängige Bedienung wie ein Anwender (AP-P9).

Alles läuft über die Wege der Oberfläche: Diskette über den Laufwerkskasten
(``DriveWidget.toggle_mount``, Dateiauswahl ersetzt), Starttaste als Qt-Tastenereignis an
das Bildschirm-Widget bzw. Klick auf ET der Bildschirmtastatur, Modellwechsel über das
Auswahlfeld der Einstellungen, Sockel über die ``QAction``s, Lochstreifen über seinen Kasten.  Gelaufen wird mit
dem Takt des Fensters (``_run_emulator``); geprüft wird der **Text** des Bildschirms
(``screen_text()``) — der ``QOpenGLWidget`` hat offscreen keinen FBO.

Es gelten ausgelieferte Disketten aus ``disks/`` (Kopie im tmp: der Emulator öffnet r/w).
"""

import shutil
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT, requires_core

pytestmark = requires_core

DISKS = PROJECT_ROOT / "disks"
UDOS_710 = "prg710_udos43_k5601_system.hfe"
UDOS_710_1 = "prg710-1_udos43_k5601_v43_189.hfe"
SCPX_710 = "prg710_scpx15_cpa640_sysprg.hfe"
SCPX_710_1 = "prg710-1_scpx17_cpa640_boot.hfe"


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


def _einlegen(w, tmp_path, monkeypatch, name, laufwerk=0):
    """Eine ausgelieferte Diskette (Kopie) über den Weg des Laufwerkskastens einlegen."""
    from PySide6.QtWidgets import QFileDialog
    kopie = tmp_path / f"l{laufwerk}_{name}"
    shutil.copy(DISKS / name, kopie)
    monkeypatch.setattr(QFileDialog, "getOpenFileName",
                        staticmethod(lambda *a, **k: (str(kopie), "")))
    assert not w.drives_widget.is_mounted(laufwerk)
    w.drives_widget.toggle_mount(laufwerk)           # derselbe Weg wie der Knopf „Mount“
    assert w.drives_widget.mounted_path(laufwerk) == str(kopie)
    return kopie


def _auswerfen(w, laufwerk=0):
    w.drives_widget.toggle_mount(laufwerk)           # belegt → „Unmount“
    assert not w.drives_widget.is_mounted(laufwerk)


def _kaltstart(w, qapp):
    """Netz aus/an, wie der Anwender nach dem Diskettenwechsel."""
    w.act_power.setChecked(False)
    w.act_power.setChecked(True)
    qapp.processEvents()


def _lauf(w, text, rahmen=1500):
    """Fensterweise rechnen (49 000 Takte je Bild), bis *text* auf dem Bildschirm steht."""
    for _ in range(rahmen):
        w._run_emulator()
        if text in w.emulator.screen_text():
            return True
    return False


def _warten(w, rahmen=30):
    for _ in range(rahmen):
        w._run_emulator()


def _taste(w, key):
    from PySide6.QtTest import QTest
    QTest.keyClick(w.screen_widget, key)
    _warten(w, 5)


def _tippen(w, text):
    """Zeichen für Zeichen als Qt-Tastenereignis an den Bildschirm (Host-Tastatur)."""
    from PySide6.QtTest import QTest
    for z in text:
        QTest.keyClicks(w.screen_widget, z)
        _warten(w, 4)                                # K7672 braucht Zeit je Zeichen


def _et_knopf(w):
    """ET1 der Bildschirmtastatur K7609 anklicken (Maus drücken/loslassen)."""
    from PySide6.QtCore import Qt
    from PySide6.QtTest import QTest
    kw = w.keyboard_widget
    kw.resize(900, 300)
    unit, ox, oy = kw._geometry()
    mitte = kw._rect_of(kw._by_pos[0x37], unit, ox, oy).center().toPoint()
    QTest.mousePress(kw, Qt.LeftButton, Qt.NoModifier, mitte)
    _warten(w, 3)
    QTest.mouseRelease(kw, Qt.LeftButton, Qt.NoModifier, mitte)


def _starten(w, tmp_path, monkeypatch, name, per_knopf=False):
    """Diskette einlegen, Netz aus/an, NKM-LOADER abwarten, Starttaste der Oberfläche."""
    from PySide6.QtCore import Qt
    _einlegen(w, tmp_path, monkeypatch, name)
    _kaltstart(w, qapp=w._qapp) if hasattr(w, "_qapp") else _kaltstart(w, _App.app)
    assert _lauf(w, "NKM-LOADER", 300), w.emulator.screen_text()
    _warten(w, 60)
    w.screen_widget.setFocus()
    if per_knopf:
        _et_knopf(w)
    else:
        _taste(w, Qt.Key_Return)                     # 710: ET1, 710-1: ENTER


class _App:
    app = None


@pytest.fixture(autouse=True)
def _qapp_merken(qapp):
    _App.app = qapp


def _udos_bis_prompt(w):
    from PySide6.QtCore import Qt
    assert _lauf(w, "Neues Datum", 3000), w.emulator.screen_text()
    _tippen(w, "031086")                             # sechs Ziffern, ohne ET
    assert _lauf(w, "%", 1500), w.emulator.screen_text()
    return w.emulator.screen_text()


def _zeile(w, befehl, erwartet):
    from PySide6.QtCore import Qt
    _tippen(w, befehl)
    _taste(w, Qt.Key_Return)
    assert _lauf(w, erwartet, 600), (befehl, w.emulator.screen_text())


# ─── Betriebssysteme: Diskette → Start → Prompt → Befehl ─────────────────────

def test_710_udos_start_ueber_et_knopf_und_cat(qapp, konfig_ordner, tmp_path, monkeypatch):
    w = _fenster(qapp)
    try:
        _starten(w, tmp_path, monkeypatch, UDOS_710, per_knopf=True)
        text = _udos_bis_prompt(w)
        assert "UDOS PRG710" in text
        _zeile(w, "CAT", "OS")                       # Verzeichnis nennt die Systemdateien
    finally:
        _zu(w, qapp)


def test_710_1_udos_start_ueber_enter_und_cat(qapp, konfig_ordner, tmp_path, monkeypatch):
    w = _fenster(qapp, "prg710")
    try:
        w.settings_widget.model_combo.setCurrentIndex(
            w.settings_widget.model_combo.findData("prg710-1"))
        assert w.emulator.prg_variant == 1
        _starten(w, tmp_path, monkeypatch, UDOS_710_1)
        text = _udos_bis_prompt(w)
        assert "UDOS PG710-1" in text
        _zeile(w, "CAT P=&", "OS")                   # am 710-1 sind alle Dateien geheim
    finally:
        _zu(w, qapp)


def test_scpx_710_1_und_710_dir(qapp, konfig_ordner, tmp_path, monkeypatch):
    w = _fenster(qapp)
    try:
        w.settings_widget.model_combo.setCurrentIndex(
            w.settings_widget.model_combo.findData("prg710-1"))
        _starten(w, tmp_path, monkeypatch, SCPX_710_1)
        assert _lauf(w, "A>", 1500), w.emulator.screen_text()
        _zeile(w, "DIR", "SYSPRG")
        # Modellwechsel in der laufenden Oberfläche: 710 mit seiner SCPX-Diskette.
        _auswerfen(w)
        w.settings_widget.model_combo.setCurrentIndex(
            w.settings_widget.model_combo.findData("prg710"))
        assert w.emulator.prg_variant == 0
        _starten(w, tmp_path, monkeypatch, SCPX_710)
        assert _lauf(w, "A>", 1500), w.emulator.screen_text()
        _zeile(w, "DIR", "SYSPRG")
    finally:
        _zu(w, qapp)


def test_modellwechsel_laufend_und_neu_booten(qapp, konfig_ordner, tmp_path, monkeypatch):
    """710 läuft in UDOS; Wechsel auf 710-1 → neue Maschine, Tastatur getauscht, bootet neu."""
    from app.ui.keyboard_k7672 import KeyboardK7672Widget
    w = _fenster(qapp)
    try:
        _starten(w, tmp_path, monkeypatch, UDOS_710)
        _udos_bis_prompt(w)
        w.settings_widget.model_combo.setCurrentIndex(
            w.settings_widget.model_combo.findData("prg710-1"))
        qapp.processEvents()
        assert w.emulator.prg_variant == 1
        assert isinstance(w.keyboard_widget, KeyboardK7672Widget)
        assert w.screen_widget.key_sink is w.keyboard_widget
        # Die 710-Diskette ist mit umgezogen; heraus damit, die des 710-1 hinein.
        _auswerfen(w)
        _starten(w, tmp_path, monkeypatch, UDOS_710_1)
        assert "UDOS PG710-1" in _udos_bis_prompt(w)
    finally:
        _zu(w, qapp)


# ─── EPROMmer: Sockel wird gemerkt, Rückfrage bei ungespeichertem Inhalt ──────

def _dialoge(monkeypatch, oeffnen=None, speichern=None, frage=None):
    """Datei- und Rückfragedialoge ersetzen; *frage* = Liste der Antworten (der Reihe nach)."""
    from PySide6.QtWidgets import QFileDialog, QMessageBox
    if oeffnen is not None:
        monkeypatch.setattr(QFileDialog, "getOpenFileName",
                            staticmethod(lambda *a, **k: (str(oeffnen), "")))
    if speichern is not None:
        monkeypatch.setattr(QFileDialog, "getSaveFileName",
                            staticmethod(lambda *a, **k: (str(speichern), "")))
    gefragt = []
    if frage is not None:
        antworten = list(frage)

        def question(parent, titel, text, *a, **k):
            gefragt.append(text)
            return antworten.pop(0)
        monkeypatch.setattr(QMessageBox, "question", staticmethod(question))
    return gefragt


def test_eprom_leeres_2716_anlegen_speichern_und_beim_start_wieder_einlegen(
        qapp, konfig_ordner, tmp_path, monkeypatch):
    ziel = tmp_path / "neu.bin"
    _dialoge(monkeypatch, speichern=ziel)
    w = _fenster(qapp)
    try:
        u2716 = [a for a in w.act_eprom_leer.menu().actions() if "U2716" in a.text()][0]
        u2716.trigger()
        assert w.emulator.eprom_type() == 2 and not w.emulator.eprom_path()
        w.act_eprom_speichern.trigger()
        assert ziel.read_bytes() == b"\xff" * 2048
    finally:
        _zu(w, qapp)
    w = _fenster(qapp)                                # neuer Start: Sockel kommt wieder
    try:
        assert w.emulator.eprom_type() == 2 and w.emulator.eprom_path() == str(ziel)
        assert str(ziel) in w.eprom_widget.sockel.text()
        w.act_eprom_entnehmen.trigger()
    finally:
        _zu(w, qapp)
    w = _fenster(qapp)                                # entnommen = bleibt leer
    try:
        assert w.emulator.eprom_type() == 0
    finally:
        _zu(w, qapp)


def test_eprom_fehlende_datei_gibt_leeren_sockel_und_protokollzeile(
        qapp, konfig_ordner, tmp_path):
    (konfig_ordner / "prg710emu.yaml").write_text(
        f"version: 1\neprom: {{path: {tmp_path / 'weg.bin'}, type: 2}}\n", encoding="utf-8")
    w = _fenster(qapp)                                # kein Meldungsfenster (Sperre in conftest)
    try:
        assert w.emulator.eprom_type() == 0
        assert "weg.bin" in w.eprom_widget.protokoll_text()
        assert "leer" in w.eprom_widget.sockel.text()
    finally:
        _zu(w, qapp)


def test_eprom_rueckfrage_beim_beenden(qapp, konfig_ordner, tmp_path, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    quelle = tmp_path / "quelle.bin"
    quelle.write_bytes(bytes(range(256)) * 8)
    _dialoge(monkeypatch, oeffnen=quelle)
    w = _fenster(qapp)
    try:
        w.act_eprom_einlegen.trigger()
        assert not w.emulator.eprom_modified()
        w.act_eprom_loeschen.trigger()                # UV-löschen = Inhalt geändert
        assert w.emulator.eprom_modified()
        # Abbrechen: das Fenster bleibt offen.
        gefragt = _dialoge(monkeypatch, frage=[QMessageBox.Cancel])
        assert w.close() is False and w.isVisible() and len(gefragt) == 1
        # Speicherauswahl abgebrochen = auch nicht beenden.
        _dialoge(monkeypatch, speichern="", frage=[QMessageBox.Save])
        assert w.close() is False and w.isVisible()
        # Speichern: Datei geschrieben, Fenster zu.
        ziel = tmp_path / "gebrannt.bin"
        _dialoge(monkeypatch, speichern=ziel, frage=[QMessageBox.Save])
        assert w.close() is True
        assert ziel.read_bytes() == b"\xff" * 2048
    finally:
        _zu(w, qapp)
    # Verwerfen: schließt ohne Datei zu schreiben; beim nächsten Start der Stand der Datei.
    w = _fenster(qapp)
    try:
        assert w.emulator.eprom_path() == str(tmp_path / "gebrannt.bin")
        w.emulator.eprom_insert_data(b"\x01\x02", 2, w.emulator.eprom_path(), True)
        gefragt = _dialoge(monkeypatch, frage=[QMessageBox.Discard])
        assert w.close() is True and len(gefragt) == 1
    finally:
        _zu(w, qapp)
    # Ohne Änderung fragt niemand (die Frage-Sperre der conftest würde sonst anschlagen).
    w = _fenster(qapp)
    try:
        assert w.close() is True
    finally:
        _zu(w, qapp)


def test_eprom_rueckfrage_beim_modellwechsel(qapp, konfig_ordner, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    w = _fenster(qapp)
    box = w.settings_widget.model_combo
    try:
        w.emulator.eprom_insert_data(b"\x01\x02\x03", 1, "", True)   # ungespeichert, ohne Datei
        # Abbrechen: Modell und Auswahlfeld bleiben, das PROM auch.
        _dialoge(monkeypatch, frage=[QMessageBox.Cancel])
        box.setCurrentIndex(box.findData("prg710-1"))
        assert w.emulator.prg_variant == 0 and box.currentData() == "prg710"
        assert w.emulator.eprom_type() == 1 and w.emulator.eprom_modified()
        # Verwerfen: neue Maschine, Sockel leer.
        _dialoge(monkeypatch, frage=[QMessageBox.Discard])
        box.setCurrentIndex(box.findData("prg710-1"))
        assert w.emulator.prg_variant == 1 and w.emulator.eprom_type() == 0
        # Ein gespeichertes (unverändertes) PROM wandert ohne Frage mit.
        w.emulator.eprom_insert_data(b"\x01\x02\x03", 1, "", False)
        box.setCurrentIndex(box.findData("prg710"))
        assert w.emulator.prg_variant == 0 and w.emulator.eprom_type() == 1
    finally:
        _zu(w, qapp)


# ─── Lochstreifen über den Kasten (Entwurf 23 AP-L4) ─────────────────────────

def test_lochstreifen_einlegen_und_entnehmen_ueber_den_kasten(qapp, konfig_ordner, tmp_path,
                                                              monkeypatch):
    from app.core_binding.k1520 import PTAPE_FMT_RAW
    from app.ui import lochstreifen_format_dialog
    band = tmp_path / "band.ptp"
    band.write_bytes(b"HALLO\r\n")
    _dialoge(monkeypatch, oeffnen=band)
    monkeypatch.setattr(lochstreifen_format_dialog, "frage", lambda *a, **k: PTAPE_FMT_RAW)
    w = _fenster(qapp)
    try:
        w.settings_widget.ptape_box.setChecked(True)          # Karte stecken
        lw = w.lochstreifen_widget
        lw.leser_oeffnen_knopf.click()
        assert w.emulator.ptape_reader_status()["inserted"]
        lw.leser_entnehmen_knopf.click()
        assert not w.emulator.ptape_reader_status()["inserted"]
        # Modellwechsel: auch das 710-1 hat den Leser (K6022, Plan §3.8).
        w.settings_widget.model_combo.setCurrentIndex(
            w.settings_widget.model_combo.findData("prg710-1"))
        lw.leser_oeffnen_knopf.click()
        assert w.emulator.ptape_reader_status()["inserted"]
    finally:
        _zu(w, qapp)


# ─── Rückstellen, Netzschalter, Standardansicht ──────────────────────────────

def test_rueckstellen_und_netzschalter_starten_neu_und_standard_laesst_das_prom(
        qapp, konfig_ordner, tmp_path, monkeypatch):
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import QMessageBox
    w = _fenster(qapp)
    try:
        _starten(w, tmp_path, monkeypatch, UDOS_710)
        _udos_bis_prompt(w)
        w.emulator.eprom_insert_data(b"\x01", 1, "", False)
        # Rückstellen (Strg+Umschalt+R): zurück ins Boot-ROM, mit der Diskette bootet es wieder.
        w.act_reset.trigger()
        assert _lauf(w, "NKM-LOADER", 300), w.emulator.screen_text()
        _warten(w, 60)
        _taste(w, Qt.Key_Return)
        # Das Datum steht noch im RAM (Rückstellen löscht es nicht): UDOS fragt je nach
        # Stand nach „Neues Datum“ oder geht gleich zum Prompt.
        for _ in range(3000):
            w._run_emulator()
            if "Neues Datum" in w.emulator.screen_text():
                _tippen(w, "031086")
            if w.emulator.screen_text().rstrip("\x00 \n").endswith("%"):
                break
        assert "UDOS PRG710" in w.emulator.screen_text()
        # Netz aus: Bild dunkel, Lauf steht; Netz an: Kaltstart.
        w.act_power.setChecked(False)
        assert not w.run_timer.isActive()
        w.act_power.setChecked(True)
        assert _lauf(w, "NKM-LOADER", 300)
        # Ansicht ▸ Standard zurücksetzen (nach Rückfrage): Diskette und PROM bleiben.
        monkeypatch.setattr(QMessageBox, "question",
                            staticmethod(lambda *a, **k: QMessageBox.Yes))
        w._standard_zuruecksetzen()
        assert w.drives_widget.is_mounted(0) and w.emulator.eprom_type() == 1
    finally:
        _zu(w, qapp)
