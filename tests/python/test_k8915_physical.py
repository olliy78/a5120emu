"""K8915 an einer „physischen“ Diskette — Ersatzsitzung ohne Greaseweazle (AP-E4k).

Das Gegenstück zu `k1520_test_k8915_physical` (C++) auf der Python-Seite: derselbe
Weg wie im Programm — `app.gw.Sync` + `TrackWorker` mit einem Ersatzlaufwerk über
`k8915scpx_boot1.hfe`, angemeldet über `K1520Emulator.mount_physical` (bzw. den
„Physisch…“-Knopf im Laufwerkskasten des k8915emu).  `greaseweazle` wird nicht
importiert; die Verfügbarkeitsprüfung der Bedienwege ist durch den Ersatz ersetzt
(`hosttools_gelten_als_vorhanden`, wie in test_gw_gui.py).
"""

import pytest

from conftest import requires_core, run_until_text
from gw_fake import fake_session

pytestmark = requires_core

FIXTURE = "k8915scpx_boot1.hfe"
QT_KEY_RETURN = 0x01000004


@pytest.fixture(autouse=True)
def hosttools_gelten_als_vorhanden(monkeypatch):
    import app.gw as gw
    monkeypatch.setattr(gw, "verfuegbar", lambda: True)


@pytest.fixture
def hfe(fixture_disks):
    return fixture_disks / FIXTURE


def _letzte_zeile(emu) -> str:
    zeilen = [z.rstrip(" \x00") for z in emu.screen_text().split("\n")]
    zeilen = [z for z in zeilen if z]
    return zeilen[-1] if zeilen else ""


def test_k8915_kaltstart_von_einer_spurweisen_scheibe_bis_zum_prompt(hfe):
    """Spuren kommen einzeln und mit 60 ms Verzögerung; die Maschinenzeit steht dabei."""
    from app.core_binding.k1520 import K1520Emulator

    sitzung = fake_session(hfe, read_ahead=False, for_emulator=True, verzoegerung=0.06)
    emu = K1520Emulator(machine="k8915")
    try:
        assert emu.mount_physical(0, sitzung.sync, True), emu.last_error()
        assert sitzung.sync.stats.tracks_known == 0, "beim Einlegen wird nichts gelesen"
        emu.power_on()
        assert run_until_text(emu, "* Coldstart *  Disk on A: ready", 80_000_000), (
            emu.screen_text())
        emu.key_press(QT_KEY_RETURN)
        emu.key_release(QT_KEY_RETURN)
        assert run_until_text(emu, "size: 1 kByte groups 0 ... 03FH", 200_000_000), (
            emu.screen_text())
        for _ in range(200):
            if _letzte_zeile(emu) == "A>":
                break
            emu.run(100_000)
        bild = emu.screen_text()
        assert _letzte_zeile(emu) == "A>", bild
        assert "SCPX 8915  V 5.3" in bild
        assert "ERR" not in bild, bild

        st = sitzung.sync.stats
        assert 0 < st.tracks_known < st.tracks_total // 2, (
            f"{st.tracks_known} von {st.tracks_total} Spuren — das ist ein Vollabzug")
        assert sitzung.device.geschrieben == [], "ein Schreibschutz-Mount schrieb"
    finally:
        del emu
        sitzung.close()


def test_der_k8915_legt_im_laufwerkskasten_physisch_ein(qapp, hfe, monkeypatch, tmp_path):
    """Der „Physisch…“-Knopf gehört auch dem k8915emu (Profil, nicht `if machine`)."""
    from app import profil
    from app.ui import physical_disk
    from app.ui.main_window import MainWindow

    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "xdg"))
    sitzung = fake_session(hfe, read_ahead=False, for_emulator=True)
    monkeypatch.setattr(physical_disk.PhysicalSession, "start",
                        classmethod(lambda cls, **kw: sitzung))
    monkeypatch.setattr(physical_disk.PhysicalDiskDialog, "frage",
                        classmethod(lambda cls, parent=None, **kw: {
                            "drive": "a", "cell_rate_kbps": 250, "num_cyls": 80,
                            "num_heads": 2, "writable": True, "read_ahead": False}))
    w = MainWindow(None, profil=profil.profil("k8915"))
    try:
        assert w.emulator.machine == "k8915"
        panel = w.drives_widget._panels[0]
        panel._phys_btn.click()
        assert 0 in w.drives_widget._physical
        assert "Greaseweazle" in panel._path_display.text()
        # Zwei Schlösser: Sitzung schreibend (Dialog: writable) <=> Laufwerk frei.
        assert not w.emulator.is_disk_write_protected(0)
        panel._toggle_btn.click()                  # Auswerfen
        assert 0 not in w.drives_widget._physical
    finally:
        w.close()
        qapp.processEvents()
