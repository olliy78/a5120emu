"""Reiter „Schnittstellen" im Einstellungen-Kasten, Statuszeile und Konfiguration (AP-S7, Entwurf 19 §9/§11).

Beide Programme (A5120 und K8915 Emulator), headless über ``offscreen``.  Echte
Loopback-Sockets, nie feste Ports (ein freier wird beim Test erfragt, ein Server
meldet den tatsächlichen), kurze Fristen.  Namen und Fähigkeiten der Schnittstellen
kommen aus dem Kern — die Tests fragen ihn, statt sie zu kennen.
"""

import socket
import time

import pytest
import yaml

from PySide6.QtCore import Qt

from conftest import requires_core

pytestmark = requires_core

from app.core_binding import k1520 as K  # noqa: E402


# ─── Hilfen ──────────────────────────────────────────────────────────────────

def freier_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def warte(qapp, bedingung, frist=4.0):
    """Ereignisschleife und Kasten laufen lassen, bis *bedingung* gilt."""
    ende = time.monotonic() + frist
    while time.monotonic() < ende:
        qapp.processEvents()
        w = bedingung()
        if w:
            return w
        time.sleep(0.02)
    qapp.processEvents()
    return bedingung()


def _fenster(qapp, maschine):
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(None, profil=profil.profil(maschine))
    w.show()
    qapp.processEvents()
    return w


def _zu(qapp, fenster):
    """Fenster schliessen und stillegen.

    ``close()`` allein lässt die Zeitgeber der Kästen (Laufwerkskasten: 120 ms und
    1 s mit einer Formatmessung) weiterlaufen, solange das Python-Objekt lebt — und
    es lebt bis zum Testende.  Jedes liegengebliebene Fenster rechnet dann in jedem
    ``processEvents`` mit, und die Datei wird mit der Zahl der Tests immer
    langsamer (aufgefallen bei ~70 Fenstern: der Lauf stand scheinbar).  Auch der
    Ereignisfilter des ``ScreenFocusGuard`` hängt an der Anwendung; er wird
    abgehängt.  Das Fenster selbst wird NICHT zerstört (``deleteLater`` riss den
    Filter mit und stürzte ab).
    """
    from PySide6.QtCore import QTimer
    fenster.close()
    for t in fenster.findChildren(QTimer):
        t.stop()
    qapp.removeEventFilter(fenster._focus_guard)
    qapp.processEvents()


@pytest.fixture(params=["a5120", "k8915"])
def maschine(request):
    return request.param


@pytest.fixture
def konfig(tmp_path, monkeypatch):
    """Eigenes Konfigurationsverzeichnis je Test."""
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    return tmp_path / "xdg"


@pytest.fixture
def w(qapp, konfig, maschine):
    fenster = _fenster(qapp, maschine)
    fenster.maschine = maschine
    yield fenster
    _zu(qapp, fenster)


def frei(block):
    """Block betriebsbereit machen: der K8915 startet im Rx/Tx-Loop (Prüfstecker)."""
    block.loop.setChecked(False)
    return block


def server_einstellen(block, port=None):
    frei(block)
    block.betriebsart.setCurrentIndex(0)           # Telnet
    block.rolle.setCurrentIndex(0)                 # Server
    block.port.setValue(port or freier_port())
    return block


def client_einstellen(block, port, host="127.0.0.1"):
    frei(block)
    block.betriebsart.setCurrentIndex(0)
    block.rolle.setCurrentIndex(1)                 # Client
    block.host.setText(host)
    block.port.setValue(port)
    return block


def sichtbar(widget, oben):
    return widget.isVisibleTo(oben)


# ─── Aufbau: Blöcke, Namen, Fähigkeiten aus dem Kern ─────────────────────────

def test_one_block_per_interface_named_by_the_core(w):
    emu = w.emulator
    bloecke = w.serial_widget.bloecke()
    assert len(bloecke) == emu.serial_count() == 3
    for i, b in enumerate(bloecke):
        info = emu.serial_info(i)
        assert info.name in b.titel.text() and info.stecker in b.titel.text()
        assert w.serial_widget.block(info.name) is b
    # Die festen Schnittstellen (Tastatur) stehen als Zeile ohne Bedienelemente.
    zeilen = [l.text() for l in w.serial_widget.findChildren(type(bloecke[0].titel))
              if "fest verdrahtet" in l.text()]
    assert zeilen == [f"{n} — fest verdrahtet" for n in emu.serial_fixed_names()]


def test_v24_and_clock_widgets_follow_the_core(w):
    for i, b in enumerate(w.serial_widget.bloecke()):
        info = w.emulator.serial_info(i)
        assert sichtbar(b.bruecke, b) == info.v24
        assert sichtbar(b.leitungen, b) == info.v24
        assert sichtbar(b.takt, b) == bool(info.taktquellen)
        assert [b.takt.itemText(q) for q in range(b.takt.count())] == info.taktquellen


def test_a_block_per_program_but_no_machine_specific_names_in_the_module():
    """Die Namen („DFÜ/V.24", „IFS 1" …) stehen im Kern, nicht im Kasten."""
    from pathlib import Path
    quelle = (Path(__file__).resolve().parents[2] / "app" / "ui" / "serial_widget.py"
              ).read_text(encoding="utf-8")
    for name in ("DFÜ/V.24", "IFS 1", "IFS 2", "DFÜ/IFSS2", "Drucker", "k8915", "a5120"):
        assert f'"{name}' not in quelle, name


def test_interfaces_are_a_tab_in_the_settings_box_not_a_dock(w):
    sw = w.settings_widget
    titel = [sw.tabs.tabText(i) for i in range(sw.tabs.count())]
    assert titel == ["Allgemein", "Laufwerke", "Schnittstellen", "CRT"]
    assert sw.tabs.widget(titel.index("Schnittstellen")).isAncestorOf(w.serial_widget)
    assert sw.isAncestorOf(w.serial_widget)
    assert not hasattr(w, "serial_dock") and not hasattr(w, "act_dock_serial")
    assert "serial_dock" not in [d.objectName() for d in w.findChildren(type(w.settings_dock))]
    sw.zeige_schnittstellen()
    assert sw.tabs.currentIndex() == 2


