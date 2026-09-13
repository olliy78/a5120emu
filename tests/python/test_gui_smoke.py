"""GUI-Rauchtest (PySide6, headless über ``QT_QPA_PLATFORM=offscreen``).

Was hier geprüft wird, ist die **Verdrahtung**: Baut sich das Fenster mit allen
Panels auf, hängt ein Emulator daran, greifen Konfigurations- und
Laufwerksfunktionen ineinander, lässt sich alles wieder schließen.

Was hier NICHT geprüft wird: gerenderte Pixel.  Der Bildschirm ist ein
``QOpenGLWidget``; offscreen gibt es keinen Framebuffer-Objekt-Kontext (Qt meldet
„QOpenGLWidget: No fbo, cannot render").  Bildinhalte prüft die C++-Seite über
das VRAM bzw. ``tools/fb_ocr.py``.
"""

import pytest

from conftest import requires_core

pytestmark = requires_core

pytest.importorskip("PySide6", reason="PySide6 nicht installiert")


@pytest.fixture
def window(qapp):
    """Aufgebautes Hauptfenster; wird am Testende geschlossen.

    **Ohne gemerkte Konfiguration.** Das Fenster schreibt seine Einrichtung
    fortlaufend nach ``$XDG_CONFIG_HOME`` (conftest zeigt das auf ein
    Testverzeichnis) — Laufwerksbestückung, eingelegte Disketten, Inhalt der
    Symbolleiste.  Damit erbte jeder Testlauf den Stand des vorigen: ein Test,
    der die Bestückung ändert, liess den ersten Test des NÄCHSTEN Laufs
    scheitern, und ein abgebrochener Lauf hinterliess eine Leiste, die niemand
    zusammengestellt hatte.  Jeder Test beginnt deshalb im Werkszustand.
    """
    from pathlib import Path

    from app import config_io
    from app.ui.main_window import MainWindow

    gemerkt = Path(config_io.default_config_path())
    if gemerkt.exists():
        gemerkt.unlink()

    w = MainWindow()
    w.show()
    qapp.processEvents()
    yield w
    w.close()
    qapp.processEvents()


def test_main_window_builds_with_all_panels(window):
    from app.ui.screen_widget import ScreenWidget
    from app.ui.keyboard import KeyboardWidget
    from app.ui.drive_widget import DriveWidget
    from app.ui.settings_widget import SettingsWidget

    assert window.windowTitle()
    for widget_type in (ScreenWidget, KeyboardWidget, DriveWidget, SettingsWidget):
        assert window.findChildren(widget_type), f"{widget_type.__name__} fehlt im Fenster"


def test_main_window_owns_a_working_emulator(window):
    from app.core_binding.k1520 import K1520Emulator

    assert isinstance(window.emulator, K1520Emulator)
    assert window.emulator.run(50_000) > 0


def test_default_drive_configuration_is_applied(window):
    """Die Laufwerksleiste zeigt genau die bestückten Slots (A:/B:/C:, D: leer)."""
    import app.drive_types as dt

    assert window._drive_types == list(dt.DEFAULT_DRIVE_TYPES)
    assert window.drives_widget.present_drives() == [0, 1, 2]


def test_changing_the_drive_bay_rebuilds_the_panels(window, qapp):
    """Ein leerer Slot verschwindet aus der Leiste, ein neuer kommt hinzu."""
    import app.drive_types as dt

    window.drives_widget.set_drive_types(["K5601", dt.NO_DRIVE, "MF3200", "K5601"])
    qapp.processEvents()
    assert window.drives_widget.present_drives() == [0, 2, 3]

    window.drives_widget.set_drive_types(dt.DEFAULT_DRIVE_TYPES)
    qapp.processEvents()
    assert window.drives_widget.present_drives() == [0, 1, 2]


def test_screen_widget_geometry_matches_the_core_framebuffer(window):
    from app.ui.screen_widget import FB_WIDTH, FB_HEIGHT

    assert (FB_WIDTH, FB_HEIGHT) == (640, 288), "Framebuffer-Geometrie geändert"
    # Die C-API muss dieselbe Geometrie melden, sonst zeigt die GUI Müll.
    from app.core_binding.k1520 import _lib
    assert _lib.k1520_fb_width(window.emulator._handle) == FB_WIDTH
    assert _lib.k1520_fb_height(window.emulator._handle) == FB_HEIGHT


def test_config_roundtrip_through_the_window(window, tmp_path):
    """Konfiguration aus dem laufenden Fenster speichern und zurücklesen."""
    import app.config_io as cfg

    path = tmp_path / "gui-config.yaml"
    data = cfg.build_config(window.screen_widget.params, {"speed": 1.0}, [],
                            {"width": 800, "height": 600}, window._drive_types)
    cfg.save_config(str(path), data)
    assert cfg.load_config(str(path)) == data


