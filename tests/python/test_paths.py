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
    (root / "share" / "k1520emu").mkdir(parents=True, exist_ok=True)
    (root / "share" / "disks").mkdir(parents=True, exist_ok=True)
    (root / "share" / "k1520emu" / "formats.yaml").write_text("formats: []\n")
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
    assert paths.formats_file() == tmp_path / "share" / "k1520emu" / "formats.yaml"
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


def _leeres_heim(tmp_path, monkeypatch) -> Path:
    """Ein HOME ohne alles — jede XDG-Variable von außen wäre sonst im Spiel."""
    heim = tmp_path / "heim"
    heim.mkdir()
    monkeypatch.setenv("HOME", str(heim))
    # Path.home() liest unter Windows USERPROFILE, nicht HOME — ohne das griffe
    # der Test dort am echten Benutzerverzeichnis vorbei ins Leere.
    monkeypatch.setenv("USERPROFILE", str(heim))
    for var in ("XDG_DOCUMENTS_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME"):
        monkeypatch.delenv(var, raising=False)
    return heim


#: ``user-dirs.dirs`` ist ein Freedesktop-Mechanismus.  Windows und macOS haben
#: ihren Dokumentenordner an fester Stelle — die beiden folgenden Fälle prüfen
#: die XDG-Auflösung und sind dort gegenstandslos (nicht „kaputt").
nur_unix = pytest.mark.skipif(
    sys.platform.startswith("win") or sys.platform == "darwin",
    reason="XDG/user-dirs.dirs gibt es nur auf Freedesktop-Systemen")


@nur_unix
def test_arbeitsdisketten_liegen_im_dokumentenordner(tmp_path, monkeypatch):
    """Der Ordnername ist sprachabhängig — verbindlich ist ``user-dirs.dirs``.

    Ein fest verdrahtetes ``~/Documents`` läge auf einem deutschen System
    daneben, und der Anwender fände seine Disketten nicht.
    """
    heim = _leeres_heim(tmp_path, monkeypatch)
    (heim / "Dokumente").mkdir()
    (heim / ".config").mkdir()
    (heim / ".config" / "user-dirs.dirs").write_text(
        '# erzeugt von xdg-user-dirs-update\nXDG_DOCUMENTS_DIR="$HOME/Dokumente"\n')

    assert paths.documents_dir() == heim / "Dokumente"
    assert paths.user_data_dir() == heim / "Dokumente" / "K1520emu"
    assert paths.user_disks_dir() == heim / "Dokumente" / "K1520emu" / "Disketten"


@nur_unix
def test_dokumentenordner_faellt_auf_den_datenpfad_zurueck(tmp_path, monkeypatch):
    """Ohne Dokumentenordner (schlankes System) bleibt der plattformübliche Ort."""
    heim = _leeres_heim(tmp_path, monkeypatch)
    assert paths.documents_dir() is None
    assert paths.user_disks_dir() == heim / ".local" / "share" / "K1520emu" / "Disketten"


def test_windows_dokumentenordner_folgt_der_registrierung(tmp_path, monkeypatch):
    """OneDrive verschiebt „Dokumente" — die Auflösung muss mitgehen.

    Verschoben wird der Ordner NICHT im Dateisystem, sondern in der
    Registrierung (``User Shell Folders`` → ``Personal``); ``~/Documents``
    existiert danach oft gar nicht mehr.  Ein fest verdrahtetes ``~/Documents``
    legte die Arbeitsdisketten also neben die Dateien des Anwenders — und
    ``--purge`` räumte an derselben Stelle vorbei.

    Die Registrierung lässt sich nicht in ein Temp-Verzeichnis stellen; geprüft
    wird deshalb über die Naht :func:`paths._windows_documents`, die genau
    dafür eine eigene Funktion ist.
    """
    monkeypatch.setattr(paths, "_is_windows", lambda: True)
    monkeypatch.setattr(paths, "_is_macos", lambda: False)

    onedrive = tmp_path / "OneDrive" / "Dokumente"
    onedrive.mkdir(parents=True)
    monkeypatch.setattr(paths, "_windows_documents", lambda: onedrive)

    assert paths.documents_dir() == onedrive
    assert paths.user_disks_dir() == onedrive / "K1520emu" / "Disketten"


