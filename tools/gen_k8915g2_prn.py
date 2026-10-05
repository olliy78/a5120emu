#!/usr/bin/env python3
"""
Baut doc/EPROMS/K8915G2/k8915g2_zre.prn und k8915g2_pfs3820.prn: die kommentierten
Listings der EPROM-Abzuege der K8915 "Generation 2" (Entwurf 24, AP-V3a), analog
tools/gen_prg710_zre_prn.py.

Quellen: die Abzuege in doc/EPROMS/K8915G2/ (MD5SUMS wird geprueft), rohes Disassemblat von
tools/z80_disasm2.py (je Abschnitt, linear, in der LAUF-Adresse), Handkommentare aus diesem
Skript.  STATISCHE Analyse (es gibt keine Gen-2-Maschine): belegt ist nur, was im Code steht;
was gedeutet ist, traegt [?].  Ausgabe reines ASCII.

Zwei Sichten je Zeile: LADE-Adresse (Lage im Abzug / ROM-Adresse) und LAUF-Adresse (die
Adresse, unter der der Code nach seinen eigenen Operanden steht).  Sie sind gleich, ausser
beim 175-Block hinter dem Kopierer (Lauf = Lade + FC00, nach "LDIR 0000 -> FC00") und beim
kopierten Haeppchen (098FH -> FFE0H); beim Karten-Chip "3C00" (Lauf = Lade + FC00) und beim
Chip "3000" (Lauf = Lade + 0800).

Aufruf:
    python3 tools/gen_k8915g2_prn.py            # beide .prn schreiben
    python3 tools/gen_k8915g2_prn.py --check    # Waechter: nichts schreiben, pruefen
        (MD5 der Abzuege, jedes ROM-Byte genau einmal und mit dem richtigen Wert
         dargestellt, Pruefsummen nachgerechnet, bytegleich zum eingecheckten .prn)
"""
import argparse
import difflib
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOL = os.path.join(HERE, "z80_disasm2.py")
# K8915G2_DIR nur fuer den Gegenversuch (Abzug in einer Kopie veraendern -> Waechter rot)
EPROMS = os.environ.get("K8915G2_DIR") or os.path.join(ROOT, "doc/EPROMS/K8915G2")

# --------------------------------------------------------------------------------------
# Abschnittstabellen.  Abschnitt = (lo, hi, art, off, titel); lo/hi = LADE-Adressen im
# Block (hi einschliesslich), off = Lauf - Lade.  Arten:
#   code   linear disassembliert (die Kachelung muss auf hi+1 enden, sonst Abbruch)
#   text   Zeichen (Bit 7 = Textende, wenn 'ende7')
#   data   Bytes, 8 je Zeile
#   words  16-Bit-Woerter (little endian)
#   auto   Folgen gleicher Bytes (>= 8) als DS-Zeile, Rest als Bytes
#   sum    24-Bit-Pruefsumme (die letzten drei Bytes), wird nachgerechnet
# --------------------------------------------------------------------------------------


def S(lo, hi, art, off, titel, **kw):
    d = dict(lo=lo, hi=hi, art=art, off=off, titel=titel)
    d.update(kw)
    return d


FC = 0xFC00

# ---- 175 (0000-03FF): Laufadressen FCxx nach der Kopie -------------------------------
S175 = [
    S(0x0000, 0x0010, "code", 0, "Reset-Einsprung (laeuft aus dem ROM): DI, IM 2, A8H, Port 61H, dann nach 0400H"),
    S(0x0011, 0x0020, "code", 0, "Kopierer (laeuft aus dem ROM): ROM 0000H-03FFH -> RAM FC00H-FFFFH, weiter bei FC21H"),
    S(0x0021, 0x0065, "code", FC, "Selbsttest Teil 1 (laeuft aus der RAM-Kopie): MROM - 24-Bit-Pruefsumme je 1-KB-Baustein"),
    S(0x0066, 0x0067, "code", 0, "NMI-Vektor: RETN (laeuft aus ROM/RAM bei 0066H; der RAM-Test schreibt ED 45 selbst dorthin)"),
    S(0x0068, 0x00E2, "code", FC, "Selbsttest Teil 1 (Fortsetzung), Teilprogramm bei FCCCH (= 00CCH)"),
    S(0x00E3, 0x02D8, "code", FC, "Selbsttest Teil 2: SIO/KEY/CTC, RAM-Test, Warteschleife auf ENTER bzw. '#'"),
    S(0x02D9, 0x033C, "code", FC, "Unterprogramme (FED9H, FEDDH, FEE2H, FEF0H, FF05H, FF0DH, FF1FH, FF2FH)"),
    S(0x033D, 0x0340, "code", FC, "Interrupt-Dienst (Vektor in FFF6H): INC A / EI / RETI"),
    S(0x0341, 0x034F, "text", FC, "Pruefnamen zu je 3 Zeichen: ROM RAM SIO KEY CTC (je einzeln per HL = FF41H, FF44H, ...)", wid=3),
    S(0x0350, 0x0359, "text", FC, "Text 'DIAGNOSTIC' (10 Byte; FF50H)"),
    S(0x035A, 0x0376, "text", FC, "Text ' ENTER: LADER  /  \"#\": ZYKL. ' (29 Byte; FF5AH -> Bild 1734H)"),
    S(0x0377, 0x0378, "data", FC, "SIO-Folge 1: 07 01 -> Port 5AH (2 Byte, OTIR in FF1FH)"),
    S(0x0379, 0x0381, "data", FC, "SIO-Folge 2: 18 04 44 03 C1 05 EA 01 00 -> Port 53H (9 Byte; WR0=18H Reset, WR4=44H, WR3=C1H, WR5=EAH, WR1=00H)"),
    S(0x0382, 0x0387, "text", FC, "Zeichenfolge ESC [ 2 ; 1 y (6 Byte; geht als Bildschirmsteuerung an Port 52H [?])"),
    S(0x0388, 0x038D, "words", FC, "Bildadressen 1740H (FF88H), 1770H (FF8AH), 1776H (FF8CH = Meldungszelle fuer den Pruefstatus)"),
    S(0x038E, 0x0397, "text", FC, "Muster 'H', 8 Blanks, 'H' (10 Byte, FF8EH; wird FFF8H x (FF98H) Byte lang ins Bild kopiert)"),
    S(0x0398, 0x0399, "words", FC, "FF98H: Laenge des Musters = 000AH"),
    S(0x039A, 0x039B, "words", FC, "FF9AH: 0190H (Zeilenabstand des Rahmens [?])"),
    S(0x039C, 0x039C, "data", FC, "FF9CH: 50H = 80 Spalten"),
    S(0x039D, 0x03C3, "data", FC, "Rahmentabelle (FF9DH): Paare (hoch, tief) als 16-Bit-Schritte, Ende bei erstem Byte FFH (03C3H)"),
    S(0x03C4, 0x03CB, "words", FC, "Bildadressen des Rahmens: FFC4H=12EFH, FFC6H=003FH, FFC8H=003EH, FFCAH=124FH"),
    S(0x03CC, 0x03D3, "data", FC, "SIO-Initialisierung (FFCCH, je 8 Byte an 45H, 47H und 55H per OTIR): 00 18 03 C1 04 45 05 68"),
    S(0x03D4, 0x03F2, "auto", FC, "ungenutzt (00H)"),
    S(0x03F3, 0x03F5, "code", 0, "Sprungvektor JP 0400H (Ziel von JP 03F3H aus 177: zurueck in den Lader 176)"),
    S(0x03F6, 0x03F7, "words", FC, "Interruptvektor FFF6H -> FF3DH (der Dienst bei 033DH)"),
    S(0x03F8, 0x03F8, "data", FC, "FFF8H: C0H = 192 (Wiederholungszahl des Bildmusters: 192 x 10 = 1920 = 80 x 24 Zeichen)"),
    S(0x03F9, 0x03F9, "data", FC, "FFF9H: Merker (Bit 0 = Fehler gemeldet, Bit 2 = '#'-Zyklusbetrieb)"),
    S(0x03FA, 0x03FC, "code", 0, "Sprungvektor JP 0011H (Wiederholung des Selbsttests ab Kopierer)"),
    S(0x03FD, 0x03FF, "sum", 0, "24-Bit-Pruefsumme der Bytes 0000H-03FCH"),
]

