"""Oberflächen der physischen Diskette — Emulator und k1520DiskTool, **ohne Hardware**.

Geprüft wird die Verdrahtung, nicht das Aussehen: dass der Weg „Knopf → Sitzung →
angemeldete Diskette → Anzeige → sauberes Abmelden" in beiden Programmen wirklich
zusammenhängt.  Das echte Laufwerk vertritt dabei das Ersatzlaufwerk aus
:mod:`gw_fake` (eine ``.hfe``-Aufnahme).

Siehe doc/design/14_physische_diskette.md §12.
"""

from __future__ import annotations

import time

import pytest

from gw_fake import fake_session

pytest.importorskip("PySide6")

FIXTURE = "udos_boot_scp.hfe"


@pytest.fixture
def hfe(fixture_disks):
    return fixture_disks / FIXTURE


@pytest.fixture
def app():
    from PySide6.QtWidgets import QApplication
    a = QApplication.instance() or QApplication([])
    yield a


def _warte(pred, frist=30.0):
    ende = time.monotonic() + frist
    while time.monotonic() < ende and not pred():
        time.sleep(0.05)
    return pred()


# ════════════════════════════════════════════════════════════════════════════
# Gemeinsames Stück: Auswahl und Sitzung
# ════════════════════════════════════════════════════════════════════════════


def test_auswahl_liefert_genau_die_argumente_der_sitzung(app):
    """Was der Dialog ausspuckt, muss ``PhysicalSession.start`` schlucken."""
    import inspect

    from app.ui.physical_disk import PhysicalDiskDialog, PhysicalSession

    d = PhysicalDiskDialog(num_cyls=40, num_heads=1, drive_label="K5600.10")
    wahl = d.auswahl()
    erlaubt = set(inspect.signature(PhysicalSession.start).parameters) - {"cls"}
    assert set(wahl) <= erlaubt, f"unbekannte Argumente: {set(wahl) - erlaubt}"
    assert wahl["num_cyls"] == 40 and wahl["num_heads"] == 1
    assert wahl["writable"] is False, "Schreiben darf nicht die Vorgabe sein"


def test_disktool_oeffnet_mit_der_vollen_dialogauswahl(app, hfe, monkeypatch):
    """Der Weg, den der Bediener geht: Dialogauswahl **unverändert** ins Fenster.

    Die Tests riefen ``open_physical`` bisher mit einer Handvoll Argumente auf und
    sahen deshalb nicht, dass die Auswahl inzwischen mehr enthält, als das Fenster
    entgegennimmt (``verify_writes`` → ``TypeError`` beim Klick auf „Einlegen").
    """
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    spuren = sitzung.sync.stats.tracks_total // 2
    wahl = physical_disk.PhysicalDiskDialog(num_cyls=spuren, num_heads=2).auswahl()

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(**wahl), "die eigene Auswahl wurde nicht angenommen"
        assert fenster.tool is not None and fenster.tool.list()
    finally:
        fenster._close_tool()


def test_beide_programme_nehmen_die_ganze_auswahl_entgegen(app):
    """Die Auswahl muss auch durch die Fenster passen, nicht nur in die Sitzung.

    Der Emulator reicht sie mit ``**wahl`` durch und wächst dadurch von selbst mit.
    Das DiskTool zählte die Argumente einmal einzeln auf — und als ``verify_writes``
    zum Dialog kam, warf der Klick auf „Einlegen" ein ``TypeError``.  Beide Wege
    werden deshalb hier gegen denselben Dialog gehalten.
    """
    import inspect

    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui.physical_disk import PhysicalDiskDialog

    wahl = PhysicalDiskDialog(num_cyls=80, num_heads=2).auswahl()
    sig = inspect.signature(DiskToolWindow.open_physical)
    if not any(p.kind is p.VAR_KEYWORD for p in sig.parameters.values()):
        fehlend = set(wahl) - set(sig.parameters)
        assert fehlend == set(), f"open_physical nimmt nicht entgegen: {fehlend}"
    # Und der Beweis am lebenden Objekt: die Bindung muss gelingen.
    sig.bind(None, **wahl)