def test_window_survives_event_processing(window, qapp):
    """Ein paar Runden Eventloop ohne Ausnahme — fängt Timer-/Signalfehler."""
    for _ in range(10):
        qapp.processEvents()


def test_mounting_a_disk_updates_the_drive_panel(window, qapp, temp_disk):
    """Diskette mounten → die Laufwerksanzeige meldet das Laufwerk als belegt."""
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert window.emulator.mount_disk(0, path, "cpa780", False), \
        window.emulator.last_error()
    qapp.processEvents()
    assert window.emulator.is_disk_active(0)


def test_drive_panel_shows_the_adaptation_notice(window, qapp, tmp_path):
    """Passt die Diskette nicht zum Laufwerk, steht der Hinweis im Laufwerkskasten."""
    drives = window.drives_widget
    fmt, path = "k5601_ss40_26x128", str(tmp_path / "vierzig_spuren.img")
    assert window.emulator.create_disk(0, path, fmt, False), window.emulator.last_error()
    assert window.emulator.unmount_disk(0)

    # Über den Wiederherstellungsweg mounten — derselbe Pfad wie beim Programmstart.
    drives.load_mounts([{"drive": 0, "path": path, "format": fmt,
                         "write_protect": False}])
    qapp.processEvents()

    label = drives._panels[0]._notice_label
    assert label.isVisibleTo(drives), "Hinweis muss sichtbar sein"
    assert "Double Step aktiviert" in label.text()
    assert "schrittverdoppelt" in label.toolTip(), "Tooltip erklärt den Hinweis"

    # Nach dem Aushängen verschwindet er wieder.
    drives.load_mounts([])
    qapp.processEvents()
    assert not drives._panels[0]._notice_label.isVisibleTo(drives)


# ─── Symbolleiste ────────────────────────────────────────────────────────────
#
# Sie ist einrichtbar und ausblendbar (app/ui/actions.py + app/ui/toolbar_config.py).
# Zwei Dinge müssen dabei halten: die Leiste lässt sich aus einer Namensliste
# beliebig oft NEU aufbauen — daran ist sie schon einmal gescheitert, weil PySide
# beim Leeren die Hülle eines Kastenschalters freigibt —, und was der Anwender
# zusammengestellt hat, steht nach einem Neustart wieder da.

def test_toolbar_shows_the_default_buttons(window):
    """Jeder Name der Werksbelegung muss auf eine wirkliche Aktion zeigen.

    Geprüft wird die Liste selbst: ein Tippfehler in ``STANDARD`` fiele sonst
    nicht auf — die Leiste übergeht einen unbekannten Namen absichtlich (eine
    ältere Konfiguration darf nicht zum Absturz führen).  Dass hier wirklich der
    Werkszustand steht, sichert die Fixture (sie räumt die gemerkte
    Konfiguration weg).
    """
    from app.ui import actions

    gezeigt = [a for a in window.controls_bar.actions() if not a.isSeparator()]
    assert len(gezeigt) == len([n for n in actions.STANDARD if n is not None])
    assert window.act_power in gezeigt and window.act_reset in gezeigt
    assert window.act_hilfe in gezeigt
    assert window.act_konfig_laden in gezeigt and window.act_konfig_speichern in gezeigt


def test_toolbar_can_be_rebuilt_repeatedly(window, qapp):
    """Zweimal füllen darf nicht scheitern — der Kastenschalter ist der Prüfstein.

    ``QToolBar.clear()`` gibt die Python-Hülle eines ``toggleViewAction()`` frei
    (das C++-Objekt überlebt).  Ein gemerkter Verweis darauf war beim zweiten
    Aufbau tot — und der zweite Aufbau ist genau der, den eine gespeicherte
    Konfiguration auslöst.
    """
    for _ in range(3):
        window._leiste_fuellen(["power", None, "dock_drives", "dock_settings",
                                "hilfe"])
        qapp.processEvents()
    gezeigt = [a.text() for a in window.controls_bar.actions() if not a.isSeparator()]
    assert "&Laufwerke" in gezeigt and "&Einstellungen" in gezeigt


def test_toolbar_ignores_names_from_an_older_configuration(window):
    """Ein Name, den es nicht mehr gibt, wird übergangen statt zu stürzen."""
    window._leiste_fuellen(["power", "gibt_es_nicht", "reset"])
    gezeigt = [a for a in window.controls_bar.actions() if not a.isSeparator()]
    assert gezeigt == [window.act_power, window.act_reset]