def test_the_view_menu_has_no_interface_entry_any_more(w):
    texte = next([a.text() for a in m.menu().actions()]
                 for m in w.menuBar().actions() if m.text() == "&Ansicht")
    assert "Sch&nittstellen" not in texte
    from app.ui import actions
    assert "dock_serial" not in actions.REIHENFOLGE


def test_an_old_toolbar_with_dock_serial_still_loads(w):
    w._leiste_fuellen(["power", "dock_serial", "dock_settings"])
    assert w.act_dock_settings in w.controls_bar.actions()
    w._apply_config({"window": {"toolbar": ["power", "dock_serial", "", "reset"]}})
    assert w.act_reset in w.controls_bar.actions()


def test_an_old_dock_state_with_serial_dock_does_no_harm(w, qapp):
    """Eine gespeicherte Kastenanordnung, in der noch ``serial_dock`` steht."""
    import base64
    from PySide6.QtWidgets import QDockWidget, QMainWindow, QLabel
    alt = QMainWindow()
    for name in ("screen_dock", "drives_dock", "settings_dock", "serial_dock"):
        d = QDockWidget(name, alt)
        d.setObjectName(name)
        d.setWidget(QLabel(name))
        alt.addDockWidget(Qt.RightDockWidgetArea, d)
    zustand = base64.b64encode(bytes(alt.saveState())).decode("ascii")
    w._apply_config({"window": {"dock_state": zustand}})
    qapp.processEvents()
    assert w.settings_dock.objectName() == "settings_dock"
    assert w.serial_widget.bloecke()                    # lebt und trägt Blöcke
    w.serial_widget.aktualisieren()


def test_the_widget_keeps_polling_with_another_tab_on_top(w, qapp):
    """Die Statuszeile ist auch ohne sichtbaren Reiter aktuell: der Takt gehört dem Widget."""
    w.settings_widget.tabs.setCurrentIndex(0)
    b = server_einstellen(w.serial_widget.bloecke()[0])
    b.knopf.click()
    port = w.emulator.serial_status(0).port_aktiv
    with socket.create_connection(("127.0.0.1", port), timeout=3):
        name = w.emulator.serial_info(0).name
        assert warte(qapp, lambda: f"{name} verbunden" in
                     w.status_widget.seriell_verbindungen.text.text())     # ohne eigenes aktualisieren()
    assert w.serial_widget._timer.isActive()


# ─── Bedienung: Knopf, Host, Etikett, Sperren ────────────────────────────────

def test_button_texts_follow_mode_and_role(w):
    b = frei(w.serial_widget.bloecke()[0])
    b.rolle.setCurrentIndex(0)
    assert b.knopf.text() == "Starten"
    b.rolle.setCurrentIndex(1)
    assert b.knopf.text() == "Verbinden"
    b.betriebsart.setCurrentIndex(1)                   # RFC2217-Client
    assert b.knopf.text() == "Verbinden"


def test_host_field_is_off_in_the_server_but_keeps_its_text(w):
    b = frei(w.serial_widget.bloecke()[0])
    b.rolle.setCurrentIndex(1)
    b.host.setText("ser2net.local")
    assert b.host.isEnabled()
    b.rolle.setCurrentIndex(0)
    assert not b.host.isEnabled() and not b.host_art.isEnabled()
    assert b.host.text() == "ser2net.local"


@pytest.mark.parametrize("text, art", [
    ("192.168.1.5", "IPv4"), ("::1", "IPv6"), ("ser2net.local", "Hostname"),
    ("300.1.1.1", "ungültig"), ("", "ungültig")])
def test_host_classification_label(w, text, art):
    b = frei(w.serial_widget.bloecke()[0])
    b.rolle.setCurrentIndex(1)
    b.host.setText(text)
    assert b.host_art.text() == art


def test_button_is_locked_for_an_invalid_host_in_the_client(w):
    b = client_einstellen(w.serial_widget.bloecke()[0], freier_port())
    assert b.knopf.isEnabled()
    b.host.setText("-kaputt-")
    assert not b.knopf.isEnabled() and b.knopf.toolTip()
    b.rolle.setCurrentIndex(0)                          # im Server zählt der Host nicht
    assert b.knopf.isEnabled()


def test_button_is_locked_while_loop_is_set_and_says_why(w):
    b = w.serial_widget.bloecke()[0]
    b.loop.setChecked(True)
    assert not b.knopf.isEnabled()
    assert "Loop" in b.meldung.text() and sichtbar(b.meldung, b)
    b.loop.setChecked(False)
    assert b.knopf.isEnabled() and not sichtbar(b.meldung, b)


def test_the_k8915_starts_with_the_loop_set_the_a5120_does_not(w):
    b = w.serial_widget.bloecke()[0]
    assert b.loop.isChecked() == (w.maschine == "k8915")
    assert b.knopf.isEnabled() == (w.maschine != "k8915")


def test_server_runs_locks_fields_and_the_statusline_shows_the_real_port(w, qapp):
    b = server_einstellen(w.serial_widget.bloecke()[0])
    eingestellt = b.port.value()
    b.knopf.click()
    st = w.emulator.serial_status(0)
    assert st.zustand == K.SER_LAUSCHT
    assert b.knopf.text() == "Beenden"
    assert b.zustand_label.text() == f"lauscht auf {st.port_aktiv}"
    # Betrieb: gesperrt sind Betriebsart, Rolle, Host, Port, Datei …
    for gesperrt in (b.betriebsart, b.rolle, b.host, b.port, b.datei_knopf):
        assert not gesperrt.isEnabled()
    # … frei bleiben Loop, Brücke, XON/XOFF, Takt (wirken sofort).
    for frei_ in (b.loop, b.xonxoff):
        assert frei_.isEnabled()
    b.xonxoff.setChecked(True)
    assert w.emulator.serial_config(0).xonxoff
    assert b.port.value() == st.port_aktiv             # im Betrieb: der benutzte Port
    assert w.emulator.serial_config(0).port == eingestellt
    srv = w.status_widget.seriell_server
    assert sichtbar(srv, w.status_widget)
    assert srv.text.text() == f"Telnet/RFC2217 Server Port: {st.port_aktiv}"
    assert not sichtbar(w.status_widget.seriell_verbindungen, w.status_widget)

    b.knopf.click()                                    # Beenden
    assert w.emulator.serial_status(0).zustand == K.SER_AUS
    assert b.knopf.text() == "Starten" and b.port.isEnabled()
    assert b.port.value() == eingestellt               # danach wieder der eingestellte
    assert not sichtbar(srv, w.status_widget)          # Feld weg, nicht leer


