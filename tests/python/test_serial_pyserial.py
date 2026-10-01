"""pyserial gegen den Emulator-Server (Entwurf 19 §11 `py_serial_pyserial`, AP-S8).

Interop-Probe mit einem fremden RFC-2217-Client: `serial.serial_for_url("rfc2217://…")`
verbindet sich mit der DFÜ/V.24 eines laufenden A5120.  Geprüft wird, was ein
Anwender mit pyserial tut — Baud lesen/setzen (Leitsatz 4: der Gast ist maßgeblich,
eine abweichende Anfrage beantwortet der Server mit dem Gastwert, pyserial lehnt dann
ab), Bytes in beide Richtungen (auch FFH = IAC), Steuerleitungen mit
Nullmodem-Kreuzung (§6.4) in beide Richtungen.

Der Gast ist ein kleines Z80-Programm im RAM: CP/A wird bis zum Prompt
gebootet, dann zeigen CONST/CONIN der BIOS-Sprungleiste auf das Programm — der
nächste Konsolenaufruf landet dort (die Bindung hat bewusst keinen Zugriff auf den
PC).  Es programmiert A33-A auf 9600 Bd 8N1 mit RTS/DTR und schickt jedes Byte zurück;
F1H nimmt DTR weg, F2H setzt DTR und RTS, F3H nimmt RTS weg.

Ohne `pyserial` wird übersprungen (es steht in requirements-dev.txt, ist aber keine
Laufzeitabhängigkeit).
"""

import socket
import threading
import time

import pytest

from conftest import requires_core

pytestmark = requires_core

serial = pytest.importorskip("serial")
pytest.importorskip("serial.rfc2217")

from serial_gast import V24, bis_zum_prompt, echo_gast_einsetzen  # noqa: E402


def _freier_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _warte(bedingung, frist=5.0):
    ende = time.monotonic() + frist
    while time.monotonic() < ende:
        if bedingung():
            return True
        time.sleep(0.02)
    return bool(bedingung())


@pytest.fixture
def gast(booted_bis_uhrzeit):
    """A5120 mit laufendem Echo-Gast und RFC-2217-Server auf der DFÜ/V.24.

    Liefert (emu, port); die Maschine läuft in einem eigenen Faden (1× ist hier
    unwichtig — pyserial wartet ohnehin auf jede Antwort).
    """
    from app.core_binding import k1520 as B
    emu = echo_gast_einsetzen(booted_bis_uhrzeit)

    assert emu.serial_configure(V24, betriebsart=B.SER_RFC2217, rolle=B.SER_SERVER,
                                port=_freier_port())
    assert emu.serial_start(V24)
    port = emu.serial_status(V24).port_aktiv

    halt = threading.Event()

    def lauf():
        while not halt.is_set():
            emu.run(24_576)          # 10 ms Maschinenzeit
            time.sleep(0.002)

    faden = threading.Thread(target=lauf, daemon=True)
    faden.start()
    yield emu, port
    halt.set()
    faden.join(5)
    emu.serial_stop(V24)


@pytest.fixture
def booted_bis_uhrzeit(emulator, temp_disk):
    return bis_zum_prompt(emulator, temp_disk)


def _oeffnen(port, **kw):
    return serial.serial_for_url(f"rfc2217://127.0.0.1:{port}", baudrate=9600,
                                 timeout=3, **kw)


def test_bytes_in_beide_richtungen(gast):
    emu, port = gast
    with _oeffnen(port) as s:
        assert _warte(lambda: emu.serial_status(V24).zustand == 3)   # VERBUNDEN
        daten = bytes(b for b in range(256) if b not in (0xF1, 0xF2, 0xF3))
        s.write(daten)                      # auch FFH (IAC), 0DH, 00H, 11H/13H
        zurueck = s.read(len(daten))
        assert zurueck == daten
        st = emu.serial_status(V24)
        assert st.bytes_empfangen >= len(daten) and st.bytes_gesendet >= len(daten)


def test_baud_der_gast_ist_massgeblich(gast):
    emu, port = gast
    with _oeffnen(port) as s:
        assert s.baudrate == 9600
        assert _warte(lambda: emu.serial_status(V24).baud_gegenseite == 9600)
        assert not emu.serial_status(V24).baud_abweichend
        # pyserial verlangt 1200 Bd — der Server antwortet mit dem Gastwert 9600,
        # pyserial lehnt das ab (Leitsatz 4: eine Anfrage ändert den Gast nie).
        with pytest.raises((ValueError, serial.SerialException)):
            s.baudrate = 1200
        st = emu.serial_status(V24)
        assert st.baud_nenn == 9600
        assert st.baud_gegenseite == 1200
        assert st.baud_abweichend


def test_steuerleitungen_nullmodem(gast):
    emu, port = gast
    with _oeffnen(port) as s:
        # Client → Server: RTS → unser CTS, DTR → unser DSR und DCD
        s.rts = True
        s.dtr = True
        assert _warte(lambda: (lambda st: st.cts and st.dsr and st.dcd)(
            emu.serial_status(V24)))
        s.rts = False
        assert _warte(lambda: not emu.serial_status(V24).cts)
        assert emu.serial_status(V24).dsr
        s.dtr = False
        assert _warte(lambda: not emu.serial_status(V24).dsr
                      and not emu.serial_status(V24).dcd)
        s.rts = True
        s.dtr = True
        assert _warte(lambda: emu.serial_status(V24).cts)

        # Server → Client: Gast-RTS → CTS, Gast-DTR → DSR + CD (NOTIFY-MODEMSTATE)
        assert _warte(lambda: s.cts and s.dsr and s.cd)
        s.write(b"\xF1")                    # Gast nimmt DTR weg → DSR + CD fallen
        assert _warte(lambda: not s.dsr and not s.cd)
        assert s.cts and not emu.serial_status(V24).dtr
        s.write(b"\xF2")                    # und setzt es wieder
        assert _warte(lambda: s.cts and s.dsr and s.cd)
        s.write(b"\xF3")                    # Gast nimmt RTS weg → CTS fällt
        assert _warte(lambda: not s.cts)
        assert s.dsr and not emu.serial_status(V24).rts
        # RTS-Halt (§6.4): der eigene Wandler stellt dem Gast jetzt nichts mehr zu —
        # das F2H bleibt im Empfangspuffer, RTS bleibt aus.
        s.write(b"\xF2")
        assert _warte(lambda: emu.serial_status(V24).puffer_empfangen == 1)
        time.sleep(0.3)
        assert not s.cts and not emu.serial_status(V24).rts
