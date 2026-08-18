"""@test Diskettenargumente von `a5120emu` (`app/main.py`).

Bis 2026-08-19 kannte der Emulator genau ein Argument (`--paths`) und nahm keine
Diskette entgegen — `a5120emu meine.hfe` wurde kommentarlos ignoriert. Das war
genau in dem Ablauf laestig, fuer den es gedacht ist: bearbeiten, assemblieren,
auf die Diskette, starten.

Zwei Dinge werden geprueft, die leicht still kaputtgehen:

1. **Die Auswertung liegt VOR den Qt-Importen.** Ein Tippfehler im Dateinamen
   soll eine Zeile im Terminal ergeben — auch auf einem Rechner ohne PySide6.
2. **Bei einem rohen `.img` entscheidet die Dateigroesse ueber das Format.** Der
   Kern prueft sie beim Mounten NICHT: ein 780K-Abbild laesst sich klaglos als
   `cpa800` einlegen und bootet dann nicht. Genau das ist beim Bau passiert.
"""

import os
import subprocess
import sys
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[2]
MAIN = PROJECT_ROOT / "app" / "main.py"
DISK780 = PROJECT_ROOT / "tests" / "fixtures" / "disks" / "cpa_cpa780_k5601_clock.img"
DISK_MINI = PROJECT_ROOT / "tests" / "fixtures" / "disks" / "cpa_mini.img"

pytestmark = pytest.mark.skipif(not DISK780.exists(), reason="Testdiskette fehlt")


def start(*args, python=None):
    return subprocess.run([python or sys.executable, str(MAIN), *args],
                          capture_output=True, text=True, encoding="utf-8",
                          timeout=120, cwd=str(PROJECT_ROOT))


# ─── Kommandozeile, ohne dass Qt gebraucht wird ──────────────────────────────

def test_hilfe_nennt_die_diskettenargumente():
    r = start("--help")
    assert r.returncode == 0, r.stderr
    assert "DISKETTE" in r.stdout
    assert "--paths" in r.stdout
    # Der Weg ohne Oberflaeche gehoert in dieselbe Hilfe.
    assert "k1520dbg" in r.stdout


def test_fehlende_datei_wird_gemeldet_statt_ignoriert():
    r = start("gibtsnichtbestimmt.hfe")
    assert r.returncode == 2
    assert "gibt es nicht" in r.stderr


def test_unbekannte_option_wird_gemeldet():
    r = start("--quatsch")
    assert r.returncode == 2
    assert "unbekannte Option" in r.stderr
    assert "--help" in r.stderr


def test_mehr_als_vier_disketten():
    r = start(*([str(DISK780)] * 5))
    assert r.returncode == 2
    assert "vier Laufwerke" in r.stderr


def test_auswertung_braucht_kein_pyside6():
    """Die Pruefung steht VOR den Qt-Importen — sonst kaeme erst ein ImportError.

    Gefahren mit einem Interpreter ohne PySide6 (dem System-Python); ist dort
    zufaellig eines installiert, sagt der Test es und prueft nichts vor.
    """
    system_py = "/usr/bin/python3"
    if not os.path.exists(system_py):
        pytest.skip("kein System-Python vorhanden")
    hat_qt = subprocess.run([system_py, "-c", "import PySide6"],
                            capture_output=True).returncode == 0
    if hat_qt:
        pytest.skip("System-Python hat PySide6 — der Vorlauf ist so nicht pruefbar")
    r = start("gibtsnichtbestimmt.hfe", python=system_py)
    assert r.returncode == 2
    assert "gibt es nicht" in r.stderr
    assert "PySide6" not in r.stderr


# ─── Einlegen und Format ─────────────────────────────────────────────────────

def test_format_wird_ueber_die_dateigroesse_gewaehlt():
    """Ein rohes .img traegt seine Geometrie nur in der Groesse.

    813824 Byte = cpa780. Waehlt die Vorauswahl stattdessen die Laufwerksvorgabe
    cpa800 (819200), gelingt das Mounten trotzdem — und die Maschine bootet nicht.
    """
    sys.path.insert(0, str(PROJECT_ROOT))
    from app.ui.drive_widget import _abbildgroessen
    groessen = _abbildgroessen()
    assert groessen.get("cpa780") == 813824
    assert groessen.get("cpa800") == 819200
    assert DISK780.stat().st_size == groessen["cpa780"]


@pytest.mark.skipif(not (PROJECT_ROOT / "build" / "libk1520core.so").exists(),
                    reason="libk1520core.so nicht gebaut")
def test_diskette_liegt_im_laufwerk_und_die_maschine_bootet_davon():
    """Der eigentliche Zweck: `a5120emu DISKETTE` startet MIT dieser Diskette."""
    os.environ["QT_QPA_PLATFORM"] = "offscreen"
    sys.path.insert(0, str(PROJECT_ROOT))
    from PySide6.QtWidgets import QApplication
    from app.ui.main_window import MainWindow

    app = QApplication.instance() or QApplication([])
    fenster = MainWindow([str(DISK780)])
    try:
        belegung = {m["drive"]: m for m in fenster.drives_widget.get_mounts()}
        assert 0 in belegung, "Laufwerk A: blieb leer"
        assert belegung[0]["path"] == str(DISK780)
        assert belegung[0]["format"] == "cpa780", \
            f"falsches Format gewaehlt: {belegung[0]['format']}"

        # Kaltstart: der Bildschirm muss CP/A zeigen (der Kaltstart lief schon in
        # __init__, hier wird nur Maschinenzeit vergeben).
        for _ in range(500):
            fenster.emulator.run(49000)
        text = "".join(
            chr(c & 0x7F) if 32 <= (c & 0x7F) < 127 else " "
            for c in (fenster.emulator.mem_read(0xF800 + i) for i in range(80 * 24)))
        assert "CP/A" in text, f"kein Kaltstart von der genannten Diskette:\n{text[:400]}"
    finally:
        fenster.close()


@pytest.mark.skipif(not (PROJECT_ROOT / "build" / "libk1520core.so").exists()
                    or not DISK_MINI.exists(), reason="Voraussetzungen fehlen")
def test_zweite_diskette_landet_in_b():
    os.environ["QT_QPA_PLATFORM"] = "offscreen"
    sys.path.insert(0, str(PROJECT_ROOT))
    from PySide6.QtWidgets import QApplication
    from app.ui.main_window import MainWindow

    app = QApplication.instance() or QApplication([])
    fenster = MainWindow([str(DISK780), str(DISK_MINI)])
    try:
        belegung = {m["drive"]: m["path"] for m in fenster.drives_widget.get_mounts()}
        assert belegung.get(0) == str(DISK780)
        assert belegung.get(1) == str(DISK_MINI)
    finally:
        fenster.close()
