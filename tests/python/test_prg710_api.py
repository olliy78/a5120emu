"""PRG 710 / PRG 710-1 über die Python-Bindung (AP-P5b) — der Weg der Oberfläche (P5d).

C++-Wächter (`Prg710Boot.*`, `Prg710Udos.*`) fahren die Maschine direkt; hier läuft
dasselbe durch ctypes: Bauen beider Varianten, Starttaste über die C-ABI
(`QK_TASTE_BASE | Position` bzw. Qt-Return), Bild über `k1520_screen_char`
(nie `mem_read`: die CPU sieht das VRAM nur bei E8H[F] = FFH), Laufwerksanzeigen,
Speicherverwaltung, Schnittstellenbelegung je Variante (AP-P4).
"""

import pytest

from conftest import requires_core, run_until_text

pytestmark = requires_core

QT_KEY_RETURN = 0x01000004
VARIANTEN = [("prg710", 0), ("prg710-1", 1)]
FIXTURE = {"prg710": "prg710_udos43_k5601_mrs_boot.hfe",
           "prg710-1": "prg710-1_udos43_k5601_v43_189.hfe"}


def _zeilen(emu):
    # Nicht druckbare Zeichen (00H, 0DH der Zeilenenden) zählen wie Leerraum.
    return ["".join(c if " " <= c < "\x7f" else " " for c in z).rstrip()
            for z in emu.screen_text().split("\n")]


def _tippe(emu, taste, schritte=1):
    emu.key_press(taste)
    emu.run(100_000 * schritte)
    emu.key_release(taste)
    emu.run(100_000 * schritte)


@pytest.mark.parametrize("name,variante", VARIANTEN)
def test_baut_beide_varianten(name, variante):
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(machine=name)
    assert emu.machine == name
    assert emu.machine_type() == 1
    assert emu.prg_variant == variante
    # Speicherverwaltung nach /RESET: Abbildung aus, alle Register 0.
    emu.power_on()
    assert emu.prg_freigabe() == 0
    assert emu.prg_page(0) is not None and emu.prg_page(15) is not None
    assert emu.prg_page(16) is None and emu.prg_page(-1) is None


def test_andere_maschinen_haben_keine_prg_diagnose():
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(machine="a5120")
    assert emu.prg_variant is None
    assert emu.prg_freigabe() is None
    assert emu.prg_page(0) is None


def test_schnittstellen_je_variante():
    """AP-P4: 710 = 3 Anschlüsse, 710-1 = 2 (Kanal B trägt die Tastatur K7672)."""
    from app.core_binding.k1520 import K1520Emulator

    e710 = K1520Emulator(machine="prg710")
    assert e710.serial_count() == 3
    assert e710.serial_fixed_names() == []
    assert all(e710.serial_info(i) is not None for i in range(3))
    assert e710.serial_info(3) is None

    e1 = K1520Emulator(machine="prg710-1")
    assert e1.serial_count() == 2
    assert e1.serial_fixed_names() == ["Tastatur K7672 (A32-B)"]
    assert all(e1.serial_info(i) is not None for i in range(2))
    assert e1.serial_info(2) is None


@pytest.mark.parametrize("name,variante", VARIANTEN)
def test_ohne_diskette_nkm_loader_und_bei_start_diskerror(name, variante):
    """Netz-Ein: „NKM-LOADER“, das ROM wartet auf die Starttaste.  Return (710: ET1,
    710-1: ENTER) startet das Laden — ohne Diskette „DISKERROR C2“ (710)."""
    import time
    from app.core_binding.k1520 import K1520Emulator

    t0 = time.monotonic()
    emu = K1520Emulator(machine=name)
    emu.power_on()
    assert run_until_text(emu, "NKM-LOADER", 20_000_000), "\n".join(_zeilen(emu))
    emu.run(3_000_000)
    assert _zeilen(emu)[0] == "NKM-LOADER"
    assert all(z == "" for z in _zeilen(emu)[1:]), "vor der Taste nichts weiter"

    # Die Taste kommt über die C-ABI an: ohne sie bliebe es bei einer Zeile.
    _tippe(emu, QT_KEY_RETURN)
    emu.run(10_000_000)
    zeilen = _zeilen(emu)
    assert zeilen[0] == "NKM-LOADER"
    if variante == 0:
        assert zeilen[1] == "DISKERROR C2", "\n".join(zeilen)
        assert zeilen[2] == "DISKERROR C2", "\n".join(zeilen)
    else:
        assert any(z for z in zeilen[1:]), "ENTER am 710-1 hat nichts bewirkt"
    assert time.monotonic() - t0 < 5, "Wandzeit"