def test_a_connection_appears_in_the_connection_field_and_leaves_it(w, qapp):
    b = server_einstellen(w.serial_widget.bloecke()[0])
    b.knopf.click()
    port = w.emulator.serial_status(0).port_aktiv
    verb = w.status_widget.seriell_verbindungen
    assert not sichtbar(verb, w.status_widget)         # lauschen ist keine Verbindung
    with socket.create_connection(("127.0.0.1", port), timeout=3) as c:
        c.sendall(b"x")
        assert warte(qapp, lambda: (w.serial_widget.aktualisieren(),
                                    sichtbar(verb, w.status_widget))[1])
        name = w.emulator.serial_info(0).name
        assert verb.text.text() == f"{name} verbunden"
        assert "127.0.0.1" in verb.text.toolTip() and name in verb.text.toolTip()
        assert b.zustand_label.text().startswith("verbunden 127.0.0.1:")
        assert sichtbar(w.status_widget.seriell_server, w.status_widget)   # lauscht weiter
    assert warte(qapp, lambda: (w.serial_widget.aktualisieren(),
                                not sichtbar(verb, w.status_widget))[1])


def test_two_servers_list_their_real_ports_in_interface_order(w, qapp):
    p0, p1 = freier_port(), freier_port()
    server_einstellen(w.serial_widget.bloecke()[0], p0).knopf.click()
    server_einstellen(w.serial_widget.bloecke()[2], p1).knopf.click()
    ports = [w.emulator.serial_status(i).port_aktiv for i in (0, 2)]
    text, tipp, _, _ = w.serial_widget.statuszeilentexte()
    assert text == f"Telnet/RFC2217 Server Port: {ports[0]}, {ports[1]}"
    assert len(tipp.splitlines()) == 2


def test_a_trying_client_never_shows_in_the_statusline_and_stays_stoppable(w, qapp):
    b = client_einstellen(w.serial_widget.bloecke()[0], freier_port())   # niemand lauscht
    assert b.knopf.text() == "Verbinden"
    b.knopf.click()
    assert warte(qapp, lambda: w.emulator.serial_status(0).versuche >= 1)
    w.serial_widget.aktualisieren()
    assert w.emulator.serial_status(0).zustand == K.SER_VERBINDET
    assert b.knopf.text() == "Trennen" and b.knopf.isEnabled()      # auch während der Versuche
    assert b.zustand_label.text().startswith("verbindet")
    assert not b.host.isEnabled() and not b.port.isEnabled()
    for feld in (w.status_widget.seriell_server, w.status_widget.seriell_verbindungen):
        assert not sichtbar(feld, w.status_widget)
    b.knopf.click()                                    # Trennen beendet die Versuche sofort
    assert w.emulator.serial_status(0).zustand == K.SER_AUS
    assert b.knopf.text() == "Verbinden"


def test_a_client_connects_once_the_server_is_there(w, qapp):
    port = freier_port()
    b = client_einstellen(w.serial_widget.bloecke()[0], port)
    b.knopf.click()
    with socket.socket() as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(1)
        assert warte(qapp, lambda: w.emulator.serial_status(0).zustand == K.SER_VERBUNDEN,
                     frist=6.0)
        w.serial_widget.aktualisieren()
        verb = w.status_widget.seriell_verbindungen
        assert sichtbar(verb, w.status_widget)
        assert not sichtbar(w.status_widget.seriell_server, w.status_widget)   # Client
        assert b.knopf.text() == "Trennen"
        assert "→" in verb.text.toolTip()


def test_file_mode_asks_for_the_file_at_once_and_is_no_connection(w, qapp, tmp_path,
                                                                   monkeypatch):
    from PySide6.QtWidgets import QFileDialog
    ziel = tmp_path / "druck.txt"
    fragen = []
    monkeypatch.setattr(QFileDialog, "getSaveFileName",
                        staticmethod(lambda *a, **k: (fragen.append(a) or str(ziel), "")))
    b = frei(w.serial_widget.bloecke()[2])
    b.betriebsart.setCurrentIndex(2)                   # Datei
    assert len(fragen) == 1                            # sofort beim Umschalten
    assert b.datei.text() == str(ziel)
    # Rolle/Host/Port ausgeblendet, Dateiname + „…" statt dessen.
    for versteckt in (b.rolle, b.host, b.port):
        assert not sichtbar(versteckt, b)
    assert sichtbar(b.datei, b) and sichtbar(b.datei_knopf, b)
    assert b.knopf.text() == "Starten"
    b.knopf.click()
    assert w.emulator.serial_status(2).zustand == K.SER_VERBUNDEN      # der Kern meldet so
    assert ziel.exists()
    # … die Statuszeile filtert Datei aber heraus.
    assert w.serial_widget.statuszeilentexte() == ("", "", "", "")
    assert not b.datei_knopf.isEnabled() and not b.betriebsart.isEnabled()
    b.knopf.click()
    assert w.emulator.serial_status(2).zustand == K.SER_AUS


def test_cancelling_the_file_dialog_keeps_the_previous_mode(w, monkeypatch):
    from PySide6.QtWidgets import QFileDialog
    monkeypatch.setattr(QFileDialog, "getSaveFileName",
                        staticmethod(lambda *a, **k: ("", "")))
    b = frei(w.serial_widget.bloecke()[2])
    b.betriebsart.setCurrentIndex(2)
    assert b.betriebsart.currentIndex() == 0
    assert w.emulator.serial_config(2).betriebsart == K.SER_TELNET