# ---- 176 (0400-07FF): laeuft unverschoben aus dem ROM ------------------------------------
S176 = [
    S(0x0400, 0x0402, "code", 0, "Sprungtabelle 1: Lader/Warmstart -> 041AH (Einsprung von 175 mit JP 0400H)"),
    S(0x0403, 0x0405, "code", 0, "Sprungtabelle 2: Kaltstart -> 042BH (Ziel von JP 0403H aus 175)"),
    S(0x0406, 0x0419, "code", 0, "nicht angesprungen: Bildspeicher 1000H-177FH Bit 7 loeschen, dann A=1, nach 0484H [?]"),
    S(0x041A, 0x042A, "code", 0, "Haeppchen 098FH.. (23 Byte) nach FFE0H kopieren und dort anspringen"),
    S(0x042B, 0x0440, "code", 0, "Kaltstart: SP=F7E0H, Bild loeschen (20H), Text ausgeben (Aufruf 0907H, Text folgt inline)"),
    S(0x0441, 0x0461, "text", 0, "Inline-Text '* Coldstart *  Disk on A: ready ?' (Textende: Bit 7 im letzten Zeichen)", ende7=True),
    S(0x0462, 0x04E6, "code", 0, "Laufwerk A: ansprechen (0484H), Leseversuch; Unterprogramme 0467H/047BH/0484H"),
    S(0x04E7, 0x04E9, "code", 0, "Text ausgeben (CALL 0907H)"),
    S(0x04EA, 0x0502, "text", 0, "Inline-Text 'Drive A: not ready, check'", ende7=True),
    S(0x0503, 0x0570, "code", 0, "Spur-/Sektorsuche, Lesen"),
    S(0x0571, 0x0573, "code", 0, "Text ausgeben (CALL 0907H)"),
    S(0x0574, 0x058A, "text", 0, "Inline-Text 'Disk-error, change disk'", ende7=True),
    S(0x058B, 0x06C7, "code", 0, "Lesen, Sektorkopf-/Datenfeldauswertung"),
    S(0x06C8, 0x06CA, "code", 0, "Text ausgeben (CALL 0907H)"),
    S(0x06CB, 0x06E5, "text", 0, "Inline-Text 'No system disk, change disk'", ende7=True),
    S(0x06E6, 0x07CB, "code", 0, "Sektorfolge einlesen, Pruefung"),
    S(0x07CC, 0x07FC, "auto", 0, "ungenutzt (00H)"),
    S(0x07FD, 0x07FF, "sum", 0, "24-Bit-Pruefsumme der Bytes 0400H-07FCH"),
]

# ---- 177 (0800-0BFF): unverschoben aus dem ROM, Haeppchen bei 098FH laeuft bei FFE0H -----------
S177 = [
    S(0x0800, 0x0836, "code", 0, "Ende des Ladevorgangs: Kopf-/Sektorfolge-Auswertung, dann Meldung oder Abschluss"),
    S(0x0837, 0x0865, "text", 0, "Inline-Text 'Loading complete, replace disk by previous disk'", ende7=True),
    S(0x0866, 0x0872, "code", 0, "Sprung hinter den Text, naechste Meldung"),
    S(0x0873, 0x0897, "text", 0, "Inline-Text 'Loading complete, replace system disk'", ende7=True),
    S(0x0898, 0x0906, "code", 0, "Unterprogramme (RET-Wert aus F702H/F712H) / Pruefsummenfolge 08D7H, 08E0H"),
    S(0x0907, 0x0976, "code", 0, "Inline-Text-Ausgabe 0907H: Port 61H, SIO-Init (OTIR 5AH/53H), Bild, Text, Warten auf Taste (Port 52H/53H)"),
    S(0x0977, 0x0978, "data", 0, "SIO-Folge 1 (OTIR an Port 5AH, 2 Byte): 07 01"),
    S(0x0979, 0x0982, "data", 0, "SIO-Folge 2 (OTIR an Port 53H, 10 Byte): 00 18 04 44 03 C1 05 EA 01 00"),
    S(0x0983, 0x098E, "text", 0, "Text ' --> <ENTER>' (12 Byte, nach Bild kopiert)"),
    S(0x098F, 0x09A5, "code", 0xFFE0 - 0x098F, "Haeppchen (wird von 176 nach FFE0H kopiert): A8H=8FH, Systemkennung 0000H/0005H=C3H pruefen, A8H=0EH, JP 0428H"),
    S(0x09A6, 0x0BFC, "auto", 0, "ungenutzt (00H); EINZELNES Byte 0A33H = 04H (Pruefsummenabweichung, siehe Kopf)"),
    S(0x0BFD, 0x0BFF, "sum", 0, "24-Bit-Pruefsumme der Bytes 0800H-0BFCH (gespeichert 00A67CH)"),
]

