"""Der Python-Wrapper `K1520Emulator` — Verhalten, nicht nur Erreichbarkeit.

`test_c_api.py` prüft, dass die Signaturen zusammenpassen; hier geht es um die
Semantik der Wrapper-Methoden: Rückgabetypen, Fehlerpfade, Zustandswechsel.
"""

import pytest

from conftest import requires_core

pytestmark = requires_core


def test_create_with_drive_configuration():
    """`K1520Emulator(drive_types)` verdrahtet die Laufwerksbestückung."""
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(["K5601", "MF3200", "none", "none"])
    assert emu.drive_types == ["K5601", "MF3200", "none", "none"]
    # Slot 1 ist ein 8"-Laufwerk → sein Formatangebot unterscheidet sich von A:.
    assert emu.drive_formats(0) != emu.drive_formats(1)


def test_unknown_drive_type_falls_back_silently():
    """Dokumentiert das IST-Verhalten: der Kern lehnt unbekannte Profile NICHT ab.

    `k1520_create_configured` fällt still auf das Standardprofil (K5601) zurück
    und `k1520_last_init_error` bleibt leer.  Der Schutz liegt allein auf der
    GUI-Seite in `app.drive_types.normalize()` — wer die C-API direkt benutzt,
    bekommt einen stillschweigend anderen Laufwerkstyp.
    """
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(["gibtsnicht", "K5601", "K5601", "none"])
    assert emu.drive_formats(0) == emu.drive_formats(1)   # = K5601-Angebot


def test_run_returns_executed_cycles(emulator):
    """`run()` läuft bis zur nächsten Befehlsgrenze — leichte Überschreitung ok."""
    emulator.power_on()
    done = emulator.run(50_000)
    assert 50_000 <= done < 50_000 + 100


def test_framebuffer_size_and_dirty_flag(emulator, temp_disk):
    """Der Framebuffer wird beim Booten beschrieben und meldet sich als dirty."""
    emulator.mount_disk(0, temp_disk("cpa_cpa780_k5601_clock.img"), "cpa780", False)
    emulator.power_on()
    emulator.clear_framebuffer_dirty_flag()

    for _ in range(200):
        emulator.run(100_000)
        if emulator.is_framebuffer_dirty():
            break
    assert emulator.is_framebuffer_dirty(), "Framebuffer wurde beim Booten nie dirty"

    fb = emulator.get_framebuffer()
    assert len(fb) == 640 * 288, "Framebuffer ist nicht 640x288 x 1 Byte"
    assert any(fb), "Framebuffer ist komplett schwarz"

    emulator.clear_framebuffer_dirty_flag()
    assert not emulator.is_framebuffer_dirty()


def test_mount_unmount_cycle(emulator, temp_disk):
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert not emulator.is_disk_active(0)

    assert emulator.mount_disk(0, path, "cpa780", False), emulator.last_error()
    assert emulator.is_disk_active(0)
    assert not emulator.is_disk_write_protected(0)

    assert emulator.unmount_disk(0)
    assert not emulator.is_disk_active(0)


def test_mount_with_write_protection(emulator, temp_disk):
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert emulator.mount_disk(0, path, "cpa780", True), emulator.last_error()
    assert emulator.is_disk_write_protected(0)

    emulator.set_disk_write_protect(0, False)
    assert not emulator.is_disk_write_protected(0)


def test_mount_rejects_unknown_format(emulator, temp_disk):
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert not emulator.mount_disk(0, path, "kein_echtes_format", False)
    assert emulator.last_error()


def test_create_disk_writes_a_mountable_image(emulator, tmp_path):
    """`create_disk` legt eine gültig formatierte Leerdiskette an und mountet sie."""
    target = tmp_path / "leer.img"
    assert emulator.create_disk(0, str(target), "cpa780", False), emulator.last_error()
    assert target.exists() and target.stat().st_size > 0
    assert emulator.is_disk_active(0)

    # Erneut mountbar — also ein gültiges Abbild, kein halber Schreibvorgang.
    other = None
    from app.core_binding.k1520 import K1520Emulator
    other = K1520Emulator()
    assert other.mount_disk(0, str(target), "cpa780", True), other.last_error()


def test_disk_notice_reports_the_track_pitch_adaptation(emulator, tmp_path):
    """40-Spur-Diskette im 80-Spur-Laufwerk (Slot 0 = K5601) → Doppelschritt-Hinweis."""
    target = tmp_path / "vierzig_spuren.img"
    assert emulator.create_disk(0, str(target), "k5601_ss40_26x128", False), \
        emulator.last_error()
    assert emulator.disk_notice(0) == "Double Step aktiviert"

    # Eine Diskette, die zum Laufwerk passt, meldet nichts.
    passend = tmp_path / "achtzig_spuren.img"
    assert emulator.create_disk(1, str(passend), "cpa780", False), emulator.last_error()
    assert emulator.disk_notice(1) == ""

    # Leeres Laufwerk ebenso.
    assert emulator.unmount_disk(0)
    assert emulator.disk_notice(0) == ""


def test_drive_status_flags_are_boolean(emulator, temp_disk):
    emulator.mount_disk(0, temp_disk("cpa_cpa780_k5601_clock.img"), "cpa780", False)
    emulator.power_on()
    emulator.run(200_000)
    for value in (emulator.is_disk_led_on(0), emulator.is_motor_on(0),
                  emulator.is_head_loaded()):
        assert isinstance(value, bool)