@pytest.mark.parametrize("name,variante", VARIANTEN)
def test_udos_bootet_bis_zum_datum_und_anzeigen_folgen_dem_laufwerk(name, variante, temp_disk):
    from app.core_binding.k1520 import K1520Emulator, QK_TASTE_BASE, K7609_ET1

    emu = K1520Emulator(machine=name)
    path = temp_disk(FIXTURE[name])
    assert emu.mount_disk(0, path, "cpa800", False), emu.last_error()
    assert emu.is_motor_on(0) is False and emu.is_disk_led_on(0) is False

    emu.power_on()
    emu.run(3_000_000)
    # 710: Position ET1 der Bildschirmtastatur (K7609); 710-1: Qt-Return → ENTER.
    # Beide Wege sind dieselbe C-ABI-Funktion `k1520_key_press`.
    start = (QK_TASTE_BASE | K7609_ET1) if variante == 0 else QT_KEY_RETURN
    emu.key_press(start)
    emu.run(100_000)
    emu.key_release(start)

    gesehen = {"led": False, "motor": False}
    erreicht = False
    for _ in range(6000):
        emu.run(20_000)
        gesehen["led"] |= bool(emu.is_disk_led_on(0))
        gesehen["motor"] |= bool(emu.is_motor_on(0))
        if "Neues Datum" in emu.screen_text():
            erreicht = True
            break
    assert erreicht, "\n".join(_zeilen(emu))
    assert gesehen["led"] and gesehen["motor"], gesehen
    # UDOS im Arbeitsmodell §4b: Abbildung frei, Seiten 0–E OPS-RAM in Identität.
    emu.run(1_000_000)
    assert emu.prg_freigabe() == 0x0F
    for n in range(15):
        attr, seite = emu.prg_page(n)
        assert attr & 0x0F == 0 and seite == n, (n, attr, seite)


@pytest.mark.parametrize("name,variante", VARIANTEN)
def test_wiederholung_in_echtzeit_ist_am_prg_ansprechbar(name, variante):
    from app.core_binding.k1520 import _lib, K1520Emulator

    emu = K1520Emulator(machine=name)
    # Kein Absturz an beiden Varianten (710-1: K7672, 710: 8279 wiederholt nicht).
    _lib.k1520_set_key_repeat_realtime(emu._handle, True)
    _lib.k1520_set_key_repeat_realtime(emu._handle, False)


# ─── EPROMmer (AP-P7b): virtueller Sockel über die C-ABI ─────────────────────