def test_toolbar_layout_survives_a_config_roundtrip(window):
    """Inhalt und Stil der Leiste stehen in der Konfiguration, nicht im Programm."""
    from PySide6.QtCore import Qt

    window._leiste_fuellen(["reset", None, "vollbild"])
    window.controls_bar.setToolButtonStyle(Qt.ToolButtonIconOnly)
    gespeichert = window._gather_window_state()
    assert gespeichert["toolbar"] == ["reset", "", "vollbild"]
    assert gespeichert["toolbar_style"] == int(Qt.ToolButtonIconOnly.value)

    window._leiste_fuellen(["power"])
    window._apply_window_state(gespeichert)
    gezeigt = [a for a in window.controls_bar.actions() if not a.isSeparator()]
    assert gezeigt == [window.act_reset, window.act_vollbild]
    assert window.controls_bar.toolButtonStyle() == Qt.ToolButtonIconOnly


def test_toolbar_can_be_hidden_and_everything_stays_in_the_menu(window, qapp):
    """Ausblenden darf nichts unerreichbar machen — das ist die ganze Zusage."""
    window.act_leiste_zeigen.trigger()
    qapp.processEvents()
    assert not window.controls_bar.isVisible()

    im_menue = set()
    for menue in window.menuBar().findChildren(type(window.menuBar()).__bases__[0]):
        pass
    for a in window.menuBar().actions():
        if a.menu() is not None:
            im_menue.update(a.menu().actions())
    for name in ("power", "reset", "konfig_laden", "konfig_speichern", "hilfe"):
        assert window._aktion(name) in im_menue, name

    window.act_leiste_zeigen.trigger()
    qapp.processEvents()
    assert window.controls_bar.isVisible()


def test_toolbar_dialog_keeps_order_and_drops_stray_separators(qapp):
    """Der Einrichtdialog liefert die Auswahl in Listenfolge, ohne lose Striche."""
    from PySide6.QtCore import Qt
    from app.ui.toolbar_config import ToolbarDialog

    dlg = ToolbarDialog(["power", "reset", "hilfe"], [None, "power", None, None],
                        {"power": "Power", "reset": "Reset", "hilfe": "Hilfe"},
                        ["power"])
    # Angehakt ist nur, was hereingegeben wurde; der Rest steht ungehakt darunter.
    assert dlg.auswahl() == ["power"], "führende/doppelte Trennstriche fallen weg"

    # Ein Trennstrich ZWISCHEN zwei Knöpfen bleibt — nur die am Rand und die
    # doppelten fallen weg.
    for i in range(dlg.liste.count()):
        if dlg.liste.item(i).data(Qt.UserRole) == "hilfe":
            dlg.liste.item(i).setCheckState(Qt.Checked)
    assert dlg.auswahl() == ["power", None, "hilfe"]


# ─── Statuszeile ─────────────────────────────────────────────────────────────

def test_status_bar_shows_a_field_per_present_drive(window):
    """Je bestücktem Steckplatz eine Leuchte und ein Feld — der vierte bekommt keins."""
    from app.ui import status_bar

    felder = window.status_widget.felder()
    assert [f.drive for f in felder] == [0, 1, 2]
    assert all(f.text().endswith("leer") for f in felder), \
        "ohne Diskette meldet das Feld genau das"
    for drive in (0, 1, 2):
        assert window.status_widget.lampe(drive) is not None
    assert window.status_widget.lampe(3) is None, "leerer Steckplatz, keine Leuchte"


def test_drive_lamp_tells_empty_from_mounted_from_busy(window, qapp, temp_disk):
    """Die drei Zustände der Leuchte: leerer Ring, gefüllt, rot."""
    from app.ui import status_bar

    window.drives_widget.load_mounts([])
    qapp.processEvents()
    window._update_drive_status()
    assert window.status_widget.lampe(0).zustand() == status_bar.LEER

    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert window.drives_widget.mount_path(0, path, "cpa780", False)
    window._update_drive_status()
    assert window.status_widget.lampe(0).zustand() in (status_bar.BELEGT,
                                                       status_bar.ZUGRIFF)

    # „rot" hängt an der Leuchte des Kerns; hier wird nur der Übergang geprüft.
    window.status_widget.lampe(0).set_zustand(status_bar.ZUGRIFF)
    assert window.status_widget.lampe(0).zustand() == status_bar.ZUGRIFF


