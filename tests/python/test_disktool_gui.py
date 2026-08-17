"""Oberfläche des k1520DiskTool (PySide6, headless).

Geprüft wird die **Verdrahtung**, nicht das Aussehen: dass die Liste nach dem
Laden gefüllt ist, nach jeder schreibenden Aktion neu gelesen wird, dass beide
UDOS-Seiten als Gruppen erscheinen und dass eine gescheiterte Erkennung die
Schaltflächen sperrt statt eine Dateiliste zu erfinden
(doc/design/13_k1520disktool.md §11.2).

Die Aktionen sind bewusst ohne Dialog aufrufbar (``open_image``, ``extract_all``,
``insert_all``, …) — die Dialoge sitzen nur in den Klick-Behandlern und werden
hier nicht angefasst.
"""

import shutil
from pathlib import Path

import pytest

from conftest import requires_disk

pytestmark = requires_disk

pytest.importorskip("PySide6")


@pytest.fixture(scope="module")
def qt_app():
    """Eine QApplication je Modul (Qt verträgt keine zweite)."""
    from PySide6.QtWidgets import QApplication
    app = QApplication.instance() or QApplication([])
    yield app


@pytest.fixture
def window(qt_app):
    from app.disktool.ui.main_window import MainWindow
    w = MainWindow()
    yield w
    w._close_tool()


def gruppen(view) -> list:
    """Beschriftungen der obersten Baumebene."""
    tree = view.tree
    return [tree.topLevelItem(i).text(0) for i in range(tree.topLevelItemCount())]


def alle_namen(view) -> set:
    """Alle Dateinamen im Baum, über beide Ebenen."""
    tree = view.tree
    out = set()
    for i in range(tree.topLevelItemCount()):
        top = tree.topLevelItem(i)
        if top.childCount() == 0:
            out.add(top.text(0))
        for k in range(top.childCount()):
            out.add(top.child(k).text(0))
    return out


# ─── Laden und Anzeigen ──────────────────────────────────────────────────────

def test_cpm_disk_is_shown_flat(window, fixture_disks):
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")

    assert window.tool.filesystem == "cpa780"
    assert window.tool.volume_count == 1
    # Ohne Seiten keine Gruppierung: die Dateien stehen direkt auf oberster Ebene.
    namen = alle_namen(window.disk_view)
    assert "@OS.COM" in namen and "PIP.COM" in namen
    assert len(namen) == 24
    assert "Side0" not in " ".join(gruppen(window.disk_view))
    # Schreibschutz ist die Vorgabe: Lesen frei, Schreiben gesperrt.
    assert window.act_schreibschutz.isChecked()
    assert window.act_alles_raus.isEnabled()
    assert not window.act_speichern.isEnabled()


def test_udos_disk_is_shown_as_one_carrier_with_two_side_groups(window, fixture_disks):
    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")

    g = gruppen(window.disk_view)
    assert len(g) == 2, "beide Seiten gehören zu EINER Diskette und werden gruppiert"
    assert g[0].startswith("Side0") and g[1].startswith("Side1")
    assert "UDOS.SYS.4.3" in g[0]

    namen = alle_namen(window.disk_view)
    assert "HELP.DAT.00" in namen
    # Die Belegung ist ein Dauerzustand — sie steht rechts in der Statuszeile.
    assert "69 Dateien" in window.st_inhalt.text()
    # Auffälligkeiten des Mediums werden angezeigt, nicht verschwiegen.
    assert window.info_bar.isVisibleTo(window)
    assert "Altbestand" in window.info_bar.text()
    assert window.info_bar.stufe == "warnung"


def test_unrecognised_image_locks_the_buttons(window, fixture_disks):
    """Ohne Erkennung bleibt die Liste leer, die Meldung steht im Fenster."""
    assert not window.open_image(fixture_disks / "cpa_mini.hfe")

    assert window.tool is None
    assert window.disk_view.tree.topLevelItemCount() == 0
    assert "passt zu keinem Format" in window.info_bar.text()
    assert "4 Sektoren" in window.info_bar.text()
    assert window.info_bar.stufe == "fehler"
    for knopf in (window.act_alles_rein, window.act_loeschen, window.act_speichern):
        assert not knopf.isEnabled(), "Schreiben muss gesperrt sein"


def test_forcing_a_filesystem_reopens_the_image(window, fixture_disks):
    """Die Erkennung ist übersteuerbar — `cpa_auto` rechnet statt nachzuschlagen.

    Das benannte Profil `scpx640` wurde von Hand nachgemessen; `cpa_auto` leitet
    dieselben Werte aus der Geometrie ab, wie es das CP/A-BIOS beim LOGIN tut
    (doc/design/13_k1520disktool.md §6.4).  Beide müssen deshalb **denselben**
    Inhalt zeigen — das ist die Gegenprobe der Regel von der Oberfläche aus.
    """
    bild = fixture_disks / "scpx17_cpa780_k5601.hfe"

    assert window.open_image(bild)
    assert window.tool.filesystem == "scpx640"
    assert window.tool.format == "cpa640"
    erkannte_dateien = alle_namen(window.disk_view)
    assert "INIT.COM" in erkannte_dateien

    assert window.open_image(bild, "cpa_auto")
    assert window.tool.filesystem == "cpa_auto"
    assert alle_namen(window.disk_view) == erkannte_dateien


def test_unknown_filesystem_name_is_refused(window, fixture_disks):
    """Ein Tippfehler bei --fs öffnet nichts und sagt, woran es liegt."""
    assert not window.open_image(fixture_disks / "scpx17_cpa780_k5601.hfe", "gibtsnicht")
    assert window.tool is None


# ─── Schreiben: die Ansicht ist danach frisch ────────────────────────────────

def test_view_refreshes_after_every_write(window, fixture_disks, tmp_path):
    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    assert window.open_image(abbild)
    window.set_read_only(False)   # der bewusste Schritt zum Schreiben

    vorher = len(alle_namen(window.disk_view))

    quelle = tmp_path / "NEU.TXT"
    quelle.write_text("Inhalt vom DiskTool\n")
    assert window.insert_paths([str(quelle)])

    # Kein Aktualisieren nötig — insert_paths liest die Liste neu.
    namen = alle_namen(window.disk_view)
    assert "NEU.TXT" in namen
    assert len(namen) == vorher + 1
    # Die Änderungsmarke ist Qts eigene (`[*]` im Titel + setWindowModified),
    # damit jede Plattform ihr übliches Zeichen zeigt.
    assert window.isWindowModified(), "ungespeicherte Änderung wird angezeigt"
    assert "cpa.img" in window.windowTitle()

    assert window.erase_refs(["NEU.TXT"])
    assert "NEU.TXT" not in alle_namen(window.disk_view)

    assert window.save()
    assert not window.isWindowModified()


def test_extract_all_creates_side_folders(window, fixture_disks, tmp_path):
    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")
    ziel = tmp_path / "auszug"
    assert window.extract_all(ziel)

    assert (ziel / "Side0").is_dir() and (ziel / "Side1").is_dir()
    assert (ziel / "Side1" / "HELP.DAT.00").exists()
    # Der Ordnerbereich zeigt danach genau diesen Ordner.
    assert window.folder_view.folder == ziel
    assert "Side0/" in gruppen(window.folder_view)


def test_insert_all_refuses_a_folder_without_side_directories(window, fixture_disks,
                                                              tmp_path, monkeypatch):
    """Fehlt `Side1/`, wird nichts geschrieben — und der Grund benannt."""
    from PySide6.QtWidgets import QMessageBox
    monkeypatch.setattr(QMessageBox, "critical", lambda *a, **k: None)

    abbild = tmp_path / "udos.hfe"
    shutil.copy(fixture_disks / "udos_boot_scp.hfe", abbild)
    assert window.open_image(abbild)
    window.set_read_only(False)   # der bewusste Schritt zum Schreiben
    vorher = len(alle_namen(window.disk_view))

    quelle = tmp_path / "quelle"
    (quelle / "Side0").mkdir(parents=True)
    (quelle / "Side0" / "NUR.EINE").write_text("x")

    assert not window.insert_all(quelle)
    assert not window.tool.dirty, "die Diskette darf nicht angefasst worden sein"
    assert len(alle_namen(window.disk_view)) == vorher
    assert "Side1/" in window.protokoll.toPlainText()


