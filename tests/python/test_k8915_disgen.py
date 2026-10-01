"""K8915: Bootdiskette mit DISGEN.COM — über den Tastenweg der OBERFLÄCHE (AP-E4n).

doc/design/16_k8915.md §8a AP-E4n und §4.4 („Bootdiskette mit DISGEN“).  Der
C++-Wächter `K8915Format.LeerdisketteFormatDisgenKaltstart` tippt mit
`K7672::sendeZeichen` und fährt nur „Read/Write system tracks“ auf Diskette 900.
Hier geht jede Taste als ``QKeyEvent`` an das Bildschirm-Widget des Hauptfensters
— genau der Weg einer PC-Taste im k8915emu (`ScreenWidget` → Bildschirmtastatur
K7672 → `k1520_key_press` → DCP-Scancode → BIOS) — und zwar auf der
Systemdiskette 901, mit der der Anwender es versucht hat:

1. Auf 901 ist B: per DISGEN **16 × 256** eingestellt.  Eine mit 5 × 1024
   formatierte Diskette in B: nimmt „Write system tracks“ deshalb NICHT an —
   DISGEN meldet ``write error or device not ready``, wie am Gerät (Befund
   AP-E4n: das ist der Anwenderbefund, kein Emulatorfehler).
2. Weg dahin: „Change device properties“ für B:, ``sector length`` mit ESC auf
   1024 weiterschalten (Sektoren je Spur folgen selbst: 5), dann schreiben —
   die neue Diskette startet bis ``A>``.

Bedienung von DISGEN (am Lauf ermittelt): Auswahlfelder schalten mit dem
Anfangsbuchstaben oder ESC weiter (klein genügt, DISGEN wandelt um), RETURN
übernimmt ein Feld bzw. einen ganzen Block, ↑/↓/←/→ wandern zwischen den
Feldern eines Blocks.
"""

from conftest import requires_core

pytestmark = requires_core

K8915_901 = "k8915scpx_boot1.hfe"

# Vergleich über den Text: DISGEN räumt nach jedem Befehl ab, was darunter stand.
SCHRITT = 100_000


def _fenster(qapp, disks):
    from app import profil
    from app.ui.main_window import MainWindow
    w = MainWindow(disks, profil=profil.profil("k8915"))
    w.show()
    qapp.processEvents()
    w.run_timer.stop()              # der Test fährt die Maschine selbst
    return w


class _Tipper:
    """PC-Tasten wie aus Qt: ``QKeyEvent`` an das Bildschirm-Widget, dazwischen
    läuft die Maschine (gedrückt ≈ 0,1 s, Pause ≈ 0,4 s Maschinenzeit)."""

    def __init__(self, emu, widget=None):
        self.emu, self.widget = emu, widget

    def _ereignis(self, art, key, text, mods):
        from PySide6.QtGui import QKeyEvent
        from PySide6.QtWidgets import QApplication
        QApplication.sendEvent(self.widget, QKeyEvent(art, key, mods, text))

    def taste(self, key, text="", mods=None):
        from PySide6.QtCore import Qt
        from PySide6.QtGui import QKeyEvent
        mods = Qt.NoModifier if mods is None else mods
        self._ereignis(QKeyEvent.KeyPress, key, text, mods)
        self.emu.run(250_000)
        self._ereignis(QKeyEvent.KeyRelease, key, text, mods)
        self.emu.run(1_000_000)

    def strg_c(self):
        """Strg+C — in DISGEN „eine Ebene zurück“ (am Befehlsfeld: Programmende)."""
        from PySide6.QtCore import Qt
        self.taste(Qt.Key_C, "\x03", Qt.ControlModifier)

    def tippe(self, text):
        from PySide6.QtCore import Qt
        for ch in text:
            if ch == "\r":
                self.taste(Qt.Key_Return, "\r")
            elif ch.isdigit():
                self.taste(getattr(Qt, f"Key_{ch}"), ch)
            else:
                self.taste(getattr(Qt, "Key_" + ch.upper()), ch)

    def return_(self):
        from PySide6.QtCore import Qt
        self.taste(Qt.Key_Return, "\r")

    def esc(self):
        from PySide6.QtCore import Qt
        self.taste(Qt.Key_Escape, "\x1b")

    def runter(self):
        from PySide6.QtCore import Qt
        self.taste(Qt.Key_Down)

    # ── Bild ────────────────────────────────────────────────────────────────
    def bild(self):
        return self.emu.screen_text()

    def zeilen(self):
        return [z.rstrip(" \x00") for z in self.bild().split("\n") if z.strip(" \x00")]

    def bis(self, text, frist=60_000_000):
        t = 0
        while t < frist:
            if text in self.bild():
                return True
            t += self.emu.run(SCHRITT)
        return text in self.bild()

    def bis_weg(self, text, frist=60_000_000):
        t = 0
        while t < frist:
            if text not in self.bild():
                return True
            t += self.emu.run(SCHRITT)
        return text not in self.bild()

    def bis_prompt(self, frist=200_000_000):
        t = 0
        while t < frist:
            z = self.zeilen()
            if z and z[-1] == "A>":
                return True
            t += self.emu.run(SCHRITT)
        return False

    def cursor_feld(self):
        """Die Bildzeile mit dem Cursor (Bit 7 der Zelle — K7024-Cursorbit)."""
        from app.core_binding.k1520 import _lib
        for r in range(24):
            for c in range(80):
                if _lib.k1520_screen_char(self.emu._handle, c, r) & 0x80:
                    return self.bild().split("\n")[r]
        return ""