def test_motor_starts_during_boot(emulator, temp_disk):
    """Der Spindelmotor läuft während des Bootens an (Laufwerksanzeige der GUI)."""
    emulator.mount_disk(0, temp_disk("cpa_cpa780_k5601_clock.img"), "cpa780", False)
    emulator.power_on()
    for _ in range(100):
        emulator.run(100_000)
        if emulator.is_motor_on(0):
            break
    assert emulator.is_motor_on(0), "Motor lief während des Bootens nie an"


def test_screen_text_shape(emulator):
    """`screen_text()` liefert 24 Zeilen à 80 Zeichen."""
    emulator.power_on()
    emulator.run(100_000)
    lines = emulator.screen_text().split("\n")
    assert len(lines) == 24
    assert all(len(line) == 80 for line in lines)


def test_pc1715w_bootet_scp30_bis_prompt(temp_disk):
    """PC 1715W (AP-W3): ``machine="pc1715w"`` — S550 → SCP-3.0-Lader → SCP3.SYS → ``A>``,
    Bild über ``k1520_screen_char`` (Bild-RAM der CRT-Karte), ``dir`` über die Tastatur."""
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(machine="pc1715w")
    assert emu.machine_type() == 3
    assert emu.mount_disk(0, temp_disk("pc1715w_scp30_system.hfe"), "cpa800"), emu.last_error()
    emu.power_on()
    for _ in range(1500):
        emu.run(100_000)
        if "A>modcs sc619.zgf[1]" in emu.screen_text() and emu.screen_text().count("A>") >= 2:
            break
    text = emu.screen_text()
    assert "PC 1715W" in text and "SCP 3.0" in text, text
    for _ in range(50):
        emu.run(100_000)
    for ch in "dir\r":
        code = 0x01000004 if ch == "\r" else ord(ch)
        emu.key_press(code)
        for _ in range(30):
            emu.run(5_000)
        emu.key_release(code)
        for _ in range(20):
            emu.run(5_000)
    for _ in range(200):
        emu.run(50_000)
        if "PROFILE  SUB" in emu.screen_text():
            break
    assert "SCP3     SYS" in emu.screen_text(), emu.screen_text()


def test_pc1715_c_abi_komplett(tmp_path, temp_disk):
    """PC 1715 (AP-4b): jede maschinenneutrale C-ABI-Funktion wirkt am Gerät oder meldet
    ihren Ruhewert — Boot von der SCP-Diskette bis `A>`, Format erkannt, Lampen, Tastatur
    (`dir` + Return), Zeile 24 (CP/A-Statuszeile) lesbar, Reset, NMI, Leerdiskette,
    save_as und PRG/EPROM-Funktionen am falschen Gerät."""
    from app.core_binding.k1520 import K1520Emulator, _lib

    emu = K1520Emulator(machine="pc1715")
    assert emu.machine_type() == 3
    assert (_lib.k1520_fb_width(emu._handle), _lib.k1520_fb_height(emu._handle)) == (640, 300)
    path = temp_disk("pc1715_scp1715_v0006_boot.hfe")
    assert emu.mount_disk(0, path, "cpa800"), emu.last_error()
    assert emu.detected_format(0) != ""
    assert emu.disk_notice(0) == ""
    emu.power_on()
    for _ in range(400):
        emu.run(100_000)
        if "A>" in emu.screen_text():
            break
    assert "A>" in emu.screen_text()
    # Tastatur: eine Taste braucht Entprellung (150 000 halten, 100 000 Pause)
    for ch in "dir\r":
        code = 0x01000004 if ch == "\r" else ord(ch)
        emu.key_press(code)
        for _ in range(30):
            emu.run(5_000)
        emu.key_release(code)
        for _ in range(20):
            emu.run(5_000)
    for _ in range(100):
        emu.run(50_000)
    assert "INSTSCP" in emu.screen_text()
    # Statuszeile (Zeile 24) ist erreichbar, ausserhalb liefert 0
    assert _lib.k1520_screen_char(emu._handle, 0, 24) in range(256)
    assert _lib.k1520_screen_char(emu._handle, 0, 25) == 0
    assert _lib.k1520_screen_char(emu._handle, 80, 0) == 0
    assert isinstance(emu.is_disk_led_on(0), bool) and isinstance(emu.is_motor_on(0), bool)
    assert emu.panel_lamps() == 0 and emu.bell_count() == 0
    emu.nmi()
    emu.run(50_000)
    assert emu.mem_read(0x0000) in range(256) and emu.io_read(0x20) in range(256)
    # Leerdiskette in Laufwerk B, als .hfe weggeschrieben
    assert emu.create_disk(1, str(tmp_path / "leer.hfe"), ""), emu.last_error()
    assert emu.save_disk_as(1, str(tmp_path / "leer2.hfe")), emu.last_error()
    assert emu.disk_path(1).endswith("leer2.hfe")
    # Reset: Urlader läuft wieder an
    emu.reset()
    assert emu.run(100_000) > 0
    # Funktionen fremder Maschinen melden Ruhewerte
    assert _lib.k1520_prg710_variant(emu._handle) == -1
    assert _lib.k1520_eprom_type(emu._handle) == -1