def test_ohne_paket_bleibt_es_bei_einem_satz(app, monkeypatch):
    """Fehlt ``greaseweazle``, gibt es einen Grund — keinen Absturz."""
    import app.gw as gw
    from app.ui import physical_disk

    monkeypatch.setattr(gw, "verfuegbar", lambda: False)
    ok, grund = physical_disk.verfuegbarkeit()
    assert ok is False
    assert "greaseweazle" in grund and "pip install" in grund


def test_ohne_paket_kommt_der_grund_vor_dem_auswahldialog(app, monkeypatch):
    """Geht es nicht, wird gar nicht erst gefragt — und der Grund steht im Klartext.

    Zwei Fallen, in die diese Stelle schon getappt ist:

    * Ein **gesperrter** Menüpunkt hilft nicht: ``QMenu`` zeigt Tooltips gesperrter
      Einträge von Haus aus nicht an (``setToolTipsVisible`` ist aus), man sähe nur
      Grau ohne Erklärung.  Er bleibt deshalb auslösbar.
    * Prüft erst ``open_physical`` (also hinter dem Auswahldialog), füllt der
      Bediener Laufwerk und Geometrie aus, klickt „Einlegen" — und bekommt dann
      erst zu hören, dass es von vornherein nicht gehen konnte.
    """
    import app.gw as gw
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    monkeypatch.setattr(gw, "verfuegbar", lambda: False)
    gemeldet = []
    monkeypatch.setattr("app.disktool.ui.main_window.QMessageBox.critical",
                        lambda parent, titel, text, *a, **k: gemeldet.append(text))
    gefragt = []
    monkeypatch.setattr(physical_disk.PhysicalDiskDialog, "frage",
                        classmethod(lambda cls, parent=None, **kw: gefragt.append(kw)))

    fenster = DiskToolWindow()
    try:
        assert fenster.act_physisch.isEnabled(), "gesperrt = Grund unsichtbar"
        assert "fehlt" not in fenster.act_physisch.text(), \
            "der Menütext darf keine Ursache raten — es gibt zwei"
        fenster.act_physisch.trigger()
        assert gefragt == [], "es wurde gefragt, obwohl es nicht gehen kann"
        assert gemeldet and "pip install" in gemeldet[0], \
            "der Grund muss vor dem Auswahldialog kommen"
    finally:
        fenster._close_tool()


def test_sitzung_meldet_den_fuellstand(app, hfe):
    sitzung = fake_session(hfe, read_ahead=True)
    try:
        assert _warte(lambda: (sitzung.stats().tracks_known or 0) > 3)
        text = sitzung.status_text()
        assert "von" in text and "Spuren gelesen" in text
    finally:
        sitzung.close()


# ════════════════════════════════════════════════════════════════════════════
# Emulator
# ════════════════════════════════════════════════════════════════════════════


def test_emulator_legt_eine_physische_diskette_ein_und_zeigt_sie_an(app, hfe,
                                                                    monkeypatch):
    from app.core_binding.k1520 import K1520Emulator
    from app.ui import physical_disk
    from app.ui.drive_widget import DriveWidget

    sitzung = fake_session(hfe, read_ahead=True, for_emulator=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))
    monkeypatch.setattr(physical_disk.PhysicalDiskDialog, "frage",
                        classmethod(lambda cls, parent=None, **kw: {
                            "drive": "a", "cell_rate_kbps": 250, "num_cyls": 80,
                            "num_heads": 2, "writable": False, "read_ahead": True}))

    emu = K1520Emulator()
    w = DriveWidget(emu)
    panel = w._panels[0]
    panel._phys_btn.click()

    assert 0 in w._physical, "die Sitzung wurde nicht gemerkt"
    assert "Greaseweazle" in panel._path_display.text()
    assert panel._toggle_btn.text() == "Auswerfen"
    assert not panel._phys_btn.isEnabled(), "zweimal einlegen darf nicht gehen"

    w._update_physical_status(0)
    assert "Spuren gelesen" in panel._phys_label.text()
    # isVisible() waere hier immer False — das Fenster wird nie gezeigt.
    assert not panel._phys_label.isHidden()

    # Auswerfen beendet die Sitzung UND hängt aus.
    panel._toggle_btn.click()
    assert 0 not in w._physical
    assert panel._toggle_btn.text() == "Mount"
    assert panel._phys_btn.isEnabled()
    assert panel._phys_label.isHidden()