def test_status_bar_names_the_mounted_image_and_its_write_state(window, qapp,
                                                                temp_disk):
    """Dateiname und R/O ↔ R/W — die beiden Angaben, die beim Arbeiten zählen."""
    import os

    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert window.drives_widget.mount_path(0, path, "cpa780", False), \
        window.emulator.last_error()
    qapp.processEvents()
    window._update_drive_status()

    feld = window.status_widget.felder()[0]
    assert os.path.basename(path) in feld.text()
    assert feld.text().endswith("R/W")
    assert path in feld.toolTip(), "der volle Pfad steht im Tooltip"

    # Der Schreibschutz wirkt SOFORT, nicht erst beim nächsten Einlegen.
    window.drives_widget._panels[0]._wp_check.setChecked(True)
    qapp.processEvents()
    window._update_drive_status()
    assert window.emulator.is_disk_write_protected(0)
    assert window.status_widget.felder()[0].text().endswith("R/O")


def test_status_bar_follows_the_drive_bay(window, qapp):
    """Ein abgemeldetes Laufwerk verschwindet auch aus der Statuszeile."""
    import app.drive_types as dt

    window._apply_drive_types(["K5601", dt.NO_DRIVE, "K5601", "K5601"],
                              cold_restart=False)
    qapp.processEvents()
    assert [f.drive for f in window.status_widget.felder()] == [0, 2, 3]

    window._apply_drive_types(dt.DEFAULT_DRIVE_TYPES, cold_restart=False)
    qapp.processEvents()
    assert [f.drive for f in window.status_widget.felder()] == [0, 1, 2]


def test_status_bar_shows_the_configured_clock_not_the_measured_one(window):
    """Angezeigt wird der EINGESTELLTE Takt — er soll nicht im Sekundentakt zappeln.

    Der gemessene Wert ist nicht verloren: er steht im Tooltip, samt dem Satz,
    wenn der Wirtsrechner nicht mitkommt.
    """
    import time

    from app import takt

    window._apply_speed(10.0)
    # Gemessen wird absichtlich etwas ANDERES als eingestellt.
    window._tempo_zeit = time.monotonic() - 1.0
    window._tempo_cycles = 0
    window.cycles = window.CPU_HZ            # also 1× statt der eingestellten 10×
    window._update_status()

    assert window.status_widget.takt.text() == f"Takt: {takt.beschriftung(10.0)}"
    assert "10 × 2,45 MHz" in window.status_widget.takt.text()
    tooltip = window.status_widget.takt.toolTip()
    assert "Gemessen: 1,0 × 2,45 MHz" in tooltip
    assert "kommt nicht mit" in tooltip

    window._apply_speed(1.0)
    assert window.status_widget.takt.text() == "Takt: 2,45 MHz"


def test_clock_labels_are_the_same_in_the_dropdown_and_the_status_bar(window):
    """Auswahlfeld und Statuszeile sprechen dieselbe Sprache (app/takt.py)."""
    from app import takt

    im_feld = [window.settings_widget.speed_combo.itemText(i)
               for i in range(window.settings_widget.speed_combo.count())]
    assert im_feld == [b for b, _ in takt.auswahl()]
    assert im_feld[:4] == ["2,45 MHz", "2 × 2,45 MHz", "5 × 2,45 MHz",
                           "10 × 2,45 MHz"]

    for beschriftung, faktor in takt.auswahl():
        window.status_widget.set_takt(faktor)
        assert window.status_widget.takt.text() == f"Takt: {beschriftung}"


def test_status_bar_has_no_cycle_counter_any_more(window):
    """Zykluszähler und Bildrate sind bewusst weg — sie sagten nichts."""
    text = " ".join([window.status_widget.takt.text()]
                    + [f.text() for f in window.status_widget.felder()])
    assert "Cycles" not in text and "FPS" not in text


# ─── Menü ────────────────────────────────────────────────────────────────────

def test_the_file_menu_can_actually_mount_a_disk(window, qapp, monkeypatch,
                                                 temp_disk):
    """*Datei ▸ Diskette einlegen ▸ A:* legt wirklich ein — früher tat es nichts."""
    # Bekannter Ausgangszustand: eine gespeicherte Konfiguration kann A: schon
    # belegt haben, und dann ist „einlegen" dort zu Recht gesperrt.
    window.drives_widget.load_mounts([])
    qapp.processEvents()

    path = temp_disk("cpa_cpa780_k5601_clock.img")
    monkeypatch.setattr("app.ui.drive_widget.QFileDialog.getOpenFileName",
                        staticmethod(lambda *a, **k: (path, "")))

    menue = window.act_einlegen.menu()
    assert menue is not None, "der Menüpunkt trägt ein Untermenü je Laufwerk"
    window._disk_menue_fuellen(menue, einlegen=True)
    eintraege = menue.actions()
    assert len(eintraege) == 3 and all(a.isEnabled() for a in eintraege)

    eintraege[0].trigger()
    qapp.processEvents()
    assert window.drives_widget.is_mounted(0)
    assert window.emulator.disk_path(0) == path

    # Jetzt ist A: belegt: einlegen gesperrt, auswerfen frei.
    window._disk_menue_fuellen(menue, einlegen=True)
    assert not menue.actions()[0].isEnabled()
    auswurf = window.act_auswerfen.menu()
    window._disk_menue_fuellen(auswurf, einlegen=False)
    assert auswurf.actions()[0].isEnabled()
    auswurf.actions()[0].trigger()
    qapp.processEvents()
    assert not window.drives_widget.is_mounted(0)


