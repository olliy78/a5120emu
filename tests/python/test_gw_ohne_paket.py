"""Die physische Diskette **ohne das Paket `greaseweazle`** — die Lage der CI.

``greaseweazle`` ist eine **freiwillige** Abhängigkeit: es liegt nicht auf PyPI, wird
mit ``pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"`` nachgezogen,
und fehlt es, soll nur der Menüpunkt fehlen — sonst nichts.  Auf dem
Entwicklungsrechner ist es meist installiert, **in der CI nie**.

Genau diese Differenz kostete am 2026-08-18 drei rote Tests auf ``main``, die lokal
grün waren (``py_gw_gui`` lief 300 s in den Zeitüberlauf, ``py_gw_physical`` und
``py_physical_cli`` fielen um).  Die Eigenschaft „läuft ohne das Paket" darf deshalb
nicht davon abhängen, **welche** Umgebung gerade prüft: dieses Modul blendet das Paket
aktiv aus und fährt die Wege, die damals brachen — es schlägt also auch auf einem
Rechner an, auf dem ``greaseweazle`` installiert ist.

Was es NICHT prüft: alles hinter :func:`app.gw.open_device`.  Dort steht in der
zweiten Zeile ``util.usb_open`` — das braucht einen Adapter, und der hängt in der CI
nie am Rechner.  Deshalb brächte es auch keine Abdeckung, das Paket dort zu
installieren: kein einziger Testfall käme hinzu (die Hardwarefälle in
``test_gw_hardware.py`` hängen an ``K1520_GW_HARDWARE=1``, nicht am Paket).

Siehe doc/design/14_physische_diskette.md §14 und tests/python/README.md.
"""

from __future__ import annotations

import contextlib
import sys

import pytest

from gw_fake import fake_session

FIXTURE = "udos_boot_scp.hfe"


class _Importsperre:
    """Meta-Path-Finder, der ``import greaseweazle`` scheitern lässt."""

    def find_spec(self, fullname, path=None, target=None):
        if fullname == "greaseweazle" or fullname.startswith("greaseweazle."):
            raise ImportError("greaseweazle ist in diesem Test ausgeblendet")
        return None


@contextlib.contextmanager
def ohne_greaseweazle():
    """Das Paket ist in diesem Block nicht importierbar — wie in der CI.

    Schon geladene Teilmodule müssen dabei aus ``sys.modules`` verschwinden, sonst
    fände ``import greaseweazle`` sie dort und die Sperre bliebe wirkungslos.
    """
    sperre = _Importsperre()
    gemerkt = {n: m for n, m in sys.modules.items()
               if n == "greaseweazle" or n.startswith("greaseweazle.")}
    for n in gemerkt:
        del sys.modules[n]
    sys.meta_path.insert(0, sperre)
    try:
        yield
    finally:
        sys.meta_path.remove(sperre)
        sys.modules.update(gemerkt)


@pytest.fixture
def hfe(fixture_disks):
    return fixture_disks / FIXTURE


def test_die_sperre_wirkt():
    """Selbstprobe — ohne sie prüften die übrigen Fälle nichts."""
    with ohne_greaseweazle():
        with pytest.raises(ImportError):
            import greaseweazle  # noqa: F401


# ════════════════════════════════════════════════════════════════════════════
# Die Anbindung selbst
# ════════════════════════════════════════════════════════════════════════════


def test_app_gw_laesst_sich_ohne_das_paket_benutzen():
    """`app.gw` rechnet mit Bitzellen — das braucht `bitarray`, nicht `greaseweazle`.

    ``bitarray`` kam bisher nur als Beipack von ``greaseweazle`` mit und steht
    seitdem ausdrücklich in ``requirements-dev.txt``.  Fehlte es, starb der
    Arbeitsfaden still und die wartenden Leser liefen in ihre Frist — ein Hänger
    ohne Fehlermeldung.
    """
    with ohne_greaseweazle():
        from app.gw.device import naht_vor_sektorkopf
        from bitarray import bitarray

        # Zwei Umdrehungen ohne jede Sync-Gruppe: der Schnitt bleibt am Index.
        zellen = bitarray("01" * 4096)
        assert naht_vor_sektorkopf(zellen, 4096) == 0


def test_die_verfuegbarkeit_nennt_den_grund_statt_abzustuerzen():
    with ohne_greaseweazle():
        from app.gw import verfuegbarkeit

        ok, grund = verfuegbarkeit()
        assert ok is False
        assert "greaseweazle" in grund and "pip install" in grund
        assert sys.executable in grund, "der Grund muss DIESEN Interpreter nennen"


# ════════════════════════════════════════════════════════════════════════════
# Kommandozeile
# ════════════════════════════════════════════════════════════════════════════


def test_argumentfehler_kommen_auch_ohne_das_paket(capsys, tmp_path):
    """`put` ohne `--write` wird abgelehnt — die Aussage hing an einer Abhängigkeit.

    Die Verfügbarkeitsprüfung stand einmal VOR den Argumentprüfungen; damit bekam
    man ohne die Hosttools nie zu hören, dass der eigentliche Fehler ein fehlendes
    ``--write`` war (doc/design/14_physische_diskette.md §12.3).
    """
    from app.disktool import physical_cli

    with ohne_greaseweazle():
        rc = physical_cli.main(["--physical", "put", str(tmp_path / "x")])
    aus = capsys.readouterr()
    assert rc == 1
    assert "--write" in aus.err
    assert "greaseweazle" not in aus.err


def test_ein_befehl_am_laufwerk_meldet_das_fehlende_paket(capsys):
    """Und wenn es ans Laufwerk geht, kommt der Grund — mit Anleitung, ohne Absturz."""
    from app.disktool import physical_cli

    with ohne_greaseweazle():
        rc = physical_cli.main(["--physical", "ls"])
    aus = capsys.readouterr()
    assert rc == 1
    assert "greaseweazle" in aus.err and "pip install" in aus.err


# ════════════════════════════════════════════════════════════════════════════
# Oberfläche
# ════════════════════════════════════════════════════════════════════════════


def test_das_disktool_oeffnet_eine_ersatzsitzung_ohne_das_paket(hfe, monkeypatch):
    """Der Fall, der 300 s hing.

    ``MainWindow.open_physical`` prüfte selbst, ob die Hosttools da sind — obwohl es
    auch mit einer **Ersatzsitzung** gerufen wird.  Ohne das Paket lief es in ein
    modales Meldungsfenster, das headless niemand wegklickt.  Die Prüfung gehört an
    die Bedienwege davor und an ``PhysicalSession.start``, nicht dazwischen.
    """
    pytest.importorskip("PySide6")
    from PySide6.QtWidgets import QApplication

    QApplication.instance() or QApplication([])

    from app.disktool.ui.main_window import MainWindow
    from app.ui import physical_disk

    sitzung = fake_session(hfe, read_ahead=False)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))

    with ohne_greaseweazle():
        fenster = MainWindow()
        try:
            assert fenster.open_physical(drive="a", num_cyls=sitzung.num_cyls,
                                         num_heads=sitzung.num_heads), \
                "die Ersatzsitzung wurde abgewiesen, obwohl sie kein Paket braucht"
            assert fenster.tool is not None and fenster.tool.list()
        finally:
            fenster._close_tool()
