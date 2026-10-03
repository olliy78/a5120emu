#!/usr/bin/env python3
"""
Baut doc/EPROMS/PRG710/prg710_zre.prn und prg710-1_zre.prn: die kommentierten Listings
der beiden Boot-ROMs "NKM-LOADER" (ZRE K2521, je 1 x U555, 1 KB), analog
tools/gen_k8915_zre_prn.py.

Quellen: doc/EPROMS/PRG710/prg710_k2521_nkm_loader.bin / prg710-1_k2521_nkm_loader.bin,
rohes Disassemblat von tools/z80_disasm2.py, Handkommentare aus doc/design/20_prg710.md
Par. 4a/4a.1/4b und doc/prg710/resident.md (die Resident-Software benutzt dieselbe
Sprungleiste und denselben Floppytreiber).

STATISCHE Analyse (kein Emulator des PRG 710 vorhanden, also keine Laufzeitbestaetigung).
Belegbar ist nur, was im Code steht; was nach Datenblatt oder Vermutung gedeutet ist, traegt
[?].  Die Ausgabe ist reines ASCII (die Listing-Leser der Werkzeuge erwarten das).

Aufruf:
    python3 tools/gen_prg710_zre_prn.py            # beide .prn schreiben
    python3 tools/gen_prg710_zre_prn.py --check    # Waechter: nichts schreiben, pruefen
                                                   #  (bytegleich zum eingecheckten .prn,
                                                   #   jedes ROM-Byte genau einmal dargestellt)
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOL = os.path.join(HERE, "z80_disasm2.py")
EPROMS = os.path.join(ROOT, "doc/EPROMS/PRG710")


# --------------------------------------------------------------------------------------
# Die beiden Fassungen.  Die Quelltexte der Kommentare sind fuer beide GEMEINSAM
# geschrieben; die Lage der Abschnitte unterscheidet sich nur durch Verschiebungen:
#
#   Abschnitt          PRG 710             PRG 710-1         Verschiebung
#   Kaltstart/Speicher 0000-006C           0000-006C         0
#   Ablauf (variant.)  006D-00AB           006D-009F         -
#   Status/Bildschirm  00AC-0156           00A0-014A         -12  (Text-/Sprungziele je +3)
#   Tastatur-Init      0157-015F           014B-0162         -
#   Tastenabfrage      0160-0167           0163-016A         -
#   Floppy-Init/Lesen  0168-03A3           016B-03A6         +3
# --------------------------------------------------------------------------------------
class Fassung:
    def __init__(self, **kw):
        self.__dict__.update(kw)


V710 = Fassung(
    name="PRG 710", rom="prg710_k2521_nkm_loader.bin", prn="prg710_zre.prn",
    md5="fe6f2b434e8952b2d71b747d4a9413b2", s_char=0, s_tail=0,
    kopf=0x03A4, lw0=0x03AF, lw1=0x03B7, diskerr=0x03BF, nosys=0x03CA, param=0x03D4,
    isr=0x0397, sm1=0x02D3, sm2=0x0300,
    # Codebereiche (Anfang, Ende exklusiv); der Rest wird als Daten dargestellt
    code=[(0x0000, 0x0157), (0x0157, 0x0160), (0x0160, 0x03A4),
          (0x03E8, 0x03F7), (0x03F9, 0x03FB), (0x03FD, 0x0400)],
    daten=[("text", 0x03A4, "Kopfzeile"), ("text", 0x03AF, "LW0 DEF"),
           ("text", 0x03B7, "LW1 DEF"), ("text", 0x03BF, "DISKERROR "),
           ("text", 0x03CA, "NO SYSTEM"), ("param", 0x03D4, 13),
           ("fuell", 0x03E1, 7), ("fuell", 0x03F7, 2),
           ("zustand", 0x03FB, 1), ("zustand", 0x03FC, 1)],
)
V710_1 = Fassung(
    name="PRG 710-1", rom="prg710-1_k2521_nkm_loader.bin", prn="prg710-1_zre.prn",
    md5="6f551b3db369134564db02f4e55e0144", s_char=-12, s_tail=3,
    kopf=0x03A7, lw0=0x03B2, lw1=0x03BA, diskerr=0x03C2, nosys=0x03CD, param=0x03D7,
    isr=0x039A, sm1=0x02D6, sm2=0x0303,
    code=[(0x0000, 0x0159), (0x0163, 0x03A7), (0x03E8, 0x03F7),
          (0x03F9, 0x03FB), (0x03FD, 0x0400)],
    daten=[("sio", 0x0159, 10),
           ("text", 0x03A7, "Kopfzeile"), ("text", 0x03B2, "LW0 DEF"),
           ("text", 0x03BA, "LW1 DEF"), ("text", 0x03C2, "DISKERROR "),
           ("text", 0x03CD, "NO SYSTEM"), ("param", 0x03D7, 13),
           ("fuell", 0x03E4, 4), ("fuell", 0x03F7, 2),
           ("zustand", 0x03FB, 1), ("zustand", 0x03FC, 1)],
)
FASSUNGEN = [V710, V710_1]


# --------------------------------------------------------------------------------------
# Namen.  Schluessel = Adresse im PRG 710; fuer Abschnitte mit Verschiebung wird sie
# umgerechnet.  Nicht genannte Sprungziele behalten den Namen Lxxxx des Disassemblers.
# --------------------------------------------------------------------------------------
NAMEN_LOW = {            # 0000-006C, in beiden Fassungen gleich
    0x0020: "NEUSTART_SPEICHER", 0x002F: "SEITEN_SCHLEIFE", 0x0055: "RAM_TEIL",
    0x006D: "HAUPT",
}
NAMEN_710 = {
    0x0097: "LESE_PROBE", 0x007D: "TASTE_WARTEN", 0x00A0: "LADEN",
    0x0157: "TASTATUR_INIT", 0x0160: "TASTE_HOLEN",
}
NAMEN_710_1 = {
    0x0070: "TASTE_WARTEN", 0x008E: "LADEN",
    0x014B: "TASTATUR_INIT", 0x0163: "TASTE_HOLEN",
}
NAMEN_CHAR = {           # Schluessel = Adresse im PRG 710, Verschiebung s_char
    0x00BF: "TEXT_ZEILE", 0x00C4: "STATUS_AUSWERTEN", 0x00D4: "DISKERROR",
    0x00E0: "ZEILENENDE", 0x00E7: "ZEICHEN_AUS", 0x00F5: "SCHIRM_LOESCHEN",
    0x0104: "ZEICHEN_STEUER", 0x0111: "CR_PRUEFEN", 0x0119: "ZEILENANFANG_SUCHEN",
    0x0121: "ZEICHEN_ABLEGEN", 0x012D: "CURSOR_SICHERN", 0x0139: "TEXT_AUS",
    0x013A: "TEXT_SCHLEIFE", 0x0142: "HEX_AUS", 0x014B: "HEX_NIBBLE",
    0x0153: "HEX_ZIFFER",
}
NAMEN_TAIL = {           # Schluessel = Adresse im PRG 710, Verschiebung s_tail
    0x0168: "FLOPPY_INIT", 0x0195: "SEKTOREN_LESEN", 0x01C3: "LW_WAHL_SCHLEIFE",
    0x01CE: "SPUR_ANFAHREN", 0x01DF: "SCHRITT_SCHLEIFE", 0x01FB: "SCHRITT_ZAEHLEN",
    0x01FC: "SCHRITT_IMPULS", 0x0211: "SCHRITT_PAUSE", 0x0218: "SPUR0_SUCHEN",
    0x021D: "EINSCHWINGEN", 0x0220: "EINSCHWING_PAUSE", 0x023D: "BEREIT_WARTEN",
    0x0240: "BEREIT_ZEITABLAUF", 0x0247: "SEKTORZAHL", 0x0275: "SEKTORZAHL_FERTIG",
    0x0279: "LESELAUF", 0x02A2: "LAENGE_FALSCH", 0x02B1: "SPUR_FALSCH",
    0x02BD: "FEHLER_SPRUNG", 0x02C7: "SEKTOR_FALSCH", 0x02CF: "ID_SUCHEN",
    0x02DD: "ID_STROBE", 0x02E7: "ID_SYNC", 0x0304: "DATEN_STROBE",
    0x030E: "DATEN_SYNC", 0x0315: "BLOCK_SCHLEIFE", 0x0317: "BYTE_SCHLEIFE",
    0x0336: "CRC_SEKTOR", 0x033C: "CRC_BLOCK", 0x033F: "CRC_BYTE",
    0x038B: "CRC_FALSCH", 0x0394: "STATUS_AUS", 0x0397: "MOTOR_AUS_ISR",
    0x03F9: "HAKEN",
}
SPRUNGLEISTE_NAMEN = {}  # wird je Fassung abgeleitet (Ziele der JP-Eintraege)


def namen(v):
    n = dict(NAMEN_LOW)
    n.update(NAMEN_710 if v is V710 else NAMEN_710_1)
    for a, t in NAMEN_CHAR.items():
        n[a + v.s_char] = t
    for a, t in NAMEN_TAIL.items():
        # 03F9 (Haken) liegt in beiden Fassungen an derselben Stelle
        n[a + (0 if a >= 0x03F9 else v.s_tail)] = t
    return n


# --------------------------------------------------------------------------------------
# Kommentare.  Platzhalter: {kopf} {lw0} {lw1} {diskerr} {nosys} {param} {isr} {sm1} {sm2}
# (Adressen der Fassung, 4 Hexziffern) -- keine Adresse im Text wird von Hand umgerechnet.
# --------------------------------------------------------------------------------------
C_LOW = {
    0x0000: "[RESET-EINSPRUNG] SP = 0D00H (ZRE-RAM 0C00-0FFF, Stapel waechst nach unten)",
    0x0003: "I = 0FH + IM 2: Vektortabelle bei 0F00H; belegt wird nur CTC-Kanal 3, Vektor E6H -> (0FE6H) "
            "(FLOPPY_INIT).  Vor deren Aufruf kommt kein Interrupt.",
    0x0009: "EI",
    0x000A: "Zeichen 1FH = Bildschirm loeschen",
    0x000C: "ZEICHEN_AUS: der erste E8H[F]-Zugriff (F0E8H) blendet das VRAM F800-FFFF ein; die Speicher"
            "verwaltung steht noch im Grundzustand (EBH = 0, Arbeitsmodell Par. 4b)",
    0x000F: "Kopfzeile: Text 'NKM-LOADER' bei {kopf}H (Laenge vorangestellt) + CR/LF",
    0x0015: "Haken fuer weiteren Programmspeicher (Plan 4a Schritt 2): steht bei 4000H ein JR (18H), "
            "Sprung dorthin, noch VOR dem Floppystart.  Der BOOT-Modul der UDOS-Disketten beginnt mit "
            "18H (resident.md Par. 1), er wird so auch bei erhaltenem RAM ohne Diskette gefunden [?]",
    0x001D: "{kbdinit}",
    0x0020: "[NEUSTART_SPEICHER] Hierher kehrt der Lader nach 'NO SYSTEM' zurueck (JR am Ende von HAUPT).  "
            "Speicherverwaltung neu programmieren (Plan Par. 4b): zuerst den Cursorzeiger (0FDEH) retten, "
            "denn 0C00-0FFF wechselt mit Seite 0 die Herkunft (ZRE-RAM <-> OPS-RAM)",
    0x0023: "OUT (EBH),00H: Abbildung AUS -- die Register E8H/EAH wirken nicht, Seite 0 = ZRE "
            "(ROM 0000-03FF, RAM 0C00-0FFF) [Arbeitsmodell]",
    0x0027: "Cursorzeiger ins (jetzt sichtbare) ZRE-RAM zurueck",
    0x002A: "B = 0: die Seitennummer geht bei OUT (C),r als B = n*10H auf A12-A15 (E/A-Adresse A8-A15 = B)",
    0x002C: "D = Seite n (0..15), E = Schleifenzaehler 16",
    0x002F: "[SEITEN_SCHLEIFE] je Seite n = 0..15: EAH[n] <- n (physische Seite = Identitaet), E8H[n] <- 10H "
            "(Herkunft RAM)",
    0x0031: "EAH[n] <- n",
    0x0037: "E8H[n] <- 10H.  Bei n = 0 geschieht das, waehrend der Code noch aus Seite 0 (ROM) laeuft: "
            "das geht nur, weil EBH = 0 die Register unwirksam laesst [Arbeitsmodell]",
    0x0039: "A = 10H + B, B = A: B waechst je Seite um 10H",
    0x003F: "danach (B = 0, C = E8H von oben): E8H[0] <- 0FH = Seite 0 wieder Systemkarte ZRE (ROM sichtbar)",
    0x0045: "OUT (EBH),0FH: Abbildung EIN (A12-A15 = 0 -> EBH[0]); der Wert 0FH hat sonst keine bekannte "
            "Wirkung [?]",
    0x0049: "ROM-Bild retten: DE = 1000H (Ziel), HL = 0000H (Quelle: LD H,E / LD L,E)",
    0x004E: "B = 04H, C = EBH (Rest des OUT) -> BC = 04EBH = 1259 Byte, NICHT 0400H: die Kopie reicht "
            "235 Byte ueber das ROM hinaus (lesen FFH/RAM, harmlos) [Befund]",
    0x0050: "LDIR: ROM (0000H..) -> RAM 1000H..",
    0x0052: "JP 1055H: weiter in der KOPIE (OPS-RAM).  Gleich wird Seite 0 auf RAM geschaltet, das ROM "
            "verschwindet",
    0x0055: "[RAM_TEIL] laeuft NUR aus der Kopie bei 1055H.  BC = 00E8H: B = 0 = Seite 0, C = E8H",
    0x0058: "Cursorzeiger aus dem ZRE-RAM lesen (Seite 0 ist hier noch 0FH) ...",
    0x005B: "A = 10H",
    0x005D: "E8H[0] <- 10H: Seite 0 = RAM.  Ab jetzt zeigt 0000-0FFF auf OPS-RAM, das ROM ist unsichtbar",
    0x005F: "... und ins neue RAM (0FDEH) zurueckschreiben",
    0x0062: "DE = 0000H (B = 0), HL = 1000H (L = B = 0, H = A = 10H)",
    0x0066: "B = 04H, C = E8H: BC = 04E8H = 1256 Byte.  [NMI-Einsprung 0066H faellt in diese Folge "
            "(LD B,04H / LDIR / JP HAUPT): kein eigener NMI-Handler [?]]",
    0x0068: "LDIR: die Kopie 1000H.. -> 0000H..: das Laderbild liegt jetzt im RAM an seiner Sollage; "
            "Zustandszellen (03FBH, 03FCH, Parameterblock) und die selbstveraendernden Operanden "
            "({sm1}H, {sm2}H) sind ab hier beschreibbar",
    0x006A: "JP HAUPT: Rueckkehr nach 006DH -- jetzt an der Sollage im RAM",
}

C_MAIN_710 = {
    0x006D: "[HAUPT] Ab hier laeuft der Lader aus dem RAM.  FLOPPY_INIT: K5122-PIOs, CTC, IM-2-Vektor",
    0x0070: "IY = Parameterblock {param}H (Floppyauftrag, Aufbau s. dort)",
    0x0074: "Laufwerk 0",
    0x0075: "LESE_PROBE: VORABLESEN von Laufwerk 0 -- nur zum Anlaufen des Motors und Anfahren von "
            "Spur 0; Status und Daten werden NICHT ausgewertet (der 710-1 liest erst nach der Starttaste)",
    0x0078: "Laufwerk 1, ebenso",
    0x007D: "[TASTE_WARTEN] Z = keine Taste, sonst A = Scancode des 8279",
    0x0082: "37H = Scancode der Taste ET (Plan 4a Schritt 7); jede andere Taste wird verworfen",
    0x0086: "Laufwerk 0: LADEN -- kehrt nur bei Fehler/ohne 'SY' zurueck (sonst JP 0400H)",
    0x008A: "Laufwerk 1, ebenso",
    0x008F: "beide ohne Erfolg: Text 'NO SYSTEM' ({nosys}H)",
    0x0095: "JR NEUSTART_SPEICHER: Speicherverwaltung und Kopie werden WIEDERHOLT, danach wieder "
            "Vorablesen und Warten auf ET",
    0x0097: "[LESE_PROBE] A = Laufwerk 0/1 -> RRCA x3 (1 -> 20H) = Bit 5-6 von (IY+11); Seite 0, Sektor 1",
    0x009A: "(IY+11) <- Laufwerk/Seite/Sektor",
    0x009D: "JP SEKTOREN_LESEN (Rueckkehr direkt zum Aufrufer)",
    0x00A0: "[LADEN] A = Laufwerk 0/1.  (03FCH) <- A: das Bootlaufwerk, fuer den Bootsektor "
            "(Sprungleiste, Zelle 03FCH)",
    0x00A3: "Laenge (IY+4/+5) = 0200H = 512 Byte = 4 Sektoren zu 128 Byte (Spur 0, Sektor 1 -> 0400H)",
    0x00A9: "LESE_PROBE: Laufwerk (A, unveraendert) nach (IY+11) und SEKTOREN_LESEN (hier als Aufruf: "
            "LESE_PROBE springt mit JP hinein, dessen Rueckkehr geht an diesen Aufrufer)",
}

C_MAIN_710_1 = {
    0x006D: "[HAUPT] Ab hier laeuft der Lader aus dem RAM.  FLOPPY_INIT: K5122-PIOs, CTC, IM-2-Vektor.  "
            "Im Unterschied zum 710 KEIN Vorablesen: die Laufwerke werden erst nach der Starttaste "
            "angesprochen",
    0x0070: "[TASTE_WARTEN] TASTE_HOLEN: Z = keine Taste, sonst A = Zeichen der K7672",
    0x0075: "0DH = ENTER startet den Ladevorgang (der 710 wartet auf ET = 37H, Plan 4a.1); jede andere "
            "Taste wird verworfen",
    0x0079: "IY = Parameterblock {param}H (Floppyauftrag, Aufbau s. dort)",
    0x007D: "Laufwerk 0",
    0x007E: "LADEN -- kehrt nur bei Fehler/ohne 'SY' zurueck (sonst JP 0400H)",
    0x0081: "Laufwerk 1, ebenso",
    0x0086: "beide ohne Erfolg: Text 'NO SYSTEM' ({nosys}H)",
    0x008C: "JR NEUSTART_SPEICHER: Speicherverwaltung und Kopie werden WIEDERHOLT, danach wieder "
            "auf ENTER warten",
    0x008E: "[LADEN] A = Laufwerk 0/1.  (03FCH) <- A: das Bootlaufwerk, fuer den Bootsektor "
            "(Sprungleiste, Zelle 03FCH)",
    0x0091: "Laenge (IY+4/+5) = 0200H = 512 Byte = 4 Sektoren zu 128 Byte (Spur 0, Sektor 1 -> 0400H)",
    0x0097: "RRCA x3 (1 -> 20H): Laufwerk in Bit 5-6 von (IY+11); Seite 0, Sektor 1",
    0x009D: "SEKTOREN_LESEN (Aufruf; der 710 geht hier ueber LESE_PROBE)",
}

C_CHAR = {                       # Schluessel = Adresse im PRG 710
    0x00AC: "Status nach dem Lesen: 80H gut, C0H Laufwerk fehlt, sonst Fehlertext.  (Alle Codes: Kopf.)",
    0x00AF: "C0H = Schrittzaehler abgelaufen (Spur 0 nie erreicht, Laufwerk fehlt) -> 'LW0 DEF'/'LW1 DEF'",
    0x00B3: "Text 'LW0 DEF' ({lw0}H)",
    0x00B6: "(03FCH) = Laufwerk: 0 -> 'LW0 DEF', sonst 'LW1 DEF' ({lw1}H)",
    0x00BA: "JR Z auf TEXT_ZEILE",
    0x00BF: "[TEXT_ZEILE] HL = Text: ausgeben + CR/LF (JR ZEILENENDE).  Auch Sprungziel von LADEN (JR Z) "
            "mit HL = 'LW0 DEF'.  Rueckkehr zum Aufrufer von ZEICHEN_AUS aus",
    0x00C4: "[STATUS_AUSWERTEN] Status ist nicht C0H: 80H = gut -> 'SY'-Pruefung, sonst DISKERROR",
    0x00C8: "'SY' = 53H 59H: das Wort (0402H) soll 5953H sein (Bootsektor beginnt 18 03 'SYL')",
    0x00CB: "HL = (0402H) = Bytes 2/3 des geladenen Bootsektors",
    0x00CE: "Carry ist hier 0 (CP 80H war gleich): SBC HL,DE = reiner Vergleich",
    0x00D0: "kein 'SY': zurueck zum Aufrufer (naechstes Laufwerk bzw. 'NO SYSTEM')",
    0x00D1: "JP 0400H: Bootsektor starten.  Der Rueckkehrstapel von HAUPT bleibt liegen",
    0x00D4: "[DISKERROR = Sprungleiste 03F4H] Text 'DISKERROR ' ({diskerr}H), dann Status (IY+10) "
            "als zwei Hexziffern, CR/LF.  Auch vom Bootsektor aufrufbar (Status in (IY+10))",
    0x00DA: "Status (IY+10)",
    0x00DD: "HEX_AUS",
    0x00E0: "[ZEILENENDE] CR (0DH) ausgeben ...",
    0x00E5: "... A = LF (0AH) und direkt in ZEICHEN_AUS weiter (Rueckkehr von dort)",
    0x00E7: "[ZEICHEN_AUS = Sprungleiste 03EBH] Zeichen in A auf den Bildschirm.  1FH: Bild loeschen, "
            "Cursor F800H.  0AH: Cursor eine Zeile (80 Zeichen) tiefer.  0DH: Cursor an den Zeilenanfang.  "
            "Sonst Zeichen ablegen, Cursor + 1.  BC/HL bleiben erhalten, AF/DE werden zerstoert",
    0x00E9: "E8H[Seite F] <- FFH: VRAM K7024 F800-FFFF sichtbar (BC = F0E8H: B = F0H = Seite F in A12-A15)",
    0x00F0: "DI: solange das VRAM eingeblendet ist, darf keine Unterbrechung laufen",
    0x00F1: "1FH = Bild loeschen?",
    0x00F5: "[SCHIRM_LOESCHEN] HL = FFFFH, DE = FFFEH, BC = 07FFH, (HL) = 00H, LDDR: F800-FFFF "
            "(2 KB) mit 00H fuellen; HL = F800H = Cursor home",
    0x0102: "JR CURSOR_SICHERN",
    0x0104: "[ZEICHEN_STEUER] HL = Cursor (0FDEH), BC = 0050H = 80 Zeichen je Zeile; A = 0AH (LF)?",
    0x010E: "LF: Cursor + 80 (kein Ueberlaufschutz: erst das naechste Zeichen loest ggf. das Loeschen aus)",
    0x0111: "[CR_PRUEFEN] A = 0DH (CR)?",
    0x0115: "DE = Cursor; HL = FF7FH = F800H + 77FH (Spalte 79 der Zeile 23)",
    0x0119: "[ZEILENANFANG_SUCHEN] HL -= 80, bis HL < Cursor: HL ist dann die LETZTE Zelle der Vorzeile "
            "(Spalte 79); die Schleife benutzt SBC ohne Carry-Vorbereitung (Carry ist nach CP = 0)",
    0x0121: "[ZEICHEN_ABLEGEN] (HL) <- A, HL + 1.  Bei CR landet 0DH in der letzten Zelle der Vorzeile "
            "(Zeile 0: in F7FFH = RAM ausserhalb des VRAM) und der Cursor am Zeilenanfang [?]",
    0x0123: "Cursor hinter FF7FH (unterste Zeile, ab FF80H)?  Dann kein Scrollen, sondern Bild loeschen "
            "(JR C auf SCHIRM_LOESCHEN)",
    0x012D: "[CURSOR_SICHERN] Cursor -> (0FDEH)",
    0x0130: "BC = F0E8H, OUT (C),B: E8H[Seite F] <- F0H = VRAM aus, F800-FFFF wieder RAM",
    0x0135: "EI; BC und HL zurueck; RET",
    0x0139: "[TEXT_AUS = Sprungleiste 03EEH] HL = Text: erstes Byte = Anzahl n, dann n Zeichen, jedes ueber "
            "ZEICHEN_AUS (B = n; ZEICHEN_AUS rettet BC)",
    0x0142: "[HEX_AUS = Sprungleiste 03F1H] A als zwei Hexziffern (hoeherwertige zuerst)",
    0x0147: "obere Tetrade nach unten (RRCA x4), ausgeben ...",
    0x014B: "[HEX_NIBBLE] untere Tetrade als Hexziffer: + 30H, ab 0AH zusaetzlich + 07H ('A'-'F')",
    0x0155: "JR ZEICHEN_AUS (Sprung statt Aufruf: Rueckkehr direkt)",
}

C_KBD_710 = {
    0x0157: "[TASTATUR_INIT] 8279 (Tastaturcontroller; C8H Daten, C9H Steuerung) initialisieren",
    0x0159: "Kommando 02H: Betriebsart (Bit 7-5 = 000) [?] nach Datenblatt: codierte Abtastung, N-Tasten-Rollover",
    0x015B: "Kommando C1H: Loeschen (110x xxxx) -- Anzeige-RAM/FIFO [?]",
    0x0160: "[TASTE_HOLEN = Sprungleiste 03E8H] Z = keine Taste.  Sonst A = Scancode",
    0x0162: "Statusbits 0-2 = Fuellstand des FIFO; 0: keine Taste, RET Z",
    0x0165: "IN (C8H): Scancode aus dem FIFO (IN aendert die Flags nicht: NZ)",
}
C_KBD_710_1 = {
    0x014B: "[TASTATUR_INIT] Tastatur K7672 an K8025 SIO A32 Kanal B (5EH Daten / 5FH Steuerung) und "
            "CTC K0 (58H) programmieren: zuerst 2 Byte (07H, 01H) an 58H -- CTC-Kanal 0: Zeitgeber, "
            "Zeitkonstante 01H (-> 153,6 kHz Takt)",
    0x014E: "BC = 0258H: B = 2 Byte, C = 58H (CTC K0)",
    0x0151: "OTIR: 07H, 01H nach 58H (Tabelle SIO_TABELLE bei 0159H)",
    0x0153: "BC = 085FH: B = 8 Byte, C = 5FH (SIO-Kanal B Steuerung)",
    0x0156: "OTIR: die 8 Byte ab 015BH: 00H 18H (Kanal-Reset), 04H 4CH (WR4: x16, 2 Stoppbit, keine "
            "Paritaet), 03H C1H (WR3: Empfaenger 8 Bit, ein), 05H 68H (WR5: Sender 8 Bit, ein) = "
            "9600 Bd, 8 Bit, 2 Stoppbit",
    0x0163: "[TASTE_HOLEN = Sprungleiste 03E8H] Z = keine Taste.  Sonst A = Zeichen",
    0x0165: "RR0 Bit 0 = Empfangszeichen verfuegbar",
    0x0168: "IN (5EH): Zeichen (IN aendert die Flags nicht: NZ)",
}

C_TAIL = {                       # Schluessel = Adresse im PRG 710, Verschiebung s_tail
    0x0168: "[FLOPPY_INIT] K5122-PIOs und CTC einrichten.  PIO-Steuerwoerter: xxxx1111 = Betriebsart "
            "(Bit 7-6: 00 Ausgabe, 01 Eingabe, 11 Bitbetrieb)",
    0x016A: "OUT (13H),FFH: Steuer-PIO Tor B (Status 12H) auf Bitbetrieb ...",
    0x016E: "... OUT (13H),F3H: Eingaberegister: 1 = Eingang; Ausgaenge sind nur Bit 2 (/HF) und Bit 3 (PRE)",
    0x0170: "OUT (17H),7FH: Daten-PIO Tor B (Lesedaten 16H) auf Betriebsart 1 (Eingabe mit Quittung)",
    0x0174: "OUT (11H),7FH: Steuer-PIO Tor A (10H) zunaechst Betriebsart 1 ...",
    0x0176: "... damit der Ausgangsspeicher vor dem Umschalten auf Ausgabe mit dem Ruhewert FDH belegt "
            "werden kann (keine Flanke an /STR) [?]",
    0x017C: "OUT (11H),3FH: Tor A Betriebsart 0 (Ausgabe), Ruhewert FDH -> /STR = 1, /HL = 1 "
            "(Kopf abgehoben), /ST = 1 -- keine Aktion",
    0x017E: "OUT (15H),3FH: Daten-PIO Tor A (Schreibdaten 14H) Betriebsart 0 (Ausgabe)",
    0x0180: "CTC 80H (Kanal 0): Interruptvektor E0H (Kanal 3 -> E6H)",
    0x0184: "CTC Kanal 2 (82H): 37H = Zeitgeber, Vorteiler 256, Zeitkonstante folgt ...",
    0x0188: "... TC = C0H = 192: Periode 256 x 192 Takte (resident.md Par. 7); der Ausgang speist Kanal 3 "
            "(Lese-Zeitablauf / Motornachlauf)",
    0x018C: "IM-2-Vektor: (0FE6H) <- {isr}H = MOTOR_AUS_ISR (liegt im ZRE-/OPS-RAM der Seite 0 -- darum erst "
            "nach der Umschaltung in RAM_TEIL)",
    0x0192: "Lesedatenport leerlesen",

    0x0195: "[SEKTOREN_LESEN = Sprungleiste 03FDH] Lesen mit Parameterblock an IY: (IY+2/3) Ladeadresse, "
            "(IY+4/5) Laenge in Byte, (IY+10) Status (Rueckgabe), (IY+11) Laufwerk (Bit 5-6), Kopf "
            "(Bit 7), Sektor-1 (Bit 0-4), (IY+12) Spur.  Die CPU liest selbst (IN (16H)/INI im "
            "/WAIT-Betrieb der K5122).  OUT (83H),03H: CTC-Kanal 3 stoppen (Motornachlauf ab)",
    0x0199: "Zielzeiger (0E96H) <- (IY+2/3)",
    0x01A2: "(0FCBH) <- 03H: Neukalibrierungsversuche",
    0x01A5: "(0FCCH) <- 10H: CRC-Wiederholungen (16); (0FCDH) <- 00H: Schrittzaehler (laeuft 256 Schritte)",
    0x01AB: "B = (IY+11);  (0E98H) <- (B AND 1FH) + 1 = gesuchte Sektornummer (1-basiert)",
    0x01B5: "RLCA x3, AND 03H: Laufwerk = Bit 5-6 -> (0EDDH), C = Laufwerk",
    0x01BE: "B = Laufwerk + 1; A = 77H; Schleife dreht A so oft nach links: EEH/DDH/BBH/77H ...",
    0x01C3: "[LW_WAHL_SCHLEIFE] RLCA, DJNZ",
    0x01C6: "... AND F0H: oberes Halbbyte = /SE (Laufwerkswahl, 0 = gewaehlt), unteres = /LCK = 0 (Motor "
            "an), OUT (18H) = 8212",
    0x01CA: "OUT (10H),BBH: /HL = 0 (Kopf aufgesetzt), /STR = 1 (kein Lesen), Richtung nach innen",
    0x01CE: "[SPUR_ANFAHREN] (0E9DH) <- Zielspur (IY+12)",
    0x01D4: "HL = 0FCEH + Laufwerk: Zelle 'aktuelle Spur' je Laufwerk (0FCEH..0FD1H), A = Zielspur - aktuelle Spur "
            "= Schrittzahl (mit Vorzeichen)",
    0x01D9: "CALL HAKEN (NOP/RET im ROM: leere Einhaengestelle, dort ueberschreibbar) [?]",
    0x01DC: "(0FCAH) <- Schrittzaehler",
    0x01DF: "[SCHRITT_SCHLEIFE] OUT (12H),04H: /HF = 1 (FM-/5-Zoll-Datenrate), PRE = 0 [?]",
    0x01E3: "BC = 9F10H: C = 10H (Steuer-PIO Tor A), B = 9FH = Schrittwort: /ST = 1, /HL = 0, MR = 0 "
            "(Richtung nach aussen, Spur 0)",
    0x01E6: "A = (0E9DH): Zielspur; 0 -> Spur 0 anlaufen (nach /TO)",
    0x01EC: "Schrittzaehler (0FCAH): 0 -> kein Schritt noetig",
    0x01F3: "Vorzeichen: negativ = nach aussen (B = 9FH, Zaehler hoch); positiv = nach innen "
            "(B = BFH: MR = 1)",
    0x01F7: "B = BFH (nach innen); (HL) zweimal -1, dann [SCHRITT_ZAEHLEN] +1: netto -1",
    0x01FB: "[SCHRITT_ZAEHLEN]",
    0x01FC: "[SCHRITT_IMPULS] /ST: 1 -> 0 -> 1: der Schritt geschieht an der fallenden Flanke",
    0x0206: "(0FCDH) - 1: 256 Schritte sind die Grenze; C = C0H = Status bei Ablauf ('Laufwerk fehlt')",
    0x020E: "Schrittpause: 0190H = 400 Schleifen",
    0x0216: "JR SCHRITT_SCHLEIFE",
    0x0218: "[SPUR0_SUCHEN] IN (12H): Bit 7 = /TO (Spur 00): 1 = nicht auf Spur 0 -> weiter schrittweise nach aussen",
    0x021D: "[EINSCHWINGEN] Pause DE = 14B4H (5300 Schleifen = Kopfberuhigung)",
    0x0225: "(0EDDH) = Laufwerk -> HL = 0FCEH + Laufwerk: aktuelle Spur <- (0E9DH)",
    0x0233: "CP (IY+12): Zielspur erreicht?  Nein (nach Neukalibrierung stand (0E9DH) = 0) -> SPUR_ANFAHREN erneut",
    0x0238: "C = C2H (Status 'nicht bereit'); DE = 0000H: 65536 Abfragen",
    0x023D: "[BEREIT_WARTEN] IN (12H) Bit 0 = /RDYL: 1 = nicht bereit -> weiter warten; sonst weiter.  "
            "Ablauf: Status C2H",
    0x0240: "[BEREIT_ZEITABLAUF] Z = Zeitablauf: FEHLER_SPRUNG mit dem Status in C (C2H aus der Schleife "
            "davor; auch Sprungziel von SCHRITT_IMPULS mit C0H, dessen DEC (HL) das Z liefert)",
    0x0247: "[SEKTORZAHL] Anzahl der Sektoren: (IY+4/5) / 128, aufgerundet (SLA/RL E)",
    0x0255: "Sektorlaenge: (03FBH) Low-Nibble (1/2/4/8 = Vielfache von 128 Byte) ...",
    0x025A: "... SELBSTVERAENDERUNG: nach {sm1}H (Operand des 'LD A,00H' bei {sm1m1}H) = Zahl der "
            "128-Byte-Bloecke je Sektor",
    0x025D: "A - 1; CP 3: Laengencode IBM (0 = 128, 1 = 256, 2 = 512, 3 = 1024 Byte): 1 -> 0, 2 -> 1, 4 -> 2, 8 -> 3",
    0x0267: "SELBSTVERAENDERUNG: Laengencode nach {sm2}H (Operand des 'CP 00H' beim Vergleich mit dem "
            "Laengenbyte des Sektorkopfes)",
    0x026A: "E = E / 2 (aufgerundet), Laengencode-mal: E = Zahl der Sektoren",
    0x0275: "[SEKTORZAHL_FERTIG] (0E99H) <- E",
    0x0279: "[LESELAUF] Zaehler und Zeiger fuer den Durchlauf setzen: (0E9EH) <- (0E98H) gesuchter Sektor, "
            "(0E9FH) <- (0E99H) Restzahl; (0E9AH) <- 1AH (26 Sektorkoepfe), (0E9BH) <- 08H, (0E9CH) <- 00H",
    0x028B: "DE = 0E50H (CRC-Tabelle der gelesenen Sektoren, 2 Byte je Sektor), HL = Zielzeiger (0E96H), "
            "C = 16H (Datenport); EXX: diese Register liegen danach im Schattensatz",
    0x0294: "DE = B585H: Lesesteuerworte Kopf 0: B5H = /STR = 0, MR = 1 (Marken-FF zuruecksetzen), dann 85H = "
            "MR = 0, MK = 0 (Marke A1), MK1 = 0: Marken-FF scharf (design/07 Par. 7.7).  "
            "B1H/81H = dasselbe fuer KOPF 1 (/FR = 0) -- NICHT FM/MFM: das ROM liest nur MFM [Befund]",
    0x0297: "(IY+11) Bit 7 = Kopf: 0 -> B585H; 1 -> B181H",
    0x02A2: "[LAENGE_FALSCH] Laengenbyte des Sektorkopfes weicht ab: (0E9BH) - 1; erst nach 8 Fehlversuchen "
            "naechste Sektorlaenge: (03FBH) RLCA 11H -> 22H -> 44H -> 88H -> 11H, dann SEKTORZAHL neu",
    0x02B1: "[SPUR_FALSCH] Spurbyte des Sektorkopfes falsch: C = C5H; (0E9AH) - 1 (26 Koepfe); danach "
            "(0FCBH) - 1 (Neukalibrierung); bei 0: Status C5H",
    0x02B9: "(0FCBH) - 1: noch Versuche uebrig?  (Z -> FEHLER_SPRUNG, Status C5H)",
    0x02BD: "[FEHLER_SPRUNG] JP Z auf STATUS_AUS (Status in C).  Sonst Neukalibrierung: (0E9DH) <- 0 und "
            "SCHRITT_SCHLEIFE: Spur 0 suchen, dann SPUR_ANFAHREN erneut",
    0x02C7: "[SEKTOR_FALSCH] Sektornummer des Sektorkopfes falsch: C = C4H; (0E9CH) - 1 (256 Versuche); "
            "bei 0: Status C4H",
    0x02CF: "[ID_SUCHEN] HL = 0E9DH (Zielspur); 'LD A,00H' ({sm1}H selbstveraendert) ins Schattenregister A "
            "(EX AF,AF'), A = 0, OUT (14H),A: Schreibdatenlatch loeschen [?]; IN (16H): Datenport leerlesen; "
            "BC = A110H: B = A1H (Sync-Byte), C = 10H",
    0x02DD: "[ID_STROBE] OUT (10H),D (B5H) / OUT (10H),E (85H): Lesen starten + Marken-FF scharf; IN (12H) "
            "Bit 1 = MKE (Marken-FF).  {mke_id}",
    0x02E7: "[ID_SYNC] IN (16H): A1-Bytes (Sync) ueberlesen; dann CP FEH = Adressmarke des Sektorkopfes; "
            "(IN (16H) holt schon das Spurbyte; IN aendert die Flags nicht)",
    0x02F0: "kein FEH (z. B. Datenmarke): von vorn, ID_SUCHEN.  Es gibt hier KEINEN Zeitablauf [?]",
    0x02F2: "Spurbyte = Zielspur (HL = 0E9DH)?  Nein: SPUR_FALSCH",
    0x02F5: "Kopfbyte (wird NICHT verglichen); HL + 1 = 0E9EH (gesuchter Sektor)",
    0x02F8: "Sektorbyte = (0E9EH)?  Nein: SEKTOR_FALSCH",
    0x02FD: "Laengenbyte; 'CP 00H' bei {sm2}H ist selbstveraendert (Laengencode); Abweichung: LAENGE_FALSCH.  "
            "Die ID-CRC (2 Byte) wird NICHT gelesen und nicht geprueft",
    0x0303: "INC (HL): (0E9EH) + 1 = naechster Sektor fuer den naechsten Durchlauf",
    0x0304: "[DATEN_STROBE] wie ID_STROBE: Strobe, Marken-FF abwarten ({mke_dat})",
    0x030E: "[DATEN_SYNC] A1-Bytes ueberlesen; das erste andere Byte (Datenmarke FBH) wird gelesen und NICHT "
            "geprueft (geloeschte Datenmarke F8H geht durch)",
    0x0313: "Schattensatz zurueck: A = Zahl der 128-Byte-Bloecke, HL' = Zielzeiger, DE' = CRC-Tabelle, "
            "C' = 16H",
    0x0315: "[BLOCK_SCHLEIFE] B = 80H = 128 Byte je Block",
    0x0317: "[BYTE_SCHLEIFE] INI: (HL) <- IN (C=16H), HL + 1, B - 1",
    0x031B: "naechster Block (Sektor = 1/2/4/8 Bloecke)",
    0x031E: "2 CRC-Byte hinter dem Datenfeld in die CRC-Tabelle (DE')",
    0x0325: "HL = 0E9FH (aus 0E9EH + 1), (0E9FH) - 1: noch Sektoren?  Ja: ID_SUCHEN",
    0x0329: "alle Sektoren gelesen: (0E9FH) <- (0E99H); HL = Zielzeiger (0E96H) = Anfang; IX = 0E50H",
    0x0336: "[CRC_SEKTOR] CRC-CCITT je Sektor, Startwert E295H (= nach A1 A1 A1 FBH), Zeichen fuer Zeichen "
            "ueber das Datenfeld: D/E = CRC, Folge XOR/RRCA ohne Tabelle",
    0x033C: "[CRC_BLOCK] Anzahl der Bloecke je Sektor aus {sm1}H (selbstveraendert); je Block 128 Byte",
    0x033F: "[CRC_BYTE]",
    0x0365: "CRC mit der gelesenen Tabelle (IX) vergleichen: High-Byte ...",
    0x036D: "... Low-Byte",
    0x0375: "Restzahl der Sektoren (0E9FH) - 1; Z -> alle Sektoren geprueft",
    0x037E: "Erfolg: Status (IY+10) <- 80H",
    0x0382: "CTC Kanal 3 (83H): C7H = Zaehler mit Interrupt, Zeitkonstante folgt ...",
    0x0386: "... TC = 64H = 100: nach 100 Perioden von Kanal 2 kommt der Interrupt -> MOTOR_AUS_ISR "
            "(Motornachlauf)",
    0x038A: "RET",
    0x038B: "[CRC_FALSCH] (0FCCH) - 1: noch Wiederholungen?  Ja: von LESELAUF an (ganze Lesung wiederholt)",
    0x0392: "C = C6H: Status 'CRC-Fehler'",
    0x0394: "[STATUS_AUS] (IY+10) <- C (Fehlerstatus).  Der Fehlerausgang FAELLT in den Interrupt-Code durch: "
            "Kanal 3 stoppen, Laufwerke abwaehlen (Motor sofort aus), dann EI/RETI statt RET -- die "
            "Rueckkehradresse des Aufrufers liegt noch oben auf dem Stapel",
    0x0397: "[MOTOR_AUS_ISR = IM-2-Einsprung CTC-Kanal 3, Vektor E6H] AF retten, OUT (83H),03H: Kanal 3 "
            "stoppen; OUT (18H),FFH: alle Laufwerke abwaehlen, Motoren aus; AF zurueck, EI, RETI",
    0x03F9: "[HAKEN] NOP / RET: leere Einhaengestelle, vom Bootsektor ueberschreibbar (RAM) [?]",
}

C_SPRUNG = {
    0x03E8: "[Sprungleiste] Tastatur abfragen (TASTE_HOLEN): Z = keine Taste, sonst A",
    0x03EB: "Zeichen ausgeben (ZEICHEN_AUS)",
    0x03EE: "Text mit vorangestellter Laenge ausgeben (TEXT_AUS)",
    0x03F1: "A als zwei Hexziffern ausgeben (HEX_AUS)",
    0x03F4: "DISKERROR + Status (IY+10) (DISKERROR)",
    0x03FD: "Sektoren lesen (SEKTOREN_LESEN); Parameterblock an IY, Aufbau s. {param}H.  "
            "Der Bootsektor und die SCPX-/UDOS-Lader benutzen genau diese Einsprungstelle",
}


# --------------------------------------------------------------------------------------
# Kopf
# --------------------------------------------------------------------------------------
def kopf(v, rom, vgl):
    x = lambda a: f"{a:04X}H"
    kbd = ("8279 an C8H/C9H (Taste ET = Scancode 37H)" if v is V710
           else "K8025 SIO A32 Kanal B an 5EH/5FH (Taste ENTER = 0DH), 9600 Bd 8N2")
    return f"""\