def test_emulator_merkt_sich_kein_physisches_laufwerk_in_der_konfiguration(
        app, hfe, monkeypatch):
    """Eine physische Diskette hat keinen Pfad — sie darf nicht in die Config.

    Sonst stünde beim nächsten Start ein Eintrag da, den niemand einlösen kann
    (das Sync-Handle ist nach dem Einlegen verbraucht).
    """
    from app.core_binding.k1520 import K1520Emulator
    from app.ui import physical_disk
    from app.ui.drive_widget import DriveWidget

    sitzung = fake_session(hfe, for_emulator=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))
    monkeypatch.setattr(physical_disk.PhysicalDiskDialog, "frage",
                        classmethod(lambda cls, parent=None, **kw: {
                            "drive": "a", "cell_rate_kbps": 250, "num_cyls": 80,
                            "num_heads": 2, "writable": False, "read_ahead": False}))

    emu = K1520Emulator()
    w = DriveWidget(emu)
    w._panels[0]._phys_btn.click()
    try:
        assert w.get_mounts() == []
        w.remount_all()          # darf die Sitzung nicht anfassen
        assert 0 in w._physical
    finally:
        w.close_physical_sessions()
    assert w._physical == {}


# ════════════════════════════════════════════════════════════════════════════
# k1520DiskTool
# ════════════════════════════════════════════════════════════════════════════


def test_disktool_oeffnet_ueber_die_oberflaeche_und_zeigt_die_dateien(
        app, hfe, monkeypatch):
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a", num_cyls=sitzung.sync.stats.tracks_total // 2,
                                     num_heads=2), "physisches Öffnen scheiterte"
        assert fenster.tool is not None
        assert fenster.tool.filesystem
        assert fenster.tool.list(), "kein Verzeichnis gelesen"
        assert "Greaseweazle" in fenster.kopf.pfad.text()
        assert fenster.tool.read_only, "ohne Schreibrecht muss es schreibgeschützt sein"
    finally:
        fenster._close_tool()
    assert fenster._physisch is None, "die Sitzung wurde nicht beendet"


def test_disktool_fuehrt_den_fuellstand_nach(app, hfe, monkeypatch):
    """Die Statuszeile darf nicht auf dem Stand vom Öffnen stehenbleiben.

    Beim Vorauslesen wächst die Zahl gelesener Spuren weiter; ohne Zeitgeber blieb
    die Anzeige auf dem Wert des letzten `_reload` stehen (beobachtet: „59" bis zum
    Schluss).  Der Emulator führt seine Füllstandszeile aus demselben Grund nach.
    """
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a")
        assert fenster._physisch_uhr.isActive(), "der Zeitgeber läuft nicht"
        assert "Spuren gelesen" in fenster.st_physisch.text(), \
            "der Füllstand steht nicht sofort da"

        # Dem Vorauslesen ein paar Spuren Zeit geben, dann von Hand ticken
        # (der QTimer feuert ohne laufende Ereignisschleife nicht).
        stand = sitzung.stats().tracks_known
        _warte(lambda: sitzung.stats().tracks_known > stand, 5.0)
        fenster._physisch_tick()
        assert fenster.st_physisch.text(), "nach dem Tick ist die Anzeige leer"
        # Der Wert muss dem Sync folgen, nicht einem Stand von früher.
        assert fenster.st_physisch.text() == sitzung.status_text()
    finally:
        fenster._close_tool()
    assert not fenster._physisch_uhr.isActive(), "der Zeitgeber läuft weiter"
    assert fenster.st_physisch.isHidden(), "das Feld bleibt sichtbar"


