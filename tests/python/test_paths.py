"""Pfadauflösung (:mod:`app.paths`) — Quellbaum **und** Installationslayout.

Die Auflösung entscheidet, ob ein verteiltes Paket überhaupt startet
(``doc/design/13_distribution.md``).  Zwei Ebenen werden geprüft:

* die reine Auflösungslogik in Python (Kandidatenreihenfolge, Layouterkennung,
  Benutzerverzeichnisse) — schnell, ohne Kern;
* die **Verschiebbarkeit** des fertigen Pakets: eine nachgebaute Installation in
  einem Temp-Verzeichnis wird in einem eigenen Prozess geladen und muss ihre
  eigene ``formats.yaml`` finden (das ist der Zweck der Modulpfad-Auflösung in
  ``FormatCatalog::searchPaths``, §6.2 des Konzepts).
"""

import os
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

import pytest

from conftest import PROJECT_ROOT, requires_core

from app import paths


# ─── Hilfsmittel ─────────────────────────────────────────────────────────────

def _fake_install(root: Path, with_lib: bool = True) -> Path:
    """Baut ein Installationslayout ``<root>/{bin,app,share}`` nach."""
    (root / "bin").mkdir(parents=True, exist_ok=True)
    (root / "app").mkdir(parents=True, exist_ok=True)
    (root / "share" / "a5120emu").mkdir(parents=True, exist_ok=True)
    (root / "share" / "disks").mkdir(parents=True, exist_ok=True)
    (root / "share" / "a5120emu" / "formats.yaml").write_text("formats: []\n")
    if with_lib:
        (root / "bin" / paths.library_filenames()[0]).write_bytes(b"")
    return root


@pytest.fixture
def at_root(monkeypatch):
    """Setzt ``K1520_HOME`` und räumt die übrigen Pfadvariablen ab."""
    def _apply(root: Path):
        monkeypatch.setenv(paths.ENV_HOME, str(root))
        for var in (paths.ENV_LIB, paths.ENV_FORMATS, paths.ENV_DISKS):
            monkeypatch.delenv(var, raising=False)
        return root
    return _apply


# ─── Quellbaum ───────────────────────────────────────────────────────────────

def test_quellbaum_wird_erkannt(at_root):
    """Ohne bin/ ist es der Quellbaum — und dort liegen Lib und Katalog unter build/ bzw. data/."""
    at_root(PROJECT_ROOT)
    assert not paths.is_installed_layout()
    assert paths.core_library().parent == (PROJECT_ROOT / "build").resolve()
    assert paths.formats_file() == (PROJECT_ROOT / "data" / "formats.yaml").resolve()
    assert paths.bundled_disks_dir() == PROJECT_ROOT / "disks"


# ─── Installationslayout ─────────────────────────────────────────────────────

def test_installation_wird_erkannt(tmp_path, at_root):
    at_root(_fake_install(tmp_path))
    assert paths.is_installed_layout()
    assert paths.core_library().parent == tmp_path / "bin"
    assert paths.formats_file() == tmp_path / "share" / "a5120emu" / "formats.yaml"
    assert paths.bundled_disks_dir() == tmp_path / "share" / "disks"


def test_installation_vor_quellbaum(tmp_path, at_root):
    """Liegen beide Layouts vor, gewinnt die Installation (bin/ vor build/)."""
    root = _fake_install(tmp_path)
    (root / "build").mkdir()
    (root / "build" / paths.library_filenames()[0]).write_bytes(b"")
    at_root(root)
    assert paths.core_library().parent == root / "bin"


# ─── Umgebungsvariablen ──────────────────────────────────────────────────────

def test_env_lib_schlaegt_layout(tmp_path, at_root, monkeypatch):
    root = _fake_install(tmp_path)
    at_root(root)
    eigen = tmp_path / "eigen" / paths.library_filenames()[0]
    eigen.parent.mkdir()
    eigen.write_bytes(b"")
    monkeypatch.setenv(paths.ENV_LIB, str(eigen))
    assert paths.core_library() == eigen.resolve()


def test_env_lib_darf_verzeichnis_sein(tmp_path, at_root, monkeypatch):
    root = _fake_install(tmp_path)
    at_root(root)
    eigen = tmp_path / "eigen"
    eigen.mkdir()
    (eigen / paths.library_filenames()[0]).write_bytes(b"")
    monkeypatch.setenv(paths.ENV_LIB, str(eigen))
    assert paths.core_library().parent == eigen


def test_env_disks_schlaegt_alles(tmp_path, at_root, monkeypatch):
    at_root(_fake_install(tmp_path))
    monkeypatch.setenv(paths.ENV_DISKS, str(tmp_path / "meine"))
    assert paths.user_disks_dir() == tmp_path / "meine"