; ============================================================================
; {v.prn}  -  {v.name}  Boot-ROM "NKM-LOADER" (ZRE K2521, 1 x U555), 1 KB, 0000H-03FFH
;
; Quelle : doc/EPROMS/PRG710/{v.rom}
;          MD5 {v.md5}, ausgelesen 2026-10-02 vom Anwender
; Erzeugt: tools/gen_prg710_zre_prn.py (rohes Disassemblat tools/z80_disasm2.py + Handkommentare)
; Plan   : doc/design/20_prg710.md Par. 4a / 4a.1 / 4b; Resident-Software: doc/prg710/resident.md
;
; STATISCHE Analyse -- es gibt noch keinen Emulator des PRG 710, also keine Laufzeitbestaetigung.
; [?] = Deutung nach Datenblatt oder Vermutung; alles andere steht im Code.  Tastatur dieser
; Fassung: {kbd}.
;
; ---- Wo was laeuft ------------------------------------------------------------------
;   0000-0052  laeuft aus dem ROM (nach dem Einschalten ist Seite 0 = ZRE-ROM)
;   0055-006A  laeuft NUR aus der RAM-Kopie bei 1055H (RAM_TEIL): schaltet Seite 0 auf RAM
;              und holt die Kopie zurueck
;   ab 006DH   laeuft ALLES aus dem RAM an der Sollage (HAUPT bis zur Sprungleiste); das ROM
;              ist nicht mehr sichtbar.  Der Lader VERAENDERT SICH SELBST ({v.sm1:04X}H und
;              {v.sm2:04X}H, s. unten) und fuehrt Zustand in 03FBH/03FCH und im Parameterblock
;   0C00-0FFF  ZRE-RAM beim Start (Stapel 0D00H, Cursor 0FDEH, IM-2-Tabelle I = 0FH); nach dem Umschalten
;              OPS-RAM (Zellen 0FCAH-0FD1H, 0E50H-0E9FH, 0EDDH: Floppytreiber)
;   0400-...   hierhin laedt der Lader den Bootsektor (512 Byte, Spur 0 Sektor 1); er startet, wenn
;              bei 0402H 'SY' steht (Bootsektor beginnt 18 03 'SYL')
;
; ---- Ablauf (Plan 4a) --------------------------------------------------------------------
;   1 Stapel, I/IM 2, Bildschirm loeschen, Kopfzeile 'NKM-LOADER'
;   2 steht bei 4000H ein JR (18H): Sprung nach 4000H
;   3 Tastatur einrichten; Speicherverwaltung E8H-EBH programmieren (Par. 4b); ROM nach 1000H
;     kopieren, dort Seite 0 auf RAM schalten, zurueckkopieren, nach HAUPT
;   4 K5122/CTC einrichten (FLOPPY_INIT); {'beide Laufwerke vorablesen, dann auf Taste ET warten' if v is V710 else 'auf Taste ENTER warten'}
;   5 je Laufwerk 0, 1: LADEN (512 Byte Spur 0 Sektor 1 nach 0400H); 'SY' -> JP 0400H; sonst
;     'LW0 DEF'/'LW1 DEF' (Status C0H) bzw. 'DISKERROR ' + Status; kein Laufwerk gut: 'NO SYSTEM'
;     und von vorn (NEUSTART_SPEICHER)
;
; ---- Schnittstelle fuer den Bootsektor (Sprungleiste 03E8H-03FFH) ------------------------
;   03E8 Tastatur   03EB Zeichen   03EE Text   03F1 Hexbyte   03F4 DISKERROR   03FD Sektoren lesen
;   03FBH Sektorlaengen-Zustand, 03FCH Bootlaufwerk (0/1), 03F9H leerer Haken (NOP/RET)
;   Parameterblock {x(v.param)} (IY): +0 ?, +1 Flags (0AH = lesen), +2/+3 Ladeadresse, +4/+5 Laenge in
;   Byte, +10 Status, +11 Laufwerk (Bit 5-6) | Kopf (Bit 7) | Sektor-1 (Bit 0-4), +12 Spur
;   Status: 80H gut | C0H Schrittzaehler (256 Schritte) abgelaufen, Laufwerk fehlt | C2H nicht
;   bereit (/RDYL 65536 Abfragen lang) | C4H Sektornummer nicht gefunden (256 Koepfe) |
;   C5H Spurbyte falsch (26 Koepfe, dreimal nach Neukalibrierung) | C6H Datenfeld-CRC falsch (16 Lesungen)
;
; ---- K5122-Bits, wie hier benutzt (design/07 Par. 4/5) -------------------------------------
;   10H Tor A (Ausgabe): Bit0 /WE, Bit1 MK (0 = Marke A1), Bit2 /FR = Seitenwahl (1 = Kopf 0),
;     Bit3 /STR, Bit4 MK1, Bit5 MR/SD = Schrittrichtung (1 = innen), Bit6 /HL, Bit7 /ST
;   12H Tor B: Bit0 /RDYL, Bit1 MKE (Marken-FF), Bit2 /HF und Bit3 PRE (Ausgaenge), Bit7 /TO (Spur 0)
;   14H Schreibdaten, 16H Lesedaten (/WAIT-Betrieb), 18H 8212: hohes Halbbyte /SE, tiefes /LCK
;   Lesesteuerworte B5H -> 85H (Kopf 0) und B1H -> 81H (Kopf 1): /STR faellt, MR 1 -> 0 macht den
;   Marken-FF scharf.  NICHT FM/MFM: der Lader liest ausschliesslich MFM (Sync A1)
;
; ---- Speicherverwaltung E8H-EBH (Arbeitsmodell Par. 4b) ----------------------------------
;   Registeradresse = A12-A15 der E/A-Adresse (OUT (C),r mit B = Seite*10H); EAH[n] = physische
;   Seite, E8H[n] = Herkunft (Seite 0: 0FH = ZRE, 10H = RAM; Seite F: FFH = VRAM sichtbar,
;   F0H = RAM), EBH = Freigabe (0 = Abbildung aus, 0FH ein)
;
; ---- Selbstveraenderung -------------------------------------------------------------------
;   {v.sm1:04X}H  Operand von 'LD A,00H' (bei {v.sm1 - 1:04X}H): Zahl der 128-Byte-Bloecke je Sektor
;   {v.sm2:04X}H  Operand von 'CP 00H': Laengencode des Sektorkopfes (0 = 128 ... 3 = 1024 Byte)
;   03FBH  RLCA bei Laengenfehler (11H -> 22H -> 44H -> 88H); das hohe Halbbyte wertet das ROM
;          nicht aus [?]
;   03FCH  Bootlaufwerk, 03D8H/03DBH (Laenge) und Parameterblock werden von LADEN beschrieben
;
; ---- Fassungsvergleich PRG 710 <-> PRG 710-1 (Plan Par. 4a.1) ----------------------------
;   Dieselbe Lader-Fassung (Texte, Aufbau, Speicherverwaltung, Bildschirmtreiber, Leseroutine);
;   {vgl} der 1024 Byte weichen ab, fast alle nur durch Verschiebung.  Inhaltlich verschieden:
;   1 Tastatur: 710 8279 an C8H/C9H (Status AND 07H); 710-1 K8025-SIO A32-B an 5EH/5FH
;     (CTC K0 58H: 07H,01H; SIO WR4 4CH, WR3 C1H, WR5 68H = 9600 Bd 8N2; Abfrage RR0 Bit 0)
;   2 Start: 710 liest beide Laufwerke VORHER (ohne Auswertung), wartet auf ET (37H);
;     710-1 wartet auf ENTER (0DH), die Laufwerke erscheinen erst danach
;   3 Marken-FF (12H Bit 1): 710 wiederholt den Lesestrobe, SOLANGE Bit 1 = 1 (JR NZ; MKE
;     low-aktiv); 710-1 strobt einmal und wartet, BIS Bit 1 = 1 (JR Z; MKE high-aktiv, wie das
;     Modell im /WAIT-Zweig).  Alle Treiber halten sich daran (Resident, SCPX-BIOS).
;   Gemeinsam: Sprungleiste mit denselben Adressen, 4000H-Haken, Parameterblock-Aufbau.
;
; ---- Befunde dieser Auswertung (Abweichungen/Zusaetze zum Plan) --------------------------
;   B1 Die Steuerworte B5H/85H und B1H/81H waehlen den KOPF (Bit 2 /FR), nicht FM/MFM
;   B2 Status C0H = Schrittzaehler abgelaufen (Laufwerk fehlt), C2H = /RDYL-Zeitablauf; das ROM
;      meldet 'LW0 DEF' nur fuer C0H
;   B3 Die Kopien nach 1000H bzw. zurueck laufen ueber 04EBH/04E8H Byte, nicht 0400H
;   B4 Weder Sektorkopf-CRC noch Datenmarke noch Kopfbyte werden geprueft; nur Spur, Sektor,
;      Laenge und die Daten-CRC; kein Zeitablauf in den Markenwartekreisen (ID_STROBE/ID_SYNC)
;   B5 Der Haken 03F9H (NOP/RET) wird vor jedem Spuranfahren gerufen
;   B6 Bei vollem Bild (Zeile 24) wird geloescht statt gescrollt
; ============================================================================

