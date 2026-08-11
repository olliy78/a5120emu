"""`app/config_io.py` — Speichern/Laden der Anwendungskonfiguration.

Die Konfiguration überlebt Programmläufe: Bildschirmparameter, gemountete
Disketten, Laufwerksbestückung, Fenstergeometrie.  Bricht der Rundlauf, verliert
der Nutzer beim nächsten Start seine Einrichtung — ohne Fehlermeldung.
"""

import sys
from pathlib import Path

import pytest

import app.config_io as cfg
from app.ui.screen_widget import CRTParams


def test_default_config_path_honours_xdg(monkeypatch, tmp_path):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    assert cfg.default_config_dir() == str(tmp_path / "k1520emu")
    # Kein endswith("k1520emu/config.yaml"): unter Windows trennt os.path.join
    # mit '\', und der Test pruefte dann nur noch, dass er auf Linux laeuft.
    assert cfg.default_config_path() == str(tmp_path / "k1520emu" / "config.yaml")


def test_default_config_path_without_xdg(monkeypatch):
    """Ohne XDG greift der plattformuebliche Ort — und der ist je System anders.

    Linux ``~/.config/k1520emu``, Windows ``%APPDATA%\\K1520emu``, macOS
    ``~/Library/Application Support/K1520emu`` (app/paths.py::config_dir).
    """
    monkeypatch.delenv("XDG_CONFIG_HOME", raising=False)
    verzeichnis = Path(cfg.default_config_dir())
    if sys.platform.startswith("win"):
        # Ein altes ~/.config/k1520emu wird weiterbenutzt — beides ist richtig.
        assert verzeichnis.name in ("K1520emu", "k1520emu")
    elif sys.platform == "darwin":
        assert verzeichnis.parent.name in ("Application Support", ".config")
    else:
        assert verzeichnis == Path.home() / ".config" / "k1520emu"


def test_build_config_has_all_sections():
    data = cfg.build_config(CRTParams(), {"speed": 1.0}, [], {}, ["K5601"])
    assert data["version"] == cfg.CONFIG_VERSION
    for section in ("crt", "general", "drive_types", "disks", "window"):
        assert section in data


def test_build_config_tolerates_none_arguments():
    data = cfg.build_config(CRTParams(), None, None, None, None)
    assert data["general"] == {} and data["disks"] == []
    assert data["drive_types"] == [] and data["window"] == {}


def test_save_load_roundtrip(tmp_path):
    disks = [{"drive": 0, "path": "/tmp/a.img", "format": "cpa780",
              "write_protect": False}]
    window = {"width": 1024, "height": 680}
    original = cfg.build_config(CRTParams(), {"speed": 2.0}, disks, window,
                                ["K5601", "MF3200", "none", "none"])

    path = tmp_path / "sub" / "dir" / "config.yaml"   # Verzeichnis wird angelegt
    cfg.save_config(str(path), original)
    assert path.exists()

    assert cfg.load_config(str(path)) == original


def test_load_empty_file_yields_empty_dict(tmp_path):
    path = tmp_path / "leer.yaml"
    path.write_text("", encoding="utf-8")
    assert cfg.load_config(str(path)) == {}


def test_crt_params_survive_the_roundtrip(tmp_path):
    """Die Bildschirmparameter sind der Grund, warum die Konfiguration existiert."""
    crt = CRTParams(brightness=3.25, contrast=1.75)
    path = tmp_path / "config.yaml"
    cfg.save_config(str(path), cfg.build_config(crt, {}, []))

    restored = CRTParams()
    restored.update_from_dict(cfg.load_config(str(path))["crt"])
    assert restored.brightness == pytest.approx(3.25)
    assert restored.contrast == pytest.approx(1.75)
    assert restored.phosphor_on == pytest.approx(crt.phosphor_on)


def test_crt_update_ignores_unknown_and_broken_input():
    """Eine ältere/neuere/kaputte Konfiguration darf das Laden nicht sprengen."""
    data = CRTParams().to_dict()
    data["ein_feld_das_es_nicht_gibt"] = 42
    params = CRTParams()
    params.update_from_dict(data)
    assert params.brightness == CRTParams().brightness

    params.update_from_dict(None)      # gar kein dict
    params.update_from_dict({})        # leeres dict
    assert params.brightness == CRTParams().brightness
