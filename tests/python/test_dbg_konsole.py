"""@test Konsolenmodus von `k1520dbg`: die Maschine live bedienen (`console`).

Der Emulator selbst hat keine Konsolenfassung (`app/main.py` kennt nur
`--paths`). `console` schliesst diese Luecke — und kann dabei etwas, das sonst
nichts kann: **die Haltepunkte bleiben scharf, waehrend man live tippt.** Von
Hand bis zum Fehler bedienen, dann steht die Maschine im Debugger.

Geprueft wird an einem echten Pseudoterminal, denn nur dort schaltet der
Rohmodus ueberhaupt ein. Nicht geprueft wird die genaue Byte-Folge der
ANSI-Ausgabe — sie ist Darstellung, kein Vertrag.

Aufbau: `doc/design/13_distribution.md` §10a, `tools/term_console.h`.
"""

import os
import re
import select
import subprocess
import sys
import time
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DBG = PROJECT_ROOT / "build" / "k1520dbg"
DISK = PROJECT_ROOT / "tests" / "fixtures" / "disks" / "cpa_cpa780_k5601_clock.img"
DISK2 = PROJECT_ROOT / "tests" / "fixtures" / "disks" / "cpa_mini.img"
DISK3 = PROJECT_ROOT / "tests" / "fixtures" / "disks" / "cpa_mini.hfe"

pytestmark = [
    pytest.mark.skipif(not DBG.exists(), reason="build/k1520dbg fehlt"),
    pytest.mark.skipif(not DISK.exists(), reason="Testdiskette fehlt"),
]

needs_pty = pytest.mark.skipif(
    sys.platform == "win32",
    reason="Pseudoterminals gibt es unter Windows nicht. Der Konsolenmodus laeuft "
           "dort ueber conio/SetConsoleMode (term_console.h) und wird von Hand geprueft.",
)


class Pty:
    """k1520dbg an einem Pseudoterminal."""

    def __init__(self, *args):
        import pty
        self.master, sklave = pty.openpty()
        os.set_blocking(self.master, False)
        self.proc = subprocess.Popen(
            [str(DBG), str(DISK), *args],
            stdin=sklave, stdout=sklave, stderr=sklave, close_fds=True,
            env=dict(os.environ, TERM="xterm-256color", COLUMNS="100", LINES="30"))
        os.close(sklave)
        self.aus = ""

    def lesen(self, dauer):
        ende = time.time() + dauer
        while time.time() < ende:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if not r:
                continue
            try:
                b = os.read(self.master, 65536)
            except OSError:
                break
            if not b:
                break
            self.aus += b.decode("utf-8", "replace")

    def warte_auf(self, muster, timeout=30.0):
        ende = time.time() + timeout
        while time.time() < ende:
            if re.search(muster, self.aus):
                return True
            self.lesen(0.1)
        return bool(re.search(muster, self.aus))

    def tippe(self, roh: bytes, ruhe=0.4):
        os.write(self.master, roh)
        self.lesen(ruhe)

    @property
    def klartext(self):
        return re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", self.aus)

    def ende(self):
        try:
            os.write(self.master, b"\x1dq\r")
        except OSError:
            pass
        try:
            self.proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
        os.close(self.master)


# ─── Ohne Terminal ───────────────────────────────────────────────────────────

def test_console_verweigert_sich_ohne_terminal():
    """Im Pipe-/Skriptbetrieb gibt es keinen Rohmodus — mit Hinweis auf den Ersatz."""
    r = subprocess.run([str(DBG), str(DISK)], input="console\nq\n",
                       capture_output=True, text=True, encoding="utf-8", timeout=120)
    assert "console braucht ein Terminal" in r.stderr
    assert "keys" in r.stderr and "gscreen" in r.stderr   # der Ersatz wird genannt


# ─── Vier Laufwerke ──────────────────────────────────────────────────────────

@pytest.mark.skipif(not (DISK2.exists() and DISK3.exists()), reason="Testdisketten fehlen")
def test_vier_laufwerke_auf_der_kommandozeile():
    """Die Hardware kann vier (K5122::drives_[4]); bis 2026-08-19 bot die CLI zwei."""
    r = subprocess.run([str(DBG), str(DISK), "-b", str(DISK2), "-c", str(DISK3)],
                       input="q\n", capture_output=True, text=True,
                       encoding="utf-8", timeout=120)
    assert "on A:" in r.stderr
    assert "on B:" in r.stderr
    assert "on C:" in r.stderr
    assert "failed" not in r.stderr


# ─── Konsolenmodus ───────────────────────────────────────────────────────────

@needs_pty
def test_console_zeichnet_und_kehrt_mit_ctrl_klammer_zurueck():
    t = Pty()
    try:
        assert t.warte_auf(r"\(dbg\)")
        t.tippe(b"console\r", 1.2)
        # Der Bildschirm wird per ANSI-Positionierung gezeichnet.
        assert re.search(r"\x1b\[\d+;\d+H", t.aus), "keine Bildschirmausgabe im Konsolenmodus"
        assert "Ctrl-]" in t.aus, "Hinweiszeile fehlt"
        t.tippe(b"\x1d", 1.0)                       # Ctrl-] = zurueck
        assert "console verlassen" in t.klartext
        t.tippe(b"where\r", 1.0)                    # Debugger nimmt wieder Befehle
        assert t.warte_auf(r"BUSRQ=")
    finally:
        t.ende()


@needs_pty
def test_live_getippte_eingabe_kommt_im_gast_an_und_haltepunkt_greift():
    """Der eigentliche Zweck: von Hand bedienen, bis der Haltepunkt zuschlaegt.

    Live getippt werden die Uhrzeit (sonst kommt CP/A nicht zum Prompt) und dann
    der Programmname. Der Haltepunkt auf 0100H ist der Programmeinsprung.
    """
    t = Pty()
    try:
        assert t.warte_auf(r"\(dbg\)")
        t.tippe(b'gscreen "Uhrzeit"\r', 0.5)
        assert t.warte_auf(r"Uhrzeit", timeout=60), "CP/A erreichte die Uhrzeitabfrage nicht"
        t.tippe(b"b 0x0100\r", 0.5)
        t.tippe(b"console\r", 1.0)

        t.tippe(b"120000\r", 3.0)     # LIVE: Uhrzeit -> CP/A geht zum Prompt
        t.tippe(b"hardy\r", 1.0)      # LIVE: Programm starten
        # Auf das ENDE der Haltezeile warten, nicht auf ihren Anfang: „** bp ZVE1"
        # und „ZVE1 PC=0100" stehen auf DERSELBEN Zeile, und ein Treffer auf den
        # Anfang kehrt zurueck, bevor der Rest ueber das Pseudoterminal da ist.
        assert t.warte_auf(r"ZVE1 PC=0100", timeout=60), \
            f"Haltepunkt griff nicht aus dem Konsolenmodus:\n{t.klartext[-800:]}"

        klar = t.klartext
        assert "bp ZVE1" in klar
        assert "console verlassen" in klar, "Konsolenmodus wurde nicht selbst verlassen"
        # …und der Debugger ist danach voll benutzbar.
        t.tippe(b"bt\r", 1.5)
        assert re.search(r"#1 [0-9A-F]{4}", t.klartext), "bt liefert keinen Aufrufweg"
    finally:
        t.ende()