def test_a_failing_start_shows_a_line_in_the_block_not_a_window(w, tmp_path, monkeypatch):
    """Das Netz aus ``conftest.py`` (kein unerwartetes Meldungsfenster) bleibt scharf."""
    from PySide6.QtWidgets import QFileDialog
    ziel = tmp_path / "gibt-es-nicht" / "aus.txt"
    monkeypatch.setattr(QFileDialog, "getSaveFileName",
                        staticmethod(lambda *a, **k: (str(ziel), "")))
    b = frei(w.serial_widget.bloecke()[2])
    b.betriebsart.setCurrentIndex(2)
    b.knopf.click()
    assert w.emulator.serial_status(2).zustand == K.SER_FEHLER
    assert sichtbar(b.meldung, b) and b.meldung.text()
    assert b.zustand_label.text() == "Fehler"
    assert "e0352b" in b.punkt.styleSheet()
    assert b.knopf.text() == "Starten"                 # Fehler ist nicht aktiv


def test_dot_colours_follow_the_state(w, qapp):
    b = server_einstellen(w.serial_widget.bloecke()[0])
    assert "8a8a8a" in b.punkt.styleSheet()            # aus: grau
    b.knopf.click()
    assert "e0b020" in b.punkt.styleSheet()            # lauscht: gelb
    port = w.emulator.serial_status(0).port_aktiv
    with socket.create_connection(("127.0.0.1", port), timeout=3):
        assert warte(qapp, lambda: (w.serial_widget.aktualisieren(),
                                    "35c43a" in b.punkt.styleSheet())[1])   # verbunden: grün


def test_setting_the_loop_ends_a_running_connection(w, qapp):
    b = server_einstellen(w.serial_widget.bloecke()[0])
    b.knopf.click()
    assert w.emulator.serial_status(0).zustand == K.SER_LAUSCHT
    b.loop.setChecked(True)
    assert w.emulator.serial_status(0).zustand == K.SER_AUS
    assert not b.knopf.isEnabled()


def test_the_timer_runs_at_about_four_hertz(w):
    assert 200 <= w.serial_widget._timer.interval() <= 300 and w.serial_widget._timer.isActive()


def test_the_blocks_follow_changes_made_in_the_core(w, qapp):
    """Quelle der Wahrheit ist der Kern: was dort geändert wird, zeigt der nächste Takt."""
    assert w.emulator.serial_configure(0, xonxoff=True, host="kern.example", loop=False)
    w.serial_widget.aktualisieren()
    b = w.serial_widget.bloecke()[0]
    assert b.xonxoff.isChecked() and b.host.text() == "kern.example" and not b.loop.isChecked()


# ─── Konfiguration und Wiederaufnahme (§7.4a) ────────────────────────────────

def test_the_config_carries_a_section_per_core_name(w):
    b = server_einstellen(w.serial_widget.bloecke()[0], 41000)
    b.xonxoff.setChecked(True)
    abschnitt = w._gather_config()["schnittstellen"]
    assert list(abschnitt) == [w.emulator.serial_info(i).name for i in range(3)]
    erster = abschnitt[w.emulator.serial_info(0).name]
    assert (erster["betriebsart"], erster["rolle"], erster["port"], erster["xonxoff"],
            erster["loop"], erster["aktiv"]) == ("telnet", "server", 41000, True, False, False)
    assert "taktquelle" in erster or not w.emulator.serial_info(0).taktquellen
    for i in range(3):
        if w.emulator.serial_info(i).taktquellen:
            assert (abschnitt[w.emulator.serial_info(i).name]["taktquelle"]
                    in w.emulator.serial_info(i).taktquellen)
    yaml.safe_dump(abschnitt, allow_unicode=True)      # serialisierbar


def test_the_shipped_defaults_carry_no_interface_section(maschine):
    from app import config_io, profil
    assert "schnittstellen" not in config_io.standard_konfiguration(profil.profil(maschine))


def test_resume_a_server_on_its_set_port(w, qapp):
    name = w.emulator.serial_info(0).name
    port = freier_port()
    w._apply_config({"schnittstellen": {name: dict(
        betriebsart="telnet", rolle="server", port=port, loop=False, aktiv=True)}})
    st = w.emulator.serial_status(0)
    assert st.zustand == K.SER_LAUSCHT and st.port_aktiv == port
    assert w.serial_widget.block(name).knopf.text() == "Beenden"
    # Die Statuszeile trägt den tatsächlichen Port.
    assert w.status_widget.seriell_server.text.text().endswith(str(port))


def test_resume_with_a_busy_port_does_not_start_and_proposes_another(w, qapp):
    name = w.emulator.serial_info(0).name
    with socket.socket() as belegt:
        belegt.bind(("", 0))
        belegt.listen(1)
        port = belegt.getsockname()[1]
        w._apply_config({"schnittstellen": {name: dict(
            betriebsart="telnet", rolle="server", port=port, loop=False, aktiv=True)}})
        st = w.emulator.serial_status(0)
        b = w.serial_widget.block(name)
        assert st.zustand == K.SER_AUS
        assert st.port_vorschlag not in (0, port)
        assert b.port.value() == st.port_vorschlag             # Vorschlag steht im Feld
        assert w.emulator.serial_config(0).port == st.port_vorschlag
        assert sichtbar(b.meldung, b) and "belegt" in b.meldung.text()
        assert b.knopf.text() == "Starten"
        assert not sichtbar(w.status_widget.seriell_server, w.status_widget)


def test_resume_a_client_goes_into_the_endless_attempt(w, qapp):
    name = w.emulator.serial_info(1).name
    w._apply_config({"schnittstellen": {name: dict(
        betriebsart="rfc2217", rolle="client", host="127.0.0.1", port=freier_port(),
        loop=False, aktiv=True)}})
    assert warte(qapp, lambda: w.emulator.serial_status(1).versuche >= 1)
    w.serial_widget.aktualisieren()
    assert w.emulator.serial_status(1).zustand == K.SER_VERBINDET
    assert w.serial_widget.block(name).knopf.text() == "Trennen"


def test_resume_a_file_appends(w, tmp_path):
    name = w.emulator.serial_info(2).name
    ziel = tmp_path / "druck.txt"
    ziel.write_bytes(b"alt\n")
    w._apply_config({"schnittstellen": {name: dict(
        betriebsart="datei", datei=str(ziel), loop=False, aktiv=True)}})
    assert w.emulator.serial_status(2).zustand == K.SER_VERBUNDEN
    assert ziel.read_bytes() == b"alt\n"                       # nicht überschrieben
    w.emulator.serial_stop(2)
    assert ziel.read_bytes() == b"alt\n"
    # Von Hand gestartet überschreibt dagegen (Gegenprobe für die Unterscheidung).
    assert w.emulator.serial_start(2)
    w.emulator.serial_stop(2)
    assert ziel.read_bytes() == b""


