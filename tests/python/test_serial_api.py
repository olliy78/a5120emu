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
        namen=["Drucker/IFSS1", "V.24", "DFÜ/IFSS2"], stecker=["X3", "X4", "X5"],
        v24=[False, True, False], takt=[0, 0, 0],
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


@pytest.mark.parametrize("maschine, namen, stecker, fest", [
    ("prg710", ["V.24", "IFSS Hauptdrucker", "ZIFSS Zusatzdrucker"], ["X4", "X6", "X5"], []),
    ("prg710-1", ["V.24", "ZIFSS Zusatzdrucker"], ["X4", "X5"], ["Tastatur K7672 (A32-B)"]),
])
def test_prg_lists_names_by_variant(maschine, namen, stecker, fest):
    """PRG 710/710-1 (AP-P4): Namen nach Gerätebeschriftung, 710-1 ohne IFSS X6 (dort die Tastatur)."""
    e = B.K1520Emulator(machine=maschine)
    assert e.serial_count() == len(namen)
    infos = [e.serial_info(i) for i in range(len(namen))]
    assert [x.name for x in infos] == namen
    assert [x.stecker for x in infos] == stecker
    assert [x.v24 for x in infos] == [True] + [False] * (len(namen) - 1)
    assert e.serial_fixed_names() == fest
    assert e.serial_info(len(namen)) is None
    del e


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


# ─── Alter Unterbau: k1520_serial_set_rx_cb / k1520_serial_send (AP-T1b) ─────
#
# Die „Weiche" der C-ABI: DFU/PRINTER → Schnittstelle der Maschine.  A5120: DFU =
# DFÜ/V.24, PRINTER = Drucker; K8915: DFU = DFÜ/IFSS2, PRINTER = Drucker/IFSS1 (Entwurf 19 §8).

def _rx_cb(liste):
    """ctypes-Rückruf, der jedes Byte in *liste* sammelt (Rückgabe festhalten!)."""
    return B.K1520SerialRxCb(lambda ctx, b: liste.append(b))


def test_k8915_old_callback_pulls_the_loop_of_its_own_channel():
    """Ein gesetzter Rückruf zieht den Loop seines Kanals, ein leerer steckt ihn
    wieder — so wird sichtbar, auf welchen Kanal die Weiche zeigt."""
    e = B.K1520Emulator(machine="k8915")
    namen = [e.serial_info(i).name for i in range(e.serial_count())]
    ifs1, ifs2, v24 = (namen.index("Drucker/IFSS1"), namen.index("DFÜ/IFSS2"),
                       namen.index("V.24"))
    assert all(e.serial_config(i).loop for i in range(e.serial_count()))
    cb = _rx_cb([])
    B._lib.k1520_serial_set_rx_cb(e._handle, 1, cb, None)          # PRINTER
    assert not e.serial_config(ifs1).loop
    assert e.serial_config(ifs2).loop and e.serial_config(v24).loop
    B._lib.k1520_serial_set_rx_cb(e._handle, 0, cb, None)          # DFU
    assert not e.serial_config(ifs2).loop
    B._lib.k1520_serial_set_rx_cb(e._handle, 1, B.K1520SerialRxCb(), None)   # abmelden
    assert e.serial_config(ifs1).loop
    assert not e.serial_config(ifs2).loop
    # Unbekannter Port: nichts geschieht (kein Absturz, keine Einstellung geändert).
    B._lib.k1520_serial_set_rx_cb(e._handle, 7, cb, None)
    B._lib.k1520_serial_send(e._handle, 7, 0x41)
    assert e.serial_config(v24).loop and e.serial_config(ifs1).loop
    del e