def test_power_action_is_the_switch_and_says_what_it_does(window, qapp):
    """Der Netzschalter ist rastend; seine Beschriftung nennt die Wirkung."""
    from app.ui import actions

    assert window.act_power.isCheckable() and window.act_power.isChecked()
    assert window.act_power.text() == actions.POWER_TEXT[True]

    window.act_power.trigger()
    qapp.processEvents()
    assert not window.act_power.isChecked()
    assert window.act_power.text() == actions.POWER_TEXT[False]
    assert not window.run_timer.isActive(), "ausgeschaltet läuft nichts mehr"

    window.act_power.trigger()
    qapp.processEvents()
    assert window.run_timer.isActive()


def test_no_shortcut_steals_a_key_from_the_emulated_machine(window):
    """Jedes Kürzel trägt Strg+Umschalt — außer F11, das der Bildschirm abfängt.

    Die Tastatur gehört dem Gast: ``^S`` hält bei CP/M die Ausgabe an, ``^P``
    schaltet den Drucker zu, die Funktionstasten gehen an die K7637.  Qt wertet
    ein Kürzel VOR dem Widget aus — was das Fenster beansprucht, kommt dort nie
    an.
    """
    from PySide6.QtGui import QAction

    fremd = []
    for a in window.findChildren(QAction):
        k = a.shortcut().toString()
        if k and k != "F11" and not k.startswith("Ctrl+Shift+"):
            fremd.append(f"{k} ({a.text()})")
    assert fremd == [], f"greift dem emulierten Rechner in die Tastatur: {fremd}"


# ─── Handbuch ────────────────────────────────────────────────────────────────
#
# Wie beim DiskTool: eine `.md`-Datei, die Qt selbst setzt — kein Bauschritt,
# keine Abhängigkeit.  Geprüft wird, dass sie mitgeliefert wird, dass das
# Inhaltsverzeichnis zum Text passt und dass die dort genannten Tastenkürzel
# WIRKLICH an den Aktionen hängen.  Eine Hilfe, die mit der Oberfläche
# auseinanderläuft, ist schlimmer als keine.

_TASTEN = {"Strg": "Ctrl", "Umschalt": "Shift", "Entf": "Del", "Eingabe": "Return"}


def _als_kuerzel(deutsch: str):
    from PySide6.QtGui import QKeySequence
    return QKeySequence("+".join(_TASTEN.get(t, t) for t in deutsch.split("+")))


def _handbuch_kuerzel() -> dict:
    """Die Tabelle „Tastenkürzel" des Handbuchs als {Kürzel: Wirkung}."""
    from app.ui.help_window import lade_handbuch

    tabelle = lade_handbuch().split("## Tastenkürzel", 1)[1].split("\n## ", 1)[0]
    out = {}
    for zeile in tabelle.splitlines():
        if not zeile.startswith("|") or "---" in zeile:
            continue
        spalten = [s.strip() for s in zeile.strip("|").split("|")]
        if len(spalten) == 2 and spalten[0] != "Kürzel":
            out[spalten[0]] = spalten[1]
    return out


def test_help_manual_ships_inside_the_app_tree():
    """Sie muss unter `app/` liegen — `doc/` ist nicht im Anwenderpaket."""
    from app.ui.help_window import HANDBUCH

    assert HANDBUCH.is_file(), HANDBUCH
    assert "app" in HANDBUCH.parts, "sonst fehlt das Handbuch im Anwenderpaket"
    assert HANDBUCH.read_text(encoding="utf-8").startswith("# a5120emu")


def test_help_window_lists_every_section_and_jumps_to_it(window):
    from app.ui.help_window import abschnitte, lade_handbuch

    h = window.open_help()
    erwartet = abschnitte(lade_handbuch())
    assert len(erwartet) >= 8, "ein Kurzhandbuch, aber kein Zettel"

    im_verzeichnis = [h.inhalt.item(i).text() for i in range(h.inhalt.count())]
    assert im_verzeichnis == erwartet
    for name in erwartet:
        assert h.springe_zu(name), name
    assert not h.springe_zu("Gibt es nicht")
    h.close()