def _kaltstart(t: _Tipper):
    """Netz-Ein, Selbsttest, RETURN auf die Coldstart-Meldung, bis zum Prompt
    hinter dem Autostart `rade`."""
    from PySide6.QtCore import Qt
    assert t.bis("* Coldstart *  Disk on A: ready", 80_000_000), t.bild()
    t.taste(Qt.Key_Return, "\r")
    assert t.bis("SCPX 8915", 150_000_000), t.bild()
    assert t.bis("A>rade", 50_000_000), t.bild()
    t.emu.run(20_000_000)
    assert t.bis_prompt(), t.bild()


def _starte_disgen(t: _Tipper):
    t.tippe("disgen\r")
    assert t.bis("command: Read system tracks"), t.bild()
    t.emu.run(2_000_000)


def _befehl(t: _Tipper, buchstabe: str, text: str):
    """Befehl mit dem Anfangsbuchstaben wählen (klein genügt)."""
    t.tippe(buchstabe)
    assert t.bis("command: " + text), t.bild()


def _lies_system_von_a(t: _Tipper):
    _befehl(t, "r", "Read system tracks")
    t.return_()
    assert t.bis("read system from device: A"), t.bild()
    t.return_()
    assert t.bis_weg("read system from device"), t.bild()
    assert "error" not in t.bild(), t.bild()
    t.emu.run(2_000_000)


def _schreibe_system_nach_b(t: _Tipper) -> bool:
    """Write system tracks ⏎, B ⏎.  True = geschrieben, False = Fehlermeldung
    (die dann schon quittiert ist, zurück im Befehlsfeld)."""
    _befehl(t, "w", "Write system tracks")
    t.return_()
    assert t.bis("write system to device:"), t.bild()
    t.tippe("b")
    assert t.bis("write system to device: B"), t.bild()
    t.return_()
    fehler = "write error or device not ready"
    zyklen = 0
    while zyklen < 150_000_000:
        bild = t.bild()
        if fehler in bild or "write system to device: B" not in bild:
            break
        zyklen += t.emu.run(SCHRITT)
    else:
        raise AssertionError("DISGEN schreibt nicht zu Ende\n" + t.bild())
    if fehler not in t.bild():
        assert "error" not in t.bild(), t.bild()
        t.emu.run(2_000_000)
        return True
    # RETURN quittiert nur und steht wieder im Laufwerksfeld — ein weiteres
    # RETURN versucht es erneut, für den, der den Ausweg nicht kennt, eine
    # Endlosschleife.  Heraus führt Strg+C, zurück ins Befehlsfeld.
    t.return_()
    assert t.bis_weg("write error"), t.bild()
    assert "write system to device: B" in t.cursor_feld(), t.bild()
    t.strg_c()
    assert t.bis_weg("write system to device"), t.bild()
    assert "command: Write system tracks" in t.cursor_feld(), t.bild()
    t.emu.run(2_000_000)
    return False