def test_oberflaeche_laedt_nach_dem_oeffnen_keine_spur_mehr_nach(app, hfe, monkeypatch):
    """Nach dem Fortschrittsdialog darf der Oberflächenfaden nicht mehr warten.

    `_reload()` ruft `tool.list()`, und bei UDOS hängt an jedem Verzeichniseintrag
    ein Kopfsektor irgendwo auf der Diskette — zwei Dutzend einzeln nachzuladende
    Spuren.  Lief das im Oberflächenfaden, verarbeitete Qt so lange keine Ereignisse:
    Dateiliste leer, und ein Klick auf den Diskeditor wurde erst danach abgearbeitet
    (sah aus wie ein eingefrorenes Programm).  Deshalb holt der Arbeitsfaden das
    Verzeichnis gleich mit — und `_reload()` findet alles vor.
    """
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=False)   # nur was ausdrücklich geholt wird
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a")
        vorher = sitzung.stats().tracks_known
        fenster._reload()
        assert sitzung.stats().tracks_known == vorher, (
            "_reload() holt noch Spuren — das blockiert die Oberfläche "
            f"({sitzung.stats().tracks_known - vorher} Stück)")
        assert fenster.disk_view.selected_refs() is not None
    finally:
        fenster._close_tool()


def test_dateiliste_kommt_zweistufig(app, hfe, monkeypatch):
    """Namen sofort, Größe und Datum nachgetragen — wie `CAT` gegen `CAT F=L`.

    Bei UDOS steht im Verzeichnis nur Name und SECRET-Bit; Länge, Typ und Datum
    liegen im Kopfsektor jeder Datei, verstreut über die Diskette.  Alles vorab zu
    lesen kostete drei Dutzend Spuren, bevor der Bediener überhaupt etwas sah.
    """
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=False)   # nur was ausdrücklich geholt wird
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    fenster = DiskToolWindow()
    try:
        vor = sitzung.stats().tracks_known
        assert fenster.open_physical(drive="a")
        fuers_verzeichnis = sitzung.stats().tracks_known - vor

        eintraege = fenster._eintraege
        assert eintraege, "keine Dateien"
        # Die Namen sind da …
        assert all(e.name for e in eintraege)
        # … die teuren Angaben noch nicht (sonst wäre nichts gespart).
        assert any(not e.details_loaded for e in eintraege)
        assert fuers_verzeichnis < len(eintraege) // 2, (
            f"{fuers_verzeichnis} Spuren für {len(eintraege)} Dateien — "
            "es werden weiter alle Kopfsektoren gelesen")

        # In der Tabelle steht dann ein Strich, keine erfundene Null.
        offen = next(i for i, e in enumerate(eintraege) if not e.details_loaded)
        assert fenster.disk_view._zeilen[offen].text(2) == "…"

        # Nachgetragen wird nur, was ohne Warten zu haben ist …
        fenster._details_nachtragen()
        assert any(not e.details_loaded for e in fenster._eintraege), \
            "es wurde nachgeladen, statt dem Laufwerk zu folgen"

        # … und sobald die Spuren da sind, füllt sich die Tabelle.
        for kopf in range(sitzung.num_heads or 2):
            for c in range(sitzung.num_cyls or 80):
                fenster.tool.track(c, kopf)
        fenster._details_nachtragen()
        assert all(e.details_loaded for e in fenster._eintraege)
        gefuellt = fenster.disk_view._zeilen[offen]
        assert gefuellt.text(2) != "…", "die Zeile wurde nicht aufgefrischt"
        assert gefuellt.text(2) == str(fenster._eintraege[offen].size)
    finally:
        fenster._close_tool()


