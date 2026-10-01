"""
K1520 Emulator - Dock „Schnittstellen" (serielle Schnittstellen nach außen)
===========================================================================

Je einstellbarer Schnittstelle der Maschine ein Block im Stil der Laufwerkskästen
(`app/ui/drive_widget.py`), darunter je feste Schnittstelle (Tastatur) eine Zeile.
Entwurf: ``doc/design/19_serielle_schnittstellen.md`` §9; Einordnung in die
Oberfläche: ``doc/design/11_python_app.md`` §10.10.

Leitsätze (nicht aufweichen):

* **Namen, Stecker, V.24-Fähigkeit und Taktquellen kommen NUR aus dem Kern**
  (``serial_info``) — dieses Modul kennt weder „DFÜ/V.24" noch „IFS 1".  Der A5120
  und der K8915 teilen sich damit denselben Kasten, ohne ``if machine == …``.
* **Der Kern ist die Quelle der Wahrheit.**  Ein Bedienelement ändert die
  Einstellung über ``serial_configure`` und lädt sich im Takt aus dem Kern nach;
  was der Kern ablehnt, bleibt nicht stehen.  Fehler stehen als Zeile im Block,
  **nie** in einem Meldungsfenster (vgl. ``DriveWidget`` und ``diskNotice``).
* **Gesperrt im Betrieb** (§4): Betriebsart, Rolle, Host, Port und Datei, solange
  die Schnittstelle aktiv ist (lauscht, verbindet, verbunden, Datei offen) — Loop,
  RTS/CTS-Brücke, XON/XOFF und Taktquelle wirken sofort.
* **Der Knopf** heißt im Server „Starten/Beenden", im Client „Verbinden/Trennen"
  (auch WÄHREND der Client noch versucht — nur der Knopf beendet den Dauerversuch,
  §7.1), bei Datei „Starten/Beenden".  Gesperrt bei Rx/Tx-Loop (§6.5) und bei einem
  ungültigen Host.
* **Die Konfiguration** (``zustand_lesen``/``zustand_anwenden``) steht je
  Schnittstelle unter ihrem Namen im Abschnitt ``schnittstellen:``; ``aktiv`` ist
  der Zustand beim Beenden, die Wiederaufnahme beim Start ist §7.4a.
"""

from __future__ import annotations

from typing import Dict, List, Optional

from PySide6.QtCore import QTimer, Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox, QComboBox, QFileDialog, QFrame, QHBoxLayout, QLabel, QLineEdit,
    QPushButton, QSpinBox, QVBoxLayout, QWidget,
)

from app.core_binding import k1520 as K
from app.ui.focus import release_focus

#: Aktualisierung des Docks aus dem Kern (Entwurf §9: ≈ 4 Hz).
TAKT_MS = 250

#: Zustände, in denen die Schnittstelle „aktiv" ist (Felder gesperrt, ``aktiv: true``).
AKTIV = (K.SER_VERBINDET, K.SER_LAUSCHT, K.SER_VERBUNDEN)

#: Anzeigenamen und Konfigurationswörter der Aufzählungen.
BETRIEBSARTEN = ((K.SER_TELNET, "Telnet", "telnet"),
                 (K.SER_RFC2217, "RFC2217", "rfc2217"),
                 (K.SER_DATEI, "Datei", "datei"))
ROLLEN = ((K.SER_SERVER, "Server", "server"), (K.SER_CLIENT, "Client", "client"))

#: Farben des Zustandspunkts: grau aus, gelb lauscht/verbindet, grün verbunden, rot Fehler.
PUNKT_FARBE = {K.SER_AUS: "#8a8a8a", K.SER_VERBINDET: "#e0b020", K.SER_LAUSCHT: "#e0b020",
               K.SER_VERBUNDEN: "#35c43a", K.SER_FEHLER: "#e0352b"}
FARBE_WARNUNG = "#c08a00"      # dieselbe Hinweisfarbe wie im Laufwerkskasten
FARBE_FEHLER = "#d33"

HOST_ART_TEXT = {K.HOST_UNGUELTIG: "ungültig", K.HOST_IPV4: "IPv4",
                 K.HOST_IPV6: "IPv6", K.HOST_NAME: "Hostname"}


def _wort(tabelle, wert, spalte: int):
    for z in tabelle:
        if z[0] == wert:
            return z[spalte]
    return None