def test_resume_does_not_start_with_the_loop_set(w):
    name = w.emulator.serial_info(0).name
    w._apply_config({"schnittstellen": {name: dict(
        betriebsart="telnet", rolle="server", port=freier_port(), loop=True, aktiv=True)}})
    assert w.emulator.serial_status(0).zustand == K.SER_AUS
    assert w.serial_widget.block(name).loop.isChecked()


def test_a_missing_section_leaves_everything_alone(w):
    b = server_einstellen(w.serial_widget.bloecke()[0])
    b.knopf.click()
    port = w.emulator.serial_status(0).port_aktiv
    w._apply_config({"general": {"speed": 1.0}})               # kein `schnittstellen`
    st = w.emulator.serial_status(0)
    assert st.zustand == K.SER_LAUSCHT and st.port_aktiv == port
    # Ein LEERER Abschnitt fasst gleichfalls nichts an (keine Namen darin).
    w._apply_config({"schnittstellen": {}})
    assert w.emulator.serial_status(0).zustand == K.SER_LAUSCHT


def test_unusable_values_are_skipped_not_fatal(w):
    name = w.emulator.serial_info(0).name
    vorher = w.emulator.serial_config(0)
    w._apply_config({"schnittstellen": {
        name: dict(betriebsart="gibtsnicht", rolle=7, port=0, host=5, loop="ja",
                   taktquelle="nirgends", aktiv="vielleicht"),
        "Gibt es nicht": dict(aktiv=True), "kaputt": 42}})
    assert w.emulator.serial_config(0) == vorher
    assert w.emulator.serial_status(0).zustand == K.SER_AUS
    w._apply_config({"schnittstellen": [1, 2]})                # falsche Form: übergangen


def test_closing_saves_the_state_and_the_next_start_resumes(qapp, konfig, maschine):
    from app import config_io, profil
    w1 = _fenster(qapp, maschine)
    name = w1.emulator.serial_info(0).name
    port = freier_port()
    server_einstellen(w1.serial_widget.bloecke()[0], port).knopf.click()
    assert w1.emulator.serial_status(0).zustand == K.SER_LAUSCHT
    _zu(qapp, w1)
    daten = config_io.load_config(config_io.default_config_path(profil.profil(maschine)))
    assert daten["schnittstellen"][name]["aktiv"] is True
    assert daten["schnittstellen"][name]["port"] == port
    # Das Beenden hat den Port freigegeben …
    with socket.socket() as s:
        s.bind(("", port))
    # … und der nächste Start nimmt den Server auf demselben Port wieder auf.
    w2 = _fenster(qapp, maschine)
    try:
        st = w2.emulator.serial_status(0)
        assert st.zustand == K.SER_LAUSCHT and st.port_aktiv == port
        w2.serial_widget.aktualisieren()
        assert w2.serial_widget.block(name).knopf.text() == "Beenden"
    finally:
        _zu(qapp, w2)


def test_a_stopped_interface_is_saved_inactive(qapp, konfig, maschine):
    from app import config_io, profil
    w1 = _fenster(qapp, maschine)
    name = w1.emulator.serial_info(0).name
    b = server_einstellen(w1.serial_widget.bloecke()[0])
    b.knopf.click()
    b.knopf.click()                                    # Beenden
    _zu(qapp, w1)
    daten = config_io.load_config(config_io.default_config_path(profil.profil(maschine)))
    assert daten["schnittstellen"][name]["aktiv"] is False


def test_changing_the_drive_bay_keeps_a_running_server(w, qapp):
    """Neue Laufwerke = neue Maschine (neuer Hub): die Verbindung wird wieder aufgenommen."""
    from app import drive_types as dt
    b = server_einstellen(w.serial_widget.bloecke()[0])
    b.knopf.click()
    port = w.emulator.serial_status(0).port_aktiv
    alt = w.emulator
    types = list(w._drive_types)
    # Den letzten Schacht umstellen (bestückt ↔ leer) — ändert die Bestückung sicher.
    types[-1] = dt.NO_DRIVE if dt.is_present(types[-1]) else dt.default_drive_types(w.maschine)[0]
    w._apply_drive_types(types, cold_restart=False)
    assert w.emulator is not alt
    assert w.serial_widget.emulator is w.emulator
    st = w.emulator.serial_status(0)
    assert st.zustand == K.SER_LAUSCHT and st.port_aktiv == port
    assert w.serial_widget.block(w.emulator.serial_info(0).name).knopf.text() == "Beenden"


def test_closing_ends_every_interface(qapp, konfig, maschine):
    w1 = _fenster(qapp, maschine)
    port = freier_port()
    server_einstellen(w1.serial_widget.bloecke()[0], port).knopf.click()
    emu = w1.emulator
    w1.close()
    assert emu.serial_status(0).zustand == K.SER_AUS
    assert not w1.serial_widget._timer.isActive()
    _zu(qapp, w1)


# ─── Ein Gast sendet über die Schnittstelle (Rauchprobe) ─────────────────────

def test_bytes_over_a_real_socket_do_not_disturb_a_running_machine(w, qapp):
    b = server_einstellen(w.serial_widget.bloecke()[0])
    b.knopf.click()
    port = w.emulator.serial_status(0).port_aktiv
    with socket.create_connection(("127.0.0.1", port), timeout=3) as c:
        c.sendall(b"Hallo\r\n")
        for _ in range(5):
            w._run_emulator()
            qapp.processEvents()
        w.serial_widget.aktualisieren()
        assert w.emulator.serial_status(0).zustand == K.SER_VERBUNDEN


# ─── Ergänzt in AP-T1b ───────────────────────────────────────────────────────

def _probe(w, b, **felder):
    """Einen Probestatus in den Block geben (der Takt wird dafür angehalten)."""
    import dataclasses
    w.serial_widget._timer.stop()
    echt = w.emulator.serial_status(b.index)
    st = dataclasses.replace(echt, **felder)
    b.aktualisieren(st)
    return st


