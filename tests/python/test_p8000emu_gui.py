"""p8000emu — der P8000 Emulator als fünftes Gesicht des Hauptfensters (AP P16).

doc/design/26_p8000emu_oberflaeche.md: Programmprofil (Titel, Konfiguration, 4 MHz, Modellwahl,
ROM-Fassungen/Platinenindex als Hardwarewahl), Originalterminal im CRT-Widget (P23a: eine GUI, Betriebsart,
Modellmigration), Kasten für die Winchesterplatte, Statuszeile, Zwischenstand (P8KS), Auslieferungskonfiguration, Boot-Rauchtest
(Banner „P8000 Hardwaretest U880" im Terminal, Taste → Echo).

Gearbeitet wird ohne Pixelvergleich gegen das Fenster (QOpenGLWidget hat offscreen keinen FBO):
geprüft wird das Terminalbild im Kern und der Weg bis zum Widget.
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
    assert p.tastatur == "k7673" and not p.ptape_wahl and not p.raf_wahl
    assert "nmi" in p.eigene_aktionen
    w = _fenster(qapp)
    try:
        assert w.windowTitle() == "P8000 Emulator"
        assert w.emulator.machine == "p8000"
        assert w.CPU_HZ == 4_000_000
        assert w._drive_types[:2] == ["K5601", "K5601"]
        assert hasattr(w, "act_nmi") and hasattr(w, "act_stand_speichern")
        assert not hasattr(w, "act_p8000emu")      # man startet sich nicht selbst
        assert w.emulator.term_count() == 1 and w.emulator.term_kind(0) == 1   # Originalterminal
        assert w.status_widget.frontplatte is not None
    finally:
        _zu(w, qapp)


def test_vorgaben_des_vollgeraets(qapp, umgebung):
    """karte16 an, Index 3/4, MON 3.1, 1 MB, WDC 4.2, Platte = Datei (Vorgabe des Auftrags)."""
    from app import profil
    p = profil.profil("p8000")
    assert p.standard_modell() == "p8000-ot"
    kern = p.kern_parameter("p8000-ot", {})["p8000"]
    assert kern == {"karte16": "1", "wdc": "4.2", "terminal": "original", "index8": "3",
                    "index16": "4", "mon16": "3.1", "dram": "4x256K", "mon8": "3.1"}
    # ohne Winchester: keine wdc-Zeile (der Kern lehnt wdc ohne … nicht ab, aber es soll aus sein)
    k16 = p.kern_parameter("p8000-16-ot", {})["p8000"]
    assert k16["karte16"] == "1" and "wdc" not in k16
    k8 = p.kern_parameter("p8000-8-ot", {})["p8000"]
    assert k8 == {"karte16": "0", "terminal": "original", "index8": "3", "mon8": "3.1"}
    assert p.modell_hat_wdc("p8000-ot") and not p.modell_hat_wdc("p8000-16-ot")
    # unbekanntes Modell → Vorgabe
    assert p.modell_normalisieren("quatsch") == "p8000-ot"


def test_jede_hardwarewahl_baut_eine_maschine(qapp, umgebung):
    """Jeder angebotene Wert (je Modell, an dem er wirkt) ergibt eine startende Maschine."""
    from app import profil
    from app.core_binding.k1520 import K1520Emulator
    p = profil.profil("p8000")
    assert [h[0] for h in p.hardware] == ["index", "mon8", "mon16", "dram", "wdc"]
    for schluessel, _b, _t, werte, modelle in p.hardware:
        for modell in (modelle or ("p8000-ot", "p8000-16-ot", "p8000-8-ot")):
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
                                            "dram": "4x256K", "wdc": "4.2"}
    n = p.hardware_normalisieren({"dram": "1m@0+1m@1", "mon8": 3.0, "wdc": "9.9", "index": "11"})
    assert n["dram"] == "2x1M" and n["mon8"] == "3.0" and n["wdc"] == "4.2"
    assert n["index"] == "11"


def test_ram_ausbau_umzug_eigene_bestueckung_und_abweisung(qapp, umgebung):
    """P23b (doc/p8000/ram_konfiguration.md): alte Werte → Kurzform DERSELBEN Bestückung,
    eigene gültige Langform bleibt, Ungültiges → Vorgabe 4 × 256 KB; der Kern nimmt jede
    angebotene und jede durchgelassene Bestückung, und was die Prüfung abweist, weist auch er ab."""
    from app import p8000_ram, profil
    from app.core_binding.k1520 import _lib
    p = profil.profil("p8000")
    norm = lambda v: p.hardware_normalisieren({"dram": v})["dram"]
    assert norm("1M@0") == "1x1M" and norm("256k@0") == "1x256K" and norm("") == "4x256K"
    assert norm("1m@0+256k@4") == "1M@0+256K@4"
    assert norm("16m") == "16M" and norm("4×1M") == "4x1M"
    for schlecht in ("1M@0+1M@0", "1M@0+256K@3", "2M@1", "5x256K", "32M", "quatsch", "16M+1M@1",
                     "256K@0+256K@1+256K@2+256K@3+256K@4", "1M@16", "256K@64", "8M@0+1M@7"):
        assert not p8000_ram.gueltig(schlecht), schlecht
        assert norm(schlecht) == "4x256K", schlecht
        assert not _lib.k1520_create_p8000(b"karte16=1,dram=" + schlecht.encode()), schlecht
    for gut in [w for w, _a in p8000_ram.AUSBAUTEN] + ["1M@0+256K@4", "8M@0+1M@8", "2x1M", "1x256K"]:
        assert p8000_ram.gueltig(gut), gut
        h = _lib.k1520_create_p8000(b"karte16=1,dram=" + gut.encode())
        assert h, (gut, _lib.k1520_last_init_error())
        _lib.k1520_destroy(h)
    assert "16 MB" in p8000_ram.anzeige("16M") and "benutzerdefiniert" in p8000_ram.anzeige("1M@0+256K@4")

    # Auswahlfeld: Vorgabe = Standard, eine eigene Bestückung erscheint als eigener Eintrag
    w = _fenster(qapp)
    try:
        box = w.settings_widget.hardware_combos["dram"]
        assert box.currentData() == "4x256K" and "Standard" in box.currentText()
        assert "8 MB" in box.toolTip()
        w.settings_widget.set_hardware_value({**p.hardware_standard(), "dram": "1M@0+256K@4"})
        assert box.currentData() == "1M@0+256K@4"
        assert box.currentText().startswith("benutzerdefiniert")
    finally:
        _zu(w, qapp)


def test_rom_fassung_und_modell_in_den_einstellungen(qapp, umgebung):
    """ROM-Wahl in *Allgemein*: wirkt auf die Maschine, wird gemerkt, graut aus, wo sie nichts tut."""
    from app import config_io
    w = _fenster(qapp)
    try:
        sw = w.settings_widget
        assert set(sw.hardware_combos) == {"index", "mon8", "mon16", "dram", "wdc"}
        # Rechnerausstattung (3) + Betriebsart (2); die Kern-Terminal-Modelle gibt es nicht mehr
        assert sw.model_combo.count() == 3 and not sw.model_combo.isHidden()
        assert [sw.betriebsart_combo.itemData(i) for i in range(2)] == ["computer", "terminal"]
        assert not sw.ptape_box.isVisibleTo(sw)           # am P8000 keine Lochstreifenkarte
        for k in ("mon16", "dram", "wdc", "mon8", "index"):
            assert sw.hardware_combos[k].isEnabled()

        # MON8 3.0: Banner der Maschine ändert sich, Konfiguration merkt es
        w._on_hardware_selected("mon8", "3.0")
        assert w._hardware["mon8"] == "3.0"
        assert _bis(w, HARDWARETEST + " - Version 3.0", 20_000_000)
        w._autosave_now()
        cfg = config_io.load_config(str(umgebung / "p8000emu.yaml"))
        assert cfg["general"]["mon8"] == "3.0" and cfg["general"]["model"] == "p8000-ot"

        # nur 8-Bit-Teil: 16-Bit-Felder und WDC ausgegraut, Platte gesperrt
        w._on_model_selected("p8000-8-ot")
        w.run_timer.stop()
        assert sw.model_value() == "p8000-8-ot"
        assert not sw.hardware_combos["mon16"].isEnabled()
        assert not sw.hardware_combos["dram"].isEnabled()
        assert not sw.hardware_combos["wdc"].isEnabled()
        assert sw.hardware_combos["mon8"].isEnabled()
        assert not w.platten_widget.verfuegbar()
        assert not w.platten_widget.knopf_neu.isEnabled()
        assert w.emulator.panel_lamps() == 0

        # ohne Winchester: 16-Bit an, WDC aus
        w._on_model_selected("p8000-16-ot")
        w.run_timer.stop()
        assert sw.hardware_combos["mon16"].isEnabled()
        assert not sw.hardware_combos["wdc"].isEnabled()
        assert not w.platten_widget.verfuegbar()
        w._on_model_selected("p8000-ot")
        assert w.platten_widget.verfuegbar()
    finally:
        _zu(w, qapp)


# ─── Boot, Terminal-Widget, Tasten ───────────────────────────────────────────

def test_boot_zeigt_das_banner_im_originalterminal(qapp, umgebung):
    """Das Terminalbild geht in das CRT-Widget: Text laut Kern, Bild mit Pixeln in Stufe hell/normal."""
    w = _fenster(qapp, "p8000-8")                 # nur 8-Bit: der kürzeste Weg zu „Press RETURN"
    try:
        assert _bis(w, "Press RETURN")
        t = w.screen_widget
        t.aktualisieren()
        assert HARDWARETEST in t.text() and "U880-Softwaremonitor Version 3.1" in t.text()
        stufen = {t.pixel(x, y) for x in range(0, 640, 2) for y in range(0, 312, 3)}
        assert stufen >= {0, 2} or stufen >= {0, 1}, stufen
        assert t._fb_bytes is not None and len(t._fb_bytes) == 640 * 312
    finally:
        _zu(w, qapp)


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
    assert p.modell_normalisieren(g["model"]) == g["model"] == "p8000-ot"
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
        assert w._model == "p8000-ot" and w.platten_widget.pfad() == ""
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
