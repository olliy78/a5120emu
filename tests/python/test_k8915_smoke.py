"""K8915 über die Python-Bindung — der Weg, den die Oberfläche nehmen wird.

doc/design/16_k8915.md §8a AP-E4b: `libk1520core` enthält seit dort die
`K8915Machine`.  Die C++-Wächter (`k1520_test_k8915_scpx`) fahren die Maschine
direkt; hier läuft derselbe Kaltstart durch ctypes, und die Taste kommt — wie aus
der Oberfläche — aus einem ANDEREN Faden als dem, der `k1520_run` ruft.

Das Bild wird über `k1520_screen_char` gelesen (von der K7024), nicht über
`k1520_mem_read`: bei A8H = 87H liegt unter 1000H das RAM der ZRE.
"""

import threading

from conftest import requires_core, run_until_text

pytestmark = requires_core

QT_KEY_RETURN = 0x01000004


def _letzte_zeile(emu) -> str:
    zeilen = [z.rstrip(" \x00") for z in emu.screen_text().split("\n")]
    zeilen = [z for z in zeilen if z]
    return zeilen[-1] if zeilen else ""


def test_k8915_boots_to_the_prompt_with_a_key_from_another_thread(temp_disk):
    from app.core_binding.k1520 import K1520Emulator

    emu = K1520Emulator(machine="k8915")
    assert emu.machine == "k8915"
    assert emu.machine_type() == 2

    # Netz-Ein OHNE Diskette: Selbsttest (≈ 29 Mio. Takte) bis zur Coldstart-Meldung.
    emu.power_on()
    assert emu.panel_lamps() == 0xFF
    assert run_until_text(emu, "* Coldstart *  Disk on A: ready", 80_000_000), (
        "Coldstart-Meldung nie erschienen:\n" + emu.screen_text())

    # Diskette einlegen, wie der Anwender es an dieser Stelle täte (TempDisk-Kopie).
    path = temp_disk("k8915scpx_boot1.hfe")
    assert emu.mount_disk(0, path, "cpa800", False), emu.last_error()

    # CR aus einem zweiten Faden, während dieser Faden die Maschine fährt.
    los = threading.Event()
    fertig = threading.Event()

    def taste():
        los.wait(10)
        emu.key_press(QT_KEY_RETURN)
        emu.key_release(QT_KEY_RETURN)
        fertig.set()

    faden = threading.Thread(target=taste, daemon=True)
    faden.start()
    los.set()
    # Solange der Faden noch nicht gedrückt hat, weiterfahren — ohne Taste keine Ladung.
    for _ in range(2000):
        emu.run(100_000)
        if fertig.is_set():
            break
    faden.join(10)
    assert fertig.is_set(), "Tastenfaden kam nicht zum Zug"

    # Letzte Meldung von `rade` (nach dem RAM-Test der vier Viertel von Bank 2),
    # wie in K8915Scpx.KaltstartVomNetzEinBisZumPrompt.
    assert run_until_text(emu, "size: 1 kByte groups 0 ... 03FH", 200_000_000), (
        "RAM-Disk-Meldung nach dem Laden nie erschienen:\n" + emu.screen_text())
    for _ in range(200):
        if _letzte_zeile(emu) == "A>":
            break
        emu.run(100_000)
    bild = emu.screen_text()
    assert _letzte_zeile(emu) == "A>", bild
    assert "SCPX 8915  V 5.3" in bild
    assert "E: containing no files" in bild
    assert "ERR" not in bild, bild

    # Anzeigefeld 61H nach dem Laden: „bereit" (aktiv low, B0H wie in K8915Scpx).
    assert emu.panel_lamps() == 0xB0
    assert isinstance(emu.bell_count(), int)
    assert isinstance(emu.keyboard_leds(), int)
