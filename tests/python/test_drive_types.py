"""`app/drive_types.py` — Laufwerkskatalog und Normalisierung.

Reine Python-Logik ohne Kern und ohne Qt.  Wichtig ist vor allem die Abbildung
alter Profilnamen: eine gespeicherte Konfiguration darf beim Laden nicht still
auf K5601 zurückfallen und damit die Laufwerksbestückung des Nutzers ändern.
"""

import app.drive_types as dt

from conftest import requires_core

# drive_types importiert die ctypes-Bindung (Konstante DRIVE_NONE) und braucht
# daher die gebaute Kernbibliothek, obwohl es selbst reine Python-Logik ist.
pytestmark = requires_core


def test_catalogue_is_consistent():
    for short, core, desc in dt.DRIVE_TYPES:
        assert short and core and desc
        assert dt.is_present(core)
        assert dt.short_label(core) == short
        assert desc in dt.combo_label(core)


def test_default_configuration_is_the_a5120_standard():
    assert dt.DEFAULT_DRIVE_TYPES == ["K5601", "K5601", "K5601", dt.NO_DRIVE]
    assert len(dt.DEFAULT_DRIVE_TYPES) == dt.NUM_SLOTS


def test_empty_slot_handling():
    assert not dt.is_present(dt.NO_DRIVE)
    assert not dt.is_present("")
    assert not dt.is_present(None)
    assert dt.combo_label(dt.NO_DRIVE) == dt.NO_DRIVE_LABEL
    assert dt.short_label(dt.NO_DRIVE) == dt.NO_DRIVE_LABEL


def test_normalize_keeps_known_names():
    for _, core, _ in dt.DRIVE_TYPES:
        assert dt.normalize(core) == core


def test_normalize_maps_legacy_profile_names():
    """Alte technische Namen aus gespeicherten Konfigurationen (2026-08-03)."""
    assert dt.normalize("ss_525_40") == "K5600.10"
    assert dt.normalize("ss_525_80") == "K5600.20"
    assert dt.normalize("mf3200_8_ss77") == "MF3200"
    assert dt.normalize("mf6400_8_ss77") == "MF6400"
    assert dt.normalize("mf6400_8_ds77") == "MF6400"
    assert dt.normalize("mfs_525_ds80") == "K5601"
    # Vollständigkeit: jeder Eintrag der Tabelle zeigt auf ein bekanntes Profil.
    for old, new in dt.LEGACY_PROFILE_NAMES.items():
        assert dt.normalize(old) == new
        assert dt.is_present(new)


def test_normalize_falls_back_for_unknown_values():
    assert dt.normalize("voellig unbekannt") == "K5601"
    assert dt.normalize(None) == "K5601"
    assert dt.normalize("") == "K5601"
    assert dt.normalize(dt.NO_DRIVE) == dt.NO_DRIVE


def test_normalize_list_always_yields_all_slots():
    assert dt.normalize_list([]) == [dt.NO_DRIVE] * dt.NUM_SLOTS
    assert dt.normalize_list(None) == [dt.NO_DRIVE] * dt.NUM_SLOTS
    assert dt.normalize_list(["K5601"]) == ["K5601"] + [dt.NO_DRIVE] * 3
    # Zu lange Listen werden auf NUM_SLOTS gekürzt.
    assert len(dt.normalize_list(["K5601"] * 9)) == dt.NUM_SLOTS


def test_normalize_list_upgrades_a_stored_legacy_configuration():
    stored = ["mfs_525_ds80", "ss_525_40", "mf6400_8_ss77", dt.NO_DRIVE]
    assert dt.normalize_list(stored) == ["K5601", "K5600.10", "MF6400", dt.NO_DRIVE]