@pytest.mark.parametrize("name,variante", VARIANTEN)
def test_eprom_sockel_ueber_die_bindung(name, variante, tmp_path):
    """Einlegen (Datei, leer), Inhalt, Speichern, UV-Löschen, Entnehmen, Protokoll."""
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(machine=name)
    assert emu.has_eprommer()
    assert emu.eprom_type() == 0                      # Sockel leer
    abbild = tmp_path / "quelle.bin"
    abbild.write_bytes(bytes(range(256)) * 3)          # 768 B → U555, aufgefüllt
    emu.eprom_insert(str(abbild))
    assert emu.eprom_type() == 1
    inhalt = emu.eprom_read()
    assert len(inhalt) == 1024 and inhalt[:768] == bytes(range(256)) * 3
    assert inhalt[768:] == b"\xff" * 256
    assert emu.eprom_path() == str(abbild) and not emu.eprom_modified()

    assert emu.eprom_erase() and emu.eprom_modified()
    ziel = tmp_path / "geloescht.bin"
    emu.eprom_save(str(ziel))
    assert ziel.read_bytes() == b"\xff" * 1024 and not emu.eprom_modified()
    assert emu.eprom_path() == str(ziel)

    # Aus dem Speicher (die Oberfläche trägt so den Sockel auf eine neue Maschine).
    emu.eprom_insert_data(b"\x12\x34", 2, "x.bin", True)
    assert emu.eprom_type() == 2 and emu.eprom_modified() and emu.eprom_path() == "x.bin"
    assert emu.eprom_read()[:3] == b"\x12\x34\xff"

    assert emu.eprom_insert_blank(2) and emu.eprom_type() == 2
    assert emu.eprom_read() == b"\xff" * 2048
    with pytest.raises(OSError, match="2048"):
        zu_gross = tmp_path / "gross.bin"
        zu_gross.write_bytes(b"\0" * 4096)
        emu.eprom_insert(str(zu_gross))
    assert emu.eprom_remove() and emu.eprom_type() == 0

    neu = emu.eprom_log(only_new=True)
    assert any("U555 eingelegt" in z for z in neu)
    assert any("entnommen" in z for z in neu)
    assert emu.eprom_log(only_new=True) == []         # jede Zeile nur einmal
    assert len(emu.eprom_log()) == len(neu)           # Ringpuffer bleibt

    # Vor dem ersten Zugriff des Programms: Steuerregister 00H, Typ aus 84H (nach
    # /RESET offen = 1 → 2 KB).
    emu.power_on()
    assert emu.eprom_control() == 0
    assert emu.eprom_selected_type() == 2


def test_andere_maschinen_haben_keinen_eprommer():
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(machine="a5120")
    assert not emu.has_eprommer()
    assert emu.eprom_type() == -1 and emu.eprom_read() == b""
    assert not emu.eprom_insert_blank(2)
    with pytest.raises(OSError, match="kein EPROMmer"):
        emu.eprom_save("/tmp/x.bin")

@pytest.mark.parametrize("name,variante", VARIANTEN)
def test_lochband_ueber_die_c_abi(name, variante, tmp_path):
    """AP-P8b: Band einlegen/entnehmen, Stand, Stanzband leer speichern, Stanzer aus/ein.
    Das Stanzen selbst prüft `Prg710Lochband.*` (UDOS); hier nur der Weg durch die C-ABI."""
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(machine=name)
    band = tmp_path / "band ä.ptp"           # Umlaut: UTF-8-Pfad bis in den Kern
    band.write_bytes(b"HALLO\r\n")
    assert emu.ptape_reader_status() == {"inserted": False, "pos": 0, "len": 0, "at_end": False}
    assert emu.ptape_load(str(band))
    assert emu.ptape_reader_status() == {"inserted": True, "pos": 0, "len": 7, "at_end": False}
    assert not emu.ptape_load(str(tmp_path / "fehlt.ptp"))
    assert emu.ptape_eject()
    assert emu.ptape_reader_status()["inserted"] is False

    assert emu.ptape_punch_length() == 0
    ziel = tmp_path / "stanz.ptp"
    assert emu.ptape_punch_save(str(ziel))
    assert ziel.read_bytes() == b""
    assert emu.ptape_punch_enabled() is True
    assert emu.ptape_punch_enable(False) and emu.ptape_punch_enabled() is False
    assert emu.ptape_punch_enable(True) and emu.ptape_punch_clear()


def test_lochband_gibt_es_nur_am_prg():
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(machine="a5120")
    assert emu.ptape_reader_status() is None
    assert emu.ptape_punch_length() is None
    assert emu.ptape_punch_enabled() is None
    assert not emu.ptape_load("egal")