def test_insert_all_refuses_when_it_does_not_fit(window, fixture_disks, tmp_path,
                                                 monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    monkeypatch.setattr(QMessageBox, "critical", lambda *a, **k: None)

    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    assert window.open_image(abbild)
    window.set_read_only(False)   # der bewusste Schritt zum Schreiben

    quelle = tmp_path / "zuviel"
    quelle.mkdir()
    (quelle / "riesig.bin").write_bytes(b"Q" * (900 * 1024))

    assert not window.insert_all(quelle)
    assert not window.tool.dirty
    assert "Es wurde nichts geschrieben" in window.protokoll.toPlainText()


def test_round_trip_through_the_window(window, fixture_disks, tmp_path):
    """Neue Diskette anlegen, Ordner einfügen, wieder extrahieren."""
    abbild = tmp_path / "neu.hfe"
    assert window.create_disk(abbild, "udos_ds77", "GUI.TEST")
    assert window.tool.volume_count == 2

    quelle = tmp_path / "quelle"
    (quelle / "Side0").mkdir(parents=True)
    (quelle / "Side1").mkdir(parents=True)
    (quelle / "Side0" / "EINS.DAT").write_bytes(b"\x01" * 500)
    (quelle / "Side1" / "ZWEI.DAT").write_bytes(b"\x02" * 300)

    assert window.insert_all(quelle)
    namen = alle_namen(window.disk_view)
    assert "EINS.DAT" in namen and "ZWEI.DAT" in namen

    assert window.save()
    ziel = tmp_path / "zurueck"
    assert window.extract_all(ziel)
    assert (ziel / "Side0" / "EINS.DAT").read_bytes() == b"\x01" * 500
    assert (ziel / "Side1" / "ZWEI.DAT").read_bytes() == b"\x02" * 300


def test_selection_yields_side_qualified_references(window, fixture_disks):
    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")
    tree = window.disk_view.tree

    seite1 = tree.topLevelItem(1)
    assert seite1.childCount() > 0
    seite1.child(0).setSelected(True)

    refs = window.disk_view.selected_refs()
    assert len(refs) == 1
    assert refs[0].startswith("Side1/"), refs
    assert window.disk_view.current_volume() in (0, 1)


# ─── Schreibschutz, Speichern unter, Archivieren ─────────────────────────────

def test_opens_read_only_and_write_needs_a_deliberate_step(window, fixture_disks,
                                                           tmp_path, monkeypatch):
    """Beim blossen Lesen soll nichts kaputtgehen koennen.

    Der Haken „Nur lesen" ist beim Öffnen gesetzt; Schreiben verlangt, ihn zu
    entfernen — ein bewusster Schritt, bei dem einem einfällt, dass man von einer
    unersetzlichen Diskette lieber erst eine Kopie anlegt.
    """
    from PySide6.QtCore import Qt
    from PySide6.QtWidgets import QMessageBox
    monkeypatch.setattr(QMessageBox, "critical", lambda *a, **k: None)

    import shutil
    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    original = abbild.read_bytes()

    assert window.open_image(abbild)
    assert window.tool.read_only
    assert window.act_schreibschutz.isChecked()

    # Lesen: frei.  Schreiben: gesperrt — und zwar wirklich, nicht nur optisch.
    assert window.act_alles_raus.isEnabled()
    assert not window.act_schreiben.isEnabled()
    assert not window.act_loeschen.isEnabled()

    quelle = tmp_path / "NEU.TXT"
    quelle.write_text("Inhalt")
    assert not window.insert_paths([str(quelle)])
    assert "schreibgeschuetzt" in window.protokoll.toPlainText()
    assert not window.tool.dirty
    assert abbild.read_bytes() == original

    # Nach dem bewussten Schritt geht es — sobald im Ordner etwas ausgewählt ist
    # (die Übertragung ist auswahlabhängig, §20.3).
    window.set_read_only(False)
    window.folder_view.set_folder(tmp_path)
    assert not window.act_schreiben.isEnabled(), "ohne Auswahl gibt es nichts zu tun"
    eintrag = window.folder_view.tree.findItems("NEU.TXT", Qt.MatchExactly)[0]
    eintrag.setSelected(True)
    assert window.act_schreiben.isEnabled()

    assert window.insert_paths([str(quelle)])
    assert "NEU.TXT" in alle_namen(window.disk_view)


def test_save_as_writes_a_copy_and_leaves_the_source_untouched(window, fixture_disks,
                                                               tmp_path):
    """Der empfohlene Weg: schreibgeschützt öffnen, Arbeitskopie anlegen, dort ändern."""
    quelle = fixture_disks / "cpa_cpa780_k5601_clock.img"
    original = quelle.read_bytes()

    assert window.open_image(quelle)
    assert window.tool.read_only

    kopie = tmp_path / "arbeitskopie.hfe"
    assert window.save_as(kopie), "Speichern unter muss auch bei Schreibschutz gehen"
    assert kopie.exists()
    assert window.tool.path == str(kopie), "ab jetzt wird an der Kopie gearbeitet"
    assert quelle.read_bytes() == original, "die Quelle darf sich nicht geändert haben"

    # Die Kopie trägt denselben Inhalt — im anderen Container.
    assert len(alle_namen(window.disk_view)) == 24


def test_archive_bundles_image_files_and_catalogue(window, fixture_disks, tmp_path):
    """Die .zip enthält Abbild, Dateien und das Inhaltsverzeichnis mit den
    Angaben, die ein Linux-Dateisystem nicht tragen kann."""
    import zipfile

    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")
    ziel = tmp_path / "archiv.zip"
    assert window.archive(ziel)
    assert ziel.exists()

    with zipfile.ZipFile(ziel) as z:
        namen = z.namelist()
        assert "udos_boot_scp.hfe" in namen, "das verlustfreie Abbild fehlt"
        assert "udos_boot_scp.txt" in namen, "das Inhaltsverzeichnis fehlt"
        assert any(n.startswith("dateien/Side0/") for n in namen)
        assert any(n.startswith("dateien/Side1/") for n in namen)
        assert "dateien/Side1/HELP.DAT.00" in namen

        text = z.read("udos_boot_scp.txt").decode("utf-8")

    # Genau die Angaben, die beim Extrahieren sonst verlorengehen:
    assert "udos_ds77" in text
    assert "UDOS.SYS.4.3" in text
    assert "HELP.DAT.00" in text
    assert "P1" in text, "der UDOS-Dateityp gehört ins Verzeichnis"
    assert "WELS" in text, "die UDOS-Eigenschaften ebenso"
    assert "LEGENDE" in text, "ohne Legende ist das Archiv in 20 Jahren stumm"
    assert "PROCEDURE" in text
    # Der Altbestand des Mediums wird mitdokumentiert.
    assert "Altbestand" in text


def test_archive_works_read_only_and_does_not_rebind(window, fixture_disks, tmp_path):
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    vorher = window.tool.path
    assert window.tool.read_only

    assert window.archive(tmp_path / "cpa.zip")
    assert window.tool.path == vorher, "Archivieren darf die Bindung nicht umhängen"
    assert not window.tool.dirty


def test_archive_converts_an_img_source_to_hfe(window, fixture_disks, tmp_path):
    """Auch aus einem .img entsteht im Archiv ein .hfe — die verlustfreie Fassung."""
    import zipfile

    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    ziel = tmp_path / "aus_img.zip"
    assert window.archive(ziel)

    with zipfile.ZipFile(ziel) as z:
        namen = z.namelist()
    assert "cpa_cpa780_k5601_clock.hfe" in namen
    assert not any(n.endswith(".img") for n in namen)


# ─── Bootdiskette ────────────────────────────────────────────────────────────

def test_boot_image_button_follows_the_disk(window, fixture_disks, tmp_path):
    """„Bootabbild sichern" gibt es nur, wo es Systemspuren gibt."""
    assert not window.act_bootabbild.isEnabled(), "ohne Diskette gesperrt"

    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert window.act_bootabbild.isEnabled()

    # Eine Datendiskette (Dateisystem ab Zylinder 0) hat nichts zu sichern.
    leer = tmp_path / "daten.hfe"
    assert window.create_disk(leer, "cpa800")
    assert not window.act_bootabbild.isEnabled()


def test_saved_boot_image_makes_a_new_disk_bootable(window, fixture_disks, tmp_path):
    """Der ganze Weg durch das Fenster: sichern → neue Diskette damit anlegen."""
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    boot = tmp_path / "cpa780_boot.bin"
    assert window.save_boot_image(boot)
    assert boot.stat().st_size == 15104
    assert window.tool.read_only, "Sichern ist eine reine Leseoperation"

    ziel = tmp_path / "bootfaehig.hfe"
    assert window.create_disk(ziel, "cpa780", "", boot)
    kontrolle = tmp_path / "kontrolle.bin"
    window.tool.read_boot_image(kontrolle)
    assert kontrolle.read_bytes() == boot.read_bytes()


def test_boot_image_that_does_not_fit_is_reported(window, tmp_path, monkeypatch):
    """Passt das Abbild nicht, wird nichts angelegt — und das Fenster sagt warum."""
    from PySide6.QtWidgets import QMessageBox
    gemeldet = []
    monkeypatch.setattr(QMessageBox, "critical",
                        lambda *a, **k: gemeldet.append(a[2]))

    zu_gross = tmp_path / "zu_gross.bin"
    zu_gross.write_bytes(b"\x5A" * 15105)
    ziel = tmp_path / "nicht_angelegt.hfe"

    assert not window.create_disk(ziel, "cpa780", "", zu_gross)
    assert not ziel.exists()
    assert gemeldet and "15104" in gemeldet[0], gemeldet


# ─── Eigenschaften einer Datei ───────────────────────────────────────────────
#
# Der Dialog ist die einzige Stelle, an der ein Anwender die Angaben zu sehen
# bekommt, die weder in den Bytes der Datei noch in einem Linux-Dateisystem
# stehen: bei UDOS der Kopfsektor, bei CP/M Nutzerbereich und Attributbits.

def _dialog(window, name):
    """Eigenschaften-Dialog zu einer Datei — ohne ihn anzuzeigen."""
    from app.disktool.ui.properties_dialog import PropertiesDialog
    eintrag = next(e for e in window.tool.list() if e.name == name)
    return PropertiesDialog(window.tool, eintrag, window)


def test_properties_dialog_shows_the_whole_udos_header(window, fixture_disks):
    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")
    d = _dialog(window, "OS")

    assert d.udos
    assert d.f_typ.currentData() == "P"
    assert d.f_eig["W"].isChecked() and d.f_eig["S"].isChecked()
    assert d.f_low.text() == "1000" and d.f_stack.text() == "0080"
    # Ohne Zutun darf nichts geschrieben werden.
    assert d.aenderungen() == {}


def test_properties_dialog_writes_only_what_changed(window, fixture_disks, tmp_path):
    abbild = tmp_path / "udos.hfe"
    shutil.copy(fixture_disks / "udos_boot_scp.hfe", abbild)
    assert window.open_image(abbild)
    window.set_read_only(False)

    d = _dialog(window, "ACTIVATE")
    d.f_entry.setText("1234")
    assert d.aenderungen() == {"entry": 0x1234}
    assert d.uebernehmen()

    eintrag = next(e for e in window.tool.list() if e.name == "ACTIVATE")
    assert eintrag.entry == 0x1234
    assert eintrag.type == "P", "der Rest des Kopfsektors bleibt stehen"
    assert eintrag.record_len == 1024


def test_properties_dialog_reports_a_bad_number(window, fixture_disks, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    gemeldet = []
    monkeypatch.setattr(QMessageBox, "warning", lambda *a, **k: gemeldet.append(a[2]))

    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")
    d = _dialog(window, "OS")
    d.f_low.setText("XYZ")
    assert not d.uebernehmen()
    assert gemeldet and "LOW" in gemeldet[0], gemeldet


def test_properties_dialog_edits_cpm_user_area_and_attributes(window, fixture_disks,
                                                              tmp_path):
    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    assert window.open_image(abbild)
    window.set_read_only(False)

    d = _dialog(window, "PIP.COM")
    assert not d.udos
    assert d.f_user.value() == 0 and not d.f_sys.isChecked()

    d.f_sys.setChecked(True)
    d.f_user.setValue(4)
    assert d.aenderungen() == {"user": 4, "system": True}
    assert d.uebernehmen()

    # Der Nutzerbereich gehört zur Identität: die Datei heißt jetzt anders.
    namen = {e.name: e for e in window.tool.list()}
    assert "PIP.COM" not in namen
    assert namen["4:PIP.COM"].attrs == "SYS"


def test_properties_dialog_is_read_only_on_a_protected_disk(window, fixture_disks):
    from PySide6.QtWidgets import QDialogButtonBox
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert window.tool.read_only

    d = _dialog(window, "PIP.COM")
    assert not d.knoepfe.button(QDialogButtonBox.Save).isEnabled()
    assert "schreibgeschützt" in d.hinweis.text()


def test_context_menu_signal_opens_the_properties_dialog(window, fixture_disks,
                                                         monkeypatch):
    """Rechtsklick → „Eigenschaften…" landet beim Fenster (die Verdrahtung)."""
    from app.disktool.ui import main_window as mw
    gesehen = []

    class Attrappe:
        def __init__(self, tool, entry, parent=None):
            gesehen.append(entry.name)
        def exec(self):
            return 0

    monkeypatch.setattr(mw, "PropertiesDialog", Attrappe)
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    window.disk_view.properties_requested.emit("PIP.COM")
    assert gesehen == ["PIP.COM"]


def test_properties_for_an_unknown_file_is_reported(window, fixture_disks, monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    monkeypatch.setattr(QMessageBox, "critical", lambda *a, **k: None)
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert not window.show_properties("GIBTSNI.CHT")


# ─── Archiv: alle ermittelbaren Angaben im Inhaltsverzeichnis ────────────────

def test_archive_catalogue_lists_every_udos_header_field(window, fixture_disks,
                                                         tmp_path):
    """Aus dem Textprotokoll allein muss sich eine Datei wiederherstellen lassen."""
    import zipfile

    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")
    ziel = tmp_path / "archiv.zip"
    assert window.archive(ziel)
    with zipfile.ZipFile(ziel) as z:
        text = z.read("udos_boot_scp.txt").decode("utf-8")
        # Das maschinenlesbare Gegenstück liegt daneben.
        assert "dateien/udos-dateiangaben.txt" in z.namelist()

    assert "DATEIANGABEN IM EINZELNEN" in text
    angaben = text.split("DATEIANGABEN IM EINZELNEN", 1)[1]
    for spalte in ("Satz", "Rest", "ENTRY", "Segment", "LOW", "HIGH", "STACK",
                   "Block", "Zusatz"):
        assert spalte in angaben, spalte

    # Die Zeile zu `OS` trägt genau das, was EXTRACT im laufenden System meldet.
    zeile = next(z for z in angaben.split("\n") if z.split()[1:2] == ["OS"])
    assert "1000+5632" in zeile, zeile      # Segment: Anfang + Abbildlänge
    assert "25FF" in zeile, zeile           # HIGH ADDRESS
    assert "0080" in zeile, zeile           # STACK SIZE
    assert " 512 " in zeile or "512" in zeile


def test_archive_catalogue_lists_cpm_user_area_and_flags(window, fixture_disks,
                                                         tmp_path):
    import zipfile

    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    assert window.open_image(abbild)
    window.set_read_only(False)
    window.tool.set_cpm_attrs("PIP.COM", system=True, user=3)

    ziel = tmp_path / "archiv.zip"
    assert window.archive(ziel)
    with zipfile.ZipFile(ziel) as z:
        text = z.read("cpa.txt").decode("utf-8")
        # Ohne dieses Beiblatt ginge der Nutzerbereich beim Zurückschreiben verloren.
        assert "dateien/cpm-dateiangaben.txt" in z.namelist()

    assert "DATEIANGABEN IM EINZELNEN" in text
    angaben = text.split("DATEIANGABEN IM EINZELNEN", 1)[1]
    zeile = next(z for z in angaben.split("\n") if z.startswith("3:PIP.COM"))
    assert zeile.split() == ["3:PIP.COM", "3", "7424", "-", "ja", "-"], zeile


# ─── Diskeditor: die Diskette als Scheibe ────────────────────────────────────
#
# Eine Ebene unter dem Dateisystem. Geprüft wird die Verdrahtung und das, was ein
# Bildvergleich nicht kann: dass der Treffertest wirklich den Sektor findet, der
# an dieser Stelle gezeichnet wurde, und dass der Schreibweg tut, was er sagt.

import math


def _editor(window, abbild):
    assert window.open_image(abbild)
    return window.open_disk_editor()


def test_disk_editor_zeigt_den_udos_anhang_auch_ohne_erkanntes_udos(
        window, fixture_disks, monkeypatch):
    """Der Anhang hängt am SEKTOR, nicht am erkannten Dateisystem.

    Eine gemischte oder gar nicht erkannte Diskette trägt ihre UDOS-Sektoren
    genauso — wer sie im Editor ansieht, will die Verkettung sehen.  Früher
    entschied das `tool.filesystem_type`; bei einer rohen Diskette blieb das Feld
    dann leer, obwohl der Kontrollblock danebenstand.
    """
    ed = _editor(window, fixture_disks / "udos_boot_scp.hfe")
    ed.udos = False              # so, als wäre nichts erkannt worden

    ed._springe(seite=0, spur=25, sektor_id=1)
    assert ed.tail_feld.isVisible(), "der Anhang wird verschwiegen"
    assert ed.tail_feld.text().strip(), "Anhangfeld leer"
    assert "UDOS" in ed.info.text()

    # Und umgekehrt: wo keiner ist, steht auch keiner.
    ed2 = _editor(window, fixture_disks / "cpa_cpa780_k5601_noclock.hfe")
    ed2.udos = True              # so, als wäre UDOS erkannt worden
    ed2._springe(seite=0, spur=25, sektor_id=1)
    assert not ed2.tail_feld.isVisible(), \
        "auf einer CP/M-Spur stehen dort Gap-Füllbytes, kein Anhang"


def test_disk_editor_loescht_und_fuegt_ganze_spuren_ein(window, temp_disk):
    """Ganze Spuren löschen und einfügen (§19.6).

    Anders als „Sektor löschen" verschiebt das die Diskette dahinter — der Weg,
    ein Abbild mit zu vielen Spuren zurechtzustutzen (82 statt 80) oder auf eine
    Zielgeometrie zu bringen (77 für 8″).
    """
    from PySide6.QtWidgets import QMessageBox
    from app.core_binding.k1520disk import SECTOR

    ed = _editor(window, temp_disk("udos_boot_scp.hfe"))
    window.tool.set_read_only(False)
    vorher = window.tool.medium_cylinders

    def sektoren(c):
        return [x for x in window.tool.track(c, 0).spans if x.kind == SECTOR]

    inhalt_dahinter = len(sektoren(6))
    with_ok = lambda *a, **k: QMessageBox.Ok          # noqa: E731
    ed_frage = QMessageBox.question
    QMessageBox.question = staticmethod(with_ok)
    try:
        ed._springe(seite=0, spur=5)
        assert ed.delete_track()
        assert window.tool.medium_cylinders == vorher - 1, "nicht gelöscht"
        # Alles dahinter ist aufgerückt: die alte Spur 6 ist jetzt die 5.
        assert len(sektoren(5)) == inhalt_dahinter

        ed._springe(seite=0, spur=4)
        assert ed.insert_track()
        assert window.tool.medium_cylinders == vorher, "nicht eingefügt"
        # Die neue Spur ist LEER — kein Abklatsch des Nachbarn.
        assert sektoren(5) == [], "die eingefügte Spur trägt Sektoren"
    finally:
        QMessageBox.question = ed_frage


def test_disk_editor_faerbt_leere_sektoren_heller(window, fixture_disks):
    """Beschrieben oder nur formatiert? — dunkelgrün gegen hellgrün.

    Ohne die Unterscheidung sieht eine frisch formatierte Diskette genauso aus wie
    eine volle.  „Leer" heisst: das **Datenfeld** trägt nichts Unterscheidbares;
    der UDOS-Anhang hinter der Daten-CRC zählt nicht mit — er ist auch auf einer
    leeren Diskette belegt.
    """
    from app.disktool.ui.disk_editor import FARBE_OK, FARBE_OK_LEER
    from app.core_binding.k1520disk import SECTOR

    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_noclock.hfe")
    leer = voll = None
    for seite in ed.surface.tracks:
        for spur in seite:
            for sp in (spur.spans if spur else ()):
                if sp.kind != SECTOR or not sp.ok:
                    continue
                if sp.blank and leer is None:
                    leer = sp
                elif not sp.blank and voll is None:
                    voll = sp
    assert leer is not None, "kein leerer Sektor — der Fall wird nicht geprüft"
    assert voll is not None, "kein beschriebener Sektor"

    assert ed.surface._farbe(leer) == FARBE_OK_LEER
    assert ed.surface._farbe(voll) == FARBE_OK
    # Heller, aber DASSELBE Grün: es ist kein anderer Zustand, nur weniger Inhalt.
    assert FARBE_OK_LEER.lightness() > FARBE_OK.lightness()
    assert abs(FARBE_OK_LEER.hue() - FARBE_OK.hue()) < 25

    # Und der Tooltip sagt es in Worten — Farbe allein trägt keine Erklärung.
    assert "leer" in ed.surface.beschreibung((0, 0, leer))
    assert "leer" not in ed.surface.beschreibung((0, 0, voll))
    # In der Legende steht beides.
    from PySide6.QtWidgets import QLabel
    # Die Legende festhalten, solange gelesen wird: gäbe man `_legende().findChildren()`
    # heraus, stürbe der Wrapper und risse die Beschriftungen mit (PySide-Eigentum).
    legende = ed._legende()
    beschriftungen = [w.text() for w in legende.findChildren(QLabel)]
    assert any("leer" == t for t in beschriftungen), beschriftungen


def test_disk_editor_shows_both_sides_of_the_medium(window, fixture_disks):
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert ed.surface.cylinders == window.tool.medium_cylinders
    assert ed.surface.heads == window.tool.medium_heads
    spur = ed.surface.tracks[0][0]
    assert spur.formatted and spur.sectors > 0
    # Die Abschnitte decken die Umdrehung lückenlos ab — sonst bliebe Fläche leer.
    assert spur.spans[0].start == 0.0
    for a, b in zip(spur.spans, spur.spans[1:]):
        assert a.end == b.start
    assert abs(spur.spans[-1].end - 1.0) < 1e-9


def test_disk_editor_hit_test_finds_the_drawn_sector(window, fixture_disks):
    """Für jeden Sektor den gezeichneten Mittelpunkt zurückrechnen und treffen.

    Das ist die einzige Prüfung, die die Grafik wirklich absichert: Winkelrichtung
    (Seite 1 ist gespiegelt), Ringlage (Spur 0 außen) und Nullpunkt (12 Uhr)
    müssen zwischen Zeichnen und Treffertest übereinstimmen.
    """
    from PySide6.QtCore import QPointF
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    ed.resize(1000, 800)
    s = ed.surface

    geprueft = 0
    for kopf in (0, 1):
        for spur in (0, 3):
            cx, cy, r_aussen, r_nabe = s._mitte(kopf)
            hoehe = s._ringhoehe(r_aussen, r_nabe)
            r = r_aussen - (spur + 0.5) * hoehe
            for span in s.tracks[kopf][spur].spans:
                if not span.is_sector:
                    continue
                anteil = (span.start + span.end) / 2
                grad = (90 - anteil * 360) if kopf == 0 else (90 + anteil * 360)
                p = QPointF(cx + math.cos(math.radians(grad)) * r,
                            cy - math.sin(math.radians(grad)) * r)
                treffer = s.treffer(p)
                assert treffer is not None, (kopf, spur, span.id)
                assert (treffer[0], treffer[1], treffer[2].index) \
                    == (kopf, spur, span.index)
                geprueft += 1
    assert geprueft > 50, "zu wenige Sektoren geprüft"


def test_disk_editor_tooltip_names_track_and_sector(window, fixture_disks):
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    s = ed.surface
    span = next(x for x in s.tracks[0][2].spans if x.is_sector)
    text = s.beschreibung((0, 2, span))
    assert "Seite 0" in text and "Spur 2" in text and f"Sektor {span.id}" in text


def test_disk_editor_shows_the_sector_content(window, fixture_disks):
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    span = next(x for x in ed.surface.tracks[0][0].spans if x.is_sector)
    assert ed.select_sector(0, 0, span.index)

    # Seite/Spur/Sektor stehen in den Eingabefeldern, nicht als Fliesstext.
    assert (ed.w_seite.value(), ed.w_spur.value()) == (0, 0)
    assert ed.w_sektor.value() == span.id
    assert "IBM-MFM" in ed.info.text() and "128 Byte" in ed.info.text()
    assert ed.crc_urteil.text() == "gültig"
    zeilen = ed.hex.toPlainText().split("\n")
    assert len(zeilen) == 4, "128 Byte = 4 Zeilen à 32 Byte"
    assert zeilen[0].startswith("0000  ")
    assert zeilen[1].startswith("0020  ")


def test_disk_editor_hexdump_round_trips(window):
    from app.disktool.ui.disk_editor import hexdump, parse_hexdump
    daten = bytes(range(256)) * 4
    assert parse_hexdump(hexdump(daten)) == daten
    # Der Offset ist Anzeige: ihn zu verbiegen ändert die Nutzdaten NICHT.
    verbogen = hexdump(daten).replace("0000  ", "AFFE  ", 1)
    assert parse_hexdump(verbogen) == daten
    with pytest.raises(ValueError):
        parse_hexdump("0000  ZZ 01 02")


def test_disk_editor_writes_a_sector_straight_into_the_file(window, fixture_disks,
                                                            tmp_path):
    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    ed = _editor(window, abbild)
    window.set_read_only(False)

    span = next(x for x in ed.surface.tracks[0][5].spans if x.is_sector)
    assert ed.select_sector(0, 5, span.index)
    zeilen = ed.hex.toPlainText().split("\n")
    zeilen[0] = zeilen[0][:6] + "DE AD BE EF" + zeilen[0][17:]
    ed.hex.setPlainText("\n".join(zeilen))

    # Die Daten stimmen jetzt nicht mehr zur CRC — und das steht auch da.
    assert "ungültig" in ed.crc_urteil.text()
    assert ed.fix_crc()
    assert ed.crc_urteil.text() == "gültig"
    assert ed.save_sector()

    # „Save Sektor" speichert wirklich — sonst waere der Knopf eine Falle.
    assert not window.tool.dirty, "der Sektor steht in der Datei, nicht nur im Speicher"
    assert (abbild.parent / (abbild.name + "~")).exists(), "Sicherungskopie"
    assert window.tool.sector_data(5, 0, span.index)[:4] == b"\xDE\xAD\xBE\xEF"
    assert ed.reload_sector()
    assert ed.hex.toPlainText().startswith("0000  DE AD BE EF")

    # Gegenprobe an der Datei selbst: neu oeffnen, ohne den alten Griff.
    from app.core_binding.k1520disk import DiskTool
    with DiskTool.open(abbild) as frisch:
        assert frisch.sector_data(5, 0, span.index)[:4] == b"\xDE\xAD\xBE\xEF"


def test_disk_editor_can_leave_a_sector_deliberately_broken(window, fixture_disks,
                                                            tmp_path):
    """Der Sinn der mitschreibbaren CRC: eine schadhafte Diskette nachbilden.

    Das geht nur in einem Container, der eine CRC überhaupt führt — deshalb `.hfe`
    (ein `.img` ist ein reines Sektorabbild, s. u.).
    """
    abbild = tmp_path / "cpa.hfe"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.hfe", abbild)
    ed = _editor(window, abbild)
    window.set_read_only(False)

    span = next(x for x in ed.surface.tracks[0][5].spans if x.is_sector)
    assert ed.select_sector(0, 5, span.index)
    ed.crc_feld.setText("BEEF")
    assert ed.save_sector()

    kaputt = next(x for x in ed.surface.tracks[0][5].spans
                  if x.is_sector and x.index == span.index)
    assert not kaputt.ok, "der Sektor muss danach als defekt gelten (rot)"
    assert window.tool.sector_crc(5, 0, span.index) == 0xBEEF

    # … und wieder heilbar.
    assert ed.fix_crc() and ed.save_sector()
    geheilt = next(x for x in ed.surface.tracks[0][5].spans
                   if x.is_sector and x.index == span.index)
    assert geheilt.ok


def test_disk_editor_is_read_only_on_a_protected_disk(window, fixture_disks):
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    span = next(x for x in ed.surface.tracks[0][0].spans if x.is_sector)
    assert ed.select_sector(0, 0, span.index)

    assert window.tool.read_only
    assert ed.hex.isReadOnly()
    assert not ed.btn_save.isEnabled()
    assert not ed.btn_fixcrc.isEnabled()
    assert ed.btn_reload.isEnabled(), "Ansehen bleibt erlaubt"


def test_disk_editor_opens_once_per_window(window, fixture_disks):
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert window.open_disk_editor() is ed, "kein zweites Fenster auf dieselbe Diskette"
    window._close_tool()
    assert window._diskeditor is None, "der Editor darf keinen toten Griff behalten"


def test_disk_editor_jumps_to_a_typed_track_and_sector(window, fixture_disks):
    """Wer weiß, dass er Spur 25 will, tippt sie — statt auf der Grafik zu suchen."""
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")

    ed.w_spur.feld.setText("25")
    ed.w_spur._eingegeben()
    assert ed.w_spur.value() == 25
    assert ed.aktuell[1] == 25

    ziel = ed._sektoren(0, 25)[2].id
    ed.w_sektor.feld.setText(str(ziel))
    ed.w_sektor._eingegeben()
    assert ed.w_sektor.value() == ziel
    assert ed.aktuell[:2] == (0, 25)

    ed.w_seite.feld.setText("1")
    ed.w_seite._eingegeben()
    assert ed.aktuell[0] == 1


def test_disk_editor_steps_through_sectors_and_tracks(window, fixture_disks):
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    ed._springe(seite=0, spur=10)

    sektoren = ed._sektoren(0, 10)
    ed.select_sector(0, 10, sektoren[0].index)
    ed._schritt_sektor(+1)
    assert ed.aktuell[2] == sektoren[1].index, "weiter IN SPURREIHENFOLGE, nicht nach ID"
    ed._schritt_sektor(-1)
    assert ed.aktuell[2] == sektoren[0].index
    ed._schritt_sektor(-1)
    assert ed.aktuell[2] == sektoren[0].index, "am Spuranfang wird nicht umgebrochen"

    ed._schritt_spur(+1)
    assert ed.w_spur.value() == 11 and ed.aktuell[1] == 11
    ed._schritt_seite(+1)
    assert ed.w_seite.value() == 1 and ed.aktuell[0] == 1


def test_disk_editor_refuses_an_impossible_sector_and_says_so(window, fixture_disks):
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    ed._springe(seite=0, spur=5)
    vorher = ed.aktuell

    ed.w_sektor.feld.setText("99")
    ed.w_sektor._eingegeben()
    assert ed.aktuell == vorher, "die Anzeige darf nicht wegspringen"
    assert "99" in ed.hinweis.text() and "vorhanden" in ed.hinweis.text()
    # Das Feld wird auf den WIRKLICH gezeigten Sektor zurückgesetzt.
    assert ed.w_sektor.value() != 99


def test_disk_editor_ascii_column_follows_the_hex_column(window, fixture_disks,
                                                         tmp_path):
    """Ein geänderter Bytewert muss sofort auch rechts lesbar werden."""
    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    ed = _editor(window, abbild)
    window.set_read_only(False)
    ed._springe(seite=0, spur=10)

    zeilen = ed.hex.toPlainText().split("\n")
    zeilen[0] = zeilen[0][:6] + "41 42 43 44" + zeilen[0][17:]
    ed.hex.setPlainText("\n".join(zeilen))

    erste = ed.hex.toPlainText().split("\n")[0]
    ascii_spalte = erste[6 + 32 * 3 - 1 + 2:]
    assert ascii_spalte.startswith("ABCD"), ascii_spalte
    # Und die Nutzdaten selbst stimmen weiterhin mit der Hexspalte überein.
    from app.disktool.ui.disk_editor import parse_hexdump
    assert parse_hexdump(ed.hex.toPlainText())[:4] == b"ABCD"


def test_disk_editor_window_can_be_maximised(window, fixture_disks):
    from PySide6.QtCore import Qt
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert ed.windowFlags() & Qt.WindowMaximizeButtonHint


def test_disk_editor_says_when_img_cannot_hold_a_broken_crc(window, fixture_disks,
                                                            tmp_path, monkeypatch):
    """Ein `.img` hat kein CRC-Feld — dann muss der Editor das SAGEN, nicht schlucken."""
    from PySide6.QtWidgets import QMessageBox
    gemeldet = []
    monkeypatch.setattr(QMessageBox, "warning", lambda *a, **k: gemeldet.append(a[2]))

    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    ed = _editor(window, abbild)
    window.set_read_only(False)

    span = next(x for x in ed.surface.tracks[0][5].spans if x.is_sector)
    assert ed.select_sector(0, 5, span.index)
    ed.crc_feld.setText("BEEF")
    assert not ed.save_sector(), "nicht gespeichert — und das ist die Wahrheit"

    assert gemeldet and ".hfe" in gemeldet[0], gemeldet
    assert "NICHT in die Datei" in ed.hinweis.text()
    # Im Speicher steht die Änderung trotzdem — sie ist über „Speichern unter…" zu retten.
    assert window.tool.sector_crc(5, 0, span.index) == 0xBEEF


# ─── UDOS-Anhang und Sektoren anlegen/löschen ────────────────────────────────

def test_disk_editor_names_the_udos_extension(window, fixture_disks):
    """Bei UDOS hängt hinter der Daten-CRC ein 4-Byte-Kontrollblock — das muss
    an Format, Größe UND als entschlüsselte Verkettung sichtbar sein."""
    ed = _editor(window, fixture_disks / "udos_boot_scp.hfe")
    ed._springe(seite=0, spur=22)

    text = ed.info.text()
    assert "IBM-MFM + UDOS-Erweiterung" in text, text
    # Nutzdaten + Kontrollblock; die Daten-CRC zaehlt hier so wenig mit wie bei
    # CP/M — sie hat ihr eigenes Feld.
    assert "128+4 Byte" in text, text
    # Die Verkettung selbst steht im eigenen, ÄNDERBAREN Feld darunter.
    assert ed.tail_feld.text() == "05 16 05 16"
    assert "zurück" in ed.tail_deutung.text() and "vor" in ed.tail_deutung.text()
    assert "Spur 22/Sektor" in ed.tail_deutung.text()


def test_disk_editor_hides_the_extension_on_cpm(window, fixture_disks):
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    span = next(x for x in ed.surface.tracks[0][0].spans if x.is_sector)
    ed.select_sector(0, 0, span.index)

    text = ed.info.text()
    assert text.startswith("Format: IBM-MFM,")
    assert "UDOS" not in text and "Kette" not in text and "Kontrollblock" not in text


def test_udos_pointer_is_decoded(window):
    from app.disktool.ui.disk_editor import udos_zeiger
    # Byte 0 = Sektorindex 0-basiert, Byte 1 = Spur (doc/udos_diskettenformat.md §1.2)
    assert udos_zeiger(b"\x05\x16") == "Spur 22/Sektor 6"
    assert udos_zeiger(b"\xFF\xFF") == "Ende"


def test_disk_editor_deletes_and_recreates_a_sector(window, fixture_disks, tmp_path,
                                                    monkeypatch):
    from PySide6.QtWidgets import QDialog, QMessageBox
    monkeypatch.setattr(QMessageBox, "question", lambda *a, **k: QMessageBox.Yes)

    abbild = tmp_path / "cpa.hfe"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.hfe", abbild)
    ed = _editor(window, abbild)
    window.set_read_only(False)

    ed._springe(seite=0, spur=0)
    vorher = ed._sektoren(0, 0)
    ziel = next(s for s in vorher if s.id == 13)
    lage = round(ziel.start * ed.tool.track(0, 0).bytes)

    ed.select_sector(0, 0, ziel.index)
    assert ed.delete_sector()
    assert len(ed._sektoren(0, 0)) == len(vorher) - 1
    assert not any(s.id == 13 for s in ed._sektoren(0, 0))

    # … und wieder anlegen: der Dialog schlägt den auf dieser Spur üblichen Gap vor,
    # der Sektor landet wieder an derselben Stelle.
    from app.disktool.ui.disk_editor import NewSectorDialog
    monkeypatch.setattr(NewSectorDialog, "exec", lambda self: QDialog.Accepted)
    monkeypatch.setattr(NewSectorDialog, "__init__", _dialog_mit(id=13, size=128))
    assert ed.new_sector()

    neu = next(s for s in ed._sektoren(0, 0) if s.id == 13)
    assert neu.ok, "ein neu angelegter Sektor ist sofort gültig"
    assert round(neu.start * ed.tool.track(0, 0).bytes) == lage


def _dialog_mit(**werte):
    """Hilfskonstruktor: Dialog wie gewohnt aufbauen, dann Felder setzen."""
    from app.disktool.ui.disk_editor import NewSectorDialog
    urspruenglich = NewSectorDialog.__init__

    def init(self, tool, cyl, head, spur, udos, parent=None):
        urspruenglich(self, tool, cyl, head, spur, udos, parent)
        if "id" in werte:
            self.f_id.setValue(werte["id"])
        if "size" in werte:
            self.f_size.setCurrentIndex([128, 256, 512, 1024].index(werte["size"]))
        if "gap" in werte:
            self.f_gap.setValue(werte["gap"])
    return init


def test_new_sector_dialog_warns_before_overwriting(window, fixture_disks, tmp_path):
    """Ihr Modell erlaubt das Überschreiben — unbeabsichtigt darf es nicht sein."""
    from app.disktool.ui.disk_editor import NewSectorDialog

    abbild = tmp_path / "cpa.hfe"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.hfe", abbild)
    ed = _editor(window, abbild)
    window.set_read_only(False)

    spur = ed.tool.track(0, 0)
    dlg = NewSectorDialog(ed.tool, 0, 0, spur, False, ed)
    dlg.f_id.setValue(2)          # Sektor 2 gibt es schon → landet auf ihm
    dlg.f_gap.setValue(0)
    von, laenge, getroffen = dlg.betroffen()
    assert getroffen, "der Plan muss die betroffenen Sektoren nennen"
    assert "Überschreibt Sektor" in dlg.vorschau.text()


def test_new_sector_dialog_refuses_what_does_not_fit(window, fixture_disks):
    from PySide6.QtWidgets import QDialogButtonBox
    from app.disktool.ui.disk_editor import NewSectorDialog

    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    spur = ed.tool.track(0, 0)
    dlg = NewSectorDialog(ed.tool, 0, 0, spur, False, ed)
    dlg.f_id.setValue(255)                 # hinter den letzten Sektor
    dlg.f_gap.setValue(4000)               # weit hinter das Spurende
    assert "passt nicht" in dlg.vorschau.text()
    assert not dlg.knoepfe.button(QDialogButtonBox.Ok).isEnabled()


def test_new_sector_dialog_locks_the_encoding_on_a_formatted_track(window,
                                                                   fixture_disks):
    """FM und MFM lassen sich in EINER Spur nicht mischen."""
    from app.disktool.ui.disk_editor import NewSectorDialog

    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    spur = ed.tool.track(0, 0)
    dlg = NewSectorDialog(ed.tool, 0, 0, spur, False, ed)
    assert not dlg.f_mfm.isEnabled()
    assert dlg.f_mfm.currentData() is True


def test_new_sector_dialog_defaults_the_gap_from_the_track(window, fixture_disks):
    from app.disktool.ui.disk_editor import NewSectorDialog

    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    spur = ed.tool.track(0, 0)
    dlg = NewSectorDialog(ed.tool, 0, 0, spur, False, ed)
    # Der Vorschlag ist ein Abstand, der auf DIESER Spur wirklich vorkommt.
    laengen = {round((s.end - s.start) * spur.bytes)
               for s in spur.spans if s.kind == 1}
    assert dlg.f_gap.value() in laengen


# ─── UDOS-Anhang bearbeiten ──────────────────────────────────────────────────

def test_udos_tail_is_an_editable_field_with_decimal_reading(window, fixture_disks):
    """Die vier Rohbytes zum Ändern, daneben was sie bedeuten — Spur/Sektor dezimal."""
    ed = _editor(window, fixture_disks / "udos_boot_scp.hfe")
    ed._springe(seite=0, spur=22)

    assert ed.tail_feld.text() == "05 16 05 16"
    assert ed.tail_bytes() == b"\x05\x16\x05\x16"
    assert ed.tail_deutung.text() == \
        "zurück: Spur 22/Sektor 6    vor: Spur 22/Sektor 6"

    # Beim Tippen läuft die Deutung mit.
    ed.tail_feld.setText("07 15 FF FF")
    assert ed.tail_deutung.text() == "zurück: Spur 21/Sektor 8    vor: Ende"

    # Unvollständige Eingabe wird benannt, nicht geraten.
    ed.tail_feld.setText("07 15")
    assert ed.tail_bytes() is None
    assert "erwartet" in ed.tail_deutung.text()


def test_udos_tail_row_is_absent_on_cpm(window, fixture_disks):
    ed = _editor(window, fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert not ed.udos
    # Bei CP/M gibt es keinen Nachspann — Feld und Deutung bleiben ganz weg.
    assert ed.tail_feld.isHidden() and ed.tail_deutung.isHidden()


def test_udos_tail_is_saved_without_touching_data_or_crc(window, fixture_disks,
                                                         tmp_path):
    """Die Verkettung zu ändern ist etwas anderes, als die Nutzdaten zu ändern."""
    abbild = tmp_path / "udos.hfe"
    shutil.copy(fixture_disks / "udos_boot_scp.hfe", abbild)
    ed = _editor(window, abbild)
    window.set_read_only(False)
    ed._springe(seite=0, spur=22)
    head, cyl, index = ed.aktuell
    daten = window.tool.sector_data(cyl, head, index)

    # Sektor absichtlich defekt lassen …
    ed.crc_feld.setText("BEEF")
    assert ed.save_sector()
    # … und danach NUR die Verkettung ändern.
    ed.tail_feld.setText("07 15 FF FF")
    assert ed.save_sector()

    assert window.tool.sector_tail(cyl, head, index)[:4] == b"\x07\x15\xFF\xFF"
    assert window.tool.sector_data(cyl, head, index) == daten
    assert window.tool.sector_crc(cyl, head, index) == 0xBEEF, \
        "eine absichtlich falsche CRC darf dabei nicht geheilt werden"

    assert ed.reload_sector()
    assert ed.tail_feld.text() == "07 15 FF FF"


def test_udos_tail_refuses_an_incomplete_value(window, fixture_disks, tmp_path,
                                               monkeypatch):
    from PySide6.QtWidgets import QMessageBox
    gemeldet = []
    monkeypatch.setattr(QMessageBox, "warning", lambda *a, **k: gemeldet.append(a[2]))

    abbild = tmp_path / "udos.hfe"
    shutil.copy(fixture_disks / "udos_boot_scp.hfe", abbild)
    ed = _editor(window, abbild)
    window.set_read_only(False)
    ed._springe(seite=0, spur=22)
    head, cyl, index = ed.aktuell
    vorher = window.tool.sector_tail(cyl, head, index)

    ed.tail_feld.setText("07 15 ZZ")
    assert not ed.save_sector()
    assert gemeldet and "hexadezimal" in gemeldet[0], gemeldet
    assert window.tool.sector_tail(cyl, head, index) == vorher, "nichts geschrieben"


def test_udos_tail_is_locked_on_a_protected_disk(window, fixture_disks):
    ed = _editor(window, fixture_disks / "udos_boot_scp.hfe")
    ed._springe(seite=0, spur=22)
    assert window.tool.read_only
    assert ed.tail_feld.isReadOnly()


# ─── Randfälle der Scheibenansicht ───────────────────────────────────────────

def test_disk_editor_shows_an_empty_track_and_still_offers_a_new_sector(
        window, tmp_path):
    """Auf einer Spur ohne Sektoren braucht man den Anlegen-Knopf am dringendsten."""
    from app.core_binding.k1520disk import DiskTool

    abbild = tmp_path / "leer.hfe"
    DiskTool.create(abbild, "cpa780").close()
    ed = _editor(window, abbild)
    window.set_read_only(False)

    # Eine Spur jenseits des Formats gibt es nicht …
    ed._springe(seite=0, spur=ed.surface.cylinders - 1)
    # … eine leergeräumte dagegen schon: alle Sektoren entfernen.
    ed._springe(seite=0, spur=5)
    while ed._sektoren(0, 5):
        window.tool.sector_erase(5, 0, ed._sektoren(0, 5)[0].index)
        ed.surface.reload_track(window.tool, 5, 0)

    ed._springe(seite=0, spur=5)
    assert ed.aktuell is None, "es gibt keinen Sektor zu wählen"
    assert "keine Sektoren" in ed.info.text()
    assert ed.hex.toPlainText() == ""
    assert ed.btn_neu.isEnabled(), "gerade hier muss Anlegen gehen"
    assert not ed.btn_save.isEnabled()


def test_disk_editor_leaves_the_back_of_a_single_sided_disk_empty(window, tmp_path):
    """Einseitige Diskette: die Rückseite bleibt leer — nur die beiden Kreise."""
    from app.core_binding.k1520disk import DiskTool

    abbild = tmp_path / "einseitig.hfe"
    DiskTool.create(abbild, "udos_ss40").close()
    ed = _editor(window, abbild)

    assert ed.surface.heads == 1
    assert ed.surface.tracks[0], "Seite 0 hat Spuren"
    assert ed.surface.tracks[1] == [], "Seite 1 gibt es nicht"

    # Ein Klick auf die rechte Hälfte findet nichts — und stürzt nicht ab.
    from PySide6.QtCore import QPointF
    ed.resize(1000, 800)
    cx, cy, r, _ = ed.surface._mitte(1)
    assert ed.surface.treffer(QPointF(cx, cy - r * 0.5)) is None


# ─── Fensteraufbau: Menüleiste, Symbolleiste, Meldungsorte ───────────────────
#
# Der Umbau von 2026-08-14 (doc/design/13_k1520disktool.md §20) hat die zwei
# Knopfleisten auf halber Höhe durch Menü + ausblendbare Symbolleiste ersetzt und
# die vier Meldungsorte auf klare Rollen verteilt.  Diese Prüfungen halten beides
# fest — vor allem, dass eine NEUE Aktion nicht heimlich menülos bleibt.

def _menue_aktionen(fenster) -> set:
    """Alle Aktionen, die über die Menüleiste erreichbar sind (rekursiv)."""
    gefunden = set()

    def sammle(menue):
        for a in menue.actions():
            if a.menu() is not None:
                sammle(a.menu())
            elif not a.isSeparator():
                gefunden.add(a)

    sammle(fenster.menuBar())
    return gefunden


def test_every_action_is_reachable_from_the_menu_bar(window):
    """Jede Aktion steht in der Menüleiste — die Symbolleiste ist nur die Abkürzung.

    Sie lässt sich ausblenden; wäre eine Aktion nur dort, wäre sie danach weg.
    """
    im_menue = _menue_aktionen(window)
    fehlend = [name for name in dir(window)
               if name.startswith("act_") and getattr(window, name) not in im_menue]
    assert fehlend == [], f"nicht im Menü erreichbar: {fehlend}"


def test_toolbar_shows_actions_that_all_exist_in_the_menu(window):
    in_leiste = [a for a in window.leiste.actions() if not a.isSeparator()]
    assert len(in_leiste) >= 10, "die Leiste soll die häufigen Wege tragen"
    assert set(in_leiste) <= _menue_aktionen(window)
    # Und jede trägt ein Symbol — eine leere Leiste wäre das Gegenteil des Zwecks.
    for a in in_leiste:
        assert not a.icon().isNull(), a.text()


def test_toolbar_can_be_hidden_from_the_view_menu(window):
    """Der Umschalter kommt von Qt selbst (QToolBar.toggleViewAction).

    Er spiegelt die *wirkliche* Sichtbarkeit, deshalb muss das Fenster hier
    tatsächlich angezeigt werden (offscreen genügt).
    """
    assert window.act_leiste_zeigen in _menue_aktionen(window)
    assert window.act_leiste_zeigen.isCheckable()

    window.show()
    assert window.leiste.isVisible() and window.act_leiste_zeigen.isChecked()

    window.act_leiste_zeigen.trigger()
    assert not window.leiste.isVisible(), "Ansicht ▸ Symbolleiste blendet sie aus"
    window.act_leiste_zeigen.trigger()
    assert window.leiste.isVisible()
    window.hide()


def test_log_dock_is_closed_at_start_but_keeps_the_history(window, fixture_disks):
    """Das Protokoll ist die Historie, nicht die Anzeige — es kostet keinen Platz."""
    assert not window.log_dock.isVisibleTo(window)

    window.log("Etwas ist geschehen")
    assert "Etwas ist geschehen" in window.protokoll.toPlainText()
    assert "Etwas ist geschehen" in window.statusBar().currentMessage()
    # Ohne Stufe bleibt der Streifen weg: eine erledigte Aktion ist nichts Dauerhaftes.
    assert not window.info_bar.isVisibleTo(window)

    window.act_protokoll_zeigen.trigger()
    assert window.log_dock.isVisibleTo(window)


def test_transfer_mode_is_a_radio_group_and_shows_in_the_status_bar(window):
    assert window.act_binaer.isChecked() and not window.text_mode
    assert window.st_modus.text() == "binär"

    window.act_text.trigger()
    assert window.text_mode
    assert not window.act_binaer.isChecked(), "die beiden schließen sich aus"
    assert window.st_modus.text() == "Text"


def test_status_bar_shows_the_write_protection_as_a_state(window, fixture_disks):
    assert window.st_schutz.text() == "", "ohne Diskette gibt es keinen Zustand"
    assert window.st_schloss.pixmap().isNull()

    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert "R/O" in window.st_schutz.text() and "nur lesen" in window.st_schutz.text()
    assert not window.st_schloss.pixmap().isNull(), "Schloss als Bild, nicht als Emoji"
    window.set_read_only(False)
    assert "R/W" in window.st_schutz.text() and "schreibbar" in window.st_schutz.text()


def test_the_protection_button_shows_which_state_it_is_in(window, fixture_disks):
    """Ein rastender Knopf allein ist nicht lesbar — Symbol UND Wort wechseln mit."""
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    zu = window.act_schreibschutz.icon().pixmap(24, 24).toImage()
    assert window.act_schreibschutz.iconText() == "R/O"

    window.set_read_only(False)
    assert window.act_schreibschutz.iconText() == "R/W"
    auf = window.act_schreibschutz.icon().pixmap(24, 24).toImage()
    assert auf != zu, "offenes und geschlossenes Schloss müssen sich unterscheiden"


def test_the_status_bar_cannot_be_switched_off(window):
    """Sie trägt den Zustand — wer sie ausblenden könnte, arbeitete blind."""
    assert not hasattr(window, "act_statusleiste")
    assert not any("tatus" in a.text() for a in window.menue_ansicht.actions())
    assert window.statusBar().isVisibleTo(window)


def test_both_panes_are_the_same_width(window):
    """Keine der beiden Listen ist die wichtigere."""
    links, mitte, rechts = window.teiler.sizes()
    assert links == rechts, (links, mitte, rechts)

    # … und sie bleiben es beim Wachsen (gleicher Dehnungsfaktor).
    window.show()
    window.resize(1400, 700)
    window.teiler.setSizes([500, 44, 500])
    links, _, rechts = window.teiler.sizes()
    assert abs(links - rechts) <= 1, (links, rechts)
    window.hide()


def test_the_folder_pane_has_no_footer_line(window, tmp_path):
    """Beide Hälften sind gleich gebaut: Überschrift + Liste, sonst nichts."""
    assert not hasattr(window.folder_view, "fuss")
    (tmp_path / "EINS.TXT").write_text("x")
    window.folder_view.set_folder(tmp_path)
    assert window.folder_view.tree.topLevelItemCount() == 1


def test_the_middle_column_offers_selection_and_everything(window):
    """Vier Knöpfe: aussen die Stapel, innen die Auswahl (→→| →| |← |←←)."""
    knoepfe = [window.btn_alles_holen, window.btn_holen,
               window.btn_schreiben, window.btn_alles_schreiben]
    aktionen = [b.defaultAction() for b in knoepfe]
    assert aktionen == [window.act_alles_raus, window.act_holen,
                        window.act_schreiben, window.act_alles_rein]
    for a in aktionen:
        assert not a.icon().isNull(), a.text()
        assert a in _menue_aktionen(window)


def test_header_carries_path_and_format(window, fixture_disks):
    """Der Kopfbereich trägt die DAUERHAFTEN Angaben — nicht die Statuszeile."""
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert "cpa_cpa780_k5601_clock.img" in window.kopf.pfad.text()
    assert "cpa780" in window.kopf.angaben.text()
    assert "erkannt" in window.kopf.angaben.text()


def test_filesystem_choice_stays_in_step_between_header_and_menu(window,
                                                                 fixture_disks):
    """Feld und Menüpunkt zeigen dieselbe Wahl — gesetzt wird sie an einer Stelle."""
    bild = fixture_disks / "scpx17_cpa780_k5601.hfe"
    assert window.open_image(bild, "cpa_auto")

    assert window.kopf.filesystem() == "cpa_auto"
    assert window._fs_aktionen["cpa_auto"].isChecked()
    assert not window._fs_aktionen[""].isChecked()


def test_context_menus_use_the_same_actions_as_the_menu_bar(window):
    """Wer eine Aktion sperrt, sperrt sie überall — dafür gibt es sie nur einmal."""
    assert window.act_eigenschaften in window.disk_view._aktionen
    assert window.act_loeschen in window.disk_view._aktionen
    assert window.act_schreiben in window.folder_view._aktionen


def test_selection_gates_the_transfer_actions(window, fixture_disks, tmp_path):
    import shutil
    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    assert window.open_image(abbild)
    window.set_read_only(False)

    assert not window.act_holen.isEnabled(), "ohne Auswahl nichts zu holen"
    assert not window.act_loeschen.isEnabled()
    assert not window.act_eigenschaften.isEnabled()

    window.disk_view.tree.topLevelItem(0).setSelected(True)
    assert window.act_holen.isEnabled()
    assert window.act_loeschen.isEnabled()
    assert window.act_eigenschaften.isEnabled()

    window.disk_view.tree.topLevelItem(1).setSelected(True)
    assert not window.act_eigenschaften.isEnabled(), "Eigenschaften gibt es je Datei"


def test_disk_info_dialog_reports_everything_about_the_carrier(window,
                                                               fixture_disks):
    from app.disktool.ui.disk_info_dialog import angaben_text

    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")
    text = angaben_text(window.tool)
    for teil in ("udos_boot_scp.hfe", "Format:", "Dateisystem:", "Zylinder",
                 "Schreibschutz:", "Systemspuren:", "BELEGUNG", "Side0", "Side1"):
        assert teil in text, teil
    assert "Altbestand" in text, "die Auffälligkeit des Mediums gehört dazu"


def test_closing_the_disk_leaves_the_window_standing(window, fixture_disks):
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert window.close_disk()

    assert window.tool is None
    assert window.disk_view.tree.topLevelItemCount() == 0
    assert "Keine Diskette" in window.kopf.pfad.text()
    assert not window.act_speichern_unter.isEnabled()
    assert not window.isWindowModified()


def test_recent_files_menu_follows_what_was_opened(window, fixture_disks):
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")

    namen = [a.text() for a in window.menue_zuletzt.actions()]
    assert namen[0] == "udos_boot_scp.hfe", "zuletzt geöffnet steht oben"
    assert "cpa_cpa780_k5601_clock.img" in namen


def test_a_nameless_application_does_not_touch_the_users_settings(window):
    """Kein Test darf in die Einstellungen des Anwenders schreiben — oder daraus erben."""
    assert window._einstellungen() is None
    window._zustand_sichern()      # muss folgenlos durchlaufen


def test_every_bundled_icon_can_be_rendered(qt_app):
    """Die Symbole liegen mit im Baum — sonst bliebe die Leiste unter Windows leer."""
    from app.disktool.ui.icons import ICON_DIR, icon

    dateien = sorted(p.stem for p in ICON_DIR.glob("*.svg"))
    assert len(dateien) >= 12, dateien
    for name in dateien:
        symbol = icon(name)
        assert not symbol.isNull(), name
        assert not symbol.pixmap(24, 24).isNull(), name


def test_a_single_recent_file_survives_qsettings(window):
    """QSettings gibt eine einelementige Liste als ZEICHENKETTE zurück.

    Ohne Behandlung zerfiel der eine zuletzt geöffnete Pfad in seine Buchstaben
    und „Zuletzt geöffnet" hatte 80 Einträge (§20.5).
    """
    assert window._als_liste("/pfad/zur/diskette.hfe") == ["/pfad/zur/diskette.hfe"]
    assert window._als_liste(["/a.hfe", "/b.hfe"]) == ["/a.hfe", "/b.hfe"]
    assert window._als_liste(None) == []
    assert window._als_liste("") == []


# ─── Handbuch (F1) ───────────────────────────────────────────────────────────
#
# Die Hilfe ist eine gewöhnliche `.md`-Datei, die Qt selbst setzt (§20.7) — kein
# Bauschritt, keine Abhängigkeit.  Geprüft wird, dass sie mitgeliefert wird, dass
# das Inhaltsverzeichnis zum Text passt und — das Wichtigste — dass die dort
# genannten Tastenkürzel WIRKLICH an den Aktionen hängen.  Eine Hilfe, die mit der
# Oberfläche auseinanderläuft, ist schlimmer als keine.

#: Deutsche Tastennamen des Handbuchs → was QKeySequence versteht.
_TASTEN = {"Strg": "Ctrl", "Umschalt": "Shift", "Entf": "Del",
           "Eingabe": "Return", "→": "Right", "←": "Left"}


def _als_kuerzel(deutsch: str):
    from PySide6.QtGui import QKeySequence
    teile = [_TASTEN.get(t, t) for t in deutsch.split("+")]
    return QKeySequence("+".join(teile))


def _handbuch_kuerzel() -> dict:
    """Die Tabelle „Tastenkürzel" des Handbuchs als {Kürzel: Wirkung}."""
    from app.disktool.ui.help_window import lade_handbuch
    text = lade_handbuch()
    tabelle = text.split("## Tastenkürzel", 1)[1].split("\n## ", 1)[0]
    out = {}
    for zeile in tabelle.splitlines():
        if not zeile.startswith("|") or "---" in zeile:
            continue
        spalten = [s.strip() for s in zeile.strip("|").split("|")]
        if len(spalten) == 2 and spalten[0] not in ("Kürzel",):
            out[spalten[0]] = spalten[1]
    return out


def test_help_manual_ships_inside_the_app_tree(window):
    """Sie muss unter `app/` liegen — `doc/` ist nicht im Paket."""
    from app.disktool.ui.help_window import HANDBUCH
    assert HANDBUCH.is_file(), HANDBUCH
    assert "app" in HANDBUCH.parts, "sonst fehlt das Handbuch im Anwenderpaket"
    assert HANDBUCH.read_text(encoding="utf-8").startswith("# k1520DiskTool")


def test_help_window_lists_every_section_and_jumps_to_it(window):
    from app.disktool.ui.help_window import abschnitte, lade_handbuch

    h = window.open_help()
    erwartet = abschnitte(lade_handbuch())
    assert len(erwartet) >= 8, "ein Kurzhandbuch, aber kein Zettel"

    im_verzeichnis = [h.inhalt.item(i).text() for i in range(h.inhalt.count())]
    assert im_verzeichnis == erwartet

    # Qt vergibt keine Anker; gesprungen wird auf den Überschriften-BLOCK.
    assert [t for t, _ in h.ueberschriften()] == erwartet
    for name in erwartet:
        assert h.springe_zu(name), name
    assert not h.springe_zu("Gibt es nicht")


def test_help_window_opens_once_per_window(window):
    h = window.open_help()
    assert window.open_help() is h, "kein zweites Handbuchfenster"
    h.close()


def test_help_search_finds_wraps_and_reports_a_miss(window):
    h = window.open_help()

    h.suchfeld.setText("Schreibschutz")
    assert h.weitersuchen()

    h.suchfeld.setText("Kernspeicherringkern")
    assert not h.weitersuchen()
    assert "nicht gefunden" in h.meldung.text()

    h.suchfeld.setText("")
    assert not h.weitersuchen(), "leere Suche tut nichts"


def test_every_shortcut_in_the_manual_really_exists(window):
    """Jedes Kürzel der Handbuchtabelle hängt an einer Aktion des Fensters."""
    from PySide6.QtGui import QAction

    vorhanden = {}
    for a in window.findChildren(QAction):
        if not a.shortcut().isEmpty():
            vorhanden[a.shortcut().toString()] = a.text()

    fehlend = []
    for kuerzel, wirkung in _handbuch_kuerzel().items():
        if _als_kuerzel(kuerzel).toString() not in vorhanden:
            fehlend.append(f"{kuerzel} ({wirkung})")
    assert fehlend == [], f"im Handbuch versprochen, aber nicht verdrahtet: {fehlend}"


def test_every_shortcut_of_the_window_is_in_the_manual(window):
    """Und die Gegenrichtung: kein Kürzel bleibt unerwähnt."""
    from PySide6.QtGui import QAction

    im_handbuch = {_als_kuerzel(k).toString() for k in _handbuch_kuerzel()}
    fehlend = []
    for a in window.findChildren(QAction):
        if not a.shortcut().isEmpty() and a.shortcut().toString() not in im_handbuch:
            fehlend.append(f"{a.shortcut().toString()} ({a.text()})")
    assert fehlend == [], f"verdrahtet, aber im Handbuch nicht genannt: {fehlend}"


# ─── Wo die Dialoge aufgehen ─────────────────────────────────────────────────
#
# Im Installationsordner liegt das Programm, nicht die Arbeit des Anwenders.
# Aufgelöst wird über `app.paths` — dieselbe Stelle, die auch der Emulator
# benutzt, damit beide Programme auf dieselben Ordner zeigen (§20.8).

def _dialog_startpunkte(window, monkeypatch) -> dict:
    """Alle Dateidialoge einmal aufrufen und ihren Startpfad einsammeln.

    Jeder Dialog wird sofort „abgebrochen" (leerer Rückgabewert), es passiert
    also nichts weiter.
    """
    from PySide6.QtWidgets import QFileDialog
    gesehen = {}

    def datei(titel_index):
        def haken(parent, titel, verzeichnis="", *a, **k):
            gesehen[titel] = verzeichnis
            return ("", "")
        return haken

    def ordner(parent, titel, verzeichnis="", *a, **k):
        gesehen[titel] = verzeichnis
        return ""

    monkeypatch.setattr(QFileDialog, "getOpenFileName", datei(1))
    monkeypatch.setattr(QFileDialog, "getSaveFileName", datei(1))
    monkeypatch.setattr(QFileDialog, "getExistingDirectory", ordner)
    return gesehen


def test_every_file_dialog_gets_a_start_directory(window, fixture_disks,
                                                  monkeypatch):
    """Der Kern der Sache: kein Dialog ohne Startpunkt.

    Ein leerer Startpfad heisst für Qt „Arbeitsverzeichnis" — und das ist beim
    installierten Programm der INSTALLATIONSORDNER.  Dort liegt das Programm,
    nicht die Arbeit des Anwenders.
    """
    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    gesehen = _dialog_startpunkte(window, monkeypatch)

    window._oeffnen_dialog()
    window._speichern_unter_dialog()
    window._archivieren_dialog()
    window._bootabbild_sichern_dialog()
    window._ordner_dialog()

    assert len(gesehen) == 5, gesehen
    for titel, start in gesehen.items():
        assert start, f"{titel}: ohne Startpunkt landet der Dialog im Programmordner"
        assert Path(start).is_absolute() or Path(start).exists(), (titel, start)


def test_folder_side_never_starts_in_the_installation(window, tmp_path,
                                                      monkeypatch):
    """Für die Ordnerseite gilt es ausnahmslos: dort gehören keine Anwenderdateien hin.

    (Bei den Abbildern gibt es eine gewollte Ausnahme — passt kein
    Benutzerordner, bietet ``default_disk_dir`` die MITGELIEFERTEN Beispiele an,
    und die liegen naturgemäß im Programmordner.  Der Emulator macht es ebenso.)
    """
    from app import paths

    # Eine Installation nachbauen, den Datenordner daneben.
    prog = tmp_path / "prog"
    (prog / "bin").mkdir(parents=True)
    (prog / "app").mkdir()
    (prog / "share" / "k1520emu").mkdir(parents=True)
    monkeypatch.setenv(paths.ENV_HOME, str(prog))
    monkeypatch.setenv(paths.ENV_DATA, str(tmp_path / "daten"))

    gesehen = _dialog_startpunkte(window, monkeypatch)
    window._ordner_dialog()

    start = Path(gesehen["Ordner wählen"]).resolve()
    assert prog.resolve() not in start.parents and start != prog.resolve(), start


def test_image_dialogs_start_where_the_emulator_looks(window, monkeypatch):
    """Abbilder: derselbe Ordner, den das Laufwerksfeld des Emulators anbietet."""
    from app import paths

    assert window._disketten_ordner() == str(paths.default_disk_dir())

    gesehen = _dialog_startpunkte(window, monkeypatch)
    window._oeffnen_dialog()
    assert gesehen["Diskettenabbild öffnen"] == str(paths.default_disk_dir())


def test_save_as_starts_next_to_the_open_disk(window, fixture_disks, monkeypatch):
    """Eine Arbeitskopie gehört dorthin, wo das Original liegt."""
    from app import paths

    # Ohne Diskette: der Diskettenordner.
    assert window._neben_der_diskette() == str(paths.default_disk_dir())

    bild = fixture_disks / "cpa_cpa780_k5601_clock.img"
    assert window.open_image(bild)
    gesehen = _dialog_startpunkte(window, monkeypatch)
    window._speichern_unter_dialog()
    assert Path(gesehen["Speichern unter"]).resolve() == bild.parent.resolve()


def test_folder_dialog_starts_in_the_users_file_directory(window, tmp_path,
                                                          monkeypatch):
    """Ordnerseite: der Dateiordner des Anwenders — und danach der gewählte."""
    from app import paths

    assert window.folder_view.folder is None
    assert window._ordner_startpunkt() == str(paths.default_folder_dir())

    # Sobald einer gewählt ist, geht der nächste Dialog dort auf.
    window.folder_view.set_folder(tmp_path)
    assert window._ordner_startpunkt() == str(tmp_path)

    gesehen = _dialog_startpunkte(window, monkeypatch)
    window._ordner_dialog()
    assert gesehen["Ordner wählen"] == str(tmp_path)
