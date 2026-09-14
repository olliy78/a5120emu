"""Die Nachbarprogramme starten (:mod:`app.programme`).

Der Emulator öffnet das DiskTool, das DiskTool den Emulator, und der Emulator
zusätzlich eine **Werkzeugkonsole** — ein Konsolenfenster, in dem ``k1520dbg``
und die Kommandozeile des DiskTool ohne Pfadangabe laufen.

Hier wird nichts wirklich gestartet: ``subprocess.Popen`` wird ersetzt und die
abgesetzte Befehlszeile geprüft.  Das ist die einzige Stelle, an der sich der
Aufruf überhaupt festhalten lässt — ein wirklich gestartetes Fenster würde die
Testebene aufhalten und liesse sich headless nicht wieder schliessen.
"""

import os
import subprocess
import sys
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT

from app import paths, programme


@pytest.fixture
def aufrufe(monkeypatch):
    """``subprocess.Popen`` abfangen — liefert die Liste der Aufrufe."""
    gesammelt = []

    class Attrappe:
        def __init__(self, befehl, **kw):
            gesammelt.append((list(befehl), kw))

    monkeypatch.setattr(programme.subprocess, "Popen", Attrappe)
    return gesammelt


@pytest.fixture
def eigener_ordner(tmp_path, monkeypatch):
    """Arbeitsdisketten und Konfiguration in ein Testverzeichnis legen."""
    disketten = tmp_path / "Disketten"
    disketten.mkdir()
    monkeypatch.setenv(paths.ENV_DISKS, str(disketten))
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    return disketten


# ─── Werkzeuge und Handbücher finden (app/paths.py) ──────────────────────────

def test_werkzeuge_liegen_im_quellbaum_unter_build():
    """Im Quellbaum stehen Debugger und DiskTool-CLI neben der Bibliothek."""
    assert paths.tools_dir() == PROJECT_ROOT / "build"
    dbg = paths.debugger()
    if dbg is None:
        pytest.skip("k1520dbg nicht gebaut")
    assert dbg.name.startswith("k1520dbg")


def test_die_cli_des_disktool_geht_der_oberflaeche_vor(tmp_path, monkeypatch):
    """In einer Installation heisst ``bin/k1520disktool`` die OBERFLÄCHE.

    Die Kommandozeile liegt dort als ``k1520disktool-cli``.  Wer den kürzeren
    Namen zuerst nähme, startete aus der Konsole heraus ein Fenster.
    """
    (tmp_path / "bin").mkdir()
    for name in ("k1520disktool", "k1520disktool-cli"):
        (tmp_path / "bin" / name).write_text("#!/bin/sh\n")
    monkeypatch.setenv(paths.ENV_HOME, str(tmp_path))

    assert paths.tools_dir() == tmp_path / "bin"
    assert paths.disktool_cli().name == "k1520disktool-cli"


def test_handbuch_des_debuggers_wird_gefunden():
    """Quellbaum ``doc/``, Installation ``share/doc/`` — hier der Quellbaum."""
    assert paths.doc_dir() == PROJECT_ROOT / "doc"
    assert paths.doc_file(programme.HANDBUCH_DBG) is not None
    assert paths.doc_file("gibt-es-nicht.md") is None


# ─── Die andere Oberfläche starten ───────────────────────────────────────────

@pytest.mark.parametrize("kennung,skript", [
    (programme.EMULATOR, "app/main.py"),
    (programme.DISKTOOL, "app/disktool/main.py"),
])
def test_das_andere_programm_startet_mit_dem_eigenen_interpreter(
        kennung, skript, aufrufe, eigener_ordner):
    """Interpreter + Skript der Installation, abgekoppelt, im Diskettenordner.

    Nicht der Starter aus ``bin/``: den gibt es nur in einer Installation.
    """
    programme.programm_starten(kennung)

    (befehl, kw), = aufrufe
    assert befehl[0] == programme._interpreter()
    assert Path(befehl[1]) == paths.base_dir() / skript
    assert kw["cwd"] == str(eigener_ordner)
    if sys.platform.startswith("win"):
        assert kw["creationflags"] & subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        # Ohne eigene Sitzung nähme das Beenden des Elternteils das Kind mit.
        assert kw["start_new_session"] is True


def test_ein_fehlendes_programm_meldet_den_erwarteten_pfad(tmp_path, monkeypatch,
                                                           aufrufe):
    """Die Meldung muss sagen, WO gesucht wurde — sonst rät der Anwender."""
    monkeypatch.setenv(paths.ENV_HOME, str(tmp_path))
    with pytest.raises(RuntimeError) as fehler:
        programme.programm_starten(programme.DISKTOOL)
    assert str(tmp_path / "app" / "disktool" / "main.py") in str(fehler.value)
    assert aufrufe == [], "es darf nichts gestartet worden sein"