def _b_auf_1024(t: _Tipper):
    """Change device properties: 2 Laufwerke ⏎, B ⏎, im ersten Block zweimal ↓
    zu „sector length", ESC schaltet 256 → 512 → 1024, ⏎ ⏎."""
    _befehl(t, "c", "Change device properties")
    t.return_()
    assert t.bis("number of drives: 2"), t.bild()
    t.return_()
    assert t.bis("define properties of:"), t.bild()
    t.tippe("b")
    assert t.bis("define properties of: B"), t.bild()
    t.return_()
    assert t.bis("sector length:     256"), "901: B: = 16 × 256\n" + t.bild()
    t.runter()
    t.runter()
    assert "sector length" in t.cursor_feld(), t.bild()
    t.esc()
    assert t.bis("sector length:     512"), t.bild()
    t.esc()
    assert t.bis("sector length:     1024"), t.bild()
    t.return_()                          # erster Block übernommen
    assert t.bis("sectors per track: 5"), "Sektoren je Spur folgen selbst\n" + t.bild()
    t.return_()                          # zweiter Block ⇒ zurück ins Befehlsfeld
    assert t.bis_weg("sectors per track"), t.bild()
    t.emu.run(2_000_000)


def _beende_disgen(t: _Tipper):
    """Exit ⏎ ⇒ Warmstart, Prompt (nicht Strg+C: s. §4.4, klemmende Strg-Taste)."""
    _befehl(t, "e", "Exit")
    t.return_()
    assert t.bis_prompt(60_000_000), t.bild()


def test_disgen_builds_a_boot_disk_on_901_through_the_ui_keys(qapp, temp_disk, tmp_path):
    a = temp_disk(K8915_901)
    b = str(tmp_path / "k8915_neu.hfe")
    w = _fenster(qapp, [a])
    try:
        emu = w.emulator
        t = _Tipper(emu, w.screen_widget)
        # B: vorformatiert 5 × 1024 (= FORMAT.COM Verfahren 24/31, `cpa800`) — das
        # FORMAT-Menü selbst bewacht K8915Format.* (test-format).
        assert emu.create_disk(1, b, "cpa800"), emu.last_error()
        _kaltstart(t)

        # ── Der Anwenderbefund: auf 901 ist B: 16 × 256 ──────────────────────
        _starte_disgen(t)
        _lies_system_von_a(t)
        assert not _schreibe_system_nach_b(t), "5 × 1024 in B: bei 16 × 256"
        # Die Falle dahinter: B: JETZT auf 1024 zu stellen hilft nicht — der
        # Hostpuffer des BIOS steht nach dem Fehlschlag noch „aktiv + geändert“
        # für B: (F1D8H/F1D9H), das nächste Schreiben scheitert wieder.  Erst
        # der Warmstart (WBOOT D945H: F1D8H = 0) räumt ihn.
        _b_auf_1024(t)
        assert not _schreibe_system_nach_b(t), "Hostpuffer des Fehlschlags"
        _beende_disgen(t)

        # ── Der richtige Weg: lesen, B: auf 1024 stellen, DANN schreiben ──────
        _starte_disgen(t)
        _lies_system_von_a(t)
        _b_auf_1024(t)
        assert _schreibe_system_nach_b(t), t.bild()
        _beende_disgen(t)
        assert emu.flush_disks()
    finally:
        w.close()
        qapp.processEvents()

    # Kaltstart von der neuen Diskette (allein in A:) bis zum Prompt: die
    # Fassung „V24 (XON/XOFF)" der 901 meldet sich, der Autostart `rade` findet
    # auf der leeren Diskette kein RADE.COM.
    from PySide6.QtCore import Qt
    from app.core_binding.k1520 import K1520Emulator
    emu = K1520Emulator(machine="k8915")
    emu.power_on()
    assert emu.mount_disk(0, b, "cpa800", False), emu.last_error()
    t = _Tipper(emu)
    assert t.bis("* Coldstart *  Disk on A: ready", 80_000_000), t.bild()
    emu.key_press(Qt.Key_Return)
    emu.run(250_000)
    emu.key_release(Qt.Key_Return)
    assert t.bis("Anpassung:  V24  (XON/XOFF)", 150_000_000), t.bild()
    assert t.bis("RADE?", 60_000_000), t.bild()
    assert t.bis_prompt(), t.bild()
    assert "ERR" not in t.bild(), t.bild()