\tORG\t0000H
"""


# --------------------------------------------------------------------------------------
def lade_rom(v):
    rom = open(os.path.join(EPROMS, v.rom), "rb").read()
    assert len(rom) == 1024, f"{v.rom}: Laenge {len(rom)}"
    assert hashlib.md5(rom).hexdigest() == v.md5, f"{v.rom}: MD5 weicht ab"
    return rom


LINE_RE = re.compile(r'^([0-9A-Fa-f]{4})\s+[0-9A-Fa-f ]*\t(.*?)\s*$')
LABEL_RE = re.compile(r'^(\S+):$')


def disasm(rom_path, lo, hi):
    """Roh-Disassemblat im Bereich [lo, hi): Liste (Adresse, Mnemonic) und Tool-Labels."""
    out = subprocess.run(
        [sys.executable, TOOL, "--org", "0", "--entry", f"0x{lo:04X}",
         "--range", f"0x{lo:04X}:0x{hi:04X}", rom_path],
        capture_output=True, text=True, check=True).stdout
    ins = []
    labels = {}
    pending = None
    for line in out.splitlines():
        m = LABEL_RE.match(line)
        if m:
            pending = m.group(1)
            continue
        m = LINE_RE.match(line)
        if not m:
            continue
        a = int(m.group(1), 16)
        if not (lo <= a < hi):
            pending = None
            continue
        ins.append((a, m.group(2)))
        if pending:
            labels[a] = pending
        pending = None
    # Der Disassembler wuerde jedes Ziel zusaetzlich rekursiv verfolgen: nur die Adressen
    # der LINEAREN Zerlegung ab lo zaehlen.  Pruefen: die Befehle muessen den Bereich ohne
    # Luecke und Ueberschneidung tilen.
    ins.sort()
    return ins, labels


def befehlslaengen(ins, hi, rom, lo):
    """Laenge je Befehl aus dem Abstand zur naechsten Adresse; prueft die Tilung."""
    res = []
    for i, (a, mn) in enumerate(ins):
        nxt = ins[i + 1][0] if i + 1 < len(ins) else hi
        res.append((a, nxt - a, mn))
    assert res and res[0][0] == lo and sum(n for _, n, _ in res) == hi - lo, \
        f"Disassemblat tilt {lo:04X}-{hi:04X} nicht"
    return res


def formatiere(v, rom):
    n = namen(v)
    ziele_code = set()
    zeilen = []

    # Kommentare je Adresse zusammensetzen
    c = {}
    for a, t in C_LOW.items():
        c[a] = t
    c.update(C_MAIN_710 if v is V710 else C_MAIN_710_1)
    for a, t in C_CHAR.items():
        c[a + v.s_char] = t
    c.update(C_KBD_710 if v is V710 else C_KBD_710_1)
    for a, t in C_TAIL.items():
        c[a + (0 if a >= 0x03F9 else v.s_tail)] = t
    c.update(C_SPRUNG)
    kbdinit = ("8279 initialisieren (TASTATUR_INIT)" if v is V710 else
               "Tastatur initialisieren (TASTATUR_INIT: K8025 SIO A32 Kanal B + CTC K0)")
    if v is V710:
        mke_id, mke_dat = ("Wiederholt den Strobe, SOLANGE Bit 1 = 1 (JR NZ): MKE low-aktiv",
                           "ebenso: wiederholt, solange Bit 1 = 1")
    else:
        mke_id, mke_dat = ("Strobt einmal, wartet dann BIS Bit 1 = 1 (JR Z): MKE high-aktiv",
                           "ebenso: wartet, bis Bit 1 = 1")
    fmtd = dict(kopf=f"{v.kopf:04X}", lw0=f"{v.lw0:04X}", lw1=f"{v.lw1:04X}",
                diskerr=f"{v.diskerr:04X}", nosys=f"{v.nosys:04X}", param=f"{v.param:04X}",
                isr=f"{v.isr:04X}", sm1=f"{v.sm1:04X}", sm1m1=f"{v.sm1 - 1:04X}", sm2=f"{v.sm2:04X}",
                kbdinit=kbdinit, mke_id=mke_id, mke_dat=mke_dat)

    benutzt = set()

    def kom(a):
        benutzt.add(a)
        return c[a].format(**fmtd) if a in c else ""

    def rename(mn, label_a):
        """Tool-Labels durch eigene Namen ersetzen; Zahlenwerte, die der Disassembler
        faelschlich als Label deutet (z. B. LD BC,L0258), wieder als Hexzahl schreiben."""
        def sub(m):
            addr = int(m.group(2), 16)
            if addr in n:
                return n[addr]
            if mn.split()[0] in ("JP", "JR", "CALL", "DJNZ"):
                return m.group(0)
            return f"{addr:04X}H"
        return re.sub(r'\b(sub_|L)([0-9A-F]{4})\b', sub, mn)

    def symbolisch(mn):
        """Konstante Adressen im Befehl (Textziele, Parameterblock) benennen."""
        namen_daten = {v.kopf: "TXT_KOPF", v.lw0: "TXT_LW0", v.lw1: "TXT_LW1",
                       v.diskerr: "TXT_DISKERROR", v.nosys: "TXT_NOSYSTEM",
                       v.param: "PARAMETER"}
        m = re.match(r'^(LD (?:HL|IY),)([0-9A-F]{4})H$', mn)
        if m and int(m.group(2), 16) in namen_daten:
            return m.group(1) + namen_daten[int(m.group(2), 16)]
        return mn

    def hexb(a, ln):
        return " ".join(f"{b:02X}" for b in rom[a:a + ln])

    def zeile(a, ln, mn, k):
        return f"{a:04X}  {hexb(a, ln):<14}\t{mn}" + (f"\t\t;{k}" if k else "")

    # Bereiche (Code und Daten) nach Adresse ordnen
    bloecke = [("code", lo, hi) for lo, hi in v.code]
    for d in v.daten:
        art, a = d[0], d[1]
        if art == "text":
            ln = 1 + rom[a]
        elif art in ("param", "fuell", "sio"):
            ln = d[2]
        else:
            ln = d[2]
        bloecke.append((art, a, a + ln, d))
    bloecke = sorted(bloecke, key=lambda b: b[1])

    # Abdeckung pruefen: lueckenlos 0000-03FF
    pos = 0
    for b in bloecke:
        assert b[1] == pos, f"{v.name}: Luecke/Ueberschneidung bei {pos:04X} (Block ab {b[1]:04X})"
        pos = b[2]
    assert pos == 0x0400, f"{v.name}: Abdeckung endet bei {pos:04X}"

    rom_path = os.path.join(EPROMS, v.rom)
    abschnitte = {
        0x0000: "Kaltstart aus dem ROM, Speicherverwaltung programmieren, ROM-Bild nach 1000H (0000H-0052H)",
        0x0055: "RAM_TEIL -- laeuft NUR aus der Kopie bei 1055H: Seite 0 auf RAM, Bild zurueck (0055H-006AH)",
        0x006D: "ab hier ALLES aus dem RAM: Hauptablauf",
    }
    abschnitte[0x00AC + v.s_char] = "Statusauswertung, Fehlertexte, Bildschirmtreiber, Textausgabe, Hexausgabe"
    abschnitte[0x0157 if v is V710 else 0x014B] = "Tastatur"
    abschnitte[0x0168 + v.s_tail] = "K5122/CTC-Init, Sektoren lesen, Motor-Aus-Interrupt"

    for b in bloecke:
        art, lo, hi = b[0], b[1], b[2]
        if lo in abschnitte and art == "code":
            zeilen.append("")
            zeilen.append(f"; ---- {abschnitte[lo]} ".ljust(78, "-"))
        if art == "code":
            ins, tlabels = disasm(rom_path, lo, hi)
            for a, ln, mn in befehlslaengen(ins, hi, rom, lo):
                nm = n.get(a)
                tl = tlabels.get(a)
                if nm:
                    zeilen.append(f"{nm}:")
                elif tl and tl.endswith(f"{a:04X}"):
                    zeilen.append(f"{tl}:")
                mn2 = symbolisch(rename(mn, a))
                if 0x03E8 <= a < 0x03F7 and mn2.startswith("JP "):
                    pass
                zeilen.append(zeile(a, ln, mn2, kom(a)))
            continue
        d = b[3]
        if art == "text":
            s = rom[lo + 1:hi].decode("ascii")
            zeilen.append("")
            tn = {v.kopf: "TXT_KOPF", v.lw0: "TXT_LW0", v.lw1: "TXT_LW1",
                  v.diskerr: "TXT_DISKERROR", v.nosys: "TXT_NOSYSTEM"}[lo]
            zeilen.append(f"{tn}:")
            zeilen.append(zeile(lo, 1, f"DB\t{rom[lo]}", f"Text: Laenge {rom[lo]}, danach die Zeichen"))
            zeilen.append(zeile(lo + 1, hi - lo - 1, f"DB\t'{s}'",
                                {"TXT_KOPF": "Kopfzeile, nach dem Bildschirmloeschen ausgegeben",
                                 "TXT_LW0": "Laufwerk 0 fehlt/Spur 0 nicht erreicht (Status C0H)",
                                 "TXT_LW1": "Laufwerk 1 fehlt/Spur 0 nicht erreicht (Status C0H)",
                                 "TXT_DISKERROR": "Text VOR dem Status in Hex (Status != 80H/C0H); "
                                                  "endet mit einem Leerzeichen",
                                 "TXT_NOSYSTEM": "beide Laufwerke ohne 'SY' im Bootsektor"}[tn]))
        elif art == "param":
            zeilen.append("")
            zeilen.append("PARAMETER:")
            zeilen.append(zeile(lo, 4, "DB\t00H,0AH,00H,04H",
                                "Parameterblock (IY): +0 00H [?], +1 0AH Flags (lesen), +2/+3 0400H "
                                "Ladeadresse"))
            zeilen.append(zeile(lo + 4, 2, "DB\t00H,02H", "+4/+5 0200H Laenge in Byte (LADEN schreibt "
                                                          f"sie nochmals nach {lo + 4:04X}H)"))
            zeilen.append(zeile(lo + 6, 4, "DB\t00H,00H,00H,00H", "+6..+9 (Fertig-/Fehlerroutine des "
                                                                  "Resident-Treibers, hier ungenutzt)"))
            zeilen.append(zeile(lo + 10, 1, "DB\t00H", "+10 Status (Rueckgabe: 80H gut, C0H-C6H Fehler)"))
            zeilen.append(zeile(lo + 11, 1, "DB\t00H", "+11 Laufwerk (Bit 5-6), Kopf (Bit 7), Sektor-1 "
                                                       "(Bit 0-4); LADEN setzt das Laufwerk"))
            zeilen.append(zeile(lo + 12, 1, "DB\t00H", "+12 Spur 0"))
        elif art == "fuell":
            zeilen.append(zeile(lo, hi - lo, "DB\t" + ",".join("FFH" for _ in range(hi - lo)),
                                "unbelegt (FFH)"))
        elif art == "sio":
            zeilen.append("")
            zeilen.append("SIO_TABELLE:")
            zeilen.append(zeile(lo, 2, "DB\t07H,01H", "CTC K0 (58H): Steuerwort 07H (Zeitgeber), "
                                                       "Zeitkonstante 01H"))
            zeilen.append(zeile(lo + 2, 8, "DB\t00H,18H,04H,4CH,03H,0C1H,05H,68H",
                                "SIO Kanal B (5FH): WR0 18H Kanalreset, WR4 4CH (x16, 2 Stoppbit), "
                                "WR3 C1H (Rx 8 Bit, ein), WR5 68H (Tx 8 Bit, ein)"))
        elif art == "zustand":
            if lo == 0x03FB:
                zeilen.append(zeile(lo, 1, "DB\t11H", "ZUSTAND Sektorlaenge: Low-Nibble 1/2/4/8 = "
                                                     "128/256/512/1024 Byte; RLCA bei Laengenfehler "
                                                     "(11H, 22H, 44H, 88H)"))
            else:
                zeilen.append(zeile(lo, 1, "DB\t0FFH", "ZUSTAND Bootlaufwerk (0/1), FFH bis LADEN es setzt"))
    # Kommentar, der an keinem Befehlsanfang haengt, ginge still verloren
    verwaist = sorted(a for a in c if a not in benutzt and not any(
        b[0] != "code" and b[1] <= a < b[2] for b in bloecke))
    assert not verwaist, f"{v.name}: Kommentare ohne Befehl bei " + ", ".join(f"{a:04X}" for a in verwaist)
    zeilen.append("")
    zeilen.append("\tEND")
    return zeilen


def erzeuge(v):
    rom = lade_rom(v)
    other = lade_rom(V710_1 if v is V710 else V710)
    vgl = sum(1 for x, y in zip(rom, other) if x != y)
    zeilen = formatiere(v, rom)
    text = kopf(v, rom, vgl) + "\n".join(zeilen) + "\n"
    return text, rom


BYTE_RE = re.compile(r'^([0-9A-F]{4})  ((?:[0-9A-F]{2} ?)+) *\t')


def pruefe_abdeckung(text, rom, name):
    """Jedes ROM-Byte genau einmal, an der richtigen Stelle, mit dem richtigen Wert."""
    belegt = [None] * len(rom)
    for z in text.splitlines():
        m = BYTE_RE.match(z)
        if not m:
            continue
        a = int(m.group(1), 16)
        for i, h in enumerate(m.group(2).split()):
            assert a + i < len(rom), f"{name}: Byte jenseits des ROMs bei {a + i:04X}"
            assert belegt[a + i] is None, f"{name}: Byte {a + i:04X} doppelt dargestellt"
            assert int(h, 16) == rom[a + i], f"{name}: Byte {a + i:04X} weicht vom ROM ab"
            belegt[a + i] = True
    fehlt = [i for i, b in enumerate(belegt) if not b]
    assert not fehlt, f"{name}: nicht dargestellt: {fehlt[0]:04X}.. ({len(fehlt)} Byte)"
    # jede Routine benannt: die Namen aus NAMEN_* muessen als Label vorkommen
    for t in list(NAMEN_LOW.values()) + list(NAMEN_CHAR.values()) + list(NAMEN_TAIL.values()):
        assert re.search(rf'^{t}:$', text, re.M), f"{name}: Label {t} fehlt"
    assert "TASTE_HOLEN:" in text and "TASTATUR_INIT:" in text and "LADEN:" in text


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="nichts schreiben: bytegleich zum eingecheckten .prn und vollstaendige "
                         "Abdeckung jedes ROM-Bytes pruefen (Exit 1 bei Abweichung)")
    args = ap.parse_args()
    ok = True
    for v in FASSUNGEN:
        text, rom = erzeuge(v)
        pruefe_abdeckung(text, rom, v.prn)
        ziel = os.path.join(EPROMS, v.prn)
        if args.check:
            alt = open(ziel, encoding="ascii").read() if os.path.exists(ziel) else None
            if alt != text:
                print(f"ABWEICHUNG: {os.path.relpath(ziel, ROOT)} ist nicht die Ausgabe des "
                      f"Generators (python3 tools/gen_prg710_zre_prn.py ausfuehren)")
                ok = False
            else:
                print(f"ok: {v.prn} bytegleich, 1024 Byte abgedeckt")
        else:
            text.encode("ascii")  # nur ASCII
            open(ziel, "w", encoding="ascii").write(text)
            print(f"geschrieben: {os.path.relpath(ziel, ROOT)}  ({len(text.splitlines())} Zeilen)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