# ─── Werkzeugkonsole ─────────────────────────────────────────────────────────

def test_beispieldiskette_nimmt_eine_wirklich_vorhandene(eigener_ordner):
    """Ein abtippbares Beispiel ist mehr wert als ein Platzhalter."""
    assert programme.beispieldiskette(eigener_ordner) == "DISKETTE.hfe"
    (eigener_ordner / "zweite.hfe").write_bytes(b"")
    (eigener_ordner / "erste.hfe").write_bytes(b"")
    assert programme.beispieldiskette(eigener_ordner) == "erste.hfe"


def test_konsolentext_nennt_aufruf_arbeitsordner_und_handbuch(eigener_ordner):
    (eigener_ordner / "meine.hfe").write_bytes(b"")
    text = programme.konsolentext(eigener_ordner)

    assert "k1520dbg meine.hfe" in text          # ein Aufruf zum Abtippen
    assert " ls meine.hfe" in text               # und die zweite Richtung
    assert str(eigener_ordner) in text           # wo man steht
    assert programme.HANDBUCH_DBG in text        # wo es ausführlich steht


def test_konsolenumgebung_nennt_wurzel_und_formatkatalog():
    umgebung = programme.konsolenumgebung()
    assert umgebung["K1520_HOME"] == str(paths.base_dir())
    # Der Katalog wird gesetzt, solange er auffindbar ist — in einer
    # verschobenen Installation findet ihn der Kern sonst nicht neben sich.
    katalog = paths.formats_file()
    if katalog is not None:
        assert umgebung["K1520_FORMATS"] == str(katalog)


@pytest.mark.skipif(sys.platform.startswith("win"),
                    reason="die Startdatei ist hier eine .cmd")
def test_startdatei_setzt_umgebung_wechselt_ordner_und_bleibt_offen(eigener_ordner):
    datei = programme._schreibe_startdatei(eigener_ordner)
    inhalt = datei.read_text(encoding="utf-8")

    assert datei.name == "werkzeugkonsole.sh"
    assert os.access(datei, os.X_OK), "ohne Ausführungsrecht nutzlos"
    assert "K1520_HOME=" in inhalt and "export K1520_HOME" in inhalt
    assert f"PATH={paths.tools_dir()}" in inhalt or str(paths.tools_dir()) in inhalt
    assert str(eigener_ordner) in inhalt
    # Ohne die interaktive Shell am Ende schlösse sich das Fenster sofort wieder.
    assert 'exec "${SHELL:-/bin/sh}" -i' in inhalt


@pytest.mark.skipif(sys.platform.startswith("win"), reason="Linux-Terminalliste")
def test_terminal_wird_gesucht_und_TERMINAL_gewinnt(monkeypatch, tmp_path):
    monkeypatch.setattr(programme.shutil, "which",
                        lambda name: "/usr/bin/" + name if name == "xterm" else None)
    befehl = programme.terminalbefehl(tmp_path / "start.sh")
    assert befehl[:2] == ["/usr/bin/xterm", "-e"]
    assert befehl[-1] == str(tmp_path / "start.sh")

    monkeypatch.setenv("TERMINAL", "meinterm")
    monkeypatch.setattr(programme.shutil, "which", lambda name: "/opt/" + name)
    assert programme.terminalbefehl(tmp_path / "start.sh")[0] == "/opt/meinterm"


@pytest.mark.skipif(sys.platform.startswith("win"), reason="Linux-Terminalliste")
def test_ohne_terminal_nennt_die_meldung_die_startdatei(monkeypatch, eigener_ordner):
    """Kein Terminal ist kein Grund, die vorbereitete Datei zu verschweigen."""
    monkeypatch.delenv("TERMINAL", raising=False)
    monkeypatch.setattr(programme.shutil, "which", lambda name: None)
    with pytest.raises(RuntimeError) as fehler:
        programme.konsole_starten()
    assert "werkzeugkonsole.sh" in str(fehler.value)


@pytest.mark.skipif(sys.platform.startswith("win"), reason="Linux-Terminalliste")
def test_konsole_startet_das_terminal_mit_der_startdatei(monkeypatch, aufrufe,
                                                         eigener_ordner):
    monkeypatch.delenv("TERMINAL", raising=False)
    monkeypatch.setattr(programme.shutil, "which",
                        lambda name: "/usr/bin/xterm" if name == "xterm" else None)
    datei = programme.konsole_starten()

    (befehl, kw), = aufrufe
    assert befehl[0] == "/usr/bin/xterm"
    assert befehl[-1] == str(datei)
    assert kw["cwd"] == str(eigener_ordner)


