"""Programmprofile: A5120 Emulator und K8915 Emulator — ein Programm, zwei Gesichter.

Die Oberfläche (`app/ui/main_window.py`) ist für beide Rechner dieselbe; was sie
unterscheidet, steht **hier** und nirgends sonst (doc/design/18_k8915emu_oberflaeche.md
§2): Maschinentyp des Kerns, Fenstertitel, Name der Konfigurationsdatei und ihrer
Auslieferungsvorgabe, Laufwerksbestückung, Nenntakt, Bildschirmtastatur, Lampen der
Frontplatte und die zusätzlichen Aktionen.  Wer eine Eigenheit einer Maschine
braucht, fragt das Profil — kein ``if machine == …`` quer durch die Oberfläche.

Gewählt wird das Profil beim Start (``app/main.py --machine k8915``, fest
übergeben vom Starter ``run_k8915emu.sh`` bzw. ``bin/k8915emu``); ohne Angabe ist
es der A5120 — so verhalten sich ältere Starter und Tests unverändert.

Das Modul importiert kein Qt: `app/paths.py` und `app/config_io.py` brauchen es
schon, bevor PySide6 geladen ist (``--paths``, ``--help``).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional, Tuple

from app import modell as _modell


@dataclass(frozen=True)
class Programmprofil:
    """Alles, worin sich die beiden Emulatoren unterscheiden."""

    #: Maschinenname der Bindung (``K1520Emulator(machine=…)``).
    maschine: str
    #: Programmname — Starter, ``.desktop``, Meldungen auf der Konsole.
    programm: str
    #: Fenstertitel, Startmenü-Eintrag, ``.desktop``-Name, Kopf der Startskripte.
    titel: str
    #: Rechner, wie er im Text genannt wird („Der A5120 läuft mit …").
    rechner: str
    #: Einzeiler für „Über“ und ``--help``.
    beschreibung: str
    #: Konfiguration des Anwenders in :func:`app.paths.config_dir`.
    konfig_datei: str
    #: Auslieferungsvorgabe (``data/`` bzw. ``share/k1520emu/``).
    vorgabe_datei: str
    #: Nenntakt in Hertz und so, wie ihn der Anwender liest.
    nenntakt_hz: int
    nenntakt_text: str
    #: Bildschirmtastatur: ``"k7637"`` oder ``"k7672"``.
    tastatur: str
    #: Frontplatte mit Lampen in der Statuszeile (K8915: Run … Power).
    frontplatte: bool = False
    #: Kasten „EPROMmer“ mit virtuellem Sockel (PRG 710, doc/design/20_prg710.md AP-P7c).
    eprommer: bool = False
    #: Modellwahl unter *Einstellungen ▸ Allgemein*: A5120 / A5120.16 (Erweiterungs-
    #: modul, `app/modell.py`), K8915 V3 / V2, PRG 710 / PRG 710-1, PC 1715 / 1715W.
    modellwahl: bool = False
    #: Die wählbaren Modelle: ``(Schlüssel, Kern-Maschine, Kern-``em``, Anzeigename,
    #: Tastatur)``.  Leer = ein Modell, die Maschine des Profils.  Der Schlüssel steht
    #: in der Konfiguration (``general.model``); ein unbekannter wird zum ersten.
    modelle: Tuple[Tuple[str, str, Optional[str], str, str], ...] = ()
    #: Hinweistext am Auswahlfeld des Modells.
    modell_tipp: str = ""
    #: Abweichender Nenntakt einzelner Modelle: ``(Schlüssel, Hertz, Text)``.  Alle
    #: anderen Modelle laufen mit :attr:`nenntakt_hz` (PC 1715W: 3,9936 MHz).
    modell_takte: Tuple[Tuple[str, int, str], ...] = ()
    #: Modelle, die das Profil kennt, der Kern aber noch nicht fährt:
    #: ``(Schlüssel, Anzeigename, Begründung)``.  Sie stehen ausgegraut im
    #: Auswahlfeld (mit der Begründung als Hinweis), sind nicht wählbar und gelten
    #: in einer Konfiguration als unbekannt.  Wer das Modell im Kern freischaltet,
    #: verschiebt die Zeile nach :attr:`modelle`.
    gesperrte_modelle: Tuple[Tuple[str, str, str], ...] = ()
    #: Wählbare Hardwarevarianten unter *Einstellungen ▸ Allgemein* (Kaltstart nötig, wie die
    #: Modellwahl): ``(Schlüssel, Beschriftung, Hinweis, ((Wert, Anzeige), …), Modelle)``.
    #: Der erste Wert ist die Vorgabe (fehlender Schlüssel in der Konfiguration = Vorgabe).
    #: ``Modelle`` = die Modellschlüssel, an denen die Wahl wirkt; sonst ist das Feld
    #: ausgegraut (PC 1715W: der Zeichensatz kommt von Diskette).  ``()`` = alle.
    hardware: Tuple[Tuple[str, str, str, Tuple[Tuple[str, str], ...], Tuple[str, ...]], ...] = ()
    #: Terminal-Maschine (P8000): statt der Bildröhre ein Textbildschirm je Kern-Terminal
    #: (`app/ui/p8000_terminal.py`), Funktionstastenleiste statt Bildschirmtastatur.
    terminal: bool = False
    #: Kasten „Winchester" mit Plattenabbild (P8000, `app/ui/platten_widget.py`).
    platte: bool = False
    #: Kästchen „Lochstreifen" unter *Allgemein* (K6022): am P8000 gibt es die Karte nicht.
    ptape_wahl: bool = True
    #: Lampen der Frontplatte: ``"k8915"`` (Latch 61H, aktiv low) oder ``"p8000"``
    #: (Run/Unit16/Platte, aktiv high) — nur mit :attr:`frontplatte`.
    frontplatte_art: str = "k8915"
    #: Kernparameter je Modell: ``(Schlüssel, ((Name, Wert), …))`` — Einträge des
    #: Konfigurationstextes (P8000: ``karte16``, ``wdc``).  Leer = keine.
    modell_kern: Tuple[Tuple[str, Tuple[Tuple[str, str], ...]], ...] = ()
    #: Auswahl „RAM-Disk" (RAF 128/512/2M, `app/raf.py`) samt Stand-by-Kästchen
    #: unter *Einstellungen ▸ Allgemein* (doc/design/22_raf512.md §7).  Alle drei
    #: Programme bieten sie an; ohne sie läuft die Maschine stets ohne RAF.
    raf_wahl: bool = True
    #: Satzteil für „Über …“ (HTML): „des Bürocomputers <b>robotron A5120</b>“.
    ueber_rechner: str = ""
    #: Weitere Emulatoren neben :attr:`andere` (Menü *Werkzeuge*) — Maschinenname
    #: des Profils.
    weitere: Tuple[str, ...] = ()
    #: Aktionen, die nur dieses Programm hat (Namen aus `app/ui/actions.py`).
    eigene_aktionen: Tuple[str, ...] = field(default_factory=tuple)
    #: Die jeweils ANDERE Maschine (für *Werkzeuge ▸ … starten*).
    andere: str = ""
    #: Frühere Namen serieller Schnittstellen → heutiger Name (Kern).  Die
    #: Konfiguration ist über den NAMEN verschlüsselt; ohne diese Abbildung wären
    #: Einstellungen unter einem alten Namen herrenlos (K8915: AP-S12).
    alte_schnittstellen: Tuple[Tuple[str, str], ...] = field(default_factory=tuple)

    # ── Modellwahl: Schlüssel → Kern-Maschine / EM / Tastatur ───────────────
    # Ein Profil ohne `modelle` hat genau ein Modell: seine Maschine.

    def standard_modell(self) -> str:
        """Schlüssel des Modells ab Werk (das erste; ohne Modellwahl ``a5120``,
        der Schlüssel „kein Erweiterungsmodul“ aus `app/modell.py`)."""
        return self.modelle[0][0] if self.modelle else _modell.DEFAULT_MODEL

    def modell_normalisieren(self, modell) -> str:
        """Ein bekannter Modellschlüssel — Unbekanntes/Fehlendes wird die Vorgabe."""
        modell = str(modell).strip().lower() if modell else ""
        return modell if any(m[0] == modell for m in self.modelle) \
            else self.standard_modell()

    def _modell_zeile(self, modell):
        schluessel = self.modell_normalisieren(modell)
        for m in self.modelle:
            if m[0] == schluessel:
                return m
        return None

    def modell_maschine(self, modell) -> str:
        """Maschinenname für ``K1520Emulator(machine=…)``."""
        z = self._modell_zeile(modell)
        return z[1] if z else self.maschine

    def modell_em(self, modell) -> Optional[str]:
        """``em=``-Parameter des Kerns (``None`` = ohne Erweiterungsmodul)."""
        z = self._modell_zeile(modell)
        return z[2] if z else None

    def modell_nenntakt(self, modell) -> Tuple[int, str]:
        """``(Hertz, Text)`` des Nenntakts des Modells."""
        schluessel = self.modell_normalisieren(modell)
        for k, hz, text in self.modell_takte:
            if k == schluessel:
                return hz, text
        return self.nenntakt_hz, self.nenntakt_text

    def hardware_standard(self) -> dict:
        """Vorgabe jeder Hardwarevariante (``{}`` ohne :attr:`hardware`)."""
        return {k: werte[0][0] for k, _b, _t, werte, _m in self.hardware}

    def hardware_normalisieren(self, daten) -> dict:
        """Hardwarewahl aus einem Konfigurationsabschnitt: fehlende oder unbekannte
        Werte werden zur Vorgabe (ältere Konfigurationen)."""
        daten = daten if isinstance(daten, dict) else {}
        aus = self.hardware_standard()
        for k, _b, _t, werte, _m in self.hardware:
            v = str(daten.get(k, "")).strip().lower()
            for w, _a in werte:        # Groß-/Kleinschreibung egal ("1M@0"), Wert wie im Profil
                if v == w.lower():
                    aus[k] = w
                    break
        return aus

    def hardware_wirkt(self, schluessel: str, modell) -> bool:
        """Wirkt die Wahl *schluessel* am Modell?  (Sonst im Feld ausgegraut.)"""
        for k, _b, _t, _w, modelle in self.hardware:
            if k == schluessel:
                return not modelle or self.modell_normalisieren(modell) in modelle
        return False

    def tastatur_bauart(self, modell, hardware=None) -> str:
        """Bauart der Bildschirmtastatur: :meth:`modell_tastatur`, beim PC 1715 mit dem
        QWERTZ-ROM ``"pc1715-tast618"``."""
        art = self.modell_tastatur(modell)
        if art == "pc1715" and (hardware or {}).get("tastatur") == "tast618":
            return "pc1715-tast618"
        return art

    def kern_parameter(self, modell, hardware=None) -> dict:
        """Zusatzargumente für ``K1520Emulator(…)`` aus Modell und Hardwarewahl.

        Ohne :attr:`modell_kern` (alle Maschinen außer dem P8000) sind es die
        Hardwarevarianten selbst (PC 1715: ``zeichensatz``, ``tastatur``).  Der P8000
        übersetzt beides in EIN Wörterbuch ``p8000={…}`` mit den Schlüsseln von
        ``k1520_create_p8000`` (Entwurf 25 §10.9): das Modell setzt ``karte16``/``wdc``,
        die Hardwarewahl ROM-Fassungen, Platinenindex und DRAM.
        """
        hardware = self.hardware_normalisieren(hardware or {})
        if not self.modell_kern:
            return dict(hardware)
        schluessel = self.modell_normalisieren(modell)
        kern = {}
        for k, paare in self.modell_kern:
            if k == schluessel:
                kern.update(dict(paare))
        mit16 = kern.get("karte16") == "1"
        mit_wdc = kern.get("wdc", "aus") != "aus"
        # Platinenindex: ein Schalter für beide Karten (nur 1/1 oder 3/4 sind zulässig).
        index = hardware.get("index", "34")
        kern["index8"] = index[0]
        if mit16:
            kern["index16"] = index[1]
            kern["mon16"] = hardware.get("mon16", "3.1")
            kern["dram"] = hardware.get("dram", "1M@0")
        kern["mon8"] = hardware.get("mon8", "3.1")
        if mit_wdc:
            kern["wdc"] = hardware.get("wdc", "4.2")
        return {"p8000": kern}

    def modell_hat_wdc(self, modell) -> bool:
        """Hat das Modell einen Winchesterkontroller (nur Profile mit :attr:`platte`)?"""
        schluessel = self.modell_normalisieren(modell)
        for k, paare in self.modell_kern:
            if k == schluessel:
                return dict(paare).get("wdc", "aus") != "aus"
        return False

    def modell_tastatur(self, modell) -> str:
        """Bildschirmtastatur des Modells (``"k7637"``/``"k7672"``/``"k7609"``)."""
        z = self._modell_zeile(modell)
        return z[4] if z else self.tastatur

    def schnittstellen_umbenennen(self, daten: dict) -> dict:
        """Abschnitt ``schnittstellen`` mit alten Namen auf die heutigen abbilden.

        Ein Eintrag unter dem heutigen Namen geht vor (er ist der jüngere); alles
        andere bleibt, wie es ist.
        """
        if not isinstance(daten, dict) or not self.alte_schnittstellen:
            return daten
        neu = dict(daten)
        for alt, heute in self.alte_schnittstellen:
            if alt in neu:
                wert = neu.pop(alt)
                neu.setdefault(heute, wert)
        return neu

    # Laufwerke stehen in `app/drive_types.py` (je Maschine) — dort ist der
    # Laufwerkskatalog; das Profil reicht nur durch.  Spät importiert: die
    # Bindung (und damit die Kernbibliothek) soll hier nicht mitkommen.
    def standard_laufwerke(self) -> list:
        """Laufwerksbestückung ab Werk, je K5122-Steckplatz ein Kernprofil."""
        from app import drive_types as dt
        return dt.default_drive_types(self.maschine)

    def waehlbare_laufwerke(self) -> list:
        """Wählbare Laufwerkstypen im Einstellungskasten (Einträge aus DRIVE_TYPES)."""
        from app import drive_types as dt
        return dt.drive_types_for(self.maschine)


A5120 = Programmprofil(
    maschine="a5120",
    programm="a5120emu",
    titel="A5120 Emulator",
    rechner="A5120",
    beschreibung="Emulator des Bürocomputers robotron A5120 (K1520-Bus)",
    konfig_datei="a5120emu.yaml",
    vorgabe_datei="default_config_a5120.yaml",
    nenntakt_hz=2_450_000,
    nenntakt_text="2,45 MHz",
    tastatur="k7637",
    modellwahl=True,
    modelle=tuple((k, "a5120", em, label, "k7637") for k, em, label in _modell.MODELS),
    modell_tipp=("A5120.16 fügt dem A5120 die Steuerkarte und das Erweiterungsmodul "
                 "EM256 mit dem U8001 hinzu.  Ein Wechsel erzeugt die Maschine neu "
                 "(wie ein Kaltstart)."),
    ueber_rechner="des Bürocomputers <b>robotron A5120</b>",
    andere="k8915",
    weitere=("prg710", "pc1715", "p8000"),
)

K8915 = Programmprofil(
    maschine="k8915",
    programm="k8915emu",
    titel="K8915 Emulator",
    rechner="K8915",
    beschreibung="Emulator des Arbeitsplatzcomputers robotron K8915 (K1520-Bus)",
    konfig_datei="k8915emu.yaml",
    vorgabe_datei="default_config_k8915.yaml",
    # 2,4576 MHz (K8915Machine::CPU_HZ; Quarz 9,8304 MHz / 4).
    nenntakt_hz=2_457_600,
    nenntakt_text="2,4576 MHz",
    tastatur="k7672",
    frontplatte=True,
    eigene_aktionen=("nmi",),
    modellwahl=True,
    # doc/design/24_k8915_varianten.md R5: die Schlüssel sind technisch und bleiben,
    # nur die Anzeigetexte tragen die Gerätenamen V3/V2 (F1, 2026-10-05: „Gen 2“ = V2).  Fehlt ``general.model`` (Vorgabe-
    # datei, ältere Konfigurationen), ist es der V3.  Ein EM gibt es hier nicht
    # (``em`` = None in jeder Zeile) — ``a5120.16`` ist am K8915 unbekannt → V3.
    modelle=(("k8915", "k8915", None, "K8915 V3 (ZRE 045-8762, 128 KB)", "k7672"),
             ("k8915-g2", "k8915-g2", None, "K8915 V2 (ZRE K2521, 64 KB)", "k7672")),
    # V1 (Tastatur K7634) fährt der Kern nicht: kein Urlader-Baustein für 0400H
    # (F11, AP-V9 zurückgestellt).  Die Bildschirmtastatur K7634 liegt bereit
    # (`app/ui/keyboard_k7634.py`), ist aber bewusst nicht eingehängt.
    gesperrte_modelle=(("k8915-g1", "K8915 V1 (Tastatur K7634)",
                        "Nicht wählbar: kein Urlader-Baustein für 0400H (F11)."),),
    modell_tipp=("K8915 V3 (ZRE 045-8762, 128 KB) oder V2 (ZRE K2521 mit RAM-Karte "
                 "K3528, 64 KB).  Ein Wechsel erzeugt die Maschine neu (wie ein Kaltstart)."),
    ueber_rechner="des Arbeitsplatzcomputers <b>robotron K8915</b>",
    andere="a5120",
    weitere=("prg710", "pc1715", "p8000"),
    # Bis AP-S12 hießen SIO1-B und SIO2-A nach dem Entwurf „IFS 1"/„IFS 2"; seitdem
    # nach der Beschriftung am Gerät.  „V.24" (SIO1-A) blieb.
    alte_schnittstellen=(("IFS 1", "Drucker/IFSS1"), ("IFS 2", "DFÜ/IFSS2")),
)

PRG710 = Programmprofil(
    maschine="prg710",
    programm="prg710emu",
    titel="PRG710 Emulator",
    rechner="PRG 710",
    beschreibung="Emulator der Programmiergeräte robotron PRG 710 und PRG 710-1 (K1520-Bus)",
    konfig_datei="prg710emu.yaml",
    vorgabe_datei="default_config_prg710.yaml",
    # 2,4576 MHz (Prg710Machine::CPU_HZ, wie der K8915).
    nenntakt_hz=2_457_600,
    nenntakt_text="2,4576 MHz",
    tastatur="k7609",
    eprommer=True,
    modellwahl=True,
    # Beide Geräte laufen auf derselben ZRE/ABS/AFS; das 710 hat die Tastatur
    # K7609 an einem 8279 (ATP), das 710-1 die K7672 an der K8025 (§3.9/§3.10).
    modelle=(("prg710", "prg710", None, "PRG 710 (Tastatur K7609)", "k7609"),
             ("prg710-1", "prg710-1", None, "PRG 710-1 (Tastatur K7672)", "k7672")),
    modell_tipp=("PRG 710 oder PRG 710-1: andere ZRE-Fassung, andere Tastatur und "
                 "andere Schnittstellen.  Ein Wechsel erzeugt die Maschine neu "
                 "(wie ein Kaltstart)."),
    ueber_rechner="der Programmiergeräte <b>robotron PRG 710 und PRG 710-1</b>",
    andere="a5120",
    weitere=("k8915", "pc1715", "p8000"),
)

PC1715 = Programmprofil(
    maschine="pc1715",
    programm="pc1715emu",
    titel="PC1715 Emulator",
    rechner="PC 1715",
    beschreibung="Emulator der Bürocomputer robotron PC 1715 und PC 1715W",
    konfig_datei="pc1715emu.yaml",
    vorgabe_datei="default_config_pc1715.yaml",
    # 2,458 MHz (Pc1715Zre::CPU_HZ); der 1715W läuft mit 3,9936 MHz (`modell_takte`).
    nenntakt_hz=2_458_000,
    nenntakt_text="2,458 MHz",
    tastatur="pc1715",
    modellwahl=True,
    # Der Bildschirm (K7222 80 × 24 / K7221 64 × 16) ist eine Hardwarevariante der
    # ZRE-Bestückung, im Kern ein Konstruktorparameter (`bild`) → ein Wechsel ist ein
    # Kaltstart wie der Modellwechsel; er steht deshalb als Modell in der Auswahl.
    modelle=(("pc1715", "pc1715", None, "PC 1715 (Bildschirm K7222, 80 × 24)", "pc1715"),
             ("pc1715-k7221", "pc1715-k7221", None,
              "PC 1715 (Bildschirm K7221, 64 × 16)", "pc1715"),
             # Der 1715W hat nur den 8275-Bildschirm mit ladbarem Zeichensatz (kein
             # K7221) — als eigenes Modell ist die Kombination gar nicht wählbar.
             ("pc1715w", "pc1715w", None, "PC 1715W (SCP 3.0, 256 KB, 80 × 24)",
              "pc1715")),
    modell_takte=(("pc1715w", 3_993_600, "3,9936 MHz"),),
    # ROM-Varianten, die technisch etwas ändern (AP-6): die ZG-EPROMs der ZRE (nur PC 1715;
    # der 1715W lädt seinen Satz von Diskette) und das Tastatur-ROM (beide).
    hardware=(
        ("zeichensatz", "Zeichensatz:",
         "Bestückung der Zeichengenerator-EPROMs A25.2/A25.1 der ZRE.  Wirkt nur am PC 1715 — "
         "der PC 1715W bekommt seinen Satz von Diskette.  Ein Wechsel startet die Maschine kalt.",
         (("deutsch", "Deutsch (S619 + S602)"),
          ("polnisch", "Polnisch (S641 + S619)"),
          ("kyrillisch", "Kyrillisch (S643 + S605)")),
         ("pc1715", "pc1715-k7221")),
        ("tastatur", "Tastatur:",
         "ROM der Tastatur-CPU: S600 (QWERTY) oder TAST_618 (QWERTZ, Y und Z vertauscht, "
         "andere Zeichensetzung).  Ein Wechsel startet die Maschine kalt.",
         (("s600", "S600 (QWERTY)"), ("tast618", "TAST_618 (QWERTZ)")),
         ()),
    ),
    modell_tipp=("PC 1715 mit dem Bildschirm K7222 (80 × 24) oder K7221 (64 × 16), oder "
                 "PC 1715W (4 MHz, 256 KB, U8272).  "
                 "Ein Wechsel erzeugt die Maschine neu (wie ein Kaltstart)."),
    ueber_rechner="der Bürocomputer <b>robotron PC 1715</b>",
    andere="a5120",
    weitere=("k8915", "prg710", "p8000"),
)

P8000 = Programmprofil(
    maschine="p8000",
    programm="p8000emu",
    titel="P8000 Emulator",
    rechner="P8000",
    beschreibung="Emulator des 16-Bit-Arbeitsplatzcomputers robotron P8000 (U880 + U8001, UDOS und WEGA)",
    konfig_datei="p8000emu.yaml",
    vorgabe_datei="default_config_p8000.yaml",
    # 4 MHz (P8000Machine::Config::takt8_hz); 16-Bit-Karte und WDC laufen ebenfalls mit 4 MHz.
    nenntakt_hz=4_000_000,
    nenntakt_text="4 MHz",
    tastatur="p8000",
    frontplatte=True,
    frontplatte_art="p8000",
    eigene_aktionen=("nmi", "stand_speichern", "stand_laden"),
    terminal=True,
    platte=True,
    ptape_wahl=False,
    modellwahl=True,
    # Das Vollgerät (16-Bit-Teil + Winchester) steht vorn und ist damit die Vorgabe.  Alle drei
    # sind dieselbe Kernmaschine; sie unterscheiden sich in `karte16`/`wdc` (`modell_kern`).
    modelle=(("p8000", "p8000", None, "P8000 mit 16-Bit-Teil und Winchester (Vollgerät)", "p8000"),
             ("p8000-16", "p8000", None, "P8000 mit 16-Bit-Teil, ohne Winchester", "p8000"),
             ("p8000-8", "p8000", None, "P8000 (nur 8-Bit-Teil, UDOS)", "p8000")),
    modell_kern=(("p8000", (("karte16", "1"), ("wdc", "4.2"))),
                 ("p8000-16", (("karte16", "1"),)),
                 ("p8000-8", (("karte16", "0"),))),
    modell_tipp=("Vollgerät: 8-Bit-Karte, 16-Bit-Karte mit U8001 und Winchesterkontroller "
                 "(WEGA); ohne Winchester: nur UDOS und Monitor; nur 8-Bit-Teil: wie die "
                 "erste Baustufe des Geräts (die Koppelsoftware meldet „Hardware Error in "
                 "Connection“).  Ein Wechsel erzeugt die Maschine neu (wie ein Kaltstart)."),
    # ROM-Fassungen und Platinenindex (Entwurf 25 §10.4); jeder Wechsel ist ein Kaltstart.
    # Wo eine Wahl am Modell nichts bewirkt (16-Bit-Karte, WDC), steht das Feld ausgegraut.
    hardware=(
        ("index", "Platinenindex:",
         "Ausgabestand der Karten: 8-Bit-Karte Index 3 mit 16-Bit-Karte Index 4 (Vorgabe) oder "
         "beide Index 1.  Wirkt auf Rücksetzweg, Kopplung und Laufwerksstecker.",
         (("34", "8-Bit 3 / 16-Bit 4 (Vorgabe)"), ("11", "8-Bit 1 / 16-Bit 1")),
         ()),
        ("mon8", "MON8 (U880-Monitor):",
         "ROM-Fassung des Monitors der 8-Bit-Karte.",
         (("3.1", "MON8 3.1"), ("3.0", "MON8 3.0")),
         ()),
        ("mon16", "MON16 (U8000-Monitor):",
         "ROM-Fassung des Monitors der 16-Bit-Karte.",
         (("3.1", "MON16 3.1"), ("3.0", "MON16 3.0"), ("3.3", "MON16 3.3")),
         ("p8000", "p8000-16")),
        ("dram", "Hauptspeicher (16-Bit):",
         "DRAM-Karten der 16-Bit-Seite.  Der Monitor errechnet MAXSEG aus dem Speichertest.",
         (("1M@0", "1 MB (eine Karte 1M)"), ("256K@0", "256 KB (eine Karte 256K)"),
          ("1M@0+1M@1", "2 MB (zwei Karten 1M)")),
         ("p8000", "p8000-16")),
        ("wdc", "WDC-Firmware:",
         "Firmware des Winchesterkontrollers (Z80 mit eigener ROM).",
         (("4.2", "WDC 4.2"), ("4.0.05", "WDC 4.0.05"), ("3.4.05", "WDC 3.4.05")),
         ("p8000",)),
    ),
    ueber_rechner="des 16-Bit-Arbeitsplatzcomputers <b>robotron P8000</b>",
    andere="a5120",
    weitere=("k8915", "prg710", "pc1715"),
)

#: Alle Profile nach Maschinenname.
PROFILE = {p.maschine: p for p in (A5120, K8915, PRG710, PC1715, P8000)}

#: Das Profil ohne Angabe — ältere Starter, Tests, ``app/main.py`` ohne Schalter.
VORGABE = A5120


def profil(name: str = "") -> Programmprofil:
    """Profil zu einem Maschinennamen (``""`` = Vorgabe A5120).

    Raises:
        ValueError: bei einem unbekannten Namen — mit den bekannten im Text.
    """
    if not name:
        return VORGABE
    try:
        return PROFILE[name.lower()]
    except KeyError:
        raise ValueError(f"unbekannte Maschine {name!r} "
                         f"(bekannt: {', '.join(PROFILE)})") from None
