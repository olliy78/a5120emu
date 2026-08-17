"""@test Interaktive Sitzung von `k1520dbg` an einem echten Pseudoterminal.

Warum eigens: die Zeilenbearbeitung (third_party/isocline) greift **nur**, wenn
stdin ein Terminal ist — genau der Pfad, den die ~30 `cli_dbg_`-Tests NICHT
berühren, weil sie über eine Pipe fahren. Ohne diese Datei könnte die
Vervollständigung kaputtgehen, ohne dass ein einziger Test rot wird.

Genau das ist beim Einbau passiert: `ic_add_completion()` allein FÜGT EIN, statt
zu ersetzen — aus „whe"+TAB wurde „whewhere". Sichtbar wurde es erst am
Pseudoterminal; erst `ic_complete_word()` richtet die Vorschläge am zu
ersetzenden Wort aus.

Geprüft wird das Verhalten am Terminal, nicht die Byte-für-Byte-Ausgabe:
isocline zeichnet die Zeile mit ANSI-Sequenzen neu, und an deren genauer Form
soll sich kein Test festbeißen.
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

pytestmark = [
    pytest.mark.skipif(
        sys.platform == "win32",
        reason="Pseudoterminals gibt es unter Windows nicht (kein pty-Modul). Die "
               "Zeilenbearbeitung selbst laeuft dort ueber die Console-API von "
               "isocline; geprueft wird sie dort von Hand.",
    ),
    pytest.mark.skipif(not DBG.exists(),
                       reason="build/k1520dbg fehlt — vorher `tools/dev.sh build`"),
    pytest.mark.skipif(not DISK.exists(), reason="Testdiskette fehlt"),
]

PROMPT = "(dbg)"


class Sitzung:
    """k1520dbg an einem Pseudoterminal, mit Warten auf Ausgabe statt fester Pausen."""

    def __init__(self):
        import pty
        self.master, sklave = pty.openpty()
        os.set_blocking(self.master, False)
        self.proc = subprocess.Popen(
            [str(DBG), str(DISK)],
            stdin=sklave, stdout=sklave, stderr=sklave, close_fds=True,
            env=dict(os.environ, TERM="xterm-256color", COLUMNS="100", LINES="30"),
        )
        os.close(sklave)
        self.aus = ""

    def _lesen(self, dauer):
        ende = time.time() + dauer
        while time.time() < ende:
            r, _, _ = select.select([self.master], [], [], 0.02)
            if not r:
                continue
            try:
                d = os.read(self.master, 65536)
            except OSError:
                break
            if not d:
                break
            self.aus += d.decode("utf-8", "replace")

    def warte_auf(self, muster, timeout=20.0):
        """Liest, bis `muster` (Regex) in der Gesamtausgabe steht. Gibt True/False."""
        ende = time.time() + timeout
        while time.time() < ende:
            if re.search(muster, self.aus):
                return True
            self._lesen(0.1)
        return bool(re.search(muster, self.aus))

    def tippe(self, roh: bytes, ruhe=0.35):
        os.write(self.master, roh)
        self._lesen(ruhe)

    def beenden(self):
        try:
            os.write(self.master, b"\x04")          # Ctrl-D
        except OSError:
            pass
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=5)
        os.close(self.master)
        return self.proc.returncode

    @property
    def sichtbar(self):
        """Ausgabe ohne ANSI-Sequenzen — dafuer wird geprueft, was der Mensch sieht."""
        return re.sub(r"\x1b\[[0-9;]*[a-zA-Z]|\x1b\][^\x07]*\x07", "", self.aus)


@pytest.fixture
def sitzung():
    s = Sitzung()
    assert s.warte_auf(re.escape(PROMPT)), f"kein Prompt erschienen:\n{s.sichtbar[-500:]}"
    yield s
    if s.proc.poll() is None:
        s.beenden()


# ─── Grundlage ───────────────────────────────────────────────────────────────

def test_prompt_erscheint_am_terminal(sitzung):
    """Ohne Terminal gibt es kein isocline — also zuerst: kommt der Prompt?"""
    assert PROMPT in sitzung.sichtbar


def test_kommando_laeuft_am_terminal(sitzung):
    sitzung.tippe(b"where\r")
    assert sitzung.warte_auf(r"BUSRQ="), sitzung.sichtbar[-500:]


# ─── Zeilenbearbeitung ───────────────────────────────────────────────────────

def test_tab_vervollstaendigt_ein_eindeutiges_kommando(sitzung):
    """`whe` + TAB muss `where` ERSETZEN, nicht `whewhere` ergeben (der Einbaufehler)."""
    sitzung.tippe(b"whe\t")
    sitzung.tippe(b"\r")
    assert sitzung.warte_auf(r"BUSRQ="), sitzung.sichtbar[-600:]
    assert "whewhere" not in sitzung.sichtbar, "Vervollstaendigung fuegt ein statt zu ersetzen"
    assert "unknown command" not in sitzung.sichtbar


def test_tab_bietet_im_argument_nichts_an(sitzung):
    """Ab dem zweiten Wort sind Argumente Adressen/Ausdruecke — keine Kommandonamen."""
    sitzung.tippe(b"d 0x01\t")
    sitzung.tippe(b"\r")
    # `d 0x01` muss unveraendert ausgefuehrt werden: Hexdump ab Adresse 0001.
    assert sitzung.warte_auf(r"\n\s*0001: "), sitzung.sichtbar[-600:]
    assert "unknown command" not in sitzung.sichtbar
    # Kein Kommandoname darf hinten an das Argument geraten sein.
    assert not re.search(r"d 0x01(disp|dump|dev|disk|d)\b", sitzung.sichtbar)


def test_pfeil_hoch_holt_das_vorige_kommando(sitzung):
    sitzung.tippe(b"where\r")
    assert sitzung.warte_auf(r"BUSRQ=")
    vorher = sitzung.sichtbar.count("BUSRQ=")
    sitzung.tippe(b"\x1b[A")                      # Pfeil hoch
    sitzung.tippe(b"\r")
    assert sitzung.warte_auf(r"(BUSRQ=[\s\S]*){%d}" % (vorher + 1)), \
        f"History hat das Kommando nicht zurueckgeholt:\n{sitzung.sichtbar[-600:]}"


def test_rueckschritt_korrigiert_die_zeile(sitzung):
    sitzung.tippe(b"whereX")
    sitzung.tippe(b"\x7f")                        # Backspace
    sitzung.tippe(b"\r")
    assert sitzung.warte_auf(r"BUSRQ="), sitzung.sichtbar[-600:]
    assert "unknown command" not in sitzung.sichtbar


# ─── Beenden ─────────────────────────────────────────────────────────────────

def test_ctrl_d_beendet_die_sitzung(sitzung):
    rc = sitzung.beenden()
    assert rc == 0, f"Ctrl-D beendete nicht sauber (rc={rc})"


def test_q_beendet_die_sitzung(sitzung):
    sitzung.tippe(b"q\r")
    try:
        rc = sitzung.proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        sitzung.proc.kill()
        pytest.fail("q hat die Sitzung nicht beendet")
    assert rc == 0


# ─── Der Grund, warum ueberhaupt getauscht wurde ─────────────────────────────

def test_haengt_nicht_mehr_an_readline():
    """GPLv3-Bibliothek in einem MIT-Programm — der eigentliche Anlass des Tauschs.

    Faellt readline je wieder in den Bau (etwa ueber eine Abhaengigkeit), meldet
    es dieser Test, bevor ein Paket damit ausgeliefert wird.
    Siehe doc/design/13_distribution.md §10a.3.
    """
    if sys.platform == "win32":
        pytest.skip("ldd gibt es nicht; unter Windows wurde readline nie gesucht")
    r = subprocess.run(["ldd", str(DBG)], capture_output=True, text=True,
                       encoding="utf-8", timeout=60)
    assert r.returncode == 0, r.stderr
    assert "readline" not in r.stdout, f"k1520dbg haengt wieder an readline:\n{r.stdout}"
    assert "libtinfo" not in r.stdout, f"readline zieht libtinfo nach:\n{r.stdout}"
