"""Kaltboot über die Python-Bindung — der Weg, den auch die GUI nimmt.

Die C++-Integrationstests booten über `A5120Machine` direkt.  Hier läuft
derselbe Boot durch `libk1520core.so` + ctypes, sodass ein Fehler in der
ABI-Schicht (falscher Handle, abgeschnittener Zeiger, vertauschte Argumente)
auffällt, bevor die GUI ihn zeigt.
"""

import pytest

from conftest import requires_core, run_until_text

pytestmark = requires_core


def test_cold_boot_reaches_bootloader_banner(emulator, temp_disk):
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert emulator.mount_disk(0, path, "cpa780", False), emulator.last_error()
    emulator.power_on()

    assert run_until_text(emulator, "Bootloader, Version"), (
        "Bootloader-Banner nie erschienen:\n" + emulator.screen_text()
    )


def test_cold_boot_reaches_the_cpa_banner(booted):
    """Nach dem Bootloader lädt CP/A und meldet sich mit Version + Konfiguration."""
    # Der Banner wird zeilenweise aufgebaut; "TPA ist OK" ist die letzte Zeile,
    # erst danach steht der ganze Text auf dem Schirm.
    assert run_until_text(booted, "TPA ist OK"), (
        "CP/A-Banner nie vollständig erschienen:\n" + booted.screen_text()
    )
    screen = booted.screen_text()
    assert "CP/A, Version" in screen
    assert "Tastatur: K7637" in screen, "Tastaturtyp im Banner geändert"
    assert 'A:5"(80,DD,DS)' in screen, "Laufwerkskonfiguration im Banner geändert"


def test_cold_boot_reaches_the_clock_prompt(booted):
    """Die Uhr-Bootdiskette fragt am Ende nach der Uhrzeit."""
    assert run_until_text(booted, "Bitte Uhrzeit eingeben"), (
        "Uhrzeit-Abfrage nie erschienen:\n" + booted.screen_text()
    )
    assert "HH:MM:SS" in booted.screen_text()


def test_boot_from_hfe_image(emulator, temp_disk):
    """Derselbe Boot aus einem HFE-Abbild (anderer Lesepfad als .img)."""
    path = temp_disk("cpa_cpa780_k5601_clock.hfe")
    assert emulator.mount_disk(0, path, "cpa780", False), emulator.last_error()
    emulator.power_on()

    assert run_until_text(emulator, "Bootloader, Version"), (
        "HFE-Boot erreichte den Bootloader nicht:\n" + emulator.screen_text()
    )


def test_boot_without_disk_does_not_crash(emulator):
    """Ohne Diskette läuft die Maschine weiter (kein Absturz, kein Hänger)."""
    emulator.power_on()
    total = 0
    for _ in range(50):
        total += emulator.run(100_000)
    assert total > 0
    assert isinstance(emulator.screen_text(), str)


def test_reset_restarts_from_the_boot_rom(booted):
    """Reset bootet erneut — der Bootloader-Banner erscheint wieder."""
    booted.run(2_000_000)
    booted.reset()
    assert run_until_text(booted, "Bootloader, Version"), (
        "nach reset() kam der Bootloader nicht wieder:\n" + booted.screen_text()
    )


def test_keyboard_input_reaches_the_machine(booted):
    """Tastendruck über die Bindung erscheint als Echo auf dem Bildschirm.

    Deckt den vollen Weg GUI → `k1520_key_press` → K7637-SIO → BIOS ab: die
    Uhrzeit-Abfrage echot jede Ziffer an der Cursorposition (`00:00:00`).
    """
    assert run_until_text(booted, "Bitte Uhrzeit eingeben")
    before = booted.screen_text()

    for char in "123456":
        booted.key_press(ord(char))
        booted.run(300_000)
        booted.key_release(ord(char))
        booted.run(300_000)

    after = booted.screen_text()
    assert after != before, "Tastatureingabe hat den Bildschirm nicht verändert"
    assert "12:34:56" in after, (
        "getippte Uhrzeit erscheint nicht im Eingabefeld:\n" + after
    )


def test_selector_key_lights_the_keyboard_lamp(booted):
    """Die Tastaturanzeigen am laufenden CP/A — der ganze Weg, nicht nur das Papier.

    Gedrückt wird die Selektortaste ``0`` mit ihrem PHYSISCHEN Code 0xA0.  Was
    danach passiert, geht durch alles hindurch: BIOS kippt Bit 0 seines
    Lampenpuffers, `lampen` schickt das Kommando 52H an die Tastatur, die
    K7637 erkennt es an der FLANKENZAHL und schaltet die Anzeige G00 um.
    Nochmal gedrückt schaltet sie wieder aus — die Kommandos negieren, sie
    setzen nicht (K7637-Doku §2.2.3).
    """
    from app.ui.keyboard import LED_G00, LED_G01, raw

    assert run_until_text(booted, "Bitte Uhrzeit eingeben")
    for char in "120000":
        booted.key_press(ord(char)); booted.run(300_000)
        booted.key_release(ord(char)); booted.run(300_000)
    booted.key_press(0x01000004); booted.key_release(0x01000004)   # ET1
    assert run_until_text(booted, "A>"), "CP/A kam nicht zum Prompt"

    assert booted.keyboard_leds() == 0, "am Prompt brennt noch keine Anzeige"

    def taste(code):
        booted.key_press(raw(code)); booted.key_release(raw(code))
        booted.run(6_000_000)

    taste(0xA0)                                   # Selektor 0
    assert booted.keyboard_leds() == LED_G00
    taste(0xA1)                                   # Selektor 1 kommt dazu
    assert booted.keyboard_leds() == LED_G00 | LED_G01
    taste(0xA0)                                   # und Selektor 0 wieder aus
    assert booted.keyboard_leds() == LED_G01