def test_guest_format_uses_the_usual_notation_with_a_tooltip(w, qapp):
    b = w.serial_widget.bloecke()[0]
    gast = dict(format_gueltig=True, baud_nenn=1200, daten=7)
    _probe(w, b, paritaet=2, stopp_halbe=4, **gast)
    assert b.format_label.text() == "Gast 1200 Bd 7E2"
    _probe(w, b, paritaet=0, stopp_halbe=3, **gast)
    assert b.format_label.text() == "Gast 1200 Bd 7N1.5"      # Punkt, nicht Komma
    _probe(w, b, paritaet=1, stopp_halbe=2, daten=8, format_gueltig=True, baud_nenn=9600)
    assert b.format_label.text() == "Gast 9600 Bd 8O1"
    tipp = b.format_label.toolTip()
    for wort in ("8O1", "8 Datenbits", "ungerade", "N = keine", "E = gerade", "O = ungerade",
                 "M = ", "S = ", "Stoppbits"):
        assert wort in tipp, wort
    _probe(w, b, paritaet=9, stopp_halbe=0, **gast)
    assert b.format_label.text() == "Gast 1200 Bd 7??"
    _probe(w, b, format_gueltig=False)
    assert b.format_label.text() == "Gast nicht programmiert"
    assert "noch nicht programmiert" in b.format_label.toolTip()


