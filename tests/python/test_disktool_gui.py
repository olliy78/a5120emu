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
    assert window.chk_readonly.isChecked()
    assert window.btn_alles_raus.isEnabled()
    assert not window.btn_speichern.isEnabled()


def test_udos_disk_is_shown_as_one_carrier_with_two_side_groups(window, fixture_disks):
    assert window.open_image(fixture_disks / "udos_boot_scp.hfe")

    g = gruppen(window.disk_view)
    assert len(g) == 2, "beide Seiten gehören zu EINER Diskette und werden gruppiert"
    assert g[0].startswith("Side0") and g[1].startswith("Side1")
    assert "UDOS.SYS.4.3" in g[0]

    namen = alle_namen(window.disk_view)
    assert "HELP.DAT.00" in namen
    assert "69 Dateien" in window.disk_view.fuss.text()
    # Auffälligkeiten des Mediums werden angezeigt, nicht verschwiegen.
    assert window.disk_view.hinweis.isVisibleTo(window)
    assert "Altbestand" in window.disk_view.hinweis.text()


def test_unrecognised_image_locks_the_buttons(window, fixture_disks):
    """Ohne Erkennung bleibt die Liste leer, die Meldung steht im Fenster."""
    assert not window.open_image(fixture_disks / "cpa_mini.hfe")

    assert window.tool is None
    assert window.disk_view.tree.topLevelItemCount() == 0
    assert "passt zu keinem Format" in window.disk_view.hinweis.text()
    assert "4 Sektoren" in window.disk_view.hinweis.text()
    for knopf in (window.btn_alles_rein, window.btn_loeschen, window.btn_speichern):
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
    assert "●" in window.windowTitle(), "ungespeicherte Änderung wird angezeigt"

    assert window.erase_refs(["NEU.TXT"])
    assert "NEU.TXT" not in alle_namen(window.disk_view)

    assert window.save()
    assert "●" not in window.windowTitle()


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
    from PySide6.QtWidgets import QMessageBox
    monkeypatch.setattr(QMessageBox, "critical", lambda *a, **k: None)

    import shutil
    abbild = tmp_path / "cpa.img"
    shutil.copy(fixture_disks / "cpa_cpa780_k5601_clock.img", abbild)
    original = abbild.read_bytes()

    assert window.open_image(abbild)
    assert window.tool.read_only
    assert window.chk_readonly.isChecked()

    # Lesen: frei.  Schreiben: gesperrt — und zwar wirklich, nicht nur optisch.
    assert window.btn_alles_raus.isEnabled()
    assert not window.btn_rein.isEnabled()
    assert not window.btn_loeschen.isEnabled()

    quelle = tmp_path / "NEU.TXT"
    quelle.write_text("Inhalt")
    assert not window.insert_paths([str(quelle)])
    assert "schreibgeschuetzt" in window.protokoll.toPlainText()
    assert not window.tool.dirty
    assert abbild.read_bytes() == original

    # Nach dem bewussten Schritt geht es.
    window.set_read_only(False)
    assert window.btn_rein.isEnabled()
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
    assert not window.btn_bootabbild.isEnabled(), "ohne Diskette gesperrt"

    assert window.open_image(fixture_disks / "cpa_cpa780_k5601_clock.img")
    assert window.btn_bootabbild.isEnabled()

    # Eine Datendiskette (Dateisystem ab Zylinder 0) hat nichts zu sichern.
    leer = tmp_path / "daten.hfe"
    assert window.create_disk(leer, "cpa800")
    assert not window.btn_bootabbild.isEnabled()


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