def test_fehlende_bibliothek_nennt_alle_kandidaten(tmp_path, at_root):
    at_root(_fake_install(tmp_path, with_lib=False))
    with pytest.raises(FileNotFoundError) as exc:
        paths.core_library()
    text = str(exc.value)
    assert str(tmp_path / "bin") in text          # Installationskandidat
    assert str(tmp_path / "build") in text        # Quellbaumkandidat
    assert "tools/dev.sh build" in text           # Abhilfe steht dabei


# ─── Benutzerverzeichnisse ───────────────────────────────────────────────────

def test_benutzerdaten_liegen_nie_in_der_installation(tmp_path, at_root):
    """Arbeitsdisketten werden zurückgeschrieben — sie dürfen das Update nicht überleben müssen."""
    root = _fake_install(tmp_path)
    at_root(root)
    assert root not in paths.user_disks_dir().parents
    assert root not in paths.config_dir().parents


def test_konfigurationsverzeichnis_folgt_xdg(monkeypatch, tmp_path):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    assert paths.config_dir() == tmp_path / "k1520emu"


def test_seed_user_disks_kopiert_einmalig(tmp_path, at_root, monkeypatch):
    root = _fake_install(tmp_path)
    at_root(root)
    (root / "share" / "disks" / "beispiel.hfe").write_bytes(b"x")
    ziel = tmp_path / "userdata" / "disks"
    monkeypatch.setenv(paths.ENV_DISKS, str(ziel))

    assert paths.seed_user_disks() == 1
    assert (ziel / "beispiel.hfe").exists()

    # Zweiter Aufruf fasst ein vorhandenes Verzeichnis nicht mehr an:
    # was der Anwender dort geändert hat, bleibt.
    (ziel / "beispiel.hfe").write_bytes(b"vom anwender geaendert")
    assert paths.seed_user_disks() == 0
    assert (ziel / "beispiel.hfe").read_bytes() == b"vom anwender geaendert"


def test_seed_user_disks_ist_im_quellbaum_wirkungslos(at_root, monkeypatch, tmp_path):
    at_root(PROJECT_ROOT)
    monkeypatch.setenv(paths.ENV_DISKS, str(tmp_path / "disks"))
    assert paths.seed_user_disks() == 0
    assert not (tmp_path / "disks").exists()


def test_describe_nennt_layout_und_alle_pfade(tmp_path, at_root):
    at_root(_fake_install(tmp_path))
    text = paths.describe()
    assert "Installation" in text
    assert str(tmp_path / "bin") in text
    assert str(tmp_path / "share" / "a5120emu") in text


# ─── Verschiebbarkeit des Pakets (Kern) ──────────────────────────────────────

@requires_core
def test_installierte_bibliothek_findet_eigenen_formatkatalog(tmp_path):
    """Der Kern findet ``formats.yaml`` über den Pfad SEINES Moduls.

    Nachgebaute Installation in einem Temp-Verzeichnis: Bibliothek nach ``bin/``,
    Katalog nach ``share/a5120emu/``.  Ein eigener Prozess lädt genau diese Kopie
    (ohne ``K1520_FORMATS``) — er muss den Katalog daneben finden.  Damit ist
    belegt, dass eine Installation an einer beliebigen Stelle liegen darf.
    """
    root = tmp_path / "install"
    (root / "bin").mkdir(parents=True)
    (root / "share" / "a5120emu").mkdir(parents=True)

    lib_name = paths.library_filenames()[0]
    shutil.copy2(PROJECT_ROOT / "build" / lib_name, root / "bin" / lib_name)
    shutil.copy2(PROJECT_ROOT / "data" / "formats.yaml",
                 root / "share" / "a5120emu" / "formats.yaml")

    code = textwrap.dedent(f"""
        import ctypes, sys
        lib = ctypes.CDLL(r"{root / 'bin' / lib_name}")
        lib.k1520_create.restype = ctypes.c_void_p
        lib.k1520_create.argtypes = [ctypes.c_int]
        lib.k1520_last_init_error.restype = ctypes.c_char_p
        lib.k1520_formats_source.restype = ctypes.c_char_p
        lib.k1520_formats_source.argtypes = [ctypes.c_void_p]
        h = lib.k1520_create(0)   # K1520_MACHINE_A5120
        if not h:
            sys.exit("Kern startete nicht: " + lib.k1520_last_init_error().decode())
        print(lib.k1520_formats_source(ctypes.c_void_p(h)).decode())
    """)
    env = {k: v for k, v in os.environ.items() if k != "K1520_FORMATS"}
    out = subprocess.run([sys.executable, "-c", code], capture_output=True,
                         text=True, env=env, timeout=120)
    assert out.returncode == 0, out.stderr
    # Der Kern meldet den Fundort so, wie er ihn zusammengesetzt hat
    # („…/bin/../share/…") — für den Vergleich normalisieren.
    geladen = {Path(p).resolve() for p in out.stdout.strip().split(":") if p}
    assert (root / "share" / "a5120emu" / "formats.yaml").resolve() in geladen, out.stdout