def test_help_window_opens_once_per_window(window):
    h = window.open_help()
    assert window.open_help() is h, "kein zweites Handbuchfenster"
    h.close()


def test_help_search_finds_and_reports_a_miss(window):
    h = window.open_help()
    h.suchfeld.setText("Schreibschutz")
    assert h.weitersuchen()
    h.suchfeld.setText("Kernspeicherringkern")
    assert not h.weitersuchen()
    assert "nicht gefunden" in h.meldung.text()
    h.close()


def test_every_shortcut_in_the_manual_really_exists(window):
    """Jedes Kürzel der Handbuchtabelle hängt an einer Aktion des Fensters."""
    from PySide6.QtGui import QAction

    vorhanden = {a.shortcut().toString() for a in window.findChildren(QAction)
                 if not a.shortcut().isEmpty()}
    fehlend = [f"{k} ({v})" for k, v in _handbuch_kuerzel().items()
               if _als_kuerzel(k).toString() not in vorhanden]
    assert fehlend == [], f"im Handbuch versprochen, aber nicht verdrahtet: {fehlend}"


def test_every_shortcut_of_the_window_is_in_the_manual(window):
    """Und die Gegenrichtung: kein Kürzel bleibt unerwähnt."""
    from PySide6.QtGui import QAction

    im_handbuch = {_als_kuerzel(k).toString() for k in _handbuch_kuerzel()}
    fehlend = [f"{a.shortcut().toString()} ({a.text()})"
               for a in window.findChildren(QAction)
               if not a.shortcut().isEmpty()
               and a.shortcut().toString() not in im_handbuch]
    assert fehlend == [], f"verdrahtet, aber im Handbuch nicht genannt: {fehlend}"


def test_every_bundled_icon_can_be_rendered(qapp):
    """Die Symbole liegen mit im Baum — sonst bliebe die Leiste unter Windows leer."""
    from app.ui_icons import ICON_DIR, icon

    dateien = sorted(p.stem for p in ICON_DIR.glob("*.svg"))
    assert len(dateien) >= 25, dateien
    for name in ("power", "reset", "settings", "drives", "help",
                 "config-open", "config-save"):
        assert name in dateien, f"{name}.svg fehlt"
        assert not icon(name).pixmap(24, 24).isNull(), name


# ─── Fensterzustand ──────────────────────────────────────────────────────────
#
# Fenstergröße, „maximiert" und die Aufteilung der Kästen stehen in der
# Konfiguration.  Der Zug an einer Trennlinie ist der heikle Fall: er ändert nur
# die Kästen, nicht das Fenster — daran hing das Speichern, und die Aufteilung
# ging deshalb bei jedem Start verloren.

def test_dragging_a_dock_separator_schedules_a_save(window, qapp):
    """Eine Größenänderung eines Kastens muss gespeichert werden."""
    from PySide6.QtCore import QEvent, QSize
    from PySide6.QtGui import QResizeEvent

    window._autosave_timer.stop()
    assert not window._autosave_timer.isActive()

    # Genau das Ereignis, das ein Zug an der Trennlinie auslöst.
    qapp.sendEvent(window.drives_dock,
                   QResizeEvent(QSize(320, 400), window.drives_dock.size()))
    assert window._autosave_timer.isActive(), \
        "ohne das wäre die Aufteilung beim nächsten Start wieder die alte"


def test_window_state_carries_size_maximized_and_dock_layout(window):
    """Was gespeichert wird: Größe, Maximierung, Kastenaufteilung, Leiste."""
    zustand = window._gather_window_state()
    for schluessel in ("geometry", "width", "height", "maximized", "dock_state",
                       "toolbar", "toolbar_style"):
        assert schluessel in zustand, schluessel
    assert isinstance(zustand["maximized"], bool)
    assert zustand["dock_state"], "QMainWindow.saveState() trägt die Kastenbreiten"


def test_maximized_survives_a_config_roundtrip(window, qapp):
    """Maximiert speichern und wiederherstellen — über Qts eigene Geometrie.

    Getragen wird das von ``saveGeometry``/``restoreGeometry``: dort steckt der
    Fensterzustand mit drin, und es wirkt auf dem noch unsichtbaren Fenster.
    """
    window.showMaximized()
    qapp.processEvents()
    assert window.isMaximized()

    zustand = window._gather_window_state()
    assert zustand["maximized"] is True
    assert zustand["geometry"], "ohne Qt-Geometrie gäbe es keinen Zustand"

    window.showNormal()
    qapp.processEvents()
    assert not window.isMaximized()

    window._apply_window_state(zustand)
    qapp.processEvents()
    assert window.isMaximized(), "maximiert muss wiederkommen"