def _aus_wort(tabelle, wert):
    """Konfigurationswort (oder Zahl) → Aufzählungswert, ``None`` wenn unbekannt."""
    for z in tabelle:
        if wert == z[0] or (isinstance(wert, str) and wert.lower() in (z[2], z[1].lower())):
            return z[0]
    return None


def format_text(st) -> str:
    """Gastformat als ``9600 Bd 8N1`` (rohes Format der SIO; ``nicht programmiert``)."""
    if not st.format_gueltig:
        return "Gast nicht programmiert"
    par = {0: "N", 1: "O", 2: "E"}.get(st.paritaet, "?")
    stopp = {2: "1", 3: "1,5", 4: "2"}.get(st.stopp_halbe, "?")
    return f"Gast {st.baud_nenn} Bd {st.daten}{par}{stopp}"


def betriebsart_text(st) -> str:
    """``RFC2217-Client`` / ``Telnet-Server`` / ``Datei`` — für Tooltips."""
    if st.betriebsart == K.SER_DATEI:
        return "Datei"
    return (f"{_wort(BETRIEBSARTEN, st.betriebsart, 1)}-"
            f"{_wort(ROLLEN, st.rolle, 1)}")


class SerialBlock(QFrame):
    """Ein Schnittstellenblock — Bedienelemente einer einstellbaren Schnittstelle."""

    #: Der Anwender hat etwas geändert, das in die Konfiguration gehört.
    changed = Signal()

    def __init__(self, emulator, index: int, info, parent=None):
        super().__init__(parent)
        self.emulator = emulator
        self.index = index
        self.info = info
        self._laden = False          # True, solange WIR die Bedienelemente setzen
        self._hinweis = ""           # eigene Zeile, wenn der Kern keine Meldung hat
        self._status = None
        self._baue()
        self.laden()
        self.aktualisieren()

    # ── Aufbau ───────────────────────────────────────────────────────────────

    def _baue(self):
        self.setFrameShape(QFrame.StyledPanel)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(6, 6, 6, 6)
        lay.setSpacing(4)
        info = self.info

        # Kopf: Zustandspunkt, Name (Stecker), rechts der Zustand in Worten.
        kopf = QHBoxLayout()
        self.punkt = QLabel(" ")
        self.punkt.setFixedSize(14, 14)
        kopf.addWidget(self.punkt)
        self.titel = QLabel(f"<b>{info.name}</b> ({info.stecker})")
        kopf.addWidget(self.titel)
        kopf.addStretch()
        self.zustand_label = QLabel()
        kopf.addWidget(self.zustand_label)
        lay.addLayout(kopf)

        # Zeile 1: Betriebsart, Rolle, Host + Etikett, Port, Knopf.
        z1 = QHBoxLayout()
        self.betriebsart = QComboBox()
        for _wert, text, _w in BETRIEBSARTEN:
            self.betriebsart.addItem(text)
        z1.addWidget(self.betriebsart)
        self.rolle = QComboBox()
        for _wert, text, _w in ROLLEN:
            self.rolle.addItem(text)
        z1.addWidget(self.rolle)
        self.host_label = QLabel("Host")
        z1.addWidget(self.host_label)
        self.host = QLineEdit()
        self.host.setMinimumWidth(90)
        z1.addWidget(self.host, 1)
        self.host_art = QLabel()
        z1.addWidget(self.host_art)
        self.port_label = QLabel("Port")
        z1.addWidget(self.port_label)
        self.port = QSpinBox()
        self.port.setRange(1, 65535)
        self.port.setKeyboardTracking(False)
        z1.addWidget(self.port)
        self.knopf = QPushButton()
        z1.addWidget(self.knopf)
        lay.addLayout(z1)

        # Datei-Zeile (nur Betriebsart Datei): Name + „…".
        zd = QHBoxLayout()
        self.datei_label = QLabel("Datei")
        zd.addWidget(self.datei_label)
        self.datei = QLineEdit()
        self.datei.setReadOnly(True)
        self.datei.setPlaceholderText("[keine Datei]")
        zd.addWidget(self.datei, 1)
        self.datei_knopf = QPushButton("…")
        self.datei_knopf.setToolTip("Zieldatei wählen")
        self.datei_knopf.setMaximumWidth(32)
        zd.addWidget(self.datei_knopf)
        self._datei_zeile = zd
        lay.addLayout(zd)

        # Zeile 2: Loop, RTS/CTS-Brücke (nur V.24), XON/XOFF, Takt (nur mit Quellen).
        z2 = QHBoxLayout()
        self.loop = QCheckBox("Rx/Tx-Loop")
        self.loop.setToolTip("Prüfstecker: TxD → RxD am Stecker.  Schließt jede "
                             "Verbindung aus (und beendet eine bestehende).")
        z2.addWidget(self.loop)
        self.bruecke = QCheckBox("RTS/CTS-Brücke")
        self.bruecke.setToolTip("Am Stecker RTS → CTS (und DTR → DSR/DCD) verbunden.")
        self.bruecke.setVisible(bool(info.v24))
        z2.addWidget(self.bruecke)
        self.xonxoff = QCheckBox("XON/XOFF")
        self.xonxoff.setToolTip("Den Empfang anhalten, solange der Gast XOFF gesendet "
                                "hat.  Bei Binärübertragungen aus (0x13 im Datenstrom).")
        z2.addWidget(self.xonxoff)
        self.takt_label = QLabel("Takt")
        self.takt = QComboBox()
        for name in info.taktquellen:
            self.takt.addItem(name)
        mit_takt = bool(info.taktquellen)
        self.takt_label.setVisible(mit_takt)
        self.takt.setVisible(mit_takt)
        z2.addWidget(self.takt_label)
        z2.addWidget(self.takt)
        z2.addStretch()
        lay.addLayout(z2)

        # Zeile 3: Gastformat, Leitungen (nur V.24), Gegenseite.
        z3 = QHBoxLayout()
        self.format_label = QLabel()
        z3.addWidget(self.format_label)
        self.leitungen = QLabel()
        self.leitungen.setVisible(bool(info.v24))
        z3.addWidget(self.leitungen)
        z3.addStretch()
        self.gegenseite = QLabel()
        self.gegenseite.setVisible(False)
        z3.addWidget(self.gegenseite)
        lay.addLayout(z3)

        # Meldungszeile — Fehler, Hinweise, „Port belegt …".
        self.meldung = QLabel()
        self.meldung.setWordWrap(True)
        self.meldung.setVisible(False)
        lay.addWidget(self.meldung)

        self.betriebsart.currentIndexChanged.connect(self._betriebsart_gewaehlt)
        self.rolle.currentIndexChanged.connect(self._rolle_gewaehlt)
        self.host.textChanged.connect(self._host_geaendert)
        self.port.valueChanged.connect(lambda v: self._aendere(port=int(v)))
        self.loop.toggled.connect(lambda an: self._aendere(loop=bool(an)))
        self.bruecke.toggled.connect(lambda an: self._aendere(rtscts_bruecke=bool(an)))
        self.xonxoff.toggled.connect(lambda an: self._aendere(xonxoff=bool(an)))
        self.takt.currentIndexChanged.connect(
            lambda i: self._aendere(taktquelle=int(i)) if i >= 0 else None)
        self.datei_knopf.clicked.connect(self._datei_waehlen)
        self.knopf.clicked.connect(self._knopf)
        release_focus(self)

    # ── Kern → Bedienelemente ────────────────────────────────────────────────

    def laden(self):
        """Die Bedienelemente aus der Einstellung im Kern nachziehen.

        Ein Feld, in dem der Anwender gerade tippt, bleibt unberührt — sonst
        schriebe der 4-Hz-Takt in sein halbes Wort hinein.
        """
        k = self.emulator.serial_config(self.index)
        if k is None:
            return
        self._laden = True
        try:
            self._setze_combo(self.betriebsart,
                              [z[0] for z in BETRIEBSARTEN].index(k.betriebsart))
            self._setze_combo(self.rolle, [z[0] for z in ROLLEN].index(k.rolle))
            if not self.host.hasFocus() and self.host.text() != k.host:
                self.host.setText(k.host)
            if not self.port.hasFocus() and self.port.value() != k.port:
                self.port.setValue(k.port)
            for box, wert in ((self.loop, k.loop), (self.bruecke, k.rtscts_bruecke),
                              (self.xonxoff, k.xonxoff)):
                if box.isChecked() != wert:
                    box.setChecked(wert)
            if self.info.taktquellen and 0 <= k.taktquelle < self.takt.count():
                self._setze_combo(self.takt, k.taktquelle)
            if self.datei.text() != k.datei:
                self.datei.setText(k.datei)
        finally:
            self._laden = False
        self._konfig = k
        self._host_art_zeigen()

    @staticmethod
    def _setze_combo(combo: QComboBox, index: int):
        if combo.currentIndex() != index:
            combo.setCurrentIndex(index)

    # ── Bedienelemente → Kern ────────────────────────────────────────────────

    def _aendere(self, **felder) -> bool:
        """Eine Einstellung im Kern übernehmen; lehnt er ab, lädt der Block neu."""
        if self._laden:
            return False
        self._hinweis = ""
        ok = self.emulator.serial_configure(self.index, **felder)
        if not ok:
            self._hinweis = "Einstellung nicht übernommen."
        self.laden()
        self.aktualisieren()
        self.changed.emit()
        return ok

    def _betriebsart_gewaehlt(self, i: int):
        if self._laden or i < 0:
            return
        art = BETRIEBSARTEN[i][0]
        if art == K.SER_DATEI:
            # Der Speichern-Dialog erscheint SOFORT beim Umschalten (§6.6); ohne
            # Datei gibt es nichts zu starten, also zurück auf die vorige Art.
            vorher = self._konfig.betriebsart
            pfad = self._datei_dialog()
            if not pfad:
                self._laden = True
                self.betriebsart.setCurrentIndex([z[0] for z in BETRIEBSARTEN].index(vorher))
                self._laden = False
                return
            self._aendere(betriebsart=art, datei=pfad)
        else:
            self._aendere(betriebsart=art)

    def _rolle_gewaehlt(self, i: int):
        if not self._laden and i >= 0:
            self._aendere(rolle=ROLLEN[i][0])

    def _host_geaendert(self, text: str):
        if self._laden:
            return
        self._aendere(host=text)

    def _datei_dialog(self) -> str:
        pfad, _ = QFileDialog.getSaveFileName(
            self, f"{self.info.name}: Ausgabedatei", self._konfig.datei or "",
            "Alle Dateien (*)")
        return pfad or ""

    def _datei_waehlen(self):
        pfad = self._datei_dialog()
        if pfad:
            self._aendere(datei=pfad)

    def _knopf(self):
        """Starten/Beenden bzw. Verbinden/Trennen."""
        self._hinweis = ""
        if self._aktiv():
            self.emulator.serial_stop(self.index)
        else:
            self.emulator.serial_start(self.index)
            # Fehlschlag: die Meldung steht im Status (Zeile im Block, kein Fenster).
        self.aktualisieren()
        self.changed.emit()

    # ── Zustand ──────────────────────────────────────────────────────────────

    def _aktiv(self) -> bool:
        st = self._status
        return st is not None and st.zustand in AKTIV

    def _host_art_zeigen(self):
        art = K.classify_host(self.host.text())
        self.host_art.setText(HOST_ART_TEXT.get(art, ""))
        self._host_art = art

    def _knopf_text(self, aktiv: bool) -> str:
        art = self._konfig.betriebsart
        if art != K.SER_DATEI and self._konfig.rolle == K.SER_CLIENT:
            return "Trennen" if aktiv else "Verbinden"
        return "Beenden" if aktiv else "Starten"

    def aktualisieren(self, status=None):
        """Anzeige aus dem Status des Kerns nachziehen (≈ 4 Hz)."""
        st = status if status is not None else self.emulator.serial_status(self.index)
        if st is None:
            return
        self._status = st
        self.laden()
        k = self._konfig
        aktiv = st.zustand in AKTIV
        datei = k.betriebsart == K.SER_DATEI
        client = k.rolle == K.SER_CLIENT and not datei

        # Sichtbarkeit je Betriebsart: Datei blendet Rolle/Host/Port aus (§9).
        for w in (self.rolle, self.host_label, self.host, self.host_art,
                  self.port_label, self.port):
            w.setVisible(not datei)
        for w in (self.datei_label, self.datei, self.datei_knopf):
            w.setVisible(datei)

        # Sperren im Betrieb (§4); der Host ist im Server immer aus (Inhalt bleibt).
        frei = not aktiv
        self.betriebsart.setEnabled(frei)
        self.rolle.setEnabled(frei)
        self.host.setEnabled(frei and client)
        self.host_label.setEnabled(frei and client)
        self.host_art.setEnabled(frei and client)
        self.port.setEnabled(frei)
        self.port_label.setEnabled(frei)
        self.datei.setEnabled(frei)
        self.datei_knopf.setEnabled(frei)

        # Der Knopf: gesperrt bei Loop, ungültigem Host oder fehlender Datei —
        # aber nie, solange er etwas beenden kann.
        grund = ""
        if not aktiv:
            if k.loop:
                grund = ("Rx/Tx-Loop ist gesetzt (Prüfstecker) — zum Verbinden "
                         "erst abschalten.")
            elif client and self._host_art == K.HOST_UNGUELTIG:
                grund = "Der Host ist ungültig."
            elif datei and not k.datei:
                grund = "Keine Datei gewählt."
        self.knopf.setText(self._knopf_text(aktiv))
        self.knopf.setEnabled(aktiv or not grund)
        self.knopf.setToolTip(grund)

        # Kopf: Punkt und Zustand in Worten.
        self.punkt.setStyleSheet(
            f"border-radius: 7px; background-color: {PUNKT_FARBE.get(st.zustand, '#8a8a8a')};"
            " border: 1px solid #666;")
        self.zustand_label.setText(self._zustand_text(st, datei))

        # Gastformat, Leitungen, Gegenseite.
        self.format_label.setText(format_text(st))
        if self.info.v24:
            def p(name, an):
                return f"{name}{'●' if an else '○'}"
            text = " ".join((p("RTS", st.rts), p("CTS", st.cts), p("DTR", st.dtr),
                             p("DSR", st.dsr), p("DCD", st.dcd)))
            if k.betriebsart == K.SER_TELNET:
                text += "  (nicht übertragen)"
            self.leitungen.setText(text)
        if st.baud_gegenseite:
            warn = st.baud_abweichend
            self.gegenseite.setText(f"Gegenseite {st.baud_gegenseite} Bd" + (" ⚠" if warn else ""))
            self.gegenseite.setStyleSheet(f"color: {FARBE_WARNUNG};" if warn else "")
            self.gegenseite.setToolTip(
                "Die Gegenseite arbeitet mit einer anderen Baudrate als der Gast — "
                "die Zeichen kommen, aber im falschen Takt." if warn else
                "Baudrate der Gegenseite (RFC 2217).")
            self.gegenseite.setVisible(True)
        else:
            self.gegenseite.setVisible(False)

        # Meldungszeile: der Kern zuerst, dann unser Hinweis, dann der Loop-Hinweis.
        text, farbe = st.meldung, FARBE_WARNUNG
        if st.zustand == K.SER_FEHLER:
            farbe = FARBE_FEHLER
        if not text:
            text = self._hinweis
        if not text and k.loop and not aktiv:
            text = ("Rx/Tx-Loop gesetzt (Prüfstecker) — zum Verbinden abschalten.")
        self.meldung.setText(text)
        self.meldung.setStyleSheet(f"color: {farbe};")
        self.meldung.setVisible(bool(text))

    @staticmethod
    def _zustand_text(st, datei: bool) -> str:
        z = st.zustand
        if z == K.SER_LAUSCHT:
            return f"lauscht auf {st.port_aktiv}"
        if z == K.SER_VERBINDET:
            n = f" (Versuch {st.versuche})" if st.versuche else ""
            return f"verbindet …{n}"
        if z == K.SER_VERBUNDEN:
            return "Datei offen" if datei else f"verbunden {st.gegenstelle}".rstrip()
        if z == K.SER_FEHLER:
            return "Fehler"
        return "aus"

    # ── Konfiguration ────────────────────────────────────────────────────────

    def konfig_lesen(self) -> dict:
        """Die Einstellung samt ``aktiv`` als Abschnitt der YAML-Konfiguration."""
        k = self.emulator.serial_config(self.index)
        st = self.emulator.serial_status(self.index)
        d = {"betriebsart": _wort(BETRIEBSARTEN, k.betriebsart, 2),
             "rolle": _wort(ROLLEN, k.rolle, 2),
             "host": k.host, "port": int(k.port), "loop": bool(k.loop),
             "rtscts_bruecke": bool(k.rtscts_bruecke), "xonxoff": bool(k.xonxoff),
             "datei": k.datei}
        if self.info.taktquellen and 0 <= k.taktquelle < len(self.info.taktquellen):
            d["taktquelle"] = self.info.taktquellen[k.taktquelle]
        d["aktiv"] = bool(st is not None and st.zustand in AKTIV)
        return d

    def konfig_anwenden(self, d: dict):
        """Einstellung aus der Konfiguration übernehmen und — bei ``aktiv`` — die
        Schnittstelle wieder aufnehmen (§7.4a).  Unbrauchbare Werte werden einzeln
        übergangen, nicht als Fehler behandelt (ältere/fremde Konfigurationen)."""
        if not isinstance(d, dict):
            return
        # Eine laufende Schnittstelle erst anhalten: die gesperrten Felder ließen
        # sich im Betrieb nicht ändern.
        self.emulator.serial_stop(self.index)
        f = {}
        art = _aus_wort(BETRIEBSARTEN, d.get("betriebsart"))
        if art is not None:
            f["betriebsart"] = art
        rolle = _aus_wort(ROLLEN, d.get("rolle"))
        if rolle is not None:
            f["rolle"] = rolle
        if isinstance(d.get("host"), str):
            f["host"] = d["host"]
        try:
            port = int(d.get("port"))
            if 1 <= port <= 65535:
                f["port"] = port
        except (TypeError, ValueError):
            pass
        for schluessel in ("loop", "rtscts_bruecke", "xonxoff"):
            if isinstance(d.get(schluessel), bool):
                f[schluessel] = d[schluessel]
        if isinstance(d.get("datei"), str):
            f["datei"] = d["datei"]
        t = d.get("taktquelle")
        if self.info.taktquellen:
            if isinstance(t, str) and t in self.info.taktquellen:
                f["taktquelle"] = self.info.taktquellen.index(t)
            elif isinstance(t, int) and not isinstance(t, bool) \
                    and 0 <= t < len(self.info.taktquellen):
                f["taktquelle"] = t
        self._hinweis = ""
        if f:
            self.emulator.serial_configure(self.index, **f)
        k = self.emulator.serial_config(self.index)
        if d.get("aktiv") is True and k is not None and not k.loop:
            # Server: nur der eingestellte Port (belegt → nicht gestartet, Vorschlag
            # ins Port-Feld); Client: Dauerversuch; Datei: anhängend.
            if not self.emulator.serial_start_auto(self.index):
                st = self.emulator.serial_status(self.index)
                if st is not None and st.port_vorschlag:
                    self.emulator.serial_configure(self.index, port=int(st.port_vorschlag))
        self.laden()
        self.aktualisieren()


