"""Paketierung (``packaging/``) — was das verteilte Paket enthalten muss.

Die schnellen Fälle laufen **ohne Netz** und in Sekunden: Syntax der Skripte,
Optionen, und ein vollständiger Durchlauf von ``build_payload.sh`` gegen die
bereits gebaute Bibliothek (``--skip-build``).  Sie fangen genau das ab, was
beim Schnüren schiefgehen kann, ohne dass es jemand merkt — eine Datei, die
nicht mehr mitkopiert wird.

Der **vollständige** Durchlauf (installieren, Python und Qt laden, starten)
lädt ~120 MB und dauert Minuten; er läuft nur mit ``K1520_PACKAGING_FULL=1``:

    K1520_PACKAGING_FULL=1 venv/bin/python3 -m pytest tests/python/test_packaging.py

Entwurf: doc/design/13_distribution.md
"""

import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT, requires_core

PACKAGING = PROJECT_ROOT / "packaging"
SCRIPTS = ["install.sh", "build_payload.sh", "launcher.sh", "lib/common.sh"]


def _sh(*args, **kw):
    return subprocess.run(args, capture_output=True, text=True, timeout=600, **kw)


# ─── Skripte ─────────────────────────────────────────────────────────────────

@pytest.mark.parametrize("script", SCRIPTS)
def test_skript_ist_syntaktisch_gueltig(script):
    """`sh -n` über jedes Skript — Bourne-Syntax, keine Bashismen."""
    out = _sh("sh", "-n", str(PACKAGING / script))
    assert out.returncode == 0, out.stderr


@pytest.mark.parametrize("script", ["install.sh", "build_payload.sh"])
def test_hilfe_bleibt_folgenlos(script, tmp_path):
    """`--help` beschreibt sich selbst und fasst nichts an."""
    out = _sh("sh", str(PACKAGING / script), "--help", cwd=tmp_path)
    assert out.returncode == 0, out.stderr
    assert "--prefix" in out.stdout or "--out" in out.stdout
    assert not list(tmp_path.iterdir()), "--help hat Dateien angelegt"


def test_unbekannte_option_bricht_ab(tmp_path):
    out = _sh("sh", str(PACKAGING / "install.sh"), "--gibtsnicht", cwd=tmp_path)
    assert out.returncode != 0
    assert "unbekannte Option" in out.stderr


def test_launcher_hat_genau_einen_platzhalter():
    """@ROOT@ wird beim Installieren ersetzt — bleibt einer stehen, startet nichts."""
    text = (PACKAGING / "launcher.sh").read_text()
    assert 'ROOT="@ROOT@"' in text, "Platzhalter, den install.sh ersetzt, fehlt"
    assert '"$ROOT/venv/bin/python3"' in text
    assert '"$ROOT/app/main.py"' in text


def test_uv_pins_vollstaendig():
    """Version und je eine Prüfsumme für alle Zielplattformen des Entwurfs."""
    zeilen = [z.split() for z in (PACKAGING / "uv_pins.txt").read_text().splitlines()
              if z.strip() and not z.startswith("#")]
    pins = {z[0]: z[1] for z in zeilen if len(z) == 2}
    assert "version" in pins
    for ziel in ("x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
                 "x86_64-apple-darwin", "aarch64-apple-darwin",
                 "x86_64-pc-windows-msvc"):
        assert ziel in pins, f"Prüfsumme für {ziel} fehlt"
        assert len(pins[ziel]) == 64, f"{ziel}: keine SHA256"


def test_lock_nagelt_mit_hashes_fest():
    """Der Installer installiert mit --require-hashes — ohne Hashes schlüge das fehl."""
    lock = (PACKAGING / "requirements.lock").read_text()
    assert "--hash=sha256:" in lock
    assert "pyside6-essentials==" in lock.lower(), "Qt-Vollausbau statt Essentials?"
    assert "pyside6-addons" not in lock.lower(), "PySide6-Addons (~170 MB) im Paket"


def test_desktop_eintrag_ist_gueltig():
    text = (PACKAGING / "a5120emu.desktop.in").read_text()
    assert text.startswith("[Desktop Entry]")
    for feld in ("Type=Application", "Name=", "Exec=@ROOT@/bin/a5120emu", "Icon=a5120emu"):
        assert feld in text, f"{feld} fehlt im Startmenü-Eintrag"


# ─── Payload schnüren ────────────────────────────────────────────────────────