# ---- Karten-Chip "3C00" (Lauf = Lade + FC00) ------------------------------------------------
S3C00 = [
    S(0x0000, 0x003E, "code", FC, "Einstieg: DI, IM 2, A8H=0EH, Ports E2H/E4H/E0H/B3H/B1H, Signatur F3 ED 5E bei D001H-D003H pruefen, JP 0400H"),
    S(0x003F, 0x004E, "code", FC, "Kopierer: LD HL,0000H (aus H=E=00, L=E) -> DE=FC00H, BC=0400H, LDIR, JP FC50H"),
    S(0x004F, 0x004F, "code", FC, "NOP (Fuellbyte)"),
    S(0x0050, 0x0063, "code", FC, "Selbsttest-Anfang"),
    S(0x0064, 0x0065, "data", FC, "Fuellbytes 00 00"),
    S(0x0066, 0x0067, "code", FC, "RETN (NMI-Vektor-Platz, vgl. 175 bei 0066H)"),
    S(0x0068, 0x034F, "code", FC, "Selbsttest und Unterprogramme (Chip-Fassung)"),
    S(0x0350, 0x0353, "code", FC, "Interrupt-Dienst (Vektor in FFF6H): INC A / EI / RETI"),
    S(0x0354, 0x0362, "text", FC, "Pruefnamen zu je 3 Zeichen: ROM RAM I/O KEY CTC", wid=3),
    S(0x0363, 0x036C, "text", FC, "Text 'DIAGNOSTIC'"),
    S(0x036D, 0x0387, "text", FC, "Text ' ENTER: LADER / OFF: ZYKL. '"),
    S(0x0388, 0x0391, "words", FC, "Bildadressen 1000H, 177FH, 1740H, 1770H, 1775H"),
    S(0x0392, 0x039A, "text", FC, "Muster 'H', 7 Blanks, 'H'"),
    S(0x039B, 0x039C, "words", FC, "Laenge 000AH"),
    S(0x039D, 0x039E, "words", FC, "0190H"),
    S(0x039F, 0x039F, "data", FC, "50H = 80 Spalten"),
    S(0x03A0, 0x03C7, "data", FC, "Rahmentabelle (wie 175 bei 039DH.., Ende bei FFH 03C7H)"),
    S(0x03C8, 0x03CF, "words", FC, "Bildadressen des Rahmens: 12EFH, 003FH, 003EH, 124FH"),
    S(0x03D0, 0x03D7, "data", FC, "SIO-Initialisierung: 00 18 03 C1 04 45 05 68"),
    S(0x03D8, 0x03F2, "auto", FC, "ungenutzt (00H)"),
    S(0x03F3, 0x03F5, "code", FC, "Sprungvektor JP FE3FH (= Lade 033FH)"),
    S(0x03F6, 0x03F7, "words", FC, "Interruptvektor -> FF50H (Dienst bei 0350H)"),
    S(0x03F8, 0x03F8, "data", FC, "C0H"),
    S(0x03F9, 0x03F9, "data", FC, "Merker"),
    S(0x03FA, 0x03FC, "code", FC, "Sprungvektor JP 003FH (Kopierer)"),
    S(0x03FD, 0x03FF, "sum", FC, "24-Bit-Pruefsumme der Bytes 0000H-03FCH"),
]

# ---- Karten-Chip "3000" (Lauf = Lade + 0800) ------------------------------------------------
S3000 = [
    S(0x0000, 0x0036, "code", 0x0800, "wie 177 bei 0800H (Zellen bei 0Cxx statt F7xx)"),
    S(0x0037, 0x0065, "text", 0x0800, "Inline-Text 'Loading complete, replace disk by previous disk'", ende7=True),
    S(0x0066, 0x0072, "code", 0x0800, "Sprung hinter den Text, naechste Meldung"),
    S(0x0073, 0x0097, "text", 0x0800, "Inline-Text 'Loading complete, replace system disk'", ende7=True),
    S(0x0098, 0x0106, "code", 0x0800, "Unterprogramme / Pruefsummenfolge (wie 177)"),
    S(0x0107, 0x014E, "code", 0x0800, "Inline-Text-Ausgabe 0907H, Chip-Fassung: Port E4H statt 61H, KEIN SIO-Init, Taste ueber IN E1H/E0H"),
    S(0x014F, 0x015A, "text", 0x0800, "Text ' --> <ENTER>' (12 Byte)"),
    S(0x015B, 0x0171, "code", 0x0800, "Haeppchen wie 177 bei 098FH (23 Byte; A8H=8FH ... JP 0428H)"),
    S(0x0172, 0x017F, "data", 0x0800, "Rest eines aelteren Programmstands [?] (CD F5 0F E6 F0 AB 5F F1 E6 E0 AA 53 5F: Endstueck der Pruefsummenfolge)"),
    S(0x0180, 0x03FC, "auto", 0x0800, "ungenutzt (00H)"),
    S(0x03FD, 0x03FF, "sum", 0x0800, "24-Bit-Pruefsumme der Bytes 0000H-03FCH (gespeichert 008D5DH)"),
]

# Handnamen (Lauf-Adresse -> Name), je Listing; alles andere bekommt Lxxxx
NAMEN_ZRE = {
    0x0000: "RESET", 0x0011: "KOPIERER", 0xFC21: "SELBSTTEST", 0xFC47: "PRUEFSUMME_BLOCK",
    0xFCCC: "WARTE_ZEICHEN", 0xFED9: "PORT61_NULL", 0xFEDD: "PORT61_FF", 0xFEE2: "BILD_LOESCHEN",
    0xFEF0: "TESTNAME_AUSGEBEN", 0xFF05: "WARTEN_C", 0xFF0D: "WARTEN_LANG", 0xFF1F: "SIO_INIT",
    0xFF2F: "TERMINAL_ESC", 0xFF3D: "ISR", 0x0400: "SPRUNG_LADER", 0x0403: "SPRUNG_KALTSTART",
    0x041A: "HAEPPCHEN_KOPIEREN", 0x042B: "KALTSTART", 0x0467: "LW_STEUERIMPULS",
    0x047B: "LW_AUS", 0x0484: "LW_INIT", 0x0907: "TEXT_INLINE", 0x08D7: "HASH_BLOCK",
    0x08E0: "HASH_FOLGE", 0xFFE0: "HAEPPCHEN", 0x03F3: "VEKTOR_0400", 0x03FA: "VEKTOR_0011",
}
NAMEN_PFS = {
    0xFC00: "EINSTIEG", 0xFC3F: "KOPIERER", 0xFC50: "SELBSTTEST", 0xFC66: "RETN66",
    0xFF50: "ISR", 0x0907: "TEXT_INLINE",
}

# Kommentare je LADE-Adresse (Block, Adresse) -> Text; nur belegbare Deutungen
KOM = {
    ("175", 0x0003): "A8H=06H: Speicherumschaltung (Bedeutung der Bits: V3b)",
    ("175", 0x0008): "FFF9H = Merkerzelle (Lade 03F9H)",
    ("175", 0x000C): "Port 61H = FFH [?]",
    ("175", 0x0047): "BC=03FDH = Laenge ohne die drei Pruefsummenbytes",
    ("175", 0x005A): "A - (IY+2) - ... : Vergleich mit der 24-Bit-Summe in den letzten drei Bytes",
    ("175", 0x0081): "Schleife ueber die Bausteine IX = 0000H, 0400H, 0800H (drei 1-KB-Bloecke)",
    ("175", 0x00F5): "I = FFH (Interrupttabellen-Seite FFxxH; Vektorwort FFF6H = FF3DH)",
    ("175", 0x00F9): "Steuerwort F0H an Port 80H (CTC), 48H und 58H",
    ("175", 0x01BB): "A8H=87H: RAM-Test ueber den ganzen Speicher (0066H wird mit ED 45 vorbelegt)",
    ("175", 0x01DE): "A8H=06H zurueck",
    ("175", 0x0200): "Port 52H lesen: 7FH -> Neustart ab L0211, sonst Zweig ueber Merker FFF9H Bit 2 (0403H bzw. FC29H)",
    ("175", 0x02AF): "Port 53H Bit 0 = Zeichen da; Port 52H = Zeichen; '#' (23H) = Zyklusbetrieb, CR (0DH) = Lader (0403H)",
    ("176", 0x041A): "Quelle 098FH (177), Ziel FFE0H, 23 Byte",
    ("176", 0x0498): "Port 61H = E0H; Port 18H = FFH (K5122-Auswahl, hohes Halbbyte /SE)",
    ("177", 0x0907): "Port 61H = A (der Aufrufer legt A vor)",
    ("177", 0x0909): "OTIR 2 + 10 Byte an Port 5AH / 53H (Tabelle 0977H)",
    ("177", 0x092F): "Text in Bild bei DE kopieren, Bit 7 des letzten Zeichens beendet",
    ("177", 0x0958): "'E' (45H) -> JP 03F3H (= 175: JP 0400H)",
    ("176", 0x06BB): "DE=FFFFH, B=10H Byte, C=1: Pruefwert ueber den Sektorkopf (Unterprogramm 08E0H) [?]",
    ("177", 0x08D7): "DE=E295H Startwert, B=80H Byte je Durchlauf, C=(F719H) Durchlaeufe: CRC-artige Rechnung [?]",
    ("177", 0x08E0): "Rechenschleife (XOR/Rotation) ueber B Byte ab HL, Ergebnis in DE",
    ("176", 0x07B0): "CALL 08D7H: Pruefwert des gelesenen Datenfeldes; Vergleich D/E mit den 2 Byte bei (F784H) [?]",
}

