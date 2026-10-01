"""C-ABI der seriellen Schnittstellen (`k1520_serial_*`, Entwurf 19 §8, AP-S6).

Kern-Bedienung ohne GUI: Anzahl/Namen/Stecker, Einstellen mit Sperren, Start/Stopp der
Betriebsarten über Loopback.  Ports werden NIE fest angenommen — ein freier wird beim
Test erfragt, ein Server meldet den tatsächlichen in `port_aktiv`.
"""

import socket
import time

import pytest

from conftest import requires_core

pytestmark = requires_core

from app.core_binding import k1520 as B  # noqa: E402

ERWARTET = {
    "a5120": dict(
        namen=["DFÜ/V.24", "DFÜ/IFSS", "Drucker"], stecker=["X6", "X5", "X3"],
        v24=[True, False, False], takt=[2, 2, 0],
        fest=["Tastatur K7637 (X4)"]),
    "k8915": dict(
        namen=["V.24", "IFS 1", "IFS 2"], stecker=["X3", "X4", "X5"],
        v24=[True, False, False], takt=[0, 0, 0],
        fest=["Tastatur K7672"]),
}


@pytest.fixture(params=["a5120", "k8915"])
def emu(request):
    e = B.K1520Emulator(machine=request.param)
    e.name = request.param
    yield e
    for i in range(e.serial_count()):
        e.serial_stop(i)
    del e


def freier_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def warte(bedingung, frist=4.0):
    ende = time.monotonic() + frist
    while time.monotonic() < ende:
        w = bedingung()
        if w:
            return w
        time.sleep(0.02)
    return bedingung()


def server_einstellen(e, i, **extra):
    """Telnet-Server auf freiem Port, ohne Loop (der K8915 steckt vorgabemäßig im Loop)."""
    assert e.serial_configure(i, betriebsart=B.SER_TELNET, rolle=B.SER_SERVER,
                              port=freier_port(), loop=False, **extra)


def test_list_names_connectors_v24_and_clock_sources(emu):
    soll = ERWARTET[emu.name]
    assert emu.serial_count() == 3
    infos = [emu.serial_info(i) for i in range(3)]
    assert [x.name for x in infos] == soll["namen"]
    assert [x.stecker for x in infos] == soll["stecker"]
    assert [x.v24 for x in infos] == soll["v24"]
    assert [len(x.taktquellen) for x in infos] == soll["takt"]
    assert emu.serial_fixed_names() == soll["fest"]
    assert emu.serial_info(3) is None and emu.serial_info(-1) is None
    assert emu.serial_status(3) is None and emu.serial_config(3) is None


def test_fixed_name_is_cut_at_a_character_boundary(emu):
    import ctypes
    buf = ctypes.create_string_buffer(8)
    assert B._lib.k1520_serial_fixed_name(emu._handle, 0, buf, len(buf))
    assert buf.value == b"Tastatur"[:7]            # n-1 Bytes + Nullterminator
    assert not B._lib.k1520_serial_fixed_name(emu._handle, 1, buf, len(buf))


@pytest.mark.parametrize("text, art", [
    ("192.168.1.5", B.HOST_IPV4), ("::1", B.HOST_IPV6), ("[fe80::1%eth0]", B.HOST_IPV6),
    ("ser2net.local", B.HOST_NAME), ("", B.HOST_UNGUELTIG), ("300.1.1.1", B.HOST_UNGUELTIG),
    ("-x.de", B.HOST_UNGUELTIG),
])
def test_classify_host(text, art):
    assert B.classify_host(text) == art


def test_configure_roundtrip_and_validation(emu):
    assert emu.serial_configure(0, betriebsart=B.SER_RFC2217, rolle=B.SER_CLIENT,
                                host="ser2net.örtlich", port=4001, loop=False,
                                xonxoff=True, datei="/tmp/ä.log")
    k = emu.serial_config(0)
    assert (k.betriebsart, k.rolle, k.host, k.port, k.xonxoff, k.datei) == \
        (B.SER_RFC2217, B.SER_CLIENT, "ser2net.örtlich", 4001, True, "/tmp/ä.log")
    before = emu.serial_config(0)
    assert not emu.serial_configure(0, port=0)                  # Port 0 an der C-ABI abgewiesen
    assert not emu.serial_configure(0, betriebsart=7)
    assert not emu.serial_configure(0, taktquelle=9)
    assert not emu.serial_configure(9, port=1234)               # Index
    assert emu.serial_config(0) == before                        # nichts übernommen
    with pytest.raises(KeyError):
        emu.serial_configure(0, gibtesnicht=1)


def test_strings_are_truncated_and_terminated(emu):
    lang = "ä" * 300                                             # 600 Bytes UTF-8, Feld 256
    assert emu.serial_configure(0, host=lang, loop=False)
    host = emu.serial_config(0).host
    assert host and set(host) == {"ä"} and len(host.encode()) <= 255