def test_old_callback_and_send_go_through_the_guest_and_come_back(emulator, temp_disk,
                                                                   tmp_path):
    """A5120 mit Echo-Gast auf der DFÜ/V.24: `k1520_serial_send(DFU)` legt das Byte in
    den Empfänger, der Gast schickt es zurück, der Rückruf bekommt es — samt seinem
    Kontextzeiger.  Belegt ein Transport die Schnittstelle, gehen beide ins Leere."""
    import ctypes
    from serial_gast import bis_zum_prompt, echo_gast_einsetzen
    emu = echo_gast_einsetzen(bis_zum_prompt(emulator, temp_disk))
    dfu, drucker, kontexte = [], [], []

    def nimm(ziel):
        def f(ctx, b):
            kontexte.append(ctx)
            ziel.append(b)
        return B.K1520SerialRxCb(f)

    cb_dfu, cb_dr = nimm(dfu), nimm(drucker)
    B._lib.k1520_serial_set_rx_cb(emu._handle, 0, cb_dfu, ctypes.c_void_p(0x1234))
    B._lib.k1520_serial_set_rx_cb(emu._handle, 1, cb_dr, ctypes.c_void_p(0x5678))
    daten = [0x41, 0x00, 0xFF, 0x0D, 0x7E]
    for b in daten:
        B._lib.k1520_serial_send(emu._handle, 0, b)
        emu.run(100_000)                    # > 1 Zeichenzeit bei 9600 Bd hin und zurück
    emu.run(200_000)
    assert dfu == daten
    assert drucker == []
    assert set(kontexte) == {0x1234}

    # Ein Transport belegt die Schnittstelle: Einspeisen geht ins Leere (der Gast
    # bekommt nichts, also landet auch in der Datei nichts), der Rückruf schweigt.
    datei = tmp_path / "v24.txt"
    assert emu.serial_configure(0, betriebsart=B.SER_DATEI, datei=str(datei))
    assert emu.serial_start(0)
    emu.run(10_000)        # die Belegung erreicht die Karte mit dem nächsten Blick
    B._lib.k1520_serial_send(emu._handle, 0, 0x43)
    emu.run(300_000)
    emu.serial_stop(0)
    assert dfu == daten
    assert datei.read_bytes() == b""

    # Abmelden: danach kommt nichts mehr an.
    B._lib.k1520_serial_set_rx_cb(emu._handle, 0, B.K1520SerialRxCb(), None)
    B._lib.k1520_serial_send(emu._handle, 0, 0x42)
    emu.run(300_000)
    assert dfu == daten


def _sb(*daten):
    """Telnet-Unterverhandlung der COM-PORT-Option (44) mit verdoppeltem IAC."""
    koerper = b"".join(bytes([d]) + (b"\xff" if d == 255 else b"") for d in daten)
    return b"\xff\xfa\x2c" + koerper + b"\xff\xf0"


def test_rfc2217_status_shows_format_and_lines_of_the_far_side(emu):
    """AP-S11: Server-Rolle — Format und RTS/DTR des Clients, abgeleitete Texte."""
    assert emu.serial_configure(0, betriebsart=B.SER_RFC2217, rolle=B.SER_SERVER,
                                port=freier_port(), loop=False)
    assert emu.serial_start(0)
    st = emu.serial_status(0)
    with socket.create_connection(("127.0.0.1", st.port_aktiv), timeout=3) as c:
        assert warte(lambda: emu.serial_status(0).zustand == B.SER_VERBUNDEN)
        s = emu.serial_status(0)
        assert s.format_gegenseite_text is None and s.leitungen_gegenseite_text is None
        assert s.leitung_gegenseite(B.SER_L_RTS) is None
        c.sendall(b"\xff\xfb\x2c" + _sb(2, 7) + _sb(3, 3) + _sb(4, 1)    # 7E1
                  + _sb(5, 11) + _sb(5, 9))                             # RTS an, DTR aus
        assert warte(lambda: emu.serial_status(0).format_gegenseite_bekannt)
        assert warte(lambda: emu.serial_status(0).leitungen_gegenseite_bekannt == 3)
        s = emu.serial_status(0)
        assert s.format_gegenseite_text == "7E1"
        assert s.leitung_gegenseite(B.SER_L_RTS) is True
        assert s.leitung_gegenseite(B.SER_L_DTR) is False
        assert s.leitung_gegenseite(B.SER_L_CTS) is None                # Rolle kennt sie nicht
        assert s.leitungen_gegenseite_text == "RTS"
        assert s.leitungen_gegenseite == B.SER_L_RTS
        assert s.format_abweichend == (s.format_gegenseite_text != "8N1")   # Gast: 8N1 im Ruhezustand
