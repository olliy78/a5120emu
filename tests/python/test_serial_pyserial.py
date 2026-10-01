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

from conftest import requires_core, run_until_text

pytestmark = requires_core

serial = pytest.importorskip("serial")
pytest.importorskip("serial.rfc2217")

V24 = 0                 # DFÜ/V.24 = SIO A33 Kanal A, Ports 50H/51H
ORG = 0x8000


def _assemble(items, org=ORG):
    """Kleinstassembler: Bytes, ("m", marke), ("jr", opcode, marke)."""
    code, marken, sprung = [], {}, []
    for it in items:
        if isinstance(it, tuple) and it[0] == "m":
            marken[it[1]] = org + len(code)
        elif isinstance(it, tuple) and it[0] == "jr":
            code += [it[1], 0]
            sprung.append((len(code) - 1, it[2]))
        else:
            code += list(it)
    for pos, ziel in sprung:
        d = marken[ziel] - (org + pos + 1)
        assert -128 <= d <= 127
        code[pos] = d & 0xFF
    return code


def _gast():
    init = []
    for x in (0x18, 0x04, 0x44, 0x03, 0xC1, 0x05, 0xEA, 0x01, 0x00):
        init += [0x3E, x, 0xD3, 0x51]               # LD A,x / OUT (51H),A
    return _assemble([
        [0xF3, 0x31, 0x00, 0xA0],                   # DI / LD SP,0A000H
        [0x3E, 0x05, 0xD3, 0x0C, 0x3E, 0x01, 0xD3, 0x0C],   # ZRE-CTC K0: ×16 → 9600 Bd
        init,
        ("m", "rx"),
        [0xDB, 0x51, 0xE6, 0x01], ("jr", 0x28, "rx"),       # warten auf Rx
        [0xDB, 0x50, 0xFE, 0xF1], ("jr", 0x28, "aus"),
        [0xFE, 0xF2], ("jr", 0x28, "an"),
        [0xFE, 0xF3], ("jr", 0x28, "rts_aus"),
        [0x47],                                     # LD B,A
        ("m", "tx"),
        [0xDB, 0x51, 0xE6, 0x04], ("jr", 0x28, "tx"),       # warten auf TxEmpty
        [0x78, 0xD3, 0x50], ("jr", 0x18, "rx"),     # LD A,B / OUT (50H),A
        ("m", "aus"),                               # WR5 6AH: DTR aus, RTS an
        [0x3E, 0x05, 0xD3, 0x51, 0x3E, 0x6A, 0xD3, 0x51], ("jr", 0x18, "rx"),
        ("m", "rts_aus"),                           # WR5 E8H: DTR an, RTS aus
        [0x3E, 0x05, 0xD3, 0x51, 0x3E, 0xE8, 0xD3, 0x51], ("jr", 0x18, "rx"),
        ("m", "an"),
        [0x3E, 0x05, 0xD3, 0x51, 0x3E, 0xEA, 0xD3, 0x51], ("jr", 0x18, "rx"),
    ])


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
    emu = booted_bis_uhrzeit
    bios = emu.mem_read(1) | (emu.mem_read(2) << 8)    # JP WBOOT → Basis + 3
    assert emu.mem_read(0) == 0xC3 and bios > 0xC000, hex(bios)
    basis = bios - 3
    for i, b in enumerate(_gast()):
        emu.mem_write(ORG + i, b)
    for eintrag in (basis + 6, basis + 9):              # CONST, CONIN
        for i, b in enumerate((0xC3, ORG & 0xFF, ORG >> 8)):
            emu.mem_write(eintrag + i, b)
    emu.key_press(ord(" ")); emu.run(300_000)          # CONIN kehrt zurück, der
    emu.key_release(ord(" "))                           # nächste Aufruf landet im Gast
    for _ in range(200):
        emu.run(100_000)
        st = emu.serial_status(V24)
        if st.format_gueltig and st.rts:
            break
    st = emu.serial_status(V24)
    assert st.format_gueltig and st.baud_nenn == 9600 and st.rts and st.dtr, st

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
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert emulator.mount_disk(0, path, "cpa780", False), emulator.last_error()
    emulator.power_on()
    assert run_until_text(emulator, "Bitte Uhrzeit eingeben!", 60_000_000)
    for z in "120000":
        emulator.key_press(ord(z)); emulator.run(300_000)
        emulator.key_release(ord(z)); emulator.run(300_000)
    emulator.key_press(0x01000004); emulator.key_release(0x01000004)
    assert run_until_text(emulator, "A>"), emulator.screen_text()
    emulator.run(2_000_000)                             # Prompt fertig, BIOS in CONIN
    return emulator


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