# --------------------------------------------------------------------------------------
BLOECKE_ZRE = [
    ("175", "k8915g2_zre_0000_175.bin", 0x0000, S175),
    ("176", "k8915g2_zre_0400_176.bin", 0x0400, S176),
    ("177", "k8915g2_zre_0800_177.bin", 0x0800, S177),
]
BLOECKE_PFS = [
    ("3C00", "k8915g2_pfs3820_3C00.bin", 0x0000, S3C00),
    ("3000", "k8915g2_pfs3820_3000.bin", 0x0000, S3000),
]
LEERE_CHIPS = ["0000", "0400", "0800", "0C00", "1000", "1400", "1800", "1C00", "2000", "2400",
               "2800", "2C00", "3400", "3800"]


def relativ(secs, base):
    """176/177 sind oben mit absoluten Lade-Adressen notiert: auf Bausteinbeginn beziehen."""
    for s in secs:
        s["lo"] -= base
        s["hi"] -= base
    return secs


relativ(S176, 0x0400)
relativ(S177, 0x0800)


def lies(name):
    return open(os.path.join(EPROMS, name), "rb").read()


def md5sums():
    d = {}
    for z in open(os.path.join(EPROMS, "MD5SUMS"), encoding="ascii"):
        z = z.split()
        if len(z) == 2:
            d[z[1]] = z[0]
    return d


def pruefe_quellen(md5):
    for n in ("k8915g2_k7024_171.bin", "k8915g2_k7024_172.bin"):
        assert hashlib.md5(lies(n)).hexdigest() == md5[n], f"{n}: MD5 weicht ab"
    for blk in BLOECKE_ZRE + BLOECKE_PFS:
        rom = lies(blk[1])
        assert len(rom) == 1024, f"{blk[1]}: Laenge {len(rom)}"
        assert hashlib.md5(rom).hexdigest() == md5[blk[1]], f"{blk[1]}: MD5 weicht ab"
    z3 = b"".join(lies(b[1]) for b in BLOECKE_ZRE)
    assert z3 == lies("k8915g2_zre_0000-0BFF.bin"), "zre_0000-0BFF ist nicht die Verkettung"
    assert hashlib.md5(z3).hexdigest() == md5["k8915g2_zre_0000-0BFF.bin"]
    chips = b""
    for a in range(0, 0x4000, 0x400):
        n = f"k8915g2_pfs3820_{a:04X}.bin"
        c = lies(n)
        assert hashlib.md5(c).hexdigest() == md5[n], f"{n}: MD5 weicht ab"
        if f"{a:04X}" in LEERE_CHIPS:
            assert c == b"\xff" * 1024, f"{n}: nicht leer"
        chips += c
    assert chips == lies("k8915g2_pfs3820_0000-3FFF.bin"), "pfs3820_0000-3FFF ist nicht die Verkettung"


# --------------------------------------------------------------------------------------
LINE_RE = re.compile(r'^([0-9A-Fa-f]{4})\s+([0-9A-Fa-f ]*?)\s*\t(.*?)\s*$')
OPND = re.compile(r'\b(?:L|sub_)([0-9A-F]{4})\b')


def disasm(rom, lo, hi, off, base=0):
    """Befehle (lade, laenge, mnemonic) im Abschnitt; Mnemonic in LAUF-Adressen."""
    lauf = base + lo + off
    n = hi - lo + 1
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(rom[lo:hi + 1])
        pfad = f.name
    try:
        out = subprocess.run(
            [sys.executable, TOOL, "--org", f"0x{lauf:04X}", "--entry", f"0x{lauf:04X}",
             "--range", f"0x{lauf:04X}:0x{lauf + n:04X}", "--no-strings", pfad],
            capture_output=True, text=True, check=True).stdout
    finally:
        os.unlink(pfad)
    ins = {}
    for z in out.splitlines():
        m = LINE_RE.match(z)
        if not m:
            continue
        a = int(m.group(1), 16)
        if not (lauf <= a < lauf + n):
            continue
        ins[a] = (len(m.group(2).split()), m.group(3))
    res = []
    a = lauf
    while a < lauf + n:
        assert a in ins, f"Disassemblat tilt {lo:04X}-{hi:04X} nicht (Loch bei Lauf {a:04X})"
        ln, mn = ins[a]
        res.append((a - off - base, ln, mn))
        a += ln
    assert a == lauf + n, f"Disassemblat tilt {lo:04X}-{hi:04X} nicht (Ende {a - off - base:04X})"
    return res


def zeichen(b):
    return chr(b) if 0x20 <= b < 0x7F else None


def db_text(bs, ende7=False):
    """DB-Operand fuer Text: Zeichenketten in Anfuehrungszeichen, Rest als Hexwert."""
    teile = []
    cur = ""
    for i, b in enumerate(bs):
        e7 = ende7 and i == len(bs) - 1
        c = zeichen(b & 0x7F) if e7 else zeichen(b)
        if c is not None and not e7 and c != '"':
            cur += c
            continue
        if cur:
            teile.append(f'"{cur}"')
            cur = ""
        if e7:
            teile.append(f"{b:02X}H")
        else:
            teile.append(f"{b:02X}H")
    if cur:
        teile.append(f'"{cur}"')
    return ",".join(teile)


