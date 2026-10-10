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
    # Kein endswith("k1520emu/a5120emu.yaml"): unter Windows trennt os.path.join
    # mit '\', und der Test pruefte dann nur noch, dass er auf Linux laeuft.
    assert cfg.default_config_path() == str(tmp_path / "k1520emu" / "a5120emu.yaml")


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


# ─── Formatversion, Programmversion, Migration (doc/ci_pipeline.md §7.4) ─────

def test_config_traegt_formatversion_und_programmversion():
    """``version`` ist die FORMATversion, ``geschrieben_von`` die Bauversion des Programms."""
    from app import version
    data = cfg.build_config(CRTParams(), {}, [], {}, [])
    assert data["version"] == cfg.CONFIG_VERSION == 1
    assert data["geschrieben_von"] == version.fassung()
    assert data["geschrieben_von"] != str(data["version"])


def test_fehlende_formatversion_gilt_als_1(tmp_path):
    p = tmp_path / "alt.yaml"
    p.write_text("general:\n  speed: 2.0\n", encoding="utf-8")
    assert cfg.format_version(cfg.load_config(str(p))) == 1


def test_hoehere_formatversion_wird_nicht_ueberschrieben(tmp_path, capsys):
    """Ein älteres Programm darf die Datei eines neueren nicht zerstören —
    weder beim Autosave noch beim Beenden (beide laufen über ``save_config``)."""
    p = tmp_path / "neu.yaml"
    original = f"version: {cfg.CONFIG_VERSION + 1}\ngeschrieben_von: 9.9.9\nzukunft: {{a: 1}}\n"
    p.write_text(original, encoding="utf-8")

    daten = cfg.load_config(str(p))
    assert cfg.ist_neuer_als_dieses_programm(daten)
    assert cfg.save_config(str(p), cfg.build_config(CRTParams(), {}, [], {}, [])) is False
    assert p.read_text(encoding="utf-8") == original
    assert "neueren Programm" in capsys.readouterr().out

    # Die ausdrückliche Wahl des Anwenders (Speichern unter …) darf.
    assert cfg.save_config(str(p), cfg.build_config(CRTParams(), {}, [], {}, []),
                           erzwingen=True) is True
    assert cfg.format_version(cfg.load_config(str(p))) == cfg.CONFIG_VERSION


def test_gleiche_und_fehlende_datei_wird_geschrieben(tmp_path):
    p = tmp_path / "d.yaml"
    daten = cfg.build_config(CRTParams(), {}, [], {}, [])
    assert cfg.save_config(str(p), daten) is True            # fehlt noch
    assert cfg.save_config(str(p), daten) is True            # gleiche Formatversion
    p.write_text("das: [ist kein yaml", encoding="utf-8")
    assert cfg.save_config(str(p), daten) is True            # defekt: nichts zu schützen


def test_migrationskette_laeuft_von_der_gelesenen_bis_zur_eigenen_version(tmp_path, monkeypatch):
    """Format 1 → 2 → 3: jede Stufe läuft genau einmal, in der Reihenfolge."""
    reihenfolge = []

    def eins_zu_zwei(d):
        reihenfolge.append(1)
        d["general"] = {**d.get("general", {}), "neu_in_2": True}
        return d

    def zwei_zu_drei(d):
        reihenfolge.append(2)
        d["drei"] = d["general"].pop("neu_in_2")
        return d

    monkeypatch.setattr(cfg, "CONFIG_VERSION", 3)
    monkeypatch.setattr(cfg, "_MIGRATIONEN", {1: eins_zu_zwei, 2: zwei_zu_drei})
    p = tmp_path / "alt.yaml"
    p.write_text("general:\n  speed: 1.5\n", encoding="utf-8")        # ohne version = 1

    daten = cfg.load_config(str(p))
    assert reihenfolge == [1, 2]
    assert daten["version"] == 3 and daten["drei"] is True and daten["general"] == {"speed": 1.5}

    # Eine Datei, die schon Format 2 hat, durchläuft nur die zweite Stufe.
    reihenfolge.clear()
    p.write_text("version: 2\ngeneral: {neu_in_2: false}\n", encoding="utf-8")
    assert cfg.load_config(str(p))["version"] == 3 and reihenfolge == [2]


def test_fehlende_migrationsstufe_ist_ein_fehler(monkeypatch):
    monkeypatch.setattr(cfg, "CONFIG_VERSION", 2)
    monkeypatch.setattr(cfg, "_MIGRATIONEN", {})
    with pytest.raises(ValueError):
        cfg.migriere({"version": 1})