def test_the_lines_are_leds_with_directions_and_tooltips(w, qapp):
    from app.ui.serial_widget import LeitungsLed
    # Telnet-Blöcke ohne V.24 haben keine Leitungsanzeige.
    for b in w.serial_widget.bloecke():
        assert sichtbar(b.leitungen, b) == b.info.v24
    b = next(x for x in w.serial_widget.bloecke() if x.info.v24)
    anz = b.leitungen_reihe.anzeigen
    assert list(anz) == ["RTS", "DTR", "CTS", "DSR", "DCD"]
    assert all(isinstance(a.led, LeitungsLed) for a in anz.values())
    # Beschriftung nennt die RICHTUNG, nicht den Zustand (AP-S12: „Aus:"/„Ein:" las
    # sich wie „ausgeschaltet"/„eingeschaltet").
    assert [t.text() for t in b.leitungen_reihe.titel] == ["Ausgänge →", "Eingänge ←"]
    assert "vom Rechner getrieben" in b.leitungen_reihe.titel[0].toolTip()
    assert "vom Rechner empfangen" in b.leitungen_reihe.titel[1].toolTip()
    _probe(w, b, rts=True, dtr=False, cts=True, dsr=False, dcd=True)
    assert b.leitungen_reihe.zustaende() == dict(RTS=True, DTR=False, CTS=True, DSR=False,
                                                 DCD=True)
    assert "CTS aktiv" in anz["CTS"].toolTip() and "Eingang" in anz["CTS"].toolTip()
    assert "inaktiv" in anz["DTR"].toolTip() and "Ausgang" in anz["DTR"].toolTip()
    # Die Leuchte zeichnet sich in verschiedenen Zuständen verschieden (Pixelprobe).
    def pixel(a):
        bild = a.led.grab().toImage()
        return bild.pixelColor(bild.width() // 2, bild.height() // 2).name()
    an, aus = pixel(anz["CTS"]), pixel(anz["DSR"])
    assert an != aus
    anz["DSR"].setze(None)
    assert anz["DSR"].zustand is None and "unbekannt" in anz["DSR"].toolTip()
    assert pixel(anz["DSR"]) not in (an, aus)               # Umriss: Mitte unausgefüllt
    # Telnet: Vermerk „nicht übertragen“, bei RFC2217 weg.
    assert w.emulator.serial_configure(b.index, betriebsart=K.SER_TELNET)
    b.aktualisieren()
    assert sichtbar(b.leitungen_hinweis, b)
    assert w.emulator.serial_configure(b.index, betriebsart=K.SER_RFC2217)
    b.aktualisieren()
    assert not sichtbar(b.leitungen_hinweis, b)



# ─── LEDs: drei unterscheidbare Zustände, Ende-zu-Ende grün (AP-S12) ──────────

def _led_mitte(led):
    """Farbe in der Mitte der LED (ohne den Glanzpunkt oben links)."""
    bild = led.grab().toImage()
    return bild.pixelColor(bild.width() // 2 + 2, bild.height() // 2 + 2)


def test_led_states_are_bright_green_dim_green_and_outline(qapp):
    """Pixelprobe: aktiv = hell leuchtend grün, inaktiv = gedimmt dunkelgrün und
    deutlich NICHT schwarz, unbekannt = nur Umriss (Mitte = Hintergrund)."""
    from PySide6.QtWidgets import QWidget
    from app.ui.serial_widget import LeitungsLed
    traeger = QWidget()
    led = LeitungsLed(traeger)
    hintergrund = _led_mitte(led)                  # vor dem ersten Setzen: Umriss

    led.setze(True)
    an = _led_mitte(led)
    led.setze(False)
    aus = _led_mitte(led)
    led.setze(None)
    unbekannt = _led_mitte(led)

    # aktiv: grün dominiert und ist hell
    assert an.green() > 200 and an.green() > an.red() + 80 and an.green() > an.blue() + 80
    # inaktiv: erkennbar grün (Farbton), aber gedimmt — nicht schwarz, nicht hell
    assert aus.green() > aus.red() + 30 and aus.green() > aus.blue() + 30, aus.name()
    assert 70 <= aus.green() <= 150, aus.name()
    assert an.green() - aus.green() >= 80
    # unbekannt: Mitte unausgefüllt
    assert unbekannt == hintergrund
    assert len({an.name(), aus.name(), unbekannt.name()}) == 3


def test_connecting_turns_the_input_leds_green_end_to_end(w, qapp):
    """Ein Telnet-Client verbindet sich mit dem Server der V.24-Schnittstelle: die
    Eingänge (CTS/DSR/DCD) werden im laufenden Rechner aktiv (§6.4: Telnet =
    „verbunden") und der Kasten zieht sie im 4-Hz-Takt nach — ohne Probestatus.
    Die Ausgänge bleiben aus: CP/A/SCPX setzen RTS/DTR nie."""
    b = next(x for x in w.serial_widget.bloecke() if x.info.v24)
    server_einstellen(b)
    b.knopf.click()
    assert warte(qapp, lambda: w.emulator.serial_status(b.index).zustand == K.SER_LAUSCHT)
    eing = ("CTS", "DSR", "DCD")
    assert warte(qapp, lambda: all(
        b.leitungen_reihe.anzeigen[n].zustand is False for n in eing))
    port = w.emulator.serial_status(b.index).port_aktiv
    with socket.create_connection(("127.0.0.1", port), timeout=3):
        assert warte(qapp, lambda: all(
            b.leitungen_reihe.anzeigen[n].zustand is True for n in eing), frist=6.0), \
            b.leitungen_reihe.zustaende()
        led = b.leitungen_reihe.anzeigen["CTS"].led
        assert _led_mitte(led).green() > 200
        z = b.leitungen_reihe.zustaende()
        assert z["RTS"] is False and z["DTR"] is False
    assert warte(qapp, lambda: all(
        b.leitungen_reihe.anzeigen[n].zustand is False for n in eing), frist=6.0)
    b.knopf.click()


def test_k8915_old_interface_names_in_a_config_still_apply(qapp, konfig):
    """Vor AP-S12 hießen SIO1-B/SIO2-A „IFS 1"/„IFS 2"; eine solche Konfiguration
    wird beim Laden auf die heutigen Namen abgebildet, statt herrenlos zu bleiben."""
    from app import profil
    p = profil.profil("k8915")
    alt = {"IFS 1": {"port": 4711}, "IFS 2": {"port": 4712}, "V.24": {"port": 4713}}
    neu = p.schnittstellen_umbenennen(alt)
    assert neu == {"Drucker/IFSS1": {"port": 4711}, "DFÜ/IFSS2": {"port": 4712},
                   "V.24": {"port": 4713}}
    # Der heutige Name geht vor, wenn beide da sind.
    assert p.schnittstellen_umbenennen({"IFS 1": {"port": 1}, "Drucker/IFSS1": {"port": 2}}) \
        == {"Drucker/IFSS1": {"port": 2}}
    assert profil.profil("a5120").schnittstellen_umbenennen(alt) == alt
    w = _fenster(qapp, "k8915")
    try:
        w.serial_widget.zustand_anwenden(p.schnittstellen_umbenennen(alt))
        namen = {b.info.name: b.index for b in w.serial_widget.bloecke()}
        assert w.emulator.serial_config(namen["Drucker/IFSS1"]).port == 4711
        assert w.emulator.serial_config(namen["DFÜ/IFSS2"]).port == 4712
    finally:
        _zu(qapp, w)

def test_the_port_field_shows_the_port_in_use_and_the_set_one_after_stopping(w, qapp):
    """Belegter eingestellter Port: der Server nimmt einen anderen, das Feld zeigt ihn;
    gespeichert wird der eingestellte."""
    b = frei(w.serial_widget.bloecke()[0])
    b.betriebsart.setCurrentIndex(0)
    b.rolle.setCurrentIndex(0)
    with socket.socket() as belegt:
        belegt.bind(("", 0))
        belegt.listen(1)
        eingestellt = belegt.getsockname()[1]
        b.port.setValue(eingestellt)
        b.knopf.click()
        st = w.emulator.serial_status(0)
        if st.zustand != K.SER_LAUSCHT:
            pytest.skip("der Kern nimmt bei belegtem Port keinen anderen (§7.2)")
        assert st.port_aktiv != eingestellt
        assert b.port.value() == st.port_aktiv and not b.port.isEnabled()
        assert str(eingestellt) in b.port.toolTip()
        assert w._gather_config()["schnittstellen"][w.emulator.serial_info(0).name]["port"] \
            == eingestellt
        b.knopf.click()                                    # Beenden
        assert b.port.value() == eingestellt and b.port.isEnabled()
        assert b.port.toolTip() == ""


def test_the_far_side_shows_format_and_lines_with_a_warning(w, qapp):
    """Echter Loopback: ein roher Client verhandelt als RFC2217-Gegenseite des Servers."""
    from app.ui import serial_widget as SW
    b = frei(w.serial_widget.bloecke()[0])
    b.betriebsart.setCurrentIndex(1)                       # RFC2217
    b.rolle.setCurrentIndex(0)
    b.port.setValue(freier_port())
    b.knopf.click()
    port = w.emulator.serial_status(0).port_aktiv

    def sb(*d):
        return b"\xff\xfa\x2c" + bytes(d) + b"\xff\xf0"
    with socket.create_connection(("127.0.0.1", port), timeout=3) as c:
        assert warte(qapp, lambda: w.emulator.serial_status(0).zustand == K.SER_VERBUNDEN)
        b.aktualisieren()
        assert not sichtbar(b.gegenseite_zeile, b)         # noch nichts bekannt
        c.sendall(b"\xff\xfb\x2c" + sb(2, 7) + sb(3, 3) + sb(4, 1) + sb(5, 11) + sb(5, 9))
        assert warte(qapp, lambda: w.emulator.serial_status(0).leitungen_gegenseite_bekannt == 3
                     and w.emulator.serial_status(0).format_gegenseite_bekannt)
        w.serial_widget.aktualisieren()
        assert sichtbar(b.gegenseite_zeile, b)
        assert "7E1" in b.gegenseite.text()
        st = w.emulator.serial_status(0)
        assert st.format_abweichend                         # Gast steht auf 8N1
        assert b.gegenseite.text().endswith("⚠")
        assert SW.FARBE_WARNUNG in b.gegenseite.styleSheet()
        assert "anderes Format" in b.gegenseite.toolTip()
        assert "Server" in b.gegenseite.toolTip() and "Client" in b.gegenseite.toolTip()
        if b.info.v24:
            # Server sieht RTS/DTR des Clients; CTS/DSR/DCD/RI sind für ihn ohne Belang.
            z = b.gegen_leitungen.anzeigen
            assert z["RTS"].zustand is True and z["DTR"].zustand is False
            assert not z["RTS"].isHidden() and not z["DTR"].isHidden()
            assert z["CTS"].isHidden() and z["RI"].isHidden()


def test_the_far_side_leds_as_client_and_unknown_as_outline(w, qapp):
    b = next(x for x in w.serial_widget.bloecke() if x.info.v24)
    _probe(w, b, rolle=K.SER_CLIENT, betriebsart=K.SER_RFC2217,
           baud_gegenseite=9600, baud_abweichend=False, format_gegenseite_bekannt=True,
           daten_gegenseite=8, paritaet_gegenseite=0, stopp_halbe_gegenseite=2,
           format_abweichend=False,
           leitungen_gegenseite=K.SER_L_CTS, leitungen_gegenseite_bekannt=K.SER_L_CTS | K.SER_L_DSR)
    assert b.gegenseite.text() == "Gegenseite 9600 Bd 8N1"
    assert b.gegenseite.styleSheet() == ""
    z = b.gegen_leitungen.anzeigen
    assert (z["CTS"].zustand, z["DSR"].zustand, z["DCD"].zustand, z["RI"].zustand) == \
        (True, False, None, None)                           # unbekannt = Umriss
    assert z["RTS"].isHidden() and not z["RI"].isHidden()
    # Nur Baud bekannt: Text ohne Format, Leitungen alle unbekannt.
    _probe(w, b, rolle=K.SER_CLIENT, baud_gegenseite=1200, baud_abweichend=True,
           format_gegenseite_bekannt=False, format_abweichend=False,
           leitungen_gegenseite_bekannt=0)
    assert b.gegenseite.text() == "Gegenseite 1200 Bd ⚠"
    assert "andere Baudrate" in b.gegenseite.toolTip()
    assert all(a.zustand is None for a in z.values())
    _probe(w, b, baud_gegenseite=0, format_gegenseite_bekannt=False,
           leitungen_gegenseite_bekannt=0)
    assert not sichtbar(b.gegenseite_zeile, b)


def test_a_setting_the_core_refuses_says_so_in_the_block(w, qapp, monkeypatch):
    b = frei(w.serial_widget.bloecke()[0])
    monkeypatch.setattr(b.emulator, "serial_configure", lambda i, **f: False)
    b.xonxoff.setChecked(not b.xonxoff.isChecked())
    assert sichtbar(b.meldung, b)
    assert "nicht übernommen" in b.meldung.text()
    # Die Anzeige folgt dem Kern, nicht dem Klick.
    assert b.xonxoff.isChecked() == w.emulator.serial_config(b.index).xonxoff


def test_changing_the_drive_bay_drops_a_connected_peer_and_listens_again(w, qapp):
    """Maschinenwechsel mit VERBUNDENER Gegenseite: die Verbindung endet (Kabel ab),
    der Server lauscht an der neuen Maschine auf demselben Port, ein neuer Client
    wird bedient und erscheint in der Statuszeile."""
    from app import drive_types as dt
    b = server_einstellen(w.serial_widget.bloecke()[0])
    b.knopf.click()
    port = w.emulator.serial_status(0).port_aktiv
    alt = socket.create_connection(("127.0.0.1", port), timeout=3)
    assert warte(qapp, lambda: w.emulator.serial_status(0).zustand == K.SER_VERBUNDEN)
    types = list(w._drive_types)
    types[-1] = dt.NO_DRIVE if dt.is_present(types[-1]) else dt.default_drive_types(w.maschine)[0]
    w._apply_drive_types(types, cold_restart=False)

    alt.settimeout(3)
    rest = b""
    try:
        while True:                          # Telnet-Verhandlung, dann EOF
            stueck = alt.recv(4096)
            if not stueck:
                break
            rest += stueck
    except (ConnectionResetError, socket.timeout) as e:
        pytest.fail(f"alte Verbindung nicht sauber beendet: {e!r}")
    alt.close()
    st = w.emulator.serial_status(0)
    assert st.zustand == K.SER_LAUSCHT and st.port_aktiv == port
    with socket.create_connection(("127.0.0.1", port), timeout=3):
        assert warte(qapp, lambda: w.emulator.serial_status(0).zustand == K.SER_VERBUNDEN)
        name = w.emulator.serial_info(0).name
        assert warte(qapp, lambda: f"{name} verbunden" in w.serial_widget.statuszeilentexte()[2])


def test_changing_the_drive_bay_keeps_a_trying_client_trying(w, qapp):
    from app import drive_types as dt
    b = client_einstellen(w.serial_widget.bloecke()[0], freier_port())
    b.knopf.click()
    assert w.emulator.serial_status(0).zustand == K.SER_VERBINDET
    types = list(w._drive_types)
    types[-1] = dt.NO_DRIVE if dt.is_present(types[-1]) else dt.default_drive_types(w.maschine)[0]
    w._apply_drive_types(types, cold_restart=False)
    assert w.emulator.serial_status(0).zustand == K.SER_VERBINDET
    assert w.serial_widget.bloecke()[0].knopf.text() == "Trennen"
    assert w.serial_widget.statuszeilentexte()[2] == ""


def test_a_widget_without_a_machine_says_there_is_nothing_to_set(qapp):
    from app.ui.serial_widget import SerialWidget
    from PySide6.QtWidgets import QLabel
    sw = SerialWidget(None)
    assert sw.bloecke() == []
    assert any("keine einstellbaren" in l.text() for l in sw.findChildren(QLabel))
    sw.alles_beenden()                       # ohne Maschine: nichts zu tun, kein Fehler
    sw.beenden()
    assert sw.statuszeilentexte() == ("", "", "", "")
    assert sw.block("gibt es nicht") is None


def test_a_clock_source_given_as_number_is_taken_and_a_wrong_one_skipped(w, qapp):
    b = next((x for x in w.serial_widget.bloecke() if len(x.info.taktquellen) > 1), None)
    if b is None:
        pytest.skip("diese Maschine hat keine wählbare Taktquelle")
    b.konfig_anwenden({"taktquelle": 1})
    assert w.emulator.serial_config(b.index).taktquelle == 1
    b.konfig_anwenden({"taktquelle": 99})
    assert w.emulator.serial_config(b.index).taktquelle == 1
    b.konfig_anwenden({"taktquelle": True})   # bool ist keine Zahl im Sinne der Datei
    assert w.emulator.serial_config(b.index).taktquelle == 1