class SerialWidget(QWidget):
    """Der Inhalt des Docks „Schnittstellen"."""

    #: Der Anwender hat etwas geändert, das gespeichert werden soll.
    changed = Signal()
    #: Statuszeile: (Servertext, Servertipp, Verbindungstext, Verbindungstipp);
    #: ein leerer Text heißt „Feld ausblenden".
    statuszeile = Signal(str, str, str, str)

    def __init__(self, emulator, parent=None):
        super().__init__(parent)
        self.emulator = None
        self._bloecke: List[SerialBlock] = []
        self._lay = QVBoxLayout(self)
        self._lay.setContentsMargins(4, 4, 4, 4)
        self._lay.setSpacing(6)
        self._timer = QTimer(self)
        self._timer.timeout.connect(self.aktualisieren)
        self.set_emulator(emulator)
        self._timer.start(TAKT_MS)

    # ── Aufbau ───────────────────────────────────────────────────────────────

    def set_emulator(self, emulator):
        """Auf eine (neue) Maschine umstellen und alle Blöcke neu aufbauen."""
        self.emulator = emulator
        while self._lay.count():
            item = self._lay.takeAt(0)
            w = item.widget()
            if w is not None:
                w.setParent(None)
                w.deleteLater()
        self._bloecke = []
        n = emulator.serial_count() if emulator is not None else 0
        for i in range(n):
            info = emulator.serial_info(i)
            if info is None:
                continue
            block = SerialBlock(emulator, i, info)
            block.changed.connect(self.changed)
            # Ein Knopfdruck soll die Statuszeile sofort nachziehen, nicht erst im
            # nächsten 4-Hz-Takt.
            block.changed.connect(self._statuszeile_melden)
            self._lay.addWidget(block)
            self._bloecke.append(block)
        # Feste Schnittstellen (Tastatur): nur eine Zeile, nichts einstellbar.
        for name in (emulator.serial_fixed_names() if emulator is not None else []):
            zeile = QLabel(f"{name} — fest verdrahtet")
            zeile.setContentsMargins(6, 0, 0, 0)
            self._lay.addWidget(zeile)
        if n == 0:
            self._lay.addWidget(QLabel("Diese Maschine hat keine einstellbaren "
                                       "Schnittstellen."))
        self._lay.addStretch()
        release_focus(self)
        self.aktualisieren()

    def bloecke(self) -> List[SerialBlock]:
        """Die Blöcke in der Reihenfolge des Kerns (für Tests und Abfragen)."""
        return list(self._bloecke)

    def block(self, name: str) -> Optional[SerialBlock]:
        """Der Block mit dem Namen aus dem Kern (``None``, wenn es ihn nicht gibt)."""
        for b in self._bloecke:
            if b.info.name == name:
                return b
        return None

    # ── Takt und Statuszeile ─────────────────────────────────────────────────

    def aktualisieren(self):
        """Alle Blöcke nachziehen und die Statuszeile melden."""
        for b in self._bloecke:
            b.aktualisieren()
        self._statuszeile_melden()

    def _statuszeile_melden(self):
        self.statuszeile.emit(*self.statuszeilentexte())

    def statuszeilentexte(self):
        """(Servertext, Servertipp, Verbindungstext, Verbindungstipp) — Entwurf §9.

        * **Server:** die TATSÄCHLICHEN Ports der Server im Zustand lauscht oder
          verbunden, in Schnittstellenreihenfolge; ohne Server leer.
        * **Verbindungen:** alle Schnittstellen im Zustand VERBUNDEN, mit dem Namen
          aus dem Kern.  Gerade versuchende Clients, getrennte und lauschende
          erscheinen nie; **Datei zählt nicht** (der Kern meldet sie als VERBUNDEN,
          also filtert die Oberfläche über ``betriebsart``).
        """
        ports, server_tipp, namen, verb_tipp = [], [], [], []
        for b in self._bloecke:
            st = b._status
            if st is None or st.betriebsart == K.SER_DATEI:
                continue
            art = betriebsart_text(st)
            if st.rolle == K.SER_SERVER and st.zustand in (K.SER_LAUSCHT, K.SER_VERBUNDEN):
                ports.append(str(st.port_aktiv))
                if st.zustand == K.SER_VERBUNDEN:
                    server_tipp.append(f"{b.info.name}: {art}, Port {st.port_aktiv} "
                                       f"← {st.gegenstelle}")
                else:
                    server_tipp.append(f"{b.info.name}: {art}, lauscht auf Port "
                                       f"{st.port_aktiv}")
            if st.zustand == K.SER_VERBUNDEN:
                namen.append(f"{b.info.name} verbunden")
                pfeil = "←" if st.rolle == K.SER_SERVER else "→"
                verb_tipp.append(f"{b.info.name}: {art} {pfeil} {st.gegenstelle}")
        server = f"Telnet/RFC2217 Server Port: {', '.join(ports)}" if ports else ""
        return (server, "\n".join(server_tipp), ", ".join(namen), "\n".join(verb_tipp))

    # ── Konfiguration ────────────────────────────────────────────────────────

    def zustand_lesen(self) -> Dict[str, dict]:
        """{Schnittstellenname: Einstellung + ``aktiv``} — Abschnitt ``schnittstellen``."""
        return {b.info.name: b.konfig_lesen() for b in self._bloecke}

    def zustand_anwenden(self, daten: dict):
        """Einstellungen übernehmen und aktive Schnittstellen wieder aufnehmen (§7.4a).

        Schnittstellen, die in *daten* fehlen, bleiben unberührt (fehlend = nicht
        anfassen); unbekannte Namen werden übergangen.
        """
        if not isinstance(daten, dict):
            return
        for b in self._bloecke:
            if b.info.name in daten:
                b.konfig_anwenden(daten[b.info.name])
        self.aktualisieren()

    def alles_beenden(self):
        """Jede Schnittstelle beenden (Kabel ab) — beim Schließen und Maschinenwechsel."""
        if self.emulator is None:
            return
        for b in self._bloecke:
            self.emulator.serial_stop(b.index)

    def beenden(self):
        """Taktgeber anhalten und alles beenden (Fenster schließt)."""
        self._timer.stop()
        self.alles_beenden()