def zeilen_block(name, rom, base, secs, ziele):
    """Zeilen eines Blocks als (lade_abs, lauf, bytes_hex, text, kommentar, art)."""
    zl = []
    prev = -1
    for s in secs:
        assert s["lo"] == prev + 1, f"{name}: Abschnitt beginnt bei {s['lo']:04X}, erwartet {prev + 1:04X}"
        prev = s["hi"]
        off = s["off"]
        zl.append(("kopf", s))
        lo, hi = s["lo"], s["hi"]
        art = s["art"]
        if art == "code":
            for a, ln, mn in disasm(rom, lo, hi, off, base):
                zl.append(("z", a, base + a + off, rom[a:a + ln], mn, KOM.get((name, base + a), "")))
        elif art in ("text", "data", "words", "sum"):
            a = lo
            ende7 = s.get("ende7", False)
            wid = s.get("wid", 16 if art == "text" else 8)
            if art == "words":
                wid = 2
            if art == "sum":
                wid = 3
            while a <= hi:
                b = min(a + wid - 1, hi)
                bs = rom[a:b + 1]
                if art == "text":
                    t = "DB\t" + db_text(bs, ende7 and b == hi)
                elif art == "words":
                    t = "DW\t" + f"{bs[0] | bs[1] << 8:04X}H"
                elif art == "sum":
                    t = "DB\t" + ",".join(f"{x:02X}H" for x in bs) + "   ; 24 Bit, hoeherwertiges Byte zuerst"
                else:
                    t = "DB\t" + ",".join(f"{x:02X}H" for x in bs)
                zl.append(("z", a, base + a + off, bs, t, KOM.get((name, base + a), "")))
                a = b + 1
        elif art == "auto":
            a = lo
            while a <= hi:
                b = a
                while b + 1 <= hi and rom[b + 1] == rom[a]:
                    b += 1
                if b - a + 1 >= 8:
                    zl.append(("fill", a, base + a + off, b - a + 1, rom[a]))
                    a = b + 1
                else:
                    # kurze Folge + Unterschiedliches bis zum naechsten langen Lauf als Bytes
                    e = a
                    while e <= hi:
                        f = e
                        while f + 1 <= hi and rom[f + 1] == rom[e]:
                            f += 1
                        if f - e + 1 >= 8:
                            break
                        e = f + 1
                    e -= 1
                    while a <= e:
                        c = min(a + 7, e)
                        bs = rom[a:c + 1]
                        zl.append(("z", a, base + a + off, bs, "DB\t" + ",".join(f"{x:02X}H" for x in bs),
                                   KOM.get((name, base + a), "")))
                        a = c + 1
        else:
            raise AssertionError(art)
    assert prev == 0x03FF, f"{name}: Abschnitte enden bei {prev:04X}"
    return zl


def sum24(rom):
    return sum(rom[:0x3FD])


def symbolisieren(mn, namen, vorhanden):
    """Operanden L/sub_xxxx -> xxxxH; Sprungziele auf bekannte Lauf-Adressen -> Name."""
    mn = OPND.sub(lambda m: m.group(1) + "H", mn)

    def ersetze(m):
        v = int(m.group(1), 16)
        if v in namen:
            return namen[v]
        if v in vorhanden:
            return f"L{v:04X}"
        return m.group(0)
    if re.match(r'(JP|CALL|JR|DJNZ)\b', mn):
        return re.sub(r'\b([0-9A-F]{4})H\b', ersetze, mn)
    return mn


def ziele_von(mn):
    m = re.match(r'(?:JP|CALL|JR|DJNZ)\s+(?:\w+,)?\s*(?:L|sub_)?([0-9A-F]{4})H?\s*$', mn)
    return int(m.group(1), 16) if m else None


def render_block(name, fname, rom, base, secs, namen, titel_zeilen):
    zl = zeilen_block(name, rom, base, secs, None)
    # Lauf-Adressen aller Befehls-/Datenzeilen und aller Sprungziele
    lauf_alle = {}
    ziele = set()
    for z in zl:
        if z[0] == "z":
            lauf_alle[z[2]] = z
            t = ziele_von(z[4])
            if t is not None:
                ziele.add(t)
    return zl, ziele


def kopf_block(name, fname, rom, base, secs, extra):
    L = [f"; ---- Baustein {name}: {fname}  (Lade {base:04X}H-{base + 0x3FF:04X}H) "
         + "-" * 10]
    L += extra
    return L


# --------------------------------------------------------------------------------------
def anzahl_ziele(zl, bereich):
    """Zaehlt absolute Sprung-/Aufrufziele (JP/CALL) nach 256-Byte-Seite der Lauf-Adresse."""
    c = {}
    for z in zl:
        if z[0] != "z":
            continue
        m = re.match(r'(JP|CALL)\s+(?:\w+,)?\s*(?:L|sub_)?([0-9A-F]{4})H?\s*$', z[4])
        if m:
            t = int(m.group(2), 16) >> 8
            c[t] = c.get(t, 0) + 1
    return c


def vergleich(a, b, na, nb):
    """Gegenueberstellung zweier Abzuege (difflib auf Byteebene)."""
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    ops = sm.get_opcodes()
    gleich = sum(i2 - i1 for t, i1, i2, j1, j2 in ops if t == "equal")
    einzel = {}
    blocks = []
    for t, i1, i2, j1, j2 in ops:
        if t == "equal":
            blocks.append(["equal", i1, i2, j1, j2])
            continue
        if t == "replace" and i2 - i1 == 1 and j2 - j1 == 1:
            ka = (a[i1], b[j1])
            einzel.setdefault(ka, []).append(i1)
        blocks.append([t, i1, i2, j1, j2])
    # nicht-triviale Bereiche: alles ausser 'equal' und ausser Einzelbyte-Ersetzungen mit den
    # haeufigen Paaren; benachbarte Bereiche mit weniger als 8 gleichen Bytes dazwischen
    # werden zusammengefasst
    bereiche = []
    for t, i1, i2, j1, j2 in blocks:
        if t == "equal":
            continue
        if t == "replace" and i2 - i1 == 1 and j2 - j1 == 1 and len(einzel[(a[i1], b[j1])]) >= 3:
            continue
        bereiche.append([i1, i2, j1, j2])
    merged = []
    for r in bereiche:
        if merged and r[0] - merged[-1][1] < 8:
            merged[-1][1] = r[1]
            merged[-1][3] = r[3]
        else:
            merged.append(r)
    L = []
    L.append(f";   {na} <-> {nb}: {gleich} von 1024 Byte stehen in der Ausrichtung (difflib) gleich beieinander;")
    L.append(f";   {1024 - gleich} Byte weichen ab bzw. sind eingefuegt/entfernt.")
    haeufig = sorted(((k, v) for k, v in einzel.items() if len(v) >= 3), key=lambda kv: -len(kv[1]))
    for (x, y), pos in haeufig:
        L.append(f";   Einzelbyte {x:02X}H -> {y:02X}H an {len(pos)} Stellen: "
                 + ", ".join(f"{p:04X}" for p in pos[:40]) + (" ..." if len(pos) > 40 else ""))
    L.append(f";   Zusammenhaengende Abweichungsbereiche ({na}-Lade <-> {nb}-Lade, Laenge je Seite):")
    for i1, i2, j1, j2 in merged:
        li = i2 - i1
        lj = j2 - j1
        art = "gleich lang" if li == lj else ("nur in " + nb if li == 0 else ("nur in " + na if lj == 0 else "verschieden lang"))
        L.append(f";     {i1:04X}-{max(i1, i2 - 1):04X} <-> {j1:04X}-{max(j1, j2 - 1):04X}  ({li}/{lj} Byte, {art})")
    return L


def kopf_zre(ab, ziel_stat):
    s3 = {n: sum24(r) for n, _, _, _ in [(b[0], 0, 0, 0) for b in BLOECKE_ZRE] for r in [lies(
        next(b[1] for b in BLOECKE_ZRE if b[0] == n))]}
    return None