def test_befund_gilt_erst_der_stichprobe_und_wird_dann_ersetzt(app, hfe, monkeypatch):
    """Ein Befund aus acht Spuren ist kein Befund über die Diskette.

    Die Zählungen („2 Spuren hinter dem Format") sind Aussagen über die ANGESEHENEN
    Spuren; auf der Referenzdiskette sind es tatsächlich sechs.  Der Satz muss das
    sagen — und verschwinden, sobald die ganze Diskette gelesen ist.
    """
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=False)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a")
        assert fenster.tool.examined_tracks > 0, "keine Stichprobe — Fall nicht geprüft"
        stichprobe = fenster.tool.remarks
        assert "Stichprobe" in stichprobe, \
            f"der Vorbehalt fehlt, der Satz liest sich wie ein Befund: {stichprobe!r}"

        # Solange etwas fehlt, wird NICHT neu bewertet (das wäre die Nachladeorgie,
        # die die Stichprobe gerade vermeidet).
        fenster._befund_auffrischen()
        assert fenster.tool.remarks == stichprobe

        for kopf in range(fenster.tool.medium_heads):
            for c in range(fenster.tool.medium_cylinders):
                fenster.tool.track(c, kopf)
        fenster._befund_auffrischen()

        assert fenster.tool.examined_tracks == 0, "der Vorbehalt steht immer noch"
        assert "Stichprobe" not in fenster.tool.remarks
        assert "Stichprobe" not in fenster.info_bar.text()
        # Und die vollständige Messung sieht mehr als die Stichprobe.
        assert fenster.tool.remarks != stichprobe
    finally:
        fenster._close_tool()


def test_ohne_befund_verschwindet_der_streifen_wieder(app, monkeypatch, fixture_disks):
    """Eine unauffällige Diskette darf am Ende NICHTS mehr melden.

    Vorher blieb „an 8 Spuren erkannt (Stichprobe)" bis zum Schluss stehen — an einer
    längst vollständig gelesenen Diskette ein Vorbehalt, der nicht mehr gilt.
    """
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sauber = fixture_disks / "cpa_cpa780_k5601_noclock.hfe"
    if not sauber.exists():
        pytest.skip("Vergleichsdiskette fehlt")

    sitzung = fake_session(sauber, read_ahead=False)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a")
        assert not fenster.info_bar.isHidden(), "der Vorbehalt wird nicht angezeigt"
        for kopf in range(fenster.tool.medium_heads):
            for c in range(fenster.tool.medium_cylinders):
                fenster.tool.track(c, kopf)
        fenster._befund_auffrischen()
        assert fenster.tool.remarks == "", f"unerwarteter Befund: {fenster.tool.remarks!r}"
        assert fenster.info_bar.isHidden(), "der Streifen bleibt stehen"
    finally:
        fenster._close_tool()


def test_diskeditor_oeffnet_sofort_und_waechst_mit(app, hfe, monkeypatch):
    """Der Editor darf nicht auf die ganze Diskette warten.

    Er las beim Öffnen jede Spur über ``tool.track()`` — an einem echten Laufwerk
    heisst das: das Fenster steht anderthalb Minuten.  Jetzt zeigt er, was bekannt
    ist; ungelesene Spuren sind ``None`` (schwarz gezeichnet) und werden
    nachgetragen, sobald sie da sind.
    """
    from app.disktool.ui.disk_editor import DiskEditorWindow
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    # OHNE Vorauslesen: sonst ist beim Ersatzlaufwerk schon alles gelesen, bevor
    # der Editor aufgeht — und der Fall, um den es hier geht, käme nie vor.
    sitzung = fake_session(hfe, read_ahead=False)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a")
        editor = DiskEditorWindow(fenster.tool)
        try:
            unbekannt = sum(1 for seite in editor.surface.tracks
                            for t in seite if t is None)
            assert unbekannt > 0, "kein ungelesener Teil — der Fall wird nicht geprüft"
            assert editor._nachlauf.isActive(), "die Ansicht wird nicht nachgeführt"

            # Eine unbekannte Spur trägt keine Abschnitte, ist aber anklickbar:
            # der Tooltip sagt, dass sie noch nicht gelesen ist.
            seite, nr = next((h, c) for h in range(len(editor.surface.tracks))
                             for c, t in enumerate(editor.surface.tracks[h])
                             if t is None)
            assert "noch nicht gelesen" in editor.surface.beschreibung((seite, nr, None))

            # Auf Wunsch wird sie doch geholt — und ist danach da.
            editor._spur_anfordern(seite, nr)
            assert editor.surface.tracks[seite][nr] is not None

            # Was inzwischen anderswo gelesen wurde, kommt beim Nachtragen dazu.
            for kopf in range(len(editor.surface.tracks)):
                for c in range(len(editor.surface.tracks[kopf])):
                    fenster.tool.track(c, kopf)          # holt die Spur ins Medium
            editor._nachtragen()
            assert all(t is not None for seite in editor.surface.tracks
                       for t in seite), "nachgelesene Spuren fehlen in der Ansicht"
            assert not editor._nachlauf.isActive(), \
                "der Zeitgeber läuft weiter, obwohl nichts mehr fehlt"
        finally:
            editor.close()
    finally:
        fenster._close_tool()


