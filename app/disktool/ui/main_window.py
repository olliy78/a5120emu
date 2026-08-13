"""Hauptfenster des k1520DiskTool.

Zwei-Fenster-Ansicht: links die Diskette, rechts ein Linux-Ordner, dazwischen die
Übertragung in beide Richtungen (doc/design/13_k1520disktool.md §11.2).

Die vier zugesagten Verhaltensregeln (§1) sind hier verdrahtet:

* **Eine beidseitige UDOS-Diskette ist EIN Datenträger** — beide Seiten in einer
  Liste, `Side0/`/`Side1/` beim Extrahieren und Einfügen.
* **Die Ansicht ist immer frisch** — jede schreibende Aktion endet mit
  :meth:`MainWindow._reload`, das das Verzeichnis neu aus dem Medium liest.
* **Passt es nicht, wird gar nicht erst geschrieben** — vor jedem Ordner-Einfügen
  läuft ``check_fit``; die Bibliothek nimmt eine gescheiterte Stapeloperation
  ohnehin zurück.
* **Ohne Erkennung kein Schreiben** — schlägt das Öffnen fehl, bleibt die Liste
  leer, die Meldung steht im Fenster und die Schaltflächen sind gesperrt.

Alle Aktionen sind als Methoden ohne Dialog erreichbar (``open_image``,
``create_disk``, ``extract_all``, …); die Dialoge sitzen nur in den
Klick-Behandlern.  Damit ist das Fenster headless prüfbar.
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import List, Optional

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QFileDialog, QHBoxLayout, QInputDialog, QLabel,
    QMainWindow, QMessageBox, QPlainTextEdit, QPushButton, QSplitter,
    QVBoxLayout, QWidget,
)

from app.disktool.archive import create_archive

from app.core_binding.k1520disk import DiskTool, K1520DiskError, filesystems
from app.disktool.ui.disk_view import DiskView
from app.disktool.ui.folder_view import FolderView
from app.disktool.ui.properties_dialog import PropertiesDialog

#: Endungen, die im Textmodus vorgeschlagen werden.
TEXT_ENDUNGEN = {".txt", ".text", ".asm", ".mac", ".doc", ".md", ".log", ".dat"}


class MainWindow(QMainWindow):
    """Fenster des Diskettenwerkzeugs."""

    def __init__(self, image: Optional[str] = None, folder: Optional[str] = None):
        super().__init__()
        self.setWindowTitle("k1520DiskTool")
        self.resize(1100, 640)

        self.tool: Optional[DiskTool] = None

        # ── Kopfleiste ──────────────────────────────────────────────────────
        self.btn_oeffnen = QPushButton("Abbild öffnen…")
        self.btn_neu = QPushButton("Neue Diskette…")
        self.fs_wahl = QComboBox()
        self.fs_wahl.addItem("automatisch erkennen", "")
        for f in filesystems():
            self.fs_wahl.addItem(f"{f.name} — {f.description}", f.name)
        self.fs_wahl.currentIndexChanged.connect(self._fs_gewaehlt)

        # Der Schreibschutz ist beim Oeffnen AN.  Ihn zu entfernen ist ein
        # bewusster Schritt — wer eine unersetzliche Diskette vor sich hat, legt
        # vorher lieber mit „Speichern unter…" eine Arbeitskopie an.
        self.chk_readonly = QCheckBox("Nur lesen")
        self.chk_readonly.setChecked(True)
        self.chk_readonly.setToolTip(
            "Solange der Haken gesetzt ist, kann die Diskette nicht veraendert werden.")
        self.chk_readonly.toggled.connect(self._readonly_umgeschaltet)

        kopf = QHBoxLayout()
        kopf.addWidget(self.btn_oeffnen)
        kopf.addWidget(self.btn_neu)
        kopf.addWidget(QLabel("Dateisystem:"))
        kopf.addWidget(self.fs_wahl, 1)
        kopf.addWidget(self.chk_readonly)

        # ── Mitte: Diskette | Übertragung | Ordner ──────────────────────────
        self.disk_view = DiskView()
        self.folder_view = FolderView()

        self.btn_raus = QPushButton("→")
        self.btn_raus.setToolTip("Ausgewählte Dateien in den Ordner holen")
        self.btn_rein = QPushButton("←")
        self.btn_rein.setToolTip("Ausgewählte Dateien auf die Diskette schreiben")
        mitte = QVBoxLayout()
        mitte.addStretch(1)
        mitte.addWidget(self.btn_raus)
        mitte.addWidget(self.btn_rein)
        mitte.addStretch(1)
        mitte_w = QWidget()
        mitte_w.setLayout(mitte)

        teiler = QSplitter(Qt.Horizontal)
        teiler.addWidget(self.disk_view)
        teiler.addWidget(mitte_w)
        teiler.addWidget(self.folder_view)
        teiler.setStretchFactor(0, 5)
        teiler.setStretchFactor(1, 0)
        teiler.setStretchFactor(2, 4)

        # ── Fußleiste ───────────────────────────────────────────────────────
        self.btn_alles_raus = QPushButton("Alles extrahieren")
        self.btn_alles_rein = QPushButton("Alles einfügen")
        self.btn_loeschen = QPushButton("Löschen")
        self.btn_speichern = QPushButton("Speichern")
        self.btn_speichern_unter = QPushButton("Speichern unter…")
        self.btn_archivieren = QPushButton("Archivieren…")
        self.btn_archivieren.setToolTip(
            "Abbild (.hfe), alle Dateien und ein Inhaltsverzeichnis in eine .zip")
        self.btn_bootabbild = QPushButton("Bootabbild sichern…")
        self.btn_bootabbild.setToolTip(
            "Die Systemspuren dieser Diskette als .bin sichern — damit lässt sich "
            "später eine neue Diskette bootfähig anlegen.")
        self.chk_text = QComboBox()
        self.chk_text.addItems(["binär", "Text (CR LF ↔ LF)"])

        fuss = QHBoxLayout()
        fuss.addWidget(self.btn_alles_raus)
        fuss.addWidget(self.btn_alles_rein)
        fuss.addWidget(self.btn_loeschen)
        fuss.addStretch(1)
        fuss.addWidget(QLabel("Übertragung:"))
        fuss.addWidget(self.chk_text)
        fuss.addWidget(self.btn_speichern)
        fuss.addWidget(self.btn_speichern_unter)
        fuss.addWidget(self.btn_archivieren)
        fuss.addWidget(self.btn_bootabbild)

        self.protokoll = QPlainTextEdit()
        self.protokoll.setReadOnly(True)
        self.protokoll.setMaximumHeight(120)

        mittelteil = QVBoxLayout()
        mittelteil.addLayout(kopf)
        mittelteil.addWidget(teiler, 1)
        mittelteil.addLayout(fuss)
        mittelteil.addWidget(self.protokoll)

        zentral = QWidget()
        zentral.setLayout(mittelteil)
        self.setCentralWidget(zentral)
        self.statusBar().showMessage("Bereit")

        # ── Verdrahtung ─────────────────────────────────────────────────────
        self.btn_oeffnen.clicked.connect(self._oeffnen_dialog)
        self.btn_neu.clicked.connect(self._neu_dialog)
        self.btn_raus.clicked.connect(self._extrahieren_auswahl)
        self.btn_rein.clicked.connect(self._einfuegen_auswahl)
        self.btn_alles_raus.clicked.connect(self._alles_extrahieren)
        self.btn_alles_rein.clicked.connect(self._alles_einfuegen)
        self.btn_loeschen.clicked.connect(self._loeschen_auswahl)
        self.btn_speichern.clicked.connect(self.save)
        self.btn_speichern_unter.clicked.connect(self._speichern_unter_dialog)
        self.btn_archivieren.clicked.connect(self._archivieren_dialog)
        self.btn_bootabbild.clicked.connect(self._bootabbild_sichern_dialog)
        self.folder_view.choose_requested.connect(self._ordner_dialog)
        self.folder_view.disk_files_dropped.connect(self._extrahieren_refs)
        self.disk_view.files_dropped.connect(self._einfuegen_pfade)
        self.disk_view.properties_requested.connect(self._eigenschaften_ref)
        self.disk_view.extract_requested.connect(self._extrahieren_refs)
        self.disk_view.erase_requested.connect(self._loeschen_refs)

        self._enable_write(False)
        if folder:
            self.folder_view.set_folder(folder)
        if image:
            self.open_image(image)

    # ════════════════════════════════════════════════════════════════════════
    # Zustand
    # ════════════════════════════════════════════════════════════════════════

    @property
    def text_mode(self) -> bool:
        return self.chk_text.currentIndex() == 1

    def log(self, text: str) -> None:
        self.protokoll.appendPlainText(text)
        self.statusBar().showMessage(text.splitlines()[0] if text else "")

    def _enable_write(self, offen: bool) -> None:
        """Bedienelemente nach Zustand freigeben.

        Zwei Stufen: **offen** (etwas Erkanntes liegt vor) gibt das Lesen frei —
        Extrahieren, Speichern unter, Archivieren.  Schreiben zusaetzlich erst,
        wenn der Schreibschutz entfernt ist.
        """
        schreibbar = offen and self.tool is not None and not self.tool.read_only
        for w in (self.btn_raus, self.btn_alles_raus,
                  self.btn_speichern_unter, self.btn_archivieren):
            w.setEnabled(offen)
        # Systemspuren gibt es nicht ueberall — eine Datendiskette (cpa800) beginnt
        # auf Zylinder 0 und hat nichts zu sichern.
        self.btn_bootabbild.setEnabled(
            offen and self.tool is not None and self.tool.boot_area_size(0) > 0)
        for w in (self.btn_rein, self.btn_alles_rein, self.btn_loeschen,
                  self.btn_speichern):
            w.setEnabled(schreibbar)
        self.chk_readonly.setEnabled(offen)

    def _reload(self) -> None:
        """Ansicht aus dem Medium neu aufbauen (§9.3 — kein Zwischenspeicher)."""
        if self.tool is None:
            self.disk_view.clear()
            return
        self.disk_view.set_disk(self.tool, self.tool.list())
        self.folder_view.refresh()
        stern = " ●" if self.tool.dirty else ""
        self.setWindowTitle(f"k1520DiskTool — {Path(self.tool.path).name}{stern}")

    def _fehler(self, titel: str, text: str) -> None:
        self.log(f"{titel}: {text}")
        QMessageBox.critical(self, titel, text)

    # ════════════════════════════════════════════════════════════════════════
    # Diskette öffnen / anlegen
    # ════════════════════════════════════════════════════════════════════════

    def open_image(self, path, filesystem: str = "") -> bool:
        """Abbild öffnen.  Scheitert die Erkennung, bleibt alles gesperrt."""
        self._close_tool()
        try:
            self.tool = DiskTool.open(path, filesystem or None)
        except (K1520DiskError, FileNotFoundError) as e:
            self.tool = None
            self.disk_view.clear()
            self.disk_view.kopf.setText(f"<b>{path}</b>")
            self.disk_view.hinweis.setText(str(e))
            self.disk_view.hinweis.show()
            self._enable_write(False)
            self.log(f"Nicht geöffnet: {e}")
            return False

        self.chk_readonly.blockSignals(True)
        self.chk_readonly.setChecked(self.tool.read_only)
        self.chk_readonly.blockSignals(False)
        self._enable_write(True)
        self._reload()
        hinweis = "" if self.tool.unambiguous else \
            f"  (nicht eindeutig, auch möglich: {', '.join(self.tool.alternatives)})"
        self.log(f"Geöffnet: {path} — {self.tool.filesystem}{hinweis}")
        return True

    def create_disk(self, path, filesystem: str, label: str = "",
                    boot_image=None) -> bool:
        """Neue Diskette anlegen — mit ``boot_image`` als **Bootdiskette**.

        Das Bootabbild geht in die Systemspuren vor dem Dateisystem; passt es nicht,
        wird gar nichts angelegt und die Meldung nennt beide Grössen.
        """
        self._close_tool()
        try:
            self.tool = DiskTool.create(path, filesystem, label, boot_image)
        except K1520DiskError as e:
            self.tool = None
            self._enable_write(False)
            self._fehler("Diskette anlegen", str(e))
            return False
        self.chk_readonly.blockSignals(True)
        self.chk_readonly.setChecked(False)      # frisch angelegt = zum Beschreiben da
        self.chk_readonly.blockSignals(False)
        self._enable_write(True)
        self._reload()
        boot = f", Bootabbild {Path(boot_image).name}" if boot_image else ""
        self.log(f"Angelegt: {path} ({filesystem}, {self.tool.volume_count} Seiten{boot})")
        return True

    def _close_tool(self) -> None:
        if self.tool is not None:
            self.tool.close()
            self.tool = None

    # ════════════════════════════════════════════════════════════════════════
    # Übertragung — ohne Dialog aufrufbar (Tests)
    # ════════════════════════════════════════════════════════════════════════

    def extract_all(self, dest_dir) -> bool:
        """Alles extrahieren; bei mehreren Seiten entstehen `Side0/`, `Side1/`."""
        if self.tool is None:
            return False
        try:
            self.tool.extract_all(dest_dir, text=self.text_mode)
        except K1520DiskError as e:
            self._fehler("Extrahieren", str(e))
            return False
        self.folder_view.set_folder(dest_dir)
        self._reload()
        self.log(f"Extrahiert nach {dest_dir}")
        return True

    def insert_all(self, src_dir) -> bool:
        """Ordner einfügen — Struktur und Platz werden VORHER geprüft (§9.2)."""
        if self.tool is None:
            return False
        bericht = self.tool.check_fit(src_dir)
        if bericht != "passt":
            self._fehler("Einfügen", bericht + "\nEs wurde nichts geschrieben.")
            return False
        try:
            self.tool.insert_all(src_dir, text=self.text_mode)
        except K1520DiskError as e:
            self._fehler("Einfügen", str(e))
            self._reload()
            return False
        self._reload()
        self.log(f"Eingefügt aus {src_dir}")
        return True

    def extract_refs(self, refs: List[str], dest_dir) -> bool:
        """Ausgewählte Dateien holen; mehrseitige Disketten in ihre `SideN/`."""
        if self.tool is None or not refs:
            return False
        ziel = Path(dest_dir)
        ziel.mkdir(parents=True, exist_ok=True)
        try:
            for ref in refs:
                unter = ziel
                if "/" in ref and self.tool.volume_count > 1:
                    unter = ziel / ref.split("/", 1)[0]
                    unter.mkdir(parents=True, exist_ok=True)
                name = ref.split("/")[-1].replace(":", "_")
                self.tool.extract(ref, unter / name, text=self.text_mode)
        except K1520DiskError as e:
            self._fehler("Extrahieren", str(e))
            return False
        self.folder_view.refresh()
        self.log(f"{len(refs)} Dateien nach {dest_dir}")
        return True

    def insert_paths(self, paths: List[str], volume: int = 0) -> bool:
        """Einzelne Dateien auf eine Seite schreiben."""
        if self.tool is None or not paths:
            return False
        praefix = ""
        if self.tool.volume_count > 1:
            praefix = f"{self.tool.volume_dir(volume)}/"
        try:
            for p in paths:
                if Path(p).is_dir():
                    raise K1520DiskError(
                        f"{p} ist ein Ordner — Ordner bitte über „Alles einfügen“")
                name = praefix + Path(p).name
                text = self.text_mode or Path(p).suffix.lower() in TEXT_ENDUNGEN
                self.tool.insert(p, name, text=text, overwrite=True)
        except K1520DiskError as e:
            self._fehler("Einfügen", str(e))
            self._reload()
            return False
        self._reload()
        self.log(f"{len(paths)} Dateien auf die Diskette")
        return True

    def erase_refs(self, refs: List[str]) -> bool:
        if self.tool is None or not refs:
            return False
        try:
            for ref in refs:
                self.tool.erase(ref)
        except K1520DiskError as e:
            self._fehler("Löschen", str(e))
            self._reload()
            return False
        self._reload()
        self.log(f"{len(refs)} Dateien gelöscht")
        return True

    def save_as(self, path) -> bool:
        """Unter neuem Namen/Container speichern und **dort weiterarbeiten**.

        Auch bei Schreibschutz erlaubt — die Quelle bleibt unberuehrt.  Genau der
        Weg, um vor Aenderungen eine Arbeitskopie anzulegen.
        """
        if self.tool is None:
            return False
        try:
            self.tool.save_as(path)
        except K1520DiskError as e:
            self._fehler("Speichern unter", str(e))
            return False
        self._reload()
        self.log(f"Gespeichert unter {path}")
        return True

    def archive(self, zip_path) -> bool:
        """Abbild (.hfe), alle Dateien und ein Inhaltsverzeichnis in eine `.zip`.

        Reine Leseoperation — auch mit gesetztem Schreibschutz benutzbar.
        """
        if self.tool is None:
            return False
        try:
            ziel = create_archive(self.tool, zip_path, text_mode=self.text_mode)
        except (K1520DiskError, OSError) as e:
            self._fehler("Archivieren", str(e))
            return False
        self.log(f"Archiviert: {ziel}")
        return True

    def save_boot_image(self, path, volume: int = 0) -> bool:
        """Die Systemspuren als `.bin` sichern — der Weg zum eigenen Bootabbild.

        Reine Leseoperation; damit wird aus einer vorhandenen Bootdiskette die Datei,
        die „Neue Diskette" später bootfähig macht.
        """
        if self.tool is None:
            return False
        try:
            self.tool.read_boot_image(path, volume)
        except (K1520DiskError, OSError) as e:
            self._fehler("Bootabbild sichern", str(e))
            return False
        self.log(f"Bootabbild gesichert: {path} ({self.tool.boot_area_size(volume)} Byte)")
        return True

    def set_read_only(self, ro: bool) -> None:
        """Schreibschutz setzen — ohne Rueckfrage (die sitzt im Klick-Behandler)."""
        if self.tool is None:
            return
        self.tool.set_read_only(ro)
        self.chk_readonly.blockSignals(True)
        self.chk_readonly.setChecked(ro)
        self.chk_readonly.blockSignals(False)
        self._enable_write(True)
        self.log("Schreibschutz " + ("gesetzt" if ro else "aufgehoben"))

    def save(self) -> bool:
        if self.tool is None:
            return False
        try:
            self.tool.flush()
        except K1520DiskError as e:
            self._fehler("Speichern", str(e))
            return False
        self._reload()
        self.log(f"Gespeichert: {self.tool.path}")
        return True

    # ════════════════════════════════════════════════════════════════════════
    # Dialoge (Klick-Behandler)
    # ════════════════════════════════════════════════════════════════════════

    def _oeffnen_dialog(self) -> None:
        pfad, _ = QFileDialog.getOpenFileName(
            self, "Diskettenabbild öffnen", "",
            "Diskettenabbilder (*.hfe *.dmk *.img);;Alle Dateien (*)")
        if pfad:
            self.fs_wahl.setCurrentIndex(0)
            self.open_image(pfad)

    def _neu_dialog(self) -> None:
        namen = [f.name for f in filesystems()]
        fs, ok = QInputDialog.getItem(self, "Neue Diskette", "Dateisystem:", namen, 0, False)
        if not ok:
            return
        pfad, _ = QFileDialog.getSaveFileName(
            self, "Neue Diskette anlegen", "",
            "HFE-Abbild (*.hfe);;DMK-Abbild (*.dmk);;Sektorabbild (*.img)")
        if not pfad:
            return

        # Bootfähig? — nur, wo es Systemspuren gibt (eine Datendiskette wie cpa800
        # beginnt auf Zylinder 0 und kann gar nicht booten).
        boot = self._bootabbild_waehlen(fs)
        if boot is False:          # Auswahl abgebrochen → gar nichts anlegen
            self.log("Neue Diskette: abgebrochen (kein Bootabbild ausgewählt)")
            return

        label, _ = QInputDialog.getText(self, "Neue Diskette", "Datenträgername:")
        self.create_disk(pfad, fs, label or "", boot or None)

    def _bootabbild_waehlen(self, filesystem: str):
        """Bootabbild für eine neue Diskette erfragen.

        Returns:
            Pfad (str) · ``None`` = gewöhnliche Diskette · ``False`` = abgebrochen,
            es soll gar nichts angelegt werden.
        """
        platz = next((f.boot_capacity for f in filesystems() if f.name == filesystem), 0)
        if platz <= 0:
            return None

        frage = QMessageBox.question(
            self, "Neue Diskette",
            f"Soll die Diskette bootfähig sein?\n\n"
            f"Die Systemspuren von „{filesystem}\" fassen {platz} Byte. "
            f"Ein Bootabbild (.bin) holt man mit „Bootabbild sichern\" aus einer "
            f"vorhandenen Bootdiskette.",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
        if frage != QMessageBox.Yes:
            return None

        pfad, _ = QFileDialog.getOpenFileName(
            self, "Bootabbild auswählen", "",
            "Bootabbild (*.bin);;Alle Dateien (*)")
        return pfad or False

    def _speichern_unter_dialog(self) -> None:
        if self.tool is None:
            return
        # UDOS kann kein .img tragen — den Filter dann gar nicht erst anbieten.
        filter_ = "HFE-Abbild (*.hfe);;DMK-Abbild (*.dmk)"
        if not any(f.name == self.tool.filesystem and f.type == "udos"
                   for f in filesystems()):
            filter_ += ";;Sektorabbild (*.img)"
        pfad, _ = QFileDialog.getSaveFileName(self, "Speichern unter", "", filter_)
        if pfad:
            self.save_as(pfad)

    def _archivieren_dialog(self) -> None:
        if self.tool is None:
            return
        vorschlag = str(Path(self.tool.path).with_suffix(".zip"))
        pfad, _ = QFileDialog.getSaveFileName(
            self, "Archivieren", vorschlag, "ZIP-Archiv (*.zip)")
        if pfad:
            self.archive(pfad)

    def _bootabbild_sichern_dialog(self) -> None:
        if self.tool is None or self.tool.boot_area_size(0) == 0:
            return
        # Bei UDOS ist jede Seite ein eigener Datentraeger — gebootet wird von Seite 0.
        vorschlag = str(Path(self.tool.path).with_suffix(".bin"))
        pfad, _ = QFileDialog.getSaveFileName(
            self, "Bootabbild sichern", vorschlag, "Bootabbild (*.bin)")
        if pfad:
            self.save_boot_image(pfad)

    def _readonly_umgeschaltet(self, an: bool) -> None:
        """Haken „Nur lesen" — der bewusste Schritt zum Schreiben.

        Beim Zurueckschalten auf Nur-Lesen mit ungespeicherten Aenderungen wird
        gefragt: speichern, verwerfen (Datei neu einlesen) oder abbrechen.
        """
        if self.tool is None:
            return
        if an and self.tool.dirty:
            antwort = QMessageBox.question(
                self, "Ungespeicherte Änderungen",
                "Die Diskette hat ungespeicherte Änderungen.\n"
                "Vor dem Schreibschutz speichern?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel)
            if antwort == QMessageBox.Cancel:
                self.chk_readonly.blockSignals(True)
                self.chk_readonly.setChecked(False)
                self.chk_readonly.blockSignals(False)
                return
            if antwort == QMessageBox.Yes:
                if not self.save():
                    self.chk_readonly.blockSignals(True)
                    self.chk_readonly.setChecked(False)
                    self.chk_readonly.blockSignals(False)
                    return
            else:
                # Verwerfen heisst: die Datei noch einmal einlesen.
                pfad = self.tool.path
                self.open_image(pfad, self.fs_wahl.currentData() or "")
                return
        self.set_read_only(an)

    def _ordner_dialog(self) -> None:
        pfad = QFileDialog.getExistingDirectory(self, "Ordner wählen")
        if pfad:
            self.folder_view.set_folder(pfad)

    def _fs_gewaehlt(self) -> None:
        """Erkennung übersteuern — Abbild mit dem gewählten Dateisystem neu öffnen."""
        if self.tool is None:
            return
        self.open_image(self.tool.path, self.fs_wahl.currentData())

    def _ziel_ordner(self) -> Optional[str]:
        if self.folder_view.folder:
            return str(self.folder_view.folder)
        pfad = QFileDialog.getExistingDirectory(self, "Zielordner wählen")
        if pfad:
            self.folder_view.set_folder(pfad)
            return pfad
        return None

    def _extrahieren_auswahl(self) -> None:
        refs = self.disk_view.selected_refs()
        if not refs:
            QMessageBox.information(self, "Extrahieren", "Keine Datei ausgewählt.")
            return
        ziel = self._ziel_ordner()
        if ziel:
            self.extract_refs(refs, ziel)

    def _extrahieren_refs(self, refs: List[str]) -> None:
        ziel = self._ziel_ordner()
        if ziel:
            self.extract_refs(refs, ziel)

    def _einfuegen_auswahl(self) -> None:
        pfade = self.folder_view.selected_paths()
        if not pfade:
            QMessageBox.information(self, "Einfügen", "Keine Datei ausgewählt.")
            return
        seite = self.folder_view.selected_side()
        if seite is None:
            seite = self.disk_view.current_volume()
        self.insert_paths(pfade, seite)

    def _einfuegen_pfade(self, pfade: List[str], volume: int) -> None:
        self.insert_paths(pfade, volume)

    def _alles_extrahieren(self) -> None:
        ziel = self._ziel_ordner()
        if ziel:
            self.extract_all(ziel)

    def _alles_einfuegen(self) -> None:
        quelle = self._ziel_ordner()
        if quelle:
            self.insert_all(quelle)

    def _loeschen_auswahl(self) -> None:
        self._loeschen_refs(self.disk_view.selected_refs())

    def _loeschen_refs(self, refs: List[str]) -> None:
        if not refs:
            QMessageBox.information(self, "Löschen", "Keine Datei ausgewählt.")
            return
        antwort = QMessageBox.question(
            self, "Löschen",
            f"{len(refs)} Datei(en) von der Diskette löschen?\n" + "\n".join(refs[:10]))
        if antwort == QMessageBox.Yes:
            self.erase_refs(refs)

    # ── Eigenschaften ───────────────────────────────────────────────────────

    def show_properties(self, ref: str) -> bool:
        """Eigenschaften-Dialog zu einer Datei öffnen.

        Der Dialog bekommt den **frisch gelesenen** Verzeichniseintrag (§9.3) und
        schreibt selbst; danach wird die Ansicht neu aufgebaut, weil ein geänderter
        Nutzerbereich den Namen verändert.

        Returns:
            False, wenn zu ``ref`` kein Eintrag (mehr) im Verzeichnis steht.
        """
        if self.tool is None:
            return False
        eintrag = next((e for e in self.tool.list() if e.ref == ref), None)
        if eintrag is None:
            self._fehler("Eigenschaften", f"'{ref}' steht nicht im Verzeichnis.")
            return False

        dialog = PropertiesDialog(self.tool, eintrag, self)
        dialog.exec()
        self._reload()
        return True

    def _eigenschaften_ref(self, ref: str) -> None:
        self.show_properties(ref)

    # ── Schließen ───────────────────────────────────────────────────────────

    def closeEvent(self, event):  # noqa: N802
        if self.tool is not None and self.tool.dirty:
            antwort = QMessageBox.question(
                self, "Ungespeicherte Änderungen",
                "Die Diskette hat ungespeicherte Änderungen. Jetzt speichern?",
                QMessageBox.Yes | QMessageBox.No | QMessageBox.Cancel)
            if antwort == QMessageBox.Cancel:
                event.ignore()
                return
            if antwort == QMessageBox.Yes and not self.save():
                event.ignore()
                return
        self._close_tool()
        event.accept()