def test_groesse_rule(emu):
    """Zu kleine `groesse`: Ausgabe nur so weit, Eingabe behält den Rest."""
    import ctypes
    s = B.K1520SerStatus()
    s.groesse = 2
    assert not B._lib.k1520_serial_status(emu._handle, 0, ctypes.byref(s))
    s.groesse = B.K1520SerStatus.port_aktiv.offset        # nur groesse + zustand
    s.baud_nenn = 12345                                   # darf nicht überschrieben werden
    assert B._lib.k1520_serial_status(emu._handle, 0, ctypes.byref(s))
    assert s.groesse == B.K1520SerStatus.port_aktiv.offset and s.baud_nenn == 12345
    k = B.K1520SerKonfig()
    k.groesse = B.K1520SerKonfig.host.offset              # nur Betriebsart + Rolle
    k.betriebsart, k.rolle, k.port = B.SER_DATEI, B.SER_CLIENT, 0   # Port liegt jenseits
    before = emu.serial_config(0)
    assert B._lib.k1520_serial_configure(emu._handle, 0, ctypes.byref(k))
    nach = emu.serial_config(0)
    assert (nach.betriebsart, nach.rolle) == (B.SER_DATEI, B.SER_CLIENT)
    assert (nach.host, nach.port) == (before.host, before.port)
    k.groesse = 0
    assert not B._lib.k1520_serial_configure(emu._handle, 0, ctypes.byref(k))


def test_telnet_server_connects_and_locks_fields(emu):
    server_einstellen(emu, 0)
    assert emu.serial_start(0)
    st = emu.serial_status(0)
    assert st.zustand == B.SER_LAUSCHT and st.port_aktiv > 0
    with socket.create_connection(("127.0.0.1", st.port_aktiv), timeout=3) as c:
        assert warte(lambda: emu.serial_status(0).zustand == B.SER_VERBUNDEN)
        assert emu.serial_status(0).gegenstelle.startswith("127.0.0.1:")
        # Betrieb: gesperrte Felder abgewiesen, freie übernommen.
        assert not emu.serial_configure(0, port=freier_port())
        assert not emu.serial_configure(0, rolle=B.SER_CLIENT)
        assert emu.serial_configure(0, xonxoff=True)
        c.sendall(b"x")
    emu.serial_stop(0)
    st = emu.serial_status(0)
    assert st.zustand == B.SER_AUS
    assert emu.serial_configure(0, port=freier_port())           # nach Stopp wieder frei


def test_start_refused_while_loop_is_set(emu):
    assert emu.serial_configure(0, loop=True)
    assert not emu.serial_start(0)
    assert emu.serial_status(0).zustand == B.SER_AUS


def test_file_mode_writes_into_a_temp_directory(emu, tmp_path):
    ziel = tmp_path / "ausgabe.txt"
    assert emu.serial_configure(0, betriebsart=B.SER_DATEI, datei=str(ziel), loop=False)
    assert emu.serial_start(0)
    assert emu.serial_status(0).zustand == B.SER_VERBUNDEN
    assert ziel.exists()
    emu.serial_stop(0)
    assert emu.serial_status(0).zustand == B.SER_AUS


def test_start_auto_with_a_busy_port_fails_and_proposes_another(emu):
    with socket.socket() as belegt:
        belegt.bind(("", 0))
        belegt.listen(1)
        port = belegt.getsockname()[1]
        assert emu.serial_configure(0, betriebsart=B.SER_TELNET, rolle=B.SER_SERVER,
                                    port=port, loop=False)
        assert not emu.serial_start_auto(0)
        st = emu.serial_status(0)
        assert st.zustand == B.SER_AUS
        assert st.port_vorschlag not in (0, port) and st.meldung
    # Frei: Wiederaufnahme klappt auf dem eingestellten Port.
    assert emu.serial_start_auto(0)
    assert emu.serial_status(0).port_aktiv == port


def test_client_retries_until_the_server_appears(emu):
    port = freier_port()
    assert emu.serial_configure(0, betriebsart=B.SER_TELNET, rolle=B.SER_CLIENT,
                                host="127.0.0.1", port=port, loop=False)
    assert emu.serial_start(0)
    assert warte(lambda: emu.serial_status(0).versuche >= 1)
    assert emu.serial_status(0).zustand == B.SER_VERBINDET
    with socket.socket() as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(1)
        srv.settimeout(4)
        gegen, _ = srv.accept()                              # Dauerversuch nimmt es auf
        with gegen:
            assert warte(lambda: emu.serial_status(0).zustand == B.SER_VERBUNDEN)
    emu.serial_stop(0)
    assert emu.serial_status(0).zustand == B.SER_AUS