def test_disktool_schliesst_die_sitzung_beim_naechsten_oeffnen(app, hfe, tmp_path,
                                                               monkeypatch):
    """Ein Abbild nach einer physischen Diskette darf das Laufwerk nicht halten."""
    import shutil

    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    kopie = tmp_path / "kopie.hfe"
    shutil.copy(hfe, kopie)

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a")
        assert fenster._physisch is sitzung
        assert fenster.open_image(kopie)
        assert fenster._physisch is None, "das echte Laufwerk blieb belegt"
    finally:
        fenster._close_tool()


# ════════════════════════════════════════════════════════════════════════════
# Schadstelle: Meldung und Ausweg
# ════════════════════════════════════════════════════════════════════════════


def _mit_schadstelle(hfe, **kw):
    """Sitzung, deren Diskette nichts mehr annimmt (alle Spuren schadhaft)."""
    sitzung = fake_session(hfe, writable=True, read_ahead=False, **kw)
    sitzung.device.schadhaft = {(c, h) for c in range(sitzung.device.num_cyls)
                                for h in range(sitzung.device.num_heads)}
    return sitzung


def test_disktool_meldet_die_schadstelle_beim_speichern(app, hfe, tmp_path,
                                                        monkeypatch):
    """Der ganze Weg durch die Oberfläche: schreiben → prüfen → Warnung → Ausweg.

    Die Diskette nimmt nichts mehr an; der Verify-Lauf des Gastsystems liefe hier
    gegen das Speicherabbild und sähe nichts.  Das Prüf-Lesen sieht es.
    """
    from app.disktool.ui.main_window import MainWindow as DiskToolWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, writable=True, read_ahead=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    # Warnungen abfangen statt anzeigen (headless gäbe es sonst ein modales Fenster).
    gemeldet = []
    monkeypatch.setattr("app.disktool.ui.main_window.QMessageBox.warning",
                        lambda parent, titel, text, *a, **k: gemeldet.append(text))
    monkeypatch.setattr("app.disktool.ui.main_window.QMessageBox.information",
                        lambda parent, titel, text, *a, **k: None)

    fenster = DiskToolWindow()
    try:
        assert fenster.open_physical(drive="a", writable=True)
        assert fenster.act_neu_beschreiben.isVisible(), "Ausweg fehlt"
        assert fenster.act_neu_beschreiben.isEnabled(), "Ausweg gesperrt"

        # Ab jetzt trägt die Diskette nicht mehr.
        sitzung.device.schadhaft = {(c, h) for c in range(sitzung.device.num_cyls)
                                    for h in range(sitzung.device.num_heads)}

        quelle = tmp_path / "PROBE.TXT"
        quelle.write_bytes(b"schadstelle\r\n" * 4)
        ziel = ("Side0/" if fenster.tool.volume_count > 1 else "") + "PROBE.TXT"
        fenster.tool.insert(quelle, ziel, overwrite=True)

        assert fenster.save() is False, "der Schaden wurde als Erfolg verbucht"
        assert gemeldet, "es wurde nicht gewarnt"
        assert "Spur" in gemeldet[0]
        assert "neu beschreiben" in gemeldet[0]
        assert sitzung.stats().tracks_defect > 0
    finally:
        fenster._close_tool()


def test_defektmeldung_erscheint_nur_einmal_je_spur(app, hfe, monkeypatch):
    """Der Zeitgeber fragt zehnmal je Sekunde — melden darf er trotzdem nur einmal."""
    sitzung = fake_session(hfe, writable=True, read_ahead=False)
    try:
        # Zwei Schadstellen vortäuschen, ohne echten Schreibweg.
        folge = ["5/1", "5/1", "5/1, 12/0", "5/1, 12/0"]
        monkeypatch.setattr(type(sitzung), "defect_tracks",
                            property(lambda self: folge.pop(0) if folge else "5/1, 12/0"))

        assert sitzung.neue_defekte() == "5/1"          # erste Meldung
        assert sitzung.neue_defekte() == ""             # unverändert → still
        assert sitzung.neue_defekte() == "5/1, 12/0"    # neue Spur → wieder melden
        assert sitzung.neue_defekte() == ""
    finally:
        sitzung.close()