def test_an_older_config_without_geometry_still_restores(window, qapp):
    """Rückfall für Konfigurationen aus einer Fassung ohne ``geometry``."""
    window.showNormal()
    qapp.processEvents()

    zustand = window._gather_window_state()
    zustand.pop("geometry")
    zustand["maximized"] = True
    window._apply_window_state(zustand)
    qapp.processEvents()
    assert window.windowState() & Qt_WindowMaximized()


def Qt_WindowMaximized():
    from PySide6.QtCore import Qt
    return Qt.WindowMaximized


def test_the_remembered_size_is_the_one_of_the_restored_window(window, qapp):
    """Maximiert überschreibt die gemerkte Größe NICHT.

    Sonst landete ein Entmaximieren beim nächsten Start auf Bildschirmgröße
    statt dort, wo der Anwender sein Fenster zuletzt hatte.
    """
    from PySide6.QtCore import QSize
    from PySide6.QtGui import QResizeEvent

    window.showNormal()
    window.resize(980, 640)
    qapp.processEvents()
    normal = QSize(window._normal_size)

    window.showMaximized()
    qapp.processEvents()
    window.resizeEvent(QResizeEvent(window.size(), QSize(980, 640)))
    assert window._normal_size == normal
    assert window._gather_window_state()["maximized"] is True


def test_closing_always_writes_the_state(window, qapp, tmp_path, monkeypatch):
    """Beim Beenden wird IMMER gesichert, nicht nur eine anstehende Änderung."""
    geschrieben = []
    monkeypatch.setattr(window, "_autosave_now",
                        lambda: geschrieben.append(True))
    window._autosave_timer.stop()
    window.close()
    qapp.processEvents()
    assert geschrieben, "sonst geht die letzte Aufteilung der Kästen verloren"


def test_drive_lamps_are_polled_at_led_speed(window):
    """Die Leuchte hängt an einem eigenen, SCHNELLEN Takt.

    Im Sekundentakt des übrigen Statuszeilen-Aufbaus abgetastet blitzte sie
    praktisch nie auf: ein Sektorzugriff ist in wenigen Zehntelsekunden vorbei.
    Derselbe Takt wie die Leuchten im Laufwerkskasten.
    """
    assert window._lamp_timer.isActive()
    assert window._lamp_timer.interval() <= 200, "zu langsam, um einen Zugriff zu zeigen"
    assert window._lamp_timer.interval() == window.drives_widget._led_timer.interval()


def test_a_disk_access_turns_the_lamp_red(window, qapp, temp_disk):
    """Der eigentliche Fall: während eines Zugriffs ist die Leuchte rot.

    Geprüft wird die Verdrahtung Kern → Leuchte, nicht die Zeit: der Zustand des
    Kerns wird vorgegeben, damit der Test nicht am Zufall eines Bootlaufs hängt.
    """
    from app.ui import status_bar

    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert window.drives_widget.mount_path(0, path, "cpa780", False)
    qapp.processEvents()

    window.emulator.is_disk_led_on = lambda drive: drive == 0
    window._update_drive_lamps()
    assert window.status_widget.lampe(0).zustand() == status_bar.ZUGRIFF
    assert window.status_widget.lampe(1).zustand() == status_bar.LEER

    window.emulator.is_disk_led_on = lambda drive: False
    window._update_drive_lamps()
    assert window.status_widget.lampe(0).zustand() == status_bar.BELEGT


def test_a_dragged_separator_is_not_pushed_back(window, qapp):
    """Ein Zug an einer Trennlinie schaltet die Startaufteilung ab.

    Die Startaufteilung (`_shrink_keyboard`: Tastatur auf Inhaltshöhe,
    Seitenkästen schmal) lief nach JEDEM Fenster-Resize und holte die Tastatur
    wieder auf ihre Minimalhöhe — die waagerechte Trennlinie liess sich dann
    scheinbar nicht verschieben.  Jetzt tritt sie zurück, sobald der Anwender
    selbst gezogen hat; gespeichert wird sein Zug ohnehin.
    """
    from PySide6.QtCore import QSize
    from PySide6.QtGui import QResizeEvent

    window._has_saved_layout = False
    window._nutzer_layout = False
    window._layout_laeuft = False

    # Ein Zug an der Trennlinie: Größenänderung eines Kastens, ohne dass wir
    # gerade selbst umbauen.
    qapp.sendEvent(window.keyboard_dock,
                   QResizeEvent(QSize(600, 300), window.keyboard_dock.size()))
    assert window._nutzer_layout, "der Zug muss als Anordnung des Anwenders gelten"

    gerueckt = []
    window._startaufteilung = lambda: gerueckt.append(True)
    window.resizeEvent(QResizeEvent(window.size(), window.size()))
    window._shrink_keyboard()
    assert gerueckt == [], "nach dem Zug rückt die Startaufteilung nichts mehr"