def bau_kopf_zre(zls, md5, stat):
    L = []
    A = L.append
    A("; ============================================================================")
    A("; k8915g2_zre.prn  -  K8915 'Generation 2': ZRE-Boot-ROM, 3 x U555 (2708), 0000H-0BFFH")
    A(";")
    A("; Quellen: doc/EPROMS/K8915G2/k8915g2_zre_0000_175.bin   MD5 " + md5["k8915g2_zre_0000_175.bin"])
    A(";          doc/EPROMS/K8915G2/k8915g2_zre_0400_176.bin   MD5 " + md5["k8915g2_zre_0400_176.bin"])
    A(";          doc/EPROMS/K8915G2/k8915g2_zre_0800_177.bin   MD5 " + md5["k8915g2_zre_0800_177.bin"])
    A("; Erzeugt: tools/gen_k8915g2_prn.py (rohes Disassemblat tools/z80_disasm2.py + Handkommentare)")
    A("; Plan   : doc/design/24_k8915_varianten.md (AP-V3a); Abzuege: doc/EPROMS/K8915G2/README.md")
    A(";")
    A("; STATISCHE Analyse -- es gibt keine Gen-2-Maschine, also keine Laufzeitbestaetigung.")
    A("; [?] = Deutung nach Vermutung; alles andere steht im Code.  Jede Zeile traegt zwei")
    A("; Adressen:  LADE  (Lage im Abzug = ROM-Adresse)   LAUF  (Adresse nach den Operanden des")
    A("; Codes selbst).  Gleich, ausser beim 175-Block hinter dem Kopierer (Lauf = Lade + FC00)")
    A("; und beim Haeppchen 098FH-09A5H (Lauf FFE0H-FFF6H).")
    A(";")
    A("; ---- Reihenfolge der drei Bausteine (BEFUND 1) --------------------------------------")
    A(";   Ergebnis: 175 = 0000H, 176 = 0400H, 177 = 0800H -- aus dem Inhalt BESTAETIGT")
    A(";   (die Reihenfolge der Aufschriften 175/176/177 ist aufsteigend, nicht abgelesen).")
    A(";   Belege (alles [ROM]):")
    A(";   a) 175 springt bei 000EH mit JP 0400H und bei 02D6H/020BH mit JP 0403H; bei 0400H steht")
    A(";      in 176 die Sprungtabelle 'C3 1A 04 / C3 2B 04' (Ziele 041AH/042BH liegen in 176).")
    A(";   b) 176 ruft 6x CALL 0907H (Text-Ausgabe, steht in 177) und kopiert mit LD HL,098FH")
    A(";      23 Byte aus 177 nach FFE0H; 177 springt mit JP 03F3H zurueck zu 175 (dort 'C3 00 04').")
    A(";   c) Ziele der absoluten Spruenge (JP/CALL) nach Seite der Lauf-Adresse, je Baustein:")
    for n, c in stat:
        A(";        " + n + ": " + ", ".join(f"{k:02X}xx:{v}" for k, v in sorted(c.items())))
    A(";      176 springt in 04xx-07xx (eigene Lage) und 08xx-09xx (177); 177 in 08xx-09xx (eigene")
    A(";      Lage) und 03xx-05xx (175/176): die Lagen 0400H und 0800H stehen im Code selbst.")
    A(";   d) Der Selbsttest 'MROM' (175, Schleife bei 0047H-0096H) summiert IX = 0000H, 0400H, 0800H")
    A(";      je 3FDH Byte und vergleicht mit den letzten 3 Bytes jedes Bausteins: drei lueckenlos")
    A(";      aufeinanderfolgende 1-KB-Bloecke ab 0000H.")
    A(";   KORREKTUR zu Entwurf 24 (AP-V3a, Punkt 2): 177 laeuft NICHT 'bei 04xx nach der Kopie';")
    A(";   nur 175 wird nach FC00H kopiert (und das 23-Byte-Haeppchen nach FFE0H).  176 und 177")
    A(";   laufen unverschoben aus dem ROM bei 0400H bzw. 0800H.  'Sprungziele 03xx-09xx' heisst:")
    A(";   JP 03F3H (Vektor in 175) bis 09xx (Text-Routine in 177).")
    A(";")
    A("; ---- Wo was laeuft ----------------------------------------------------------------")
    A(";   0000-0020  laeuft aus dem ROM (Reset; Kopierer 0011H: HL=0000H -> DE=FC00H, BC=0400H, LDIR)")
    A(";   FC21-FFFF  laeuft aus der RAM-Kopie von 175 (Lade 0021H-03FFH, Lauf FC21H-FFFFH): der Selbst-")
    A(";              test; die Tabellen am Ende (FF77H.., FF88H.., FFC4H.., FFF3H-FFFFH) sind Daten UND")
    A(";              Arbeitszellen (FFF9H = Merker, FF8CH = Meldungszelle)")
    A(";   0400-0BFF  Lader 176/177 aus dem ROM; I = FFH gesetzt (Vektorwort FFF6H -> FF3DH; Zuordnung CTC/SIO: V3b)")
    A(";   F700-F7FF  Arbeitszellen des Laders (176/177: F700H.. F719H, F784H.., F786H ...; SP=F7E0H)")
    A(";   1000-177F  Bildspeicher 80 x 24 (Bild loeschen = 20H ab 1000H, 077FH Byte nachkopiert)")
    A(";")
    A("; ---- Pruefsummen (BEFUND 2) -----------------------------------------------------------")
    A(";   Jeder Baustein traegt in den letzten drei Bytes (03FDH-03FFH, hoeherwertiges zuerst) die")
    A(";   24-Bit-Summe seiner ersten 3FDH Byte (so rechnet der Selbsttest 'MROM' bei 0047H-0068H).")
    A(";   Nachgerechnet (diese Auswertung):")
    return L


def ab_ror(L):
    return L


# --------------------------------------------------------------------------------------
def erzeuge_listing(bloecke, namen, kopf, extra_nach_kopf, anhang):
    """Zeilen aller Bloecke zusammensetzen; Rueckgabe (text, abdeckung[name]=[(lo,hi,wert)])."""
    # erster Durchgang: alle Zeilen und Ziele
    alle = []
    ziele = set()
    lauf_vorh = set()
    for name, fname, base, secs in bloecke:
        rom = lies(fname)
        zl = zeilen_block(name, rom, base, secs, None)
        alle.append((name, fname, base, secs, rom, zl))
        for z in zl:
            if z[0] == "z":
                lauf_vorh.add(z[2])
                t = ziele_von(z[4])
                if t is not None:
                    ziele.add(t)
            elif z[0] == "fill":
                pass
    marken = set(ziele) | set(namen)
    L = list(kopf)
    for name, fname, base, secs, rom, zl in alle:
        L.append("")
        L.append("; " + "=" * 76)
        L.append(f"; Baustein {name}  ({fname})   Lade {base:04X}H-{base + 0x3FF:04X}H")
        L.append("; " + "=" * 76)
        for z in zl:
            if z[0] == "kopf":
                s = z[1]
                off = s["off"]
                L.append("")
                L.append(f";-- Lade {base + s['lo']:04X}H-{base + s['hi']:04X}H  Lauf {(base + s['lo'] + off) & 0xFFFF:04X}H-{(base + s['hi'] + off) & 0xFFFF:04X}H  [{s['art']}]  {s['titel']}")
            elif z[0] == "fill":
                _, a, lf, n, v = z
                A = base + a
                LA = lf & 0xFFFF
                L.append(f"{A:04X} {LA:04X}  --          \tDS\t{n},{v:02X}H   ; Lade {A:04X}H-{A + n - 1:04X}H")
            else:
                _, a, lf, bs, mn, kom = z
                A = base + a
                LA = lf & 0xFFFF
                if LA in marken and z[2] in lauf_vorh and re.match(r'[A-Z]', mn) and not mn.startswith("DB") \
                        and not mn.startswith("DW"):
                    nm = namen.get(LA, f"L{LA:04X}")
                    L.append(f"{nm}:")
                mn2 = symbolisieren(mn, namen, marken & lauf_vorh)
                hexs = " ".join(f"{x:02X}" for x in bs)
                zeile = f"{A:04X} {LA:04X}  {hexs:<11}\t{mn2}"
                if kom:
                    zeile += "\t; " + kom
                L.append(zeile)
    L += anhang
    L.append("")
    L.append("\tEND")
    return "\n".join(L) + "\n", alle


