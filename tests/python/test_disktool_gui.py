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
    """Die Erkennung ist übersteuerbar — und beides führt zu einem anderen Inhalt.

    `cpa640` und `scpx640` teilen sich die GEOMETRIE und unterscheiden sich nur im
    `data_start` (Zylinder 0 gegen Zylinder 2).  Die Geometrie kann das nicht
    trennen, das Dateisystem schon: erkannt wird `scpx640`, weil dort ein
    plausibles Verzeichnis steht.
    """
    bild = fixture_disks / "scpx17_cpa780_k5601.hfe"

    assert window.open_image(bild)
    assert window.tool.filesystem == "scpx640"
    assert window.tool.format == "cpa640"
    erkannte_dateien = alle_namen(window.disk_view)
    assert "INIT.COM" in erkannte_dateien

    # Mit erzwungenem cpa640 wird ab Zylinder 0 gelesen — dort steht der
    # Systembereich, also etwas ANDERES als das echte Verzeichnis.
    assert window.open_image(bild, "cpa640")
    assert window.tool.filesystem == "cpa640"
    assert alle_namen(window.disk_view) != erkannte_dateien


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