def test_showing_a_panel_is_not_mistaken_for_a_drag(window, qapp):
    """Ein- und Ausblenden ordnet die Nachbarn um — das ist kein Zug.

    Sonst schaltete schon der erste Klick auf „Tastatur" die Startaufteilung ab,
    und die Tastatur ginge in voller Kastenhöhe auf.
    """
    from PySide6.QtCore import QSize
    from PySide6.QtGui import QResizeEvent

    window._has_saved_layout = False
    window._nutzer_layout = False

    window._kasten_sichtbarkeit()          # das Signal des Kastens
    qapp.sendEvent(window.keyboard_dock,
                   QResizeEvent(QSize(600, 300), window.keyboard_dock.size()))
    assert not window._nutzer_layout

    # Nach einer Runde der Ereignisschleife zählt ein Zug wieder als Zug.
    qapp.processEvents()
    qapp.sendEvent(window.keyboard_dock,
                   QResizeEvent(QSize(600, 260), window.keyboard_dock.size()))
    assert window._nutzer_layout


def test_a_window_resize_is_not_mistaken_for_a_drag(window, qapp):
    """Die Kästen wachsen mit dem Fenster — auch das ist kein Zug des Anwenders."""
    from PySide6.QtCore import QSize
    from PySide6.QtGui import QResizeEvent

    window._has_saved_layout = False
    window._nutzer_layout = False

    window.resizeEvent(QResizeEvent(QSize(1200, 800), window.size()))
    qapp.sendEvent(window.drives_dock,
                   QResizeEvent(QSize(260, 800), window.drives_dock.size()))
    assert not window._nutzer_layout


def test_the_keyboard_dock_can_be_resized_at_all(qapp):
    """Der Kasten der Tastatur darf nicht in der Höhe festgenagelt sein.

    Mit ``QSizePolicy.Fixed`` (so war es) nimmt Qt die Wunschhöhe zugleich als
    Mindest- UND Höchsthöhe: ``min == max``, und damit lässt sich die Trennlinie
    über dem Kasten **überhaupt nicht** ziehen.  Sichtbar wurde das, als die
    Tastatur unter die Laufwerke gedockt wurde — dort stand sie auf ihrer
    Wunschhöhe, mit schwarzen Balken über und unter dem Tastenfeld (die
    Zeichnung hält ihr Seitenverhältnis), und drückte die Laufwerke in den
    Rollbalken.
    """
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import QDockWidget, QMainWindow, QSizePolicy

    from app.ui.keyboard import KeyboardWidget

    kw = KeyboardWidget()
    senkrecht = kw.sizePolicy().verticalPolicy()
    assert senkrecht != QSizePolicy.Fixed, "sonst ist der Kasten festgenagelt"

    fenster = QMainWindow()
    fenster.resize(900, 700)
    kasten = QDockWidget("Tastatur", fenster)
    kasten.setWidget(kw)
    fenster.addDockWidget(Qt.RightDockWidgetArea, kasten)
    fenster.show()
    qapp.processEvents()

    assert kasten.minimumHeight() < kasten.maximumHeight(), \
        "zwischen Mindest- und Höchsthöhe muss Luft sein, sonst gibt es nichts zu ziehen"
    assert kasten.minimumHeight() <= kw.heightForWidth(kw.width()), \
        "die Höhe, die zur Breite passt, muss erreichbar sein"
    fenster.close()


def test_the_keyboard_is_refitted_when_it_is_docked_elsewhere(window, qapp):
    """Umdocken ändert die Breite — und damit die Höhe, die die Tastatur braucht."""
    from PySide6.QtCore import Qt

    window.keyboard_dock.show()
    qapp.processEvents()
    # Wie im Bericht: Tastatur unter die Laufwerke, also in die schmale Spalte.
    window.addDockWidget(Qt.RightDockWidgetArea, window.keyboard_dock)
    window.splitDockWidget(window.drives_dock, window.keyboard_dock, Qt.Vertical)
    qapp.processEvents()
    window._tastatur_einpassen()
    qapp.processEvents()

    kw = window.keyboard_widget
    balken = kw.height() - kw.heightForWidth(kw.width())
    assert balken <= 2, f"schwarze Balken über/unter der Tastatur: {balken} px"