LINE_BYTES = re.compile(r'^([0-9A-F]{4}) ([0-9A-F]{4})  ((?:[0-9A-F]{2} ?)+) *\t')
LINE_FILL = re.compile(r'^([0-9A-F]{4}) ([0-9A-F]{4})  --  +\tDS\t(\d+),([0-9A-F]{2})H')


def pruefe_abdeckung(text, bloecke, name):
    """Jedes ROM-Byte jedes Bausteins genau einmal, an der richtigen Stelle, mit dem richtigen Wert."""
    roms = {}
    bel = {}
    for bname, fname, base, secs in bloecke:
        roms[bname] = (base, lies(fname))
        bel[bname] = [None] * 1024
    cur = None
    for z in text.splitlines():
        h = re.match(r'^; Baustein (\S+)  \(', z)
        if h:
            cur = h.group(1)
            continue
        m = LINE_BYTES.match(z)
        f = LINE_FILL.match(z)
        if m:
            a = int(m.group(1), 16)
            vals = [int(x, 16) for x in m.group(3).split()]
        elif f:
            a = int(f.group(1), 16)
            vals = [int(f.group(4), 16)] * int(f.group(3))
        else:
            continue
        assert cur is not None, f"{name}: Zeile vor dem ersten Baustein"
        base, rom = roms[cur]
        for i, v in enumerate(vals):
            idx = a + i - base
            assert 0 <= idx < 1024, f"{name}: {cur}: Adresse {a + i:04X} ausserhalb"
            assert bel[cur][idx] is None, f"{name}: Byte {cur}+{idx:04X} doppelt dargestellt"
            assert v == rom[idx], f"{name}: Byte {cur}+{idx:04X} weicht vom ROM ab"
            bel[cur][idx] = True
    for bname in bel:
        fehlt = [i for i, b in enumerate(bel[bname]) if not b]
        assert not fehlt, f"{name}: {bname}: nicht dargestellt: {fehlt[0]:04X}.. ({len(fehlt)} Byte)"