def test_windows_ohne_registrierungseintrag_bleibt_documents(tmp_path, monkeypatch):
    """Ohne verwertbaren Eintrag bleibt ``~/Documents`` die Reserve."""
    monkeypatch.setattr(paths, "_is_windows", lambda: True)
    monkeypatch.setattr(paths, "_is_macos", lambda: False)
    monkeypatch.setattr(paths, "_windows_documents", lambda: None)

    heim = tmp_path / "heim"
    (heim / "Documents").mkdir(parents=True)
    monkeypatch.setattr(paths.Path, "home", staticmethod(lambda: heim))

    assert paths.documents_dir() == heim / "Documents"


def test_env_data_schlaegt_den_dokumentenordner(tmp_path, monkeypatch):
    """Die Wahl aus dem Installationsdialog (``K1520_DATA``) gewinnt.

    Sie steht im Starter und ist der Weg, auf dem ein Anwender seine
    Arbeitsdisketten aus einem nach OneDrive synchronisierten ``Documents``
    herausholt.
    """
    heim = _leeres_heim(tmp_path, monkeypatch)
    (heim / "Documents").mkdir()
    eigen = tmp_path / "woanders"
    monkeypatch.setenv(paths.ENV_DATA, str(eigen))

    assert paths.user_data_dir() == eigen
    assert paths.user_disks_dir() == eigen / "Disketten"


def test_env_disks_ist_spezieller_als_env_data(tmp_path, monkeypatch):
    """Wer NUR die Disketten verschiebt, soll nicht den ganzen Datenordner mitnehmen.

    Im Datenordner liegen auch Zustände und ``logs/`` — ``K1520_DISKS`` ist die
    speziellere Angabe und schlägt deshalb ``K1520_DATA``.
    """
    _leeres_heim(tmp_path, monkeypatch)
    monkeypatch.setenv(paths.ENV_DATA, str(tmp_path / "daten"))
    monkeypatch.setenv(paths.ENV_DISKS, str(tmp_path / "nur_disketten"))

    assert paths.user_data_dir() == tmp_path / "daten"
    assert paths.user_disks_dir() == tmp_path / "nur_disketten"


@nur_unix   # die Umbenennung prüft die XDG-/Namensauflösung; Windows hat „Documents" fest
def test_ohne_env_data_bleibt_die_aufloesung_dynamisch(tmp_path, monkeypatch):
    """Die VORGABE wird bewusst nicht festgeschrieben.

    Der Installer trägt nur eine abweichende Wahl in den Starter ein.  Sonst
    stünde dort ein fester Pfad, der einem später umbenannten Dokumentenordner
    nicht mehr folgen könnte — hier nachgestellt, indem der Ordner wechselt.
    """
    heim = _leeres_heim(tmp_path, monkeypatch)
    monkeypatch.delenv(paths.ENV_DATA, raising=False)
    (heim / "Documents").mkdir()
    assert paths.user_data_dir() == heim / "Documents" / "K1520emu"

    # Anwender benennt um (bzw. stellt die Systemsprache um):
    (heim / "Documents").rename(heim / "Dokumente")
    assert paths.user_data_dir() == heim / "Dokumente" / "K1520emu"


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


def test_seed_user_disks_packt_gepackte_beispiele_aus(tmp_path, at_root, monkeypatch):
    """Im Paket liegen die Abbilder als ``*.hfe.gz`` — beim Anwender müssen sie
    entpackt und bitgleich ankommen (sonst mountet der Emulator Müll)."""
    import gzip

    root = _fake_install(tmp_path)
    at_root(root)
    inhalt = bytes(range(256)) * 40
    with gzip.open(root / "share" / "disks" / "beispiel.hfe.gz", "wb") as f:
        f.write(inhalt)
    ziel = tmp_path / "userdata" / "disks"
    monkeypatch.setenv(paths.ENV_DISKS, str(ziel))

    assert paths.seed_user_disks() == 1
    assert (ziel / "beispiel.hfe").read_bytes() == inhalt
    assert not list(ziel.glob("*.gz")), "gepackte Datei blieb beim Anwender liegen"


def test_seed_user_disks_ist_im_quellbaum_wirkungslos(at_root, monkeypatch, tmp_path):
    at_root(PROJECT_ROOT)
    monkeypatch.setenv(paths.ENV_DISKS, str(tmp_path / "disks"))
    assert paths.seed_user_disks() == 0
    assert not (tmp_path / "disks").exists()


# ─── Dateiordner des DiskTool ────────────────────────────────────────────────
#
# Das Gegenstück zu den Arbeitsdisketten für die Ordnerseite des Werkzeugs.  Er
# muss aus demselben Grund außerhalb der Installation liegen: dort wird
# geschrieben (doc/design/13_k1520disktool.md §20.8).