@requires_core
def test_payload_enthaelt_alles_zum_starten(tmp_path):
    """Ein vollständiger Schnür-Lauf gegen die gebaute Bibliothek.

    Prüft den Inhalt gegen die Liste dessen, was der Emulator zum Start
    braucht — fällt eine Datei aus dem Skript, schlägt das hier fehl und nicht
    erst beim Anwender.
    """
    out = _sh("sh", str(PACKAGING / "build_payload.sh"),
              "--skip-build", "--build-dir", str(PROJECT_ROOT / "build"),
              "--out", str(tmp_path), "--version", "test", "--disks", "none")
    assert out.returncode == 0, out.stdout + out.stderr

    stage = next(p for p in tmp_path.glob("a5120emu-test-*") if p.is_dir())
    for pflicht in [
        "install.sh", "launcher.sh", "a5120emu.desktop.in", "uv_pins.txt",
        "lib/common.sh", "requirements.lock", "VERSION", "README.md",
        "payload/bin/libk1520core.so",
        "payload/app/main.py", "payload/app/paths.py",
        "payload/app/core_binding/k1520.py", "payload/app/ui/main_window.py",
        "payload/share/a5120emu/formats.yaml",
        "payload/share/icons/a5120emu.svg",
    ]:
        assert (stage / pflicht).exists(), f"{pflicht} fehlt im Paket"

    assert os.access(stage / "install.sh", os.X_OK), "install.sh ist nicht ausführbar"
    assert not list(stage.rglob("__pycache__")), "Bytecode des Baurechners im Paket"

    archiv = next(tmp_path.glob("a5120emu-test-*.tar.gz"))
    with tarfile.open(archiv) as tf:
        namen = tf.getnames()
    assert all(n.startswith(stage.name) for n in namen), \
        "Archiv entpackt ohne gemeinsames Wurzelverzeichnis"
    pruefsumme = archiv.with_suffix(".gz.sha256").read_text().split()[0]
    assert len(pruefsumme) == 64


@requires_core
def test_paket_traegt_keinen_pfad_des_baurechners(tmp_path):
    """Der einkompilierte Katalog-Fallback muss im Release leer sein.

    Sonst schleppte jede ausgelieferte Bibliothek den absoluten Pfad des
    Baurechners als Suchkandidaten mit sich herum (`K1520_FORMATS_DEFAULT`).
    Geprüft wird an einer frisch als Release gebauten Bibliothek.
    """
    build = tmp_path / "build"
    cfg = _sh("cmake", "-S", str(PROJECT_ROOT), "-B", str(build),
              "-DCMAKE_BUILD_TYPE=Release", "-DK1520_FORMATS_DEFAULT=",
              "-DBUILD_K1520_TESTS=OFF")
    assert cfg.returncode == 0, cfg.stderr
    bld = _sh("cmake", "--build", str(build), "--target", "k1520core", "-j4")
    assert bld.returncode == 0, bld.stderr[-3000:]

    roh = (build / "libk1520core.so").read_bytes()
    assert bytes(str(PROJECT_ROOT / "data" / "formats.yaml"), "utf-8") not in roh


# ─── Vollständige Installation (langsam, braucht Netz) ───────────────────────

voll = pytest.mark.skipif(
    not os.environ.get("K1520_PACKAGING_FULL"),
    reason="lädt ~120 MB — mit K1520_PACKAGING_FULL=1 einschalten",
)


@requires_core
@voll
def test_installation_laeuft_durch_und_startet(tmp_path):
    """Paket schnüren, in ein eigenes HOME installieren, GUI headless starten."""
    out = _sh("sh", str(PACKAGING / "build_payload.sh"),
              "--skip-build", "--build-dir", str(PROJECT_ROOT / "build"),
              "--out", str(tmp_path / "dist"), "--version", "test")
    assert out.returncode == 0, out.stdout + out.stderr
    stage = next(p for p in (tmp_path / "dist").glob("a5120emu-test-*") if p.is_dir())

    heim = tmp_path / "heim"
    heim.mkdir()
    umgebung = {k: v for k, v in os.environ.items()
                if k not in ("XDG_DATA_HOME", "XDG_CONFIG_HOME")}
    umgebung["HOME"] = str(heim)

    inst = subprocess.run(["sh", str(stage / "install.sh")], capture_output=True,
                          text=True, env=umgebung, timeout=1800)
    assert inst.returncode == 0, inst.stdout + inst.stderr
    assert "läuft" in inst.stdout, "Rauchtest des Installers hat nicht bestätigt"
    # Der Rauchtest muss die INSTALLATION geprüft haben, nicht zufällig den
    # Baum, aus dem heraus er gestartet wurde (sys.path[0] = Arbeitsverzeichnis).
    assert str(heim) in inst.stdout, "Rauchtest prüfte nicht die Installation"

    starter = heim / ".local" / "opt" / "a5120emu" / "bin" / "a5120emu"
    assert starter.is_file() and os.access(starter, os.X_OK)
    assert (heim / ".local" / "share" / "applications" / "a5120emu.desktop").is_file()
    # Beispieldisketten liegen beim Anwender, nicht in der Installation.
    nutzer_disks = heim / ".local" / "share" / "a5120emu" / "disks"
    assert any(nutzer_disks.glob("*.hfe"))

    # Der Starter läuft aus der Installation heraus und löst alles dorthin auf.
    umgebung["QT_QPA_PLATFORM"] = "offscreen"
    ausk = subprocess.run([str(starter), "--paths"], capture_output=True, text=True,
                          env=umgebung, timeout=60)
    assert ausk.returncode == 0, ausk.stderr
    assert "Layout:            Installation" in ausk.stdout

    # Deinstallieren räumt das Programm ab, lässt die Disketten stehen.
    deinst = subprocess.run(["sh", str(stage / "install.sh"), "--uninstall"],
                            capture_output=True, text=True, env=umgebung, timeout=300)
    assert deinst.returncode == 0, deinst.stderr
    assert not starter.exists()
    assert any(nutzer_disks.glob("*.hfe")), "Deinstallieren hat Anwenderdisketten gelöscht"
    assert not (heim / ".local" / "bin" / "python3.12").exists(), \
        "uv hat einen Python-Symlink im PATH des Anwenders hinterlassen"