# --------------------------------------------------------------------------------------
def bauen():
    md5 = md5sums()
    pruefe_quellen(md5)
    roms = {b[0]: lies(b[1]) for b in BLOECKE_ZRE + BLOECKE_PFS}

    # ---------- ZRE ----------
    stat = []
    for name, fname, base, secs in BLOECKE_ZRE:
        zl = zeilen_block(name, roms[name], base, secs, None)
        # Lauf-Seiten der JP/CALL-Ziele
        stat.append((name, anzahl_ziele(zl, None)))
    kopf = bau_kopf_zre(None, md5, stat)
    for n, fn in (("175", "k8915g2_zre_0000_175.bin"), ("176", "k8915g2_zre_0400_176.bin"),
                  ("177", "k8915g2_zre_0800_177.bin")):
        r = roms[n]
        soll = r[0x3FD] << 16 | r[0x3FE] << 8 | r[0x3FF]
        ist = sum24(r)
        if soll == ist:
            kopf.append(f";     {n}: berechnet {ist:06X}H = gespeichert {soll:06X}H  ok")
        else:
            kopf.append(f";     {n}: berechnet {ist:06X}H, gespeichert {soll:06X}H  ** STIMMT NICHT ** (Differenz {ist - soll})")
    r177 = roms["177"]
    soll = r177[0x3FD] << 16 | r177[0x3FE] << 8 | r177[0x3FF]
    assert sum24(r177) - soll == r177[0x233] == 4 and all(b == 0 for b in r177[0x1A6:0x3FD] if b != 4)
    kopf += [
        ";   177 ist der einzige Abzug mit falscher Summe: die Summe waere erfuellt, wenn Byte 0A33H",
        ";   (Lade 0233H) 00H statt 04H waere -- ein einzelnes Bit in sonst ungenutztem 00H-Bereich.",
        ";   Die Karten-Chips (siehe k8915g2_pfs3820.prn) stimmen beide.  Der Code laeuft in diesem",
        ";   Bereich nicht; ein Lesefehler des Anwenders ODER ein Bitfehler im 2708 ist moeglich [?].",
        ";   Folge: der Selbsttest 'MROM' meldet am echten Geraet fuer 177 vermutlich einen Fehler, falls",
        ";   der Abzug die Hardware treu wiedergibt.  Empfehlung: 177 am Geraet ein zweites Mal lesen.",
        ";",
        "; ---- Befunde zur Hardware (aus dem Code, [ROM]; ausfuehrlich: AP-V3b) ----------------",
        ";   A8H (Port) wird mit 06H, 0EH, 87H, 8FH beschrieben: Speicherumschaltung (Gegenstueck zu",
        ";   A8H der ZRE 045-8762 [?]).  Port 61H wird mit 00H/FFH/E0H/7FH/B0H beschrieben [?].",
        ";   Bildspeicher 1000H-177FH (80 x 24); SIO-Kanal 52H/53H: 53H Bit 0 = Zeichen da, 52H = Zeichen",
        ";   (Tasteneingabe ENTER/'#'); SIO-Init ueber 5AH/53H, weitere Tore 45H/47H/55H.  K5122: Ports 10H-19H (16H Daten,",
        ";   12H Status, 10H/18H Steuerung), Meldungen 'Coldstart * Disk on A: ready', 'Drive A: not",
        ";   ready, check disk', 'Disk-error, change disk', 'No system disk, change disk',",
        ";   'Loading complete, replace disk by previous disk' / '... replace system disk'.",
        "; ============================================================================",
        "",
        "\tORG\t0000H",
    ]
    anhang = ["", "; " + "=" * 76,
              "; ANHANG: Gegenueberstellung ZRE <-> Karten-Chips (Byte-Diff, difflib-Ausrichtung)",
              "; " + "=" * 76,
              "; (ZRE-Seite hier; die Karten-Seite steht gespiegelt in k8915g2_pfs3820.prn)"]
    anhang += vergleich(roms["175"], roms["3C00"], "175", "3C00")
    anhang += vergleich(roms["177"], roms["3000"], "177", "3000")
    anhang += [";   Deutung (Beleg: die Listings): 3C00 = 175 mit anderem Anfang (Ports E0H-E4H, B1H/B3H,",
               ";   A8H = 0EH statt 06H an allen drei Stellen (Einzelbyte 06H -> 0EH oben),",
               ";   Signaturpruefung D001H, eigener Kopierer 003FH) und 'I/O' statt 'SIO'; die Tabellen am",
               ";   Ende sind um 3 bzw. 4 Byte verschoben.  3000 = 177 mit Zellen bei 0Cxx statt F7xx",
               ";   (Einzelbyte F7H -> 0CH an den Adress-Operanden), Port 61H -> E4H und einer kuerzeren",
               ";   Text-Routine (kein SIO-Init, Taste ueber E1H/E0H); sonst identisch."]
    text_zre, alle_zre = erzeuge_listing(BLOECKE_ZRE, NAMEN_ZRE, kopf, None, anhang)

    # ---------- Karte ----------
    k = []
    A = k.append
    A("; ============================================================================")
    A("; k8915g2_pfs3820.prn  -  K8915 'Generation 2': 2708-Karte PFS K3820 (16 x 1 KB), belegte Chips")
    A(";")
    A("; Quellen: doc/EPROMS/K8915G2/k8915g2_pfs3820_3C00.bin   MD5 " + md5["k8915g2_pfs3820_3C00.bin"])
    A(";          doc/EPROMS/K8915G2/k8915g2_pfs3820_3000.bin   MD5 " + md5["k8915g2_pfs3820_3000.bin"])
    A("; Erzeugt: tools/gen_k8915g2_prn.py   Plan: doc/design/24_k8915_varianten.md (AP-V3a)")
    A(";")
    A("; STATISCHE Analyse; [?] = Deutung.  Die Chip-Namen (3C00, 3000) sind die Lage RELATIV zur")
    A("; Kartenstartadresse (Wickelbruecken X8/X9, noch nicht abgelesen).  Lade = Lage im Chip")
    A("; (0000H-03FFH), Lauf = Adresse nach den Operanden des Codes: 3C00 -> Lade + FC00H,")
    A("; 3000 -> Lade + 0800H.  Das ist KEINE Aussage ueber die Lage der Karte im Adressraum.")
    A(";")
    A("; ---- Belegung der Karte (BEFUND 3) -------------------------------------------------")
    A(";   Nur 2 von 16 Chips sind programmiert; die uebrigen 14 (0000, 0400, 0800, 0C00, 1000, 1400,")
    A(";   1800, 1C00, 2000, 2400, 2800, 2C00, 3400, 3800) sind leer (alle Bytes FFH, MD5")
    A(";   " + md5["k8915g2_pfs3820_0000.bin"] + ").")
    A(";   3C00: Fassung des 175 (Urlader + Selbsttest 'MROM RAM I/O KEY CTC'); 3000: Fassung des")
    A(";   177 (Lader-Meldungen, Zellen bei 0Cxx).  Beide Chips tragen die richtige 24-Bit-Summe.")
    A(";   Der Floppy-Lader (ZRE 176, 0400H-07FFH) hat auf der Karte KEIN Gegenstueck: 3C00 springt wie")
    A(";   175 mit JP 0400H dorthin und 3000 ruft 047BH/0496H/0548H (Adressen des 176) [?].")
    A(";")
    A("; ---- Unterschiede zur ZRE (Auszug; Vollstaendiges im Anhang von k8915g2_zre.prn) ----------")
    A(";   3C00: Ports E0H-E4H (statt 61H), B1H/B3H; prueft D001H-D003H auf 'F3 ED 5E' (DI, IM 2);")
    A(";         eigener Kopierer (003FH) - kopiert wie 175 von 0000H nach FC00H; Selbsttest 'I/O'")
    A(";         (statt 'SIO': keine SIO 5AH/53H-Initialisierung im Chip), Abbruchtaste 'OFF'.")
    A(";   3000: Taste ueber IN E1H (wartet, solange Bit 3 = 1) / IN E0H (1FH -> JP 03F3H; 9DH -> weiter;")
    A(";         sonst von vorn)")
    A(";         statt SIO 52H/53H; Port E4H statt 61H; Zellen 0C00H-0FFFH statt F700H-F7FFH [?].")
    A(";   Hinweis: ein Bildschirm an E0H/E1H/E4H (ATS-Bereich E0H-E4H) und kein SIO-Init -- das")
    A(";   passt zu einer ANDEREN Ein-/Ausgabekarte als bei der ZRE (Gen 1: K7634 + K7028 [?]).")
    A("; ============================================================================")
    A("")
    A("\tORG\t0000H")
    anhang_k = ["", "; " + "=" * 76,
                "; ANHANG: leere Chips", "; " + "=" * 76]
    anhang_k.append(";   14 Chips (" + ", ".join(LEERE_CHIPS) + ") sind in der Gesamtdatei k8915g2_pfs3820_0000-3FFF.bin")
    anhang_k.append(";   leer (FFH); sie sind hier nicht aufgefuehrt (jeder Chip 1024 x FFH, MD5 "
                    + md5["k8915g2_pfs3820_0000.bin"] + ").")
    text_pfs, alle_pfs = erzeuge_listing(BLOECKE_PFS, NAMEN_PFS, k, None, anhang_k)
    # jede benannte Routine muss als Label im Listing stehen (ein Name ohne Fundstelle = Tippfehler)
    for namen, text in ((NAMEN_ZRE, text_zre), (NAMEN_PFS, text_pfs)):
        for adr, nm in namen.items():
            assert re.search(rf"^{nm}:$", text, re.M), f"Label {nm} ({adr:04X}H) fehlt im Listing"
    for n, r in (("3C00", roms["3C00"]), ("3000", roms["3000"])):
        soll = r[0x3FD] << 16 | r[0x3FE] << 8 | r[0x3FF]
        assert sum24(r) == soll, f"Chip {n}: Pruefsumme"
    return (("k8915g2_zre.prn", text_zre, BLOECKE_ZRE), ("k8915g2_pfs3820.prn", text_pfs, BLOECKE_PFS))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="nichts schreiben: bytegleich zum eingecheckten .prn und vollstaendige "
                         "Abdeckung jedes ROM-Bytes pruefen (Exit 1 bei Abweichung)")
    args = ap.parse_args()
    ok = True
    for name, text, bloecke in bauen():
        pruefe_abdeckung(text, bloecke, name)
        ziel = os.path.join(EPROMS, name)
        if args.check:
            alt = open(ziel, encoding="ascii").read() if os.path.exists(ziel) else None
            if alt != text:
                print(f"ABWEICHUNG: {os.path.relpath(ziel, ROOT)} ist nicht die Ausgabe des "
                      f"Generators (python3 tools/gen_k8915g2_prn.py ausfuehren)")
                ok = False
            else:
                print(f"ok: {name} bytegleich, {len(bloecke) * 1024} Byte abgedeckt")
        else:
            text.encode("ascii")
            open(ziel, "w", encoding="ascii").write(text)
            print(f"geschrieben: {os.path.relpath(ziel, ROOT)}  ({len(text.splitlines())} Zeilen)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