def test_unter_windows_oeffnet_cmd_die_startdatei_im_neuen_fenster(monkeypatch,
                                                                    tmp_path):
    """``cmd /c <batch>``, nicht ``start``: die Batchdatei endet auf ``cmd /k``.

    Das Fenster muss dabei von uns kommen (``CREATE_NEW_CONSOLE``) — die
    Oberfläche läuft unter ``pythonw.exe`` und hat selbst keine Konsole.
    """
    monkeypatch.setattr(programme, "_ist_windows", lambda: True)
    befehl = programme.terminalbefehl(tmp_path / "werkzeugkonsole.cmd")
    assert befehl == ["cmd.exe", "/c", str(tmp_path / "werkzeugkonsole.cmd")]


def test_die_windows_startdatei_bleibt_ascii():
    """``cmd.exe`` liest eine Batchdatei in der Kodepage, nicht in UTF-8.

    Geprüft wird die Umschrift selbst — die Datei entsteht nur unter Windows,
    ihr Text wird aber überall gleich gebaut.
    """
    umgeschrieben = programme._nur_ascii(programme.konsolentext())
    assert umgeschrieben.isascii()
    assert "?" not in umgeschrieben, "ein Zeichen fehlt in der Umschrifttabelle"


# ─── Verdrahtung in den beiden Oberflächen ───────────────────────────────────

def _menueeintraege(fenster, titel):
    """Die Aktionen des Untermenüs ``titel`` der Menüleiste.

    Es werden die EINTRÄGE geliefert, nicht das Menü: die Python-Hülle eines
    ``QMenu`` aus ``QAction.menu()`` überlebt die Schleifenrunde nicht, in der
    sie geholt wurde — ein zurückgegebenes Menü läuft beim nächsten Zugriff in
    „Internal C++ object (QMenu) already deleted".
    """
    for aktion in fenster.menuBar().actions():
        if aktion.text() == titel:
            menue = aktion.menu()
            assert menue is not None, titel
            return list(menue.actions())
    raise AssertionError(f"kein Menü {titel}")


def test_der_emulator_hat_beide_eintraege_im_werkzeugmenue(monkeypatch):
    """Menüeintrag vorhanden, verdrahtet und ohne Tastenkürzel.

    Ohne Kürzel bewusst: jedes weitere ``Strg+Umschalt+…`` müsste in die
    Kürzeltabelle des Handbuchs, und die ist ein Vertrag.
    """
    pytest.importorskip("PySide6")
    from conftest import _core_lib
    if _core_lib() is None:
        pytest.skip("libk1520core.so nicht gebaut")

    from PySide6.QtWidgets import QApplication
    from app.ui.main_window import MainWindow

    app = QApplication.instance() or QApplication([])
    fenster = MainWindow()
    try:
        eintraege = _menueeintraege(fenster, "&Werkzeuge")
        assert fenster.act_disktool in eintraege
        assert fenster.act_konsole in eintraege
        assert fenster.act_disktool.shortcut().isEmpty()
        assert fenster.act_konsole.shortcut().isEmpty()

        gerufen = []
        monkeypatch.setattr(programme, "programm_starten",
                            lambda k, *a, **kw: gerufen.append(k))
        monkeypatch.setattr(programme, "konsole_starten",
                            lambda: gerufen.append("konsole"))
        fenster.act_disktool.trigger()
        fenster.act_konsole.trigger()
        assert gerufen == [programme.DISKTOOL, "konsole"]
    finally:
        fenster.close()
        app.processEvents()


def test_das_disktool_hat_den_emulator_im_werkzeugmenue(monkeypatch):
    pytest.importorskip("PySide6")
    from conftest import _disk_lib
    if _disk_lib() is None:
        pytest.skip("libk1520disk.so nicht gebaut")

    from PySide6.QtWidgets import QApplication
    from app.disktool.ui.main_window import MainWindow

    app = QApplication.instance() or QApplication([])
    fenster = MainWindow()
    try:
        assert fenster.act_emulator in _menueeintraege(fenster, "&Werkzeuge")
        assert fenster.act_emulator.shortcut().isEmpty()

        gerufen = []
        monkeypatch.setattr(programme, "programm_starten",
                            lambda k, *a, **kw: gerufen.append(k))
        fenster.act_emulator.trigger()
        assert gerufen == [programme.EMULATOR]
    finally:
        fenster._close_tool()
        app.processEvents()


def test_beide_handbuecher_erklaeren_das_werkzeugmenue():
    """Ein Bedienweg, den das Handbuch nicht kennt, ist einer zu wenig."""
    emu = (PROJECT_ROOT / "app" / "help" / "handbuch.md").read_text(encoding="utf-8")
    assert "Werkzeugkonsole" in emu and "k1520DiskTool starten" in emu

    dt = (PROJECT_ROOT / "app" / "disktool" / "help" / "handbuch.md"
          ).read_text(encoding="utf-8")
    assert "A5120-Emulator starten" in dt