def test_die_meldung_sagt_was_zu_tun_ist(app, hfe):
    sitzung = fake_session(hfe)
    try:
        text = sitzung.defekt_meldung("5/1")
        assert "Spur 5/1" in text
        assert "Speichern unter" in text, "der Weg in eine Datei fehlt"
        assert "neu beschreiben" in text, "der Weg auf eine neue Diskette fehlt"
        assert "unversehrt" in text, "der Bediener muss wissen, dass nichts verloren ist"
    finally:
        sitzung.close()


def test_emulator_zeigt_den_rettungsknopf_erst_beim_schreiben(app, hfe, monkeypatch):
    """Ohne Schreibrecht gibt es nichts zurückzuschreiben — der Knopf bleibt weg."""
    from app.core_binding.k1520 import K1520Emulator
    from app.ui import physical_disk
    from app.ui.drive_widget import DriveWidget

    for schreibbar in (False, True):
        sitzung = fake_session(hfe, writable=schreibbar, for_emulator=True)
        monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                            classmethod(lambda cls, **kw: sitzung))
        monkeypatch.setattr(physical_disk.PhysicalDiskDialog, "frage",
                            classmethod(lambda cls, parent=None, **kw: {
                                "drive": "a", "cell_rate_kbps": 250, "num_cyls": 80,
                                "num_heads": 2, "writable": schreibbar,
                                "read_ahead": False}))
        w = DriveWidget(K1520Emulator())
        panel = w._panels[0]
        try:
            panel._phys_btn.click()
            assert panel._rewrite_btn.isHidden() != schreibbar, (
                f"Ausweg falsch sichtbar (schreibbar={schreibbar})")
        finally:
            w.close_physical_sessions()


def test_neu_beschreiben_stellt_die_bekannten_spuren_ein(app, hfe):
    """Der Ausweg selbst: alles Bekannte noch einmal wegschreiben."""
    sitzung = fake_session(hfe, writable=True, read_ahead=True)
    try:
        assert _warte(lambda: (sitzung.stats().tracks_known or 0) > 5)
        # Vorauslesen ANHALTEN, bevor gezählt wird: sonst kommen zwischen dem Ablesen
        # und dem rewrite_all() weitere Spuren herein und die Zahlen weichen ab
        # (unter Last durchaus zu beobachten).
        sitzung.sync.set_read_ahead(False)
        time.sleep(0.2)                       # laufenden Leseauftrag auslaufen lassen
        bekannt = sitzung.stats().tracks_known
        assert sitzung.rewrite_all() == bekannt
        # Und der Vermerk „schon gemeldet" ist zurückgesetzt — auf der neuen
        # Diskette gilt nichts von der alten.
        assert sitzung.gemeldete_defekte == ""
    finally:
        sitzung.close()


def test_das_pruef_lesen_steht_in_der_statuszeile(app, hfe):
    from app.gw import Stats

    sitzung = fake_session(hfe, read_ahead=False)
    try:
        # busy_kind 4 = Verify; der Bediener soll „prüft" lesen, nicht „arbeitet an".
        original = type(sitzung).stats
        werte = Stats(tracks_total=160, tracks_known=8, tracks_dirty=1, tracks_failed=0,
                      tracks_defect=0, reads_done=8, writes_done=1, verifies_done=0,
                      verify_failed=0, errors=0, busy_kind=4, busy_cyl=5, busy_head=1,
                      stopped=False)
        type(sitzung).stats = lambda self: werte
        try:
            text = sitzung.status_text()
            assert "prüft 5/1" in text
            assert "1 zu schreiben" in text
        finally:
            type(sitzung).stats = original
    finally:
        sitzung.close()
