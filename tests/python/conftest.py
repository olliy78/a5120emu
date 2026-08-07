"""Gemeinsame pytest-Fixtures der Python-Testebene.

Die Ebene testet, was die C++-Tests nicht erreichen: die **C-ABI**
(`libk1520core.so` ↔ ctypes-Bindung) und die **GUI** (PySide6).

Voraussetzungen (beides prüft `pytest` beim Start und meldet sonst klar):
  * `libk1520core.so` gebaut (``tools/dev.sh build``)
  * `PySide6` + `PyYAML` installiert (``pip install -r requirements-dev.txt``)

GUI-Tests laufen headless über ``QT_QPA_PLATFORM=offscreen`` — das wird hier
gesetzt, BEVOR PySide6 importiert wird.  Der Bildschirm ist ein
``QOpenGLWidget``; offscreen gibt es keinen FBO, das Widget wird also gebaut und
verdrahtet, aber nicht gerendert.  Pixelprüfungen gehören deshalb nicht hierher,
Verdrahtungsprüfungen schon.
"""

import os
import shutil
import sys
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIXTURE_DISKS = PROJECT_ROOT / "tests" / "fixtures" / "disks"

# `import app...` funktioniert damit unabhängig vom Aufrufverzeichnis.
sys.path.insert(0, str(PROJECT_ROOT))

# Headless-Qt + eigenes Konfigurationsverzeichnis: Tests dürfen die echte
# ~/.config/k1520emu/config.yaml des Nutzers weder lesen noch überschreiben.
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ["XDG_CONFIG_HOME"] = str(Path(__file__).parent / ".config-testrun")


# ─── Verfügbarkeit ───────────────────────────────────────────────────────────

def pytest_report_header(config):
    return [f"K1520: core={_core_lib() or 'FEHLT'}", f"K1520: fixtures={FIXTURE_DISKS}"]


def _core_lib():
    for name in ("libk1520core.so", "libk1520core.so.1"):
        p = PROJECT_ROOT / "build" / name
        if p.exists():
            return p
    return None


requires_core = pytest.mark.skipif(
    _core_lib() is None,
    reason="libk1520core.so nicht gebaut — vorher `tools/dev.sh build` ausführen",
)


# ─── Fixtures ────────────────────────────────────────────────────────────────

@pytest.fixture(scope="session")
def project_root() -> Path:
    return PROJECT_ROOT


@pytest.fixture(scope="session")
def fixture_disks() -> Path:
    """Verzeichnis der committeten Testdisketten (`tests/fixtures/disks`)."""
    assert FIXTURE_DISKS.is_dir(), f"Fixture-Verzeichnis fehlt: {FIXTURE_DISKS}"
    return FIXTURE_DISKS


@pytest.fixture
def temp_disk(tmp_path):
    """Beschreibbare Kopie einer Fixture-Diskette.

    Der Emulator mountet schreibend — eine committete Diskette darf ein Test
    niemals direkt öffnen.  Aufruf: ``temp_disk("cpa_cpa780_k5601_clock.img")``.
    """
    def _copy(name: str) -> str:
        src = FIXTURE_DISKS / name
        assert src.exists(), f"Fixture fehlt: {src}"
        dst = tmp_path / name
        shutil.copy(src, dst)
        return str(dst)
    return _copy


@pytest.fixture
def emulator():
    """Frischer, NICHT eingeschalteter Emulator; wird am Testende freigegeben."""
    from app.core_binding.k1520 import K1520Emulator
    emu = K1520Emulator()
    yield emu
    del emu


@pytest.fixture
def booted(emulator, temp_disk):
    """Emulator mit CP/A-Uhr-Bootdiskette in A:, eingeschaltet.

    Läuft bis zum Bootloader-Banner — ab hier ist die Maschine in einem
    definierten Zustand für weitere Schritte.
    """
    path = temp_disk("cpa_cpa780_k5601_clock.img")
    assert emulator.mount_disk(0, path, "cpa780", False), emulator.last_error()
    emulator.power_on()
    assert run_until_text(emulator, "Bootloader"), "Bootloader-Banner nie erschienen"
    return emulator


@pytest.fixture(scope="session")
def qapp():
    """Einmalige QApplication für alle GUI-Tests (Qt erlaubt nur eine)."""
    pytest.importorskip("PySide6", reason="PySide6 nicht installiert")
    from PySide6.QtWidgets import QApplication
    app = QApplication.instance() or QApplication([])
    yield app
    app.processEvents()


# ─── Hilfsfunktionen ─────────────────────────────────────────────────────────

def run_until_text(emu, needle: str, max_cycles: int = 60_000_000,
                   chunk: int = 100_000) -> bool:
    """Emulieren, bis *needle* im Textbildschirm steht (oder das Budget endet)."""
    done = 0
    while done < max_cycles:
        done += emu.run(chunk)
        if needle in emu.screen_text():
            return True
    return False