def test_dateiordner_liegt_neben_den_disketten(tmp_path, monkeypatch):
    monkeypatch.setenv(paths.ENV_DATA, str(tmp_path / "daten"))
    monkeypatch.delenv(paths.ENV_DISKS, raising=False)
    assert paths.user_files_dir() == tmp_path / "daten" / "Dateien"
    assert paths.user_files_dir().parent == paths.user_disks_dir().parent


def test_dateiordner_liegt_nie_in_der_installation(tmp_path, at_root):
    root = _fake_install(tmp_path)
    at_root(root)
    assert root not in paths.user_files_dir().parents


def test_env_disks_verschiebt_den_dateiordner_nicht(tmp_path, monkeypatch):
    """``K1520_DISKS`` meint die ABBILDER — die Dateien bleiben, wo sie sind."""
    monkeypatch.setenv(paths.ENV_DATA, str(tmp_path / "daten"))
    monkeypatch.setenv(paths.ENV_DISKS, str(tmp_path / "woanders"))
    assert paths.user_disks_dir() == tmp_path / "woanders"
    assert paths.user_files_dir() == tmp_path / "daten" / "Dateien"


def test_startordner_weicht_nach_oben_aus_statt_in_die_installation(tmp_path,
                                                                    at_root,
                                                                    monkeypatch):
    """Gibt es den Dateiordner noch nicht, wird nach oben ausgewichen — nie ins Programm."""
    root = _fake_install(tmp_path / "prog")
    at_root(root)
    daten = tmp_path / "daten"
    monkeypatch.setenv(paths.ENV_DATA, str(daten))

    # Weder Dateiordner noch Datenordner vorhanden: irgendein Ort im Heimat-,
    # aber keiner im Installationsbereich.
    assert root not in paths.default_folder_dir().parents
    assert paths.default_folder_dir() != root

    daten.mkdir()
    assert paths.default_folder_dir() == daten          # der Datenordner
    (daten / "Dateien").mkdir()
    assert paths.default_folder_dir() == daten / "Dateien"


def test_dateiordner_entsteht_nur_in_einer_installation(tmp_path, at_root,
                                                        monkeypatch):
    monkeypatch.setenv(paths.ENV_DATA, str(tmp_path / "daten"))

    # Quellbaum: es darf nichts im Heimatverzeichnis angelegt werden, bloß weil
    # jemand das Werkzeug einmal gestartet hat.
    at_root(PROJECT_ROOT)
    assert paths.ensure_user_files_dir() is None
    assert not (tmp_path / "daten").exists()

    at_root(_fake_install(tmp_path / "prog"))
    assert paths.ensure_user_files_dir() == tmp_path / "daten" / "Dateien"
    assert (tmp_path / "daten" / "Dateien").is_dir()
    # Ein zweiter Start ist folgenlos.
    assert paths.ensure_user_files_dir() == tmp_path / "daten" / "Dateien"


def test_describe_nennt_layout_und_alle_pfade(tmp_path, at_root):
    at_root(_fake_install(tmp_path))
    text = paths.describe()
    assert "Installation" in text
    assert str(tmp_path / "bin") in text
    assert str(tmp_path / "share" / "k1520emu") in text
    # Beide Arbeitsverzeichnisse stehen dabei — das ist die erste Frage, wenn ein
    # Dialog am falschen Ort aufgeht.
    assert str(paths.user_disks_dir()) in text
    assert str(paths.user_files_dir()) in text


# ─── Verschiebbarkeit des Pakets (Kern) ──────────────────────────────────────

@requires_core
def test_installierte_bibliothek_findet_eigenen_formatkatalog(tmp_path):
    """Der Kern findet ``formats.yaml`` über den Pfad SEINES Moduls.

    Nachgebaute Installation in einem Temp-Verzeichnis: Bibliothek nach ``bin/``,
    Katalog nach ``share/k1520emu/``.  Ein eigener Prozess lädt genau diese Kopie
    (ohne ``K1520_FORMATS``) — er muss den Katalog daneben finden.  Damit ist
    belegt, dass eine Installation an einer beliebigen Stelle liegen darf.
    """
    root = tmp_path / "install"
    (root / "bin").mkdir(parents=True)
    (root / "share" / "k1520emu").mkdir(parents=True)

    lib_name = paths.library_filenames()[0]
    shutil.copy2(PROJECT_ROOT / "build" / lib_name, root / "bin" / lib_name)
    shutil.copy2(PROJECT_ROOT / "data" / "formats.yaml",
                 root / "share" / "k1520emu" / "formats.yaml")

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
    assert (root / "share" / "k1520emu" / "formats.yaml").resolve() in geladen, out.stdout
