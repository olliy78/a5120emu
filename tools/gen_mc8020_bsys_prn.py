#!/usr/bin/env python3
"""
Baut doc/mc80/bsys_2x.prn: das kommentierte Listing des Betriebssystems MC80.21/22
(ZRE K2521: S0.1-S0.3 = 0000H-0BFFH, dazu S0.4 = 2000H-23FFH auf der PFS K3820) in der
Fassung "Programmversion 12.01.84" des Nutzerhandbuchs (= MAME-Satz mc8020, BIOS "ver2":
s01 + s02_v2 + s03_v2 + mo01), AP M10 (doc/design/32_... Par. 6).

Quellen:
  * ROM-Abzuege doc/EPROMS/MC80/mc8020/ (MD5 unten festgeschrieben)
  * Original-Listing "MC 80 - Betriebssystem -" (doc/original_docs/mc80/mc80_22-bsys-doku.pdf),
    abgeschrieben in tools/mc80/bsys_2x_abschrift.txt.  Es gilt fuer 0000H-05B5H bytegleich mit
    den Abzuegen; ab 05B6H beschreibt es eine ANDERE Fassung (Magnetbandteil "MB-BSY" fuer das
    KMBG mit Taste 'LAUF', ohne Register EFH), von der es keinen Abzug gibt -> Anhang.
  * MC80.2x-Nutzerhandbuch 12/86 (tiffe.de, 43 MB; Textebene doc/mc80/mc80_2x_handbuch_ocr.txt),
    Abschnitt 4.3.2: Arbeitszellen und Sprungleiste des Magnetbandprogramms.
Kommentare in GROSSSCHRIFT = Abschrift des Originals, Kommentare in normaler Schrift = eigene
Deutung (statische Analyse, kein Emulator vorhanden), [?] = Vermutung.  Ausgabe reines ASCII.

Aufruf:
    python3 tools/gen_mc8020_bsys_prn.py            # doc/mc80/bsys_2x.prn schreiben
    python3 tools/gen_mc8020_bsys_prn.py --check    # Waechter: nichts schreiben, nur pruefen
"""
import argparse
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import z80_disasm2  # noqa: E402

EPROMS = os.path.join(ROOT, "doc/EPROMS/MC80/mc8020")
ABSCHRIFT = os.path.join(HERE, "mc80/bsys_2x_abschrift.txt")
ZIEL = os.path.join(ROOT, "doc/mc80/bsys_2x.prn")

# Fassung "ver2" (Handbuch 12.01.84): Datei, Ladeadresse, MD5
ROMS = [("s01.rom", 0x0000, "64541a0db24406353aa24a540afba277"),
        ("s02_v2.rom", 0x0400, "385c2b5391884c064b45c7278ceb5268"),
        ("s03_v2.rom", 0x0800, "0bb2cf99e29b89c42446cbb70b0bfeb7"),
        ("mo01.rom", 0x2000, "aaf41de1cccc92986e911ed666b145ec")]
# zum Fassungsvergleich im Kopf
ANDERE = {"s02.rom": ("6bc9317f5aa856b9082e62aa82394cf1", "s02_v2.rom"),
          "s02_v3.rom": ("0c83676b0ee7d2c1840c09850fb9d5e3", "s02_v2.rom"),
          "s03.rom": ("2c1e1d43b2064f5ad0f7bacd63156974", "s03_v2.rom"),
          "mo01_v2.rom": ("ed02e82f4c475ff9725fa42928a5c715", "mo01.rom")}

# --------------------------------------------------------------------------------------
# Datenbereiche: (Adresse, Laenge, Art).  Alles andere in den belegten Bereichen ist Code.
#   fill = FFH-Fuellung, db = Bytes, dw = Wort, txt = Text (Bit 7 = Anfang/Ende, s. TXS)
# --------------------------------------------------------------------------------------
DATEN = [
    (0x000E, 2, "fill"), (0x0016, 2, "fill"), (0x001E, 2, "fill"), (0x0026, 2, "fill"),
    (0x002E, 2, "fill"), (0x0036, 2, "fill"),
    (0x003B, 2, "dw"), (0x003D, 2, "dw"), (0x003F, 2, "dw"), (0x0041, 2, "dw"),
    (0x0043, 8, "db"), (0x004B, 8, "db"), (0x0053, 4, "db"), (0x0057, 1, "db"),
    (0x0058, 1, "db"), (0x0059, 1, "db"), (0x005A, 1, "db"), (0x005B, 1, "db"),
    (0x005C, 1, "db"), (0x005D, 1, "db"), (0x005E, 1, "db"), (0x005F, 2, "db"),
    (0x0061, 2, "db"), (0x0063, 2, "db"), (0x0065, 1, "db"),
    (0x0305, 14, "db"), (0x0313, 14, "db"), (0x0321, 14, "db"), (0x032F, 14, "db"),
    (0x0598, 3, "db"), (0x059B, 1, "db"), (0x059C, 3, "txt"), (0x059F, 1, "db"),
    (0x05A0, 2, "dw"), (0x05A2, 1, "db"), (0x05A3, 3, "txt"), (0x05A6, 1, "db"),
    (0x05A7, 2, "dw"), (0x05A9, 1, "db"), (0x05AA, 9, "txt"), (0x05B3, 1, "db"),
    (0x05B4, 2, "dw"), (0x05B6, 0x4A, "fill"),
    (0x0BFC, 4, "fill"),
    (0x2138, 0x2F, "txt"), (0x2167, 0x0D, "txt"), (0x2174, 0x09, "txt"), (0x217D, 0x14, "txt"),
    (0x2191, 0x1D, "txt"), (0x21AE, 0x0D, "txt"), (0x21BB, 5, "db"), (0x21C0, 0x0F, "txt"),
    (0x21CF, 0x0F, "txt"), (0x21DE, 0x05, "txt"), (0x21E3, 0x0F, "txt"), (0x21F2, 0x11, "txt"),
    (0x2203, 0x1FD, "fill"),
]
BEREICHE = [(0x0000, 0x0C00), (0x2000, 0x2400)]

# --------------------------------------------------------------------------------------
# Eigene Marken (ab 05B6H und S0.4).  Die Sprungleiste 062DH-065CH traegt die Namen des
# Handbuchs (Abschnitt 4.3.2.3); die angesprungene Ausfuehrung heisst NAME0.
# --------------------------------------------------------------------------------------
MARKEN_EIGEN = {
    0x0600: "BAND",
    0x062D: "BLE", 0x0630: "MRD", 0x0633: "MWR", 0x0636: "MCL", 0x0639: "RCK", 0x063C: "AUS",
    0x063F: "LIN", 0x0642: "WIN", 0x0645: "WVO", 0x0648: "GRZ", 0x064B: "R1S", 0x064E: "LES",
    0x0651: "LWK", 0x0654: "PIN", 0x0657: "VGL", 0x065A: "ZEI",
    0x092A: "BLE0", 0x065D: "MRD0", 0x066F: "MWR0", 0x0723: "MCL0", 0x0B4F: "RCK0",
    0x088F: "AUS0", 0x07A0: "LIN0", 0x0A60: "WIN0", 0x0A97: "WVO0", 0x0784: "GRZ0",
    0x07CE: "R1S0", 0x07ED: "LES0", 0x089F: "LWK0", 0x08DA: "PIN0", 0x09AC: "VGL0",
    0x091F: "ZEI0",
    0x0756: "FDATM", 0x0769: "BFEHL", 0x08EF: "KAS", 0x090E: "SSP", 0x0A26: "NAMU",
    0x0A3F: "CRR", 0x0A8B: "WARB", 0x0B15: "BLP", 0x0B67: "IPB", 0x0B81: "ISY",
    0x0BA2: "ILB", 0x0BD1: "ISB",
    0x2000: "MBM", 0x20EA: "BLA", 0x2102: "IPA", 0x212C: "IVL",
    0x2138: "TMB", 0x2167: "TMS", 0x2174: "TML", 0x217D: "TBL", 0x2191: "TAD", 0x21AE: "TNA",
    0x21BB: "PIOT", 0x21C0: "TKE", 0x21CF: "TKG", 0x21DE: "TFD", 0x21E3: "TLO", 0x21F2: "TBF",
}

# Eigene Kommentare (normale Schrift).  Adressen bis 05B5H nur dort, wo das Original nichts
# sagt oder die Fassung abweicht.
K_EIGEN = {
    0x0008: "RST-Aussprung RST 08H -> 6007H (Handbuch 6.2: RST1..RST6 -> 6007H..6016H, Anwender)",
    0x0038: "RST 38H -> 2C00H (Testmodus des Objektcodeeditors, Handbuch 6.2)",
    0x0043: "0D08-0D0F: IM-2-Vektoren der MBI-CTC (Kanal 0..3), vom Bandprogramm besetzt",
    0x004B: "0D10-0D17: Adressen der Analog-BWS fuer Zeile 2/4/6/8 (0000 = Textzeile)",
    0x0066: "NMI-Einsprung -> 6004H (Handbuch 6.2)",
    0x00A8: "CTC 1 (DPL): 47H/D0H Zaehler ohne Interrupt",
    0x0157: "Ende der Analogzeile: Zweitregister zurueck, weiter bei TI0",
    0x05A0: "STARTADR. \"BAND\": in dieser Fassung 0600H (Original-Listing: 05B6H, MB-BSY)",
    0x05B6: "frei (FFH); im Original-Listing steht hier das Menue MEN der KMBG-Fassung",
    # ---- S0.2/S0.3: Magnetbandprogramm (Handbuch 4.3.2, Programmversion 12.01.84) ----
    0x0600: "Menuepunkt BAND: Untermenue S0.4 (MBM) fragt Betriebsart und Parameter ab",
    0x0603: "Cy=1 (OFF) -> Neustart",
    0x0606: "MBB (0DFFH): 00 Schreiben, 01 Lesen, 02 Loeschen",
    0x060E: "MWR: Datei schreiben",
    0x0611: "Cy=0: gut",
    0x0613: "Cy=1,Z=1: vorhergehender Block fehlt -> 'BLOCK xxxx FEHLT'",
    0x0616: "Cy=1,Z=0: Schreibversuch innerhalb einer Datei -> 'FDAT'",
    0x061F: "MRD: Datei lesen; Cy=1 -> 'BLOCK xxxx FEHLT'",
    0x0628: "MCL: Kassette loeschen",
    0x062B: "zurueck ins Magnetband-Menue",
    0x062D: "Sprungleiste des Handbuchs (4.3.2.2/4.3.2.3): BLE = eine Blockstruktur lesen",
    0x0630: "MRD Magnetband lesen (RAM,BLZ,BLN,MBB,NAZ,BYE)",
    0x0633: "MWR Magnetband schreiben",
    0x0636: "MCL Magnetband loeschen (mit Bildschirmdialog)",
    0x0639: "RCK Rueckspulen bis Bandanfang, danach 'Lauf'",
    0x063C: "AUS Endabschaltung Laufwerk",
    0x063F: "LIN Initialisierung Lesen",
    0x0642: "WIN Initialisierung Schreiben",
    0x0645: "WVO Vorbereitung Schreiben (Art in SPH)",
    0x0648: "GRZ Grundzustand Pufferspeicher",
    0x064B: "R1S Ruecksetzen um ca. 1 s",
    0x064E: "LES Leseprogramm nach BLN,BLZ,RAM,BYE,MBB,NAZ",
    0x0651: "LWK Laufwerk einschalten (Register E = Steuerdaten fuer PIO B)",
    0x0654: "PIN Initialisierung Steuerport (MBI-PIO Kanal B)",
    0x0657: "VGL Blocknummernvergleich und Suchlauf (BC = gesuchte BLN)",
    0x065A: "ZEI Zeitschleife (A aussen, C mitte, B=256 innen)",
    0x065D: "MRD0: Grundzustand, LIN, 1 s zurueck, LES, AUS",
    0x066F: "MWR0: Schreiben einer Datei",
    0x0673: "BLN = 0001 ?",
    0x0679: "nein: Bandstelle suchen",
    0x067B: "ja: zum Bandanfang",
    0x067E: "Vorluecke: 1400H Byte-Interrupts (ca. 27 s bei 192 Byte/s [?]; Handbuch 'ca. 30 s')",
    0x0684: "Vektor CTC3 (0D0EH) = IVL (zaehlt 0DF9H herunter)",
    0x068A: "WIN: Schreiben einrichten",
    0x0690: "SPH Bit 0: Bandmarke vorbereiten",
    0x0698: "warten, bis die Vorluecke abgelaufen ist",
    0x069F: "PIO B: Daten schreiben (0AH)",
    0x06A3: "Praeambel AAH in das Schieberegister",
    0x06A7: "Vektor CTC3 = ISB (Byte schreiben)",
    0x06AD: "warten, bis der Block heraus ist (BYZ = 0)",
    0x06B3: "SPH Bit 1: Kennzeichnungsblock (Name, BLN FFFFH)",
    0x06BE: "Datenbloecke",
    0x06C7: "BLN+1, BLZ-1, Protokoll; Z = alle Bloecke geschrieben",
    0x06CF: "SPH Bit 0: abschliessende Bandmarke",
    0x06DD: "warten auf SPH Bit 2 (Nachpause fertig, Laufwerk aus)",
    0x06E4: "BLN > 1: 1 s zurueck, die VORHERGEHENDE Blocknummer suchen",
    0x06F1: "nicht gefunden: AUS, Cy=1 (Z aus LES)",
    0x06F6: "naechste Struktur lesen: muss eine Bandmarke sein",
    0x06FC: "SPH Bit 6 = Bandmarke gelesen",
    0x0700: "nein: Cy=1, Z=0 = Schreibversuch in einer Datei (FDAT)",
    0x0705: "MBI-CTC 3 ruecksetzen",
    0x070C: "Pausenzaehler PAZ (0DE8H) = 51H Byte-Zeiten",
    0x0711: "Vektor CTC3 = IPA (Pause), dann wie beim Bandanfang weiter",
    0x0723: "MCL0: 'MB-LOESCHEN ?', Start mit CTR+L (Code 0CH), Abbruch OFF",
    0x0740: "Laufwerk einschalten, zum Bandanfang",
    0x0746: "PIO B 02H: Schreiben mit Pausenpegel = Loeschen bis OFF",
    0x0756: "FDATM: Laufwerk aus, 'FDAT', auf Taste warten",
    0x0769: "BFEHL: 'BLOCK xxxx FEHLT' mit BLN in Zeile 1, auf Taste warten",
    0x0784: "GRZ0: Puffer 0E00-0EFFH = 0, PUZ = 0E00H, SPH = 0, CRC = 0, BYZ = 0",
    0x07A0: "LIN0: Laufwerk ein (E = 03H: Lesen, Capstan)",
    0x07A8: "Vektor CTC3 = ISY (Synchronisations-/Blockendeinterrupt)",
    0x07AE: "MBI-PIO A: Betriebsart 1 (Byte-Eingabe)",
    0x07B2: "MBI-CTC Vektorbasis 08H (Tabelle 0D08-0D0FH)",
    0x07B6: "CTC 1 und CTC 2: 47H = Zaehler ohne Interrupt, Zeitkonstante 4BH [?Zweck]",
    0x07C2: "CTC 3: F7H = Zaehler mit Interrupt, steigende Flanke, Zeitkonstante 1",
    0x07CE: "R1S0: Rueckspulen (EFH = 0AH), dann kurz Vorlauf (06H) zum Bremsen [?], dann 13H",
    0x07D2: "Laufwerksregister EFH: 0AH = schneller Ruecklauf [?]",
    0x07DD: "06H = schneller Vorlauf [?]",
    0x07E8: "13H = Wiedergabe/Lauf 4,76 cm/s [?]",
    0x07ED: "LES0: Blockweise lesen, Fehlversuchszaehler FEZ = 10H",
    0x07F5: "naechste Blockstruktur",
    0x07FB: "Fehlversuche erschoepft: Cy=1",
    0x0801: "SPH Bit 5: Datenblock gelesen?",
    0x080A: "CRC ueber BLN + BYE Daten + CRC (BYE+4 Byte ab 0E01H): Rest 0 = gut",
    0x0821: "CRC-Rest <> 0: neu lesen",
    0x0828: "BLN = FFFFH: Kennzeichnungsblock -> nach NAZ umlagern",
    0x0834: "gelesene mit gesuchter BLN vergleichen, ggf. spulen",
    0x083D: "beim Schreiben (MBB Bit 0 = 0) endet LES an der gefundenen Stelle, A = 08H",
    0x084E: "'RD' in Zeile 2 Spalte 10 (0412H = Codes 'R','D')",
    0x0854: "Daten in den Hauptspeicher (RAM, 0DFBH) umlagern",
    0x0866: "Speicherstand in Zeile 2 ab Spalte 1CH",
    0x0877: "BLZ-1, Protokoll in Zeile 2 ab Spalte 12H",
    0x088F: "AUS0: PIO B 01H (Lesen, Capstan aus), CTC 2 ruecksetzen, PIO-B-Interrupt aus, EFH = 0",
    0x089F: "LWK0: Steuerport einrichten",
    0x08A4: "MBI-PIO B Bit 7 = 1: Kassette eingelegt",
    0x08AC: "beim Schreiben und Loeschen (MBB Bit 0 = 0):",
    0x08B3: "Bit 5 = 1: Schreibsperre (Riegel ausgebrochen)",
    0x08BA: "Steuerdaten E an PIO B (Bit 0 Lesen, Bit 1 Capstan, Bit 3 Daten; Handbuch LWK)",
    0x08C4: "EFH = 11H, dann PIO-B-Interrupt frei (83H), EFH = 13H [Magneten, ?]",
    0x08DA: "PIN0: Vektor 0D2CH = IPB, PIO B mit Tabelle PIOT einrichten (Interrupt noch gesperrt)",
    0x08EF: "KAS: 'KAS. EINLEGEN', warten auf PIO B Bit 7 = 1",
    0x090E: "SSP: 'KAS. GESPERRT', warten auf PIO B Bit 5 = 0",
    0x091F: "ZEI0: Wartezeit A x C x 256 DJNZ (C = 0 zaehlt 256)",
    0x092A: "BLE0: auf das Blockende warten (SPH Bit 7 aus ISY), Zeitablauf ca. 35 s",
    0x0945: "Zeitablauf: FEZ Bit 7 = 0 -> zum Bandanfang; sonst ca. 10 s zurueck",
    0x0966: "Blockende erkannt: Bytezahl BYZ (0DF0H) pruefen",
    0x0969: "4 Byte und letztes = AAH: Bandmarke (AA 00 00 AA) -> SPH Bit 6",
    0x0987: "BYE+6 Byte und letztes = AAH: Block -> SPH Bit 5, FEZ Bit 7",
    0x09AC: "VGL0: gelesene BLN (0E01H, hoeherwertiges Byte zuerst) mit BC vergleichen",
    0x09B4: "Protokoll in Zeile 1 Spalte 0DH (nicht bei MBB Bit 7)",
    0x09CA: "gelesen > gesucht: zurueckspulen, Dauer ~ Differenz",
    0x09F2: "gelesen < gesucht: im Fangbereich (20H Bloecke) mit Lauf weiter (A=1, NZ)",
    0x09FF: "sonst vorspulen, Dauer ~ Differenz",
    0x0A26: "NAMU: beim Lesen Kennzeichnungsblock (BYE Byte ab 0E03H) nach (NAZ)",
    0x0A3F: "CRR: CRC-16, rechtsschiebend, Polynom A001H (x16+x15+x2+1), Byte in C, Akku 0DE5H",
    0x0A60: "WIN0: MBI-PIO A Ausgabe (0FH), Praeambel AAH bereitstellen",
    0x0A69: "PIO B 02H: Schreiben, Capstan, Pausenpegel",
    0x0A6D: "CTC 0: Vektor 08H, 47H/08H = Zaehler ohne Interrupt, alle 8 Bit [?]",
    0x0A79: "CTC 3: C7H/01H = Zaehler mit Interrupt bei jedem Impuls (je Byte [?])",
    0x0A81: "CTC 2: 07H/32H = Zeitgeber Vorteiler 16, ZK 50: 2457600/16/50 = 3072 Hz [Bitzellentakt?]",
    0x0A8B: "WARB: warten bis BYZ (0DF0H) = 0",
    0x0A97: "WVO0: Puffer fuellen; PUZ = 0E01H",
    0x0AA2: "SPH Bit 0: Bandmarke = 00 00 AA (mit Praeambel AA: AA 00 00 AA), BYZ = 4",
    0x0AB6: "Block: BYZ = BYE+6 (AA, BLN 2, Daten, CRC 2, AA)",
    0x0AC9: "SPH Bit 1: Kennzeichnungsblock, BLN = FFFFH, Daten aus (NAZ)",
    0x0AE1: "Datenblock: BLN hoeherwertiges Byte zuerst, Daten aus (RAM), RAM += BYE",
    0x0AF9: "CRC ueber BLN + Daten",
    0x0B09: "CRC niederwertiges Byte zuerst, dann Postambel AAH",
    0x0B15: "BLP: Protokoll 'WR' (1217H), RAM, BLN+1, BLZ-1; Z = BLZ abgelaufen",
    0x0B4F: "RCK0: Ruecklauf, warten bis PIO B Bit 6 = 0 (Bandende), dann 13H",
    0x0B67: "IPB: Interrupt MBI-PIO B (Vektor 0D2CH)",
    0x0B69: "Bit 7 = 0: Kassette ausgeworfen -> AUS, Ruecksprung nach 0600H (Handbuch 2.7)",
    0x0B7A: "sonst (Bit 6: Bandende): EFH = 02H, Endabschaltung [?]",
    0x0B81: "ISY: Interrupt MBI-CTC 3 beim Lesen = Synchronisation/Blockende",
    0x0B84: "Vektor CTC 0 (0D08H) = ILB",
    0x0B8A: "PIO B Bit 4 (Pausenpegel) waehlt 8 oder 7 Bit bis zum ersten Byte",
    0x0B99: "SPH Bit 7: Blockende erkannt",
    0x0BA2: "ILB: Interrupt MBI-CTC 0 nach 8 Bit: ein Byte lesen",
    0x0BB1: "Byte aus dem Schieberegister (MBI-PIO A)",
    0x0BB7: "Ringpuffer 0E00-0F1FH",
    0x0BC5: "BYZ + 1",
    0x0BD1: "ISB: Interrupt MBI-CTC 3 beim Schreiben: naechstes Byte ausgeben",
    0x0BDE: "BYZ - 1",
    0x0BE9: "Block heraus: Vektor CTC3 = IPA, PIO B 02H (Pausenpegel), PAZ = 51H",
    # ---- S0.4 (mo01) ----
    0x2000: "MBM: Magnetband-Menue (Handbuch 4.3): NAZ = 0C60H (Zeilen 4-7 des BWS)",
    0x2011: "Markierung '>' auf Zeile 1..3",
    0x2025: "Taste (Cy = 1: neu gedrueckt)",
    0x2031: "OFF: Cy = 1 zurueck",
    0x2049: "MBB = Zeile-1 (0 Schreiben, 1 Lesen, 2 Loeschen)",
    0x204D: "Loeschen: ohne Parameter",
    0x205F: "BYE = 0080H (MC-80-Datenformat)",
    0x2065: "'AB BLOCK-NR.:' -> BLN, Anzeige Zeile 0 Spalte 0DH",
    0x207C: "'ADRESSE VON .... BIS ....' -> RAM (0DFBH)",
    0x2096: "Endadresse",
    0x20A0: "BLA: Blockzahl BC, aufgerundete Endadresse DE",
    0x20A7: "END (0DF9H) = DE-1, Anzeige Zeile 0 Spalte 1CH",
    0x20B3: "BLZ (0DF5H) = BC, Anzeige Spalte 12H",
    0x20BE: "beim Schreiben: 'NAME :' und Kennzeichnungsblock auf Zeile 4-7 eingeben",
    0x20D0: "Dateiangaben aus Zeile 0 (ab Spalte 0DH) an das Ende von Zeile 6 (0CCDH)",
    0x20DB: "Zeile 1 loeschen, A = 0, Cy = 0",
    0x20EA: "BLA: Bloecke zu BYE Byte von HL bis DE zaehlen (BC), DE = aufgerundetes Ende+1",
    0x2102: "IPA: Pausen-Interrupt (CTC 3): PAZ-1; bei 0 naechster Block oder Ende",
    0x210B: "PIO B 0AH: Daten schreiben, Vektor CTC3 = ISB, Praeambel AAH",
    0x2119: "kein Block vorbereitet (BYZ = 0): AUS, SPH Bit 2 = fertig",
    0x212C: "IVL: Interrupt CTC 3 waehrend der Vorluecke: 0DF9H - 1",
    0x2138: "'MAGNETBAND :' / SCHREIBEN / LESEN / LOESCHEN (8CH = Bild loeschen)",
    0x2167: "'MB-SCHREIBEN'",
    0x2174: "'MB-LESEN'",
    0x217D: "' AB BLOCK-NR.:....' (82H = Displayanfang, 0AH = Zeile tiefer)",
    0x2191: "'ADRESSE VON .... BIS ....', FFH = Textende",
    0x21AE: "'NAME :'",
    0x21BB: "PIOT: an MBI-PIO B (FFH): Vektor 2CH, Betriebsart 3, E/A-Maske F0H (7-4 Eingang), "
            "Int-Steuerwort 17H (gesperrt, ODER, low-aktiv), Maske 3FH (Bit 7,6)",
    0x21C0: "'KAS. EINLEGEN'",
    0x21CF: "'KAS. GESPERRT'",
    0x21DE: "'FDAT' (98H = Zeile loeschen)",
    0x21E3: "'MB-LOESCHEN ?'",
    0x21F2: "'BLOCK      FEHLT'",
}


# --------------------------------------------------------------------------------------
def lade():
    mem = {}
    for f, base, md5 in ROMS:
        d = open(os.path.join(EPROMS, f), "rb").read()
        assert len(d) == 1024, f"{f}: Laenge {len(d)}"
        assert hashlib.md5(d).hexdigest() == md5, f"{f}: MD5 weicht ab"
        for i, b in enumerate(d):
            mem[base + i] = b
    return mem


def lade_abschrift():
    marken, kom = {}, {}
    for z in open(ABSCHRIFT, encoding="ascii"):
        z = z.rstrip("\n")
        if not z or z.startswith("#"):
            continue
        a, m, k = z.split("|", 2)
        a = int(a, 16)
        if m:
            marken[a] = m
        if k:
            kom[a] = k
    return marken, kom


def vergleich():
    """Abweichungen der anderen MAME-Fassungen, als Kopfzeilen."""
    z = []
    for f, (md5, gegen) in ANDERE.items():
        a = open(os.path.join(EPROMS, f), "rb").read()
        assert hashlib.md5(a).hexdigest() == md5, f"{f}: MD5 weicht ab"
        b = open(os.path.join(EPROMS, gegen), "rb").read()
        base = dict((r[0], r[1]) for r in ROMS)[gegen]
        d = [i for i in range(1024) if a[i] != b[i]]
        z.append(f";   {f:12s} gegen {gegen:11s}: {len(d):4d} Byte verschieden"
                 + (f", {base + d[0]:04X}H-{base + d[-1]:04X}H" if d else ""))
    return z


KOPF = """; ============================================================================
; bsys_2x.prn -- Betriebssystem MC80.21/22 (VEB Elektronik Gera), kommentiert
; ERZEUGT von tools/gen_mc8020_bsys_prn.py -- nicht von Hand aendern (AP M10, 2026-10-14)
; ============================================================================
;
; Fassung: "Programmversion 12.01.84" (Nutzerhandbuch 12/86, Abschnitt 4.3.2) =
;   MAME mc8020 BIOS "ver2": s01 (0000H) + s02_v2 (0400H) + s03_v2 (0800H) + mo01 (2000H).
;   Nur diese Fassung passt zu den Arbeitszellen des Handbuchs (BLZ 0DF5H, BYE 0DF7H 2 Byte,
;   PAZ 0DE8H, SPH 0DF2H).  Andere Abzuege:
{vergleich}
;   ver1 (s02+s03+mo01_v2) ist dasselbe Programm mit anders gelegten Zellen (CRC 0DEFH,
;   FEZ 0DEEH, PAZ 0DECH, BYZ 0DF1H, SPH 0DF3H, PUZ 0DF4H, BLZ 0DF6H, BYE nur 1 Byte bei 0DF8H)
;   und passt damit NICHT zum Handbuch.  s02_v3 (MAME ver3) enthaelt bei 05B6H-05FFH das Menue
;   MEN der KMBG-Fassung des Original-Listings (bytegleich), ab 0600H aber den Bandteil von s02:
;   MEN springt nach 060DH/068AH/0691H, wo in s02_v3 anderer Code steht -> unstimmiger Satz,
;   vermutlich Mischabzug [?].
;
; Kommentare: GROSSSCHRIFT = Abschrift des Original-Listings "MC 80 - Betriebssystem -"
;   (Module S0.1-S0.3, Z-Nr. 50001:4100.00, 29 Blatt, doc/original_docs/mc80/
;   mc80_22-bsys-doku.pdf; Abschrift tools/mc80/bsys_2x_abschrift.txt).  Das Listing stimmt
;   mit den Abzuegen von 0000H bis 05B5H ueberein (bis auf 05A0H, s. dort); ab 05B6H gehoert
;   es zu einer ANDEREN Fassung ohne Abzug -> Anhang am Ende.  Marken bis 05B5H = Original.
;   normale Schrift = eigene Deutung (statisch, kein Emulator), [?] = Vermutung.
;   Mnemonics in Zilog-Schreibweise (das Original schreibt JMP/CMP/JRNZ/CAZ ...).
;
; ---- Speicher (Handbuch 3.2.1) ----------------------------------------------------------
;   0000-0BFF  ROM S0.1-S0.3 auf der ZRE K2521      0C00-0CFF  BWS 8 x 32 (ZRE-RAM)
;   0D00-0D2B  Systemzellen, IM-2-Tabelle (I=0DH)   0D2C-0DEF  Vektoren Anwender (0D2C MBI-PIO B)
;   0DE5-0DFF  Zellen Magnetband                    0E00-0EFF  Magnetbandpuffer
;   0F00-0FFF  Stapel (SP = 1000H)                  2000-23FF  S0.4 (PFS K3820, mo01)
;   2400-27FF  frei (Copy-Modus, Drucker/Lochband)  2800-2BFF  EPROM-Programmierung S2 (mo03)
;   2C00-3FFF  Objektcodeeditor S1 (mo04-mo08)      4000-5FFF  EPAS/BASIC (mo09-mo16)
;   6000-7FFF  Prozesssteuerprogramm (6000=00, 6001=JP: Start nach RESET)
;   C000-FFFF  OPS K3525 16 KB (F020: Markentabelle des Editors)
;
; ---- Systemzellen 0D00H-0D2BH (aus TII kopiert) -------------------------------------------
;   0D00 DPI (DPL-CTC 0)  0D02 TI1 (DPL-CTC 1)  0D04 TSI (DPL-CTC 2)  0D06 BREAK (DPL-CTC 3)
;   0D08-0D0F MBI-CTC 0-3  0D10-0D17 Analog-BWS-Adressen  0D18-0D1C Tabulatoren
;   0D1D KPS (Kursorposition, Low = BWS-Zeiger)  0D1E Zeichen unter dem Kursor
;   0D1F Kursorblinkzaehler  0D20 Repetierzaehler  0D21-0D27 Tastenspeicher (7 Zeilen)
;
; ---- Arbeitszellen Magnetband (Handbuch 4.3.2.1) --------------------------------------------
;   0DE5 CRC   0DE7 FEZ   0DE8 PAZ   0DEA NAZ   0DF0 BYZ   0DF2 SPH   0DF3 PUZ   0DF5 BLZ
;   0DF7 BYE   0DF9 END   0DFB RAM   0DFD BLN   0DFF MBB
;   SPH Bit 0 Bandmarke, 1 Kennzeichnungsblock, 2 Datei fertig, 5 Block gelesen,
;       6 Bandmarke gelesen, 7 Blockende erkannt (eigene Deutung aus dem Code)
;
; ---- E/A (Handbuch 3.2.2, 4.3.2.1; Original-Listing) ------------------------------------
;   01H/02H  Schreibfreigabe/-sperre RAM, 1-KB-Bereich auf A10-A15 (OUT (n),A: A auf A8-A15)
;   80H-83H  ZRE-CTC, 84H-87H ZRE-PIO (A Daten, B Daten, A Steuer, B Steuer) -- Anwender
;   EFH      MBI: Register Laufwerksteuerung (nur schreiben)
;   F0H-F3H  DPL-CTC 0-3;  F4H/F5H DPL-PIO A Daten/Steuer;  F6H/F7H DPL-PIO B Daten/Steuer
;   F8H-FBH  MBI-CTC 0-3;  FCH/FDH MBI-PIO A Daten/Steuer;  FEH/FFH MBI-PIO B Daten/Steuer
;   E0H-E8H  EPROM-Programmiereinheit (nur S2/mo03, hier nicht benutzt)
; ============================================================================
"""


def db_text(bs):
    """Bytes als DB: druckbare Laeufe (20H-7EH, ohne ') in Hochkommas, Rest hex."""
    teile, lauf = [], ""
    for b in bs:
        if 0x20 <= b < 0x7F and b != 0x27:
            lauf += chr(b)
        else:
            if lauf:
                teile.append(f"'{lauf}'")
                lauf = ""
            teile.append(f"{b:02X}H" if b < 0xA0 else f"0{b:02X}H")
    if lauf:
        teile.append(f"'{lauf}'")
    return "DB\t" + ",".join(teile)


def hexzahl(v, n=2):
    s = f"{v:0{n}X}H"
    return "0" + s if s[0] in "ABCDEF" else s


def erzeuge():
    mem = lade()
    o_marken, o_kom = lade_abschrift()
    marken = {a: m for a, m in o_marken.items() if a < 0x05B6}
    for a, m in MARKEN_EIGEN.items():
        assert a not in marken, f"Marke doppelt bei {a:04X}"
        marken[a] = m
    kom = {}
    for a, k in o_kom.items():
        if a < 0x05B6:
            kom[a] = k
    for a, k in K_EIGEN.items():
        kom[a] = (kom[a] + "  -- " + k) if (a in kom and a != 0x05A0) else k

    daten = {a: (n, art) for a, n, art in DATEN}
    # Abdeckung zerlegen: Code zwischen den Datenstuecken
    stuecke = []
    for lo, hi in BEREICHE:
        a = lo
        while a < hi:
            if a in daten:
                n, art = daten[a]
                stuecke.append(("daten", a, n, art))
                a += n
                continue
            nxt = min([d for d in daten if a < d < hi] + [hi])
            stuecke.append(("code", a, nxt - a, None))
            a = nxt
    # Code dekodieren, Sprungziele sammeln
    dis = z80_disasm2.Z80Disassembler(bytes(mem.get(i, 0xFF) for i in range(0x2400)), 0)
    befehle = []
    ziele = set()
    for art, a, n, _ in stuecke:
        if art != "code":
            continue
        p = a
        while p < a + n:
            mn, ln, tg, _end = dis.decode_one(p)
            assert ln > 0 and p + ln <= a + n, f"Befehl bei {p:04X} ragt aus dem Codebereich"
            befehle.append((p, ln, mn, tg))
            if mn.split()[0] in ("JP", "JR", "CALL", "DJNZ"):
                ziele.update(t for t in tg if t in mem)
            p += ln
    for t in sorted(ziele):
        marken.setdefault(t, f"L{t:04X}")
    bef = {p: (ln, mn, tg) for p, ln, mn, tg in befehle}

    def name(mn, tg):
        op = mn.split()[0]
        if op in ("JP", "JR", "CALL", "DJNZ") and tg:
            t = tg[0]
            if t in marken:
                return mn.replace(f"{t:04X}H", marken[t])
        m = re.match(r"^(LD (?:HL|DE|BC),)([0-9A-F]{4})H$", mn)
        if m and int(m.group(2), 16) in marken and int(m.group(2), 16) in daten:
            return m.group(1) + marken[int(m.group(2), 16)]
        return mn

    def hexb(a, n):
        return " ".join(f"{mem[a + i]:02X}" for i in range(n))

    benutzt = set()

    def zeile(a, n, mn):
        k = kom.get(a, "")
        if k:
            benutzt.add(a)
        z = f"{a:04X}  {hexb(a, n):<12}\t{mn}"
        return z + (f"\t\t; {k}" if k else "")

    abschn = {0x0000: "S0.1: Neustart, RST-Leiste, Systemzellen (TII), Initialisierung",
              0x00C4: "S0.1: Display- und Tastaturinterrupts der DPL",
              0x015B: "S0.1: Displayprogramme (BSN, BSS, BSL, BKS, BKL)",
              0x0281: "S0.1: Tastaturabfrage TST, Tastentabelle TTA",
              0x033D: "S0.1: Rollen, Zeileneingabe, Hex-Ein-/Ausgabe",
              0x0465: "S0.2: Texte, BSA, Schreibsperre, INIT, Menueverwaltung BSK",
              0x0598: "S0.2: Menuetabelle 1 (rueckwaerts gelesen ab 05B5H)",
              0x0600: "S0.2/S0.3: Magnetbandprogramm (Programmversion 12.01.84)",
              0x0B67: "S0.3: Interruptroutinen des Magnetbandprogramms",
              0x2000: "S0.4 (mo01): Magnetband-Menue, Pausen-Interrupts, Texte"}
    out = []
    for art, a, n, extra in stuecke:
        if a in abschn:
            out.append("")
            out.append(f"; ---- {abschn[a]} ".ljust(78, "-"))
            if a == 0x2000:
                out.append("\tORG\t2000H")
        if art == "code":
            p = a
            while p < a + n:
                ln, mn, tg = bef[p]
                if p in marken:
                    out.append(f"{marken[p]}:")
                out.append(zeile(p, ln, name(mn, tg)))
                p += ln
            continue
        if a in marken:
            out.append(f"{marken[a]}:")
        bs = [mem[a + i] for i in range(n)]
        if extra == "fill":
            assert all(b == 0xFF for b in bs), f"Fuellung {a:04X} nicht FFH"
            out.append(zeile(a, min(n, 4), "DB\t" + ",".join(["0FFH"] * min(n, 4))))
            if n > 4:
                out.append(f"\tDS\t{n - 4},0FFH\t\t; bis {a + n - 1:04X}H FFH")
                # DS-Zeile traegt keine Bytes; Abdeckung unten getrennt geprueft
        elif extra == "dw":
            out.append(zeile(a, 2, f"DW\t{hexzahl(bs[0] | bs[1] << 8, 4)}"))
        elif extra == "db":
            out.append(zeile(a, n, "DB\t" + ",".join(hexzahl(b) for b in bs)))
        else:  # txt: je Zeile hoechstens 16 Byte
            for i in range(0, n, 16):
                out.append(zeile(a + i, min(16, n - i), db_text(bs[i:i + 16])))
    starts = set(bef) | set(daten)
    falsch = sorted(a for a in marken if a not in starts)
    assert not falsch, "Marken nicht an Befehls-/Datenanfang: " + ", ".join(f"{x:04X}" for x in falsch)
    verwaist = sorted(set(kom) - benutzt)
    assert not verwaist, "Kommentare ohne Zeile: " + ", ".join(f"{x:04X}" for x in verwaist)

    # Anhang: KMBG-Fassung des Original-Listings
    anh = ["", "; " + "=" * 76,
           "; ANHANG: Original-Listing ab 05B6H -- Fassung 'MB-BSY' (KMBG), KEIN ABZUG vorhanden",
           "; Marken und Kommentare wie im Original; [...] = E/A-Befehle der Zeile, abgeschrieben.",
           "; Diese Fassung kennt KEIN Register EFH: das Laufwerk wird nur ueber die MBI-PIO B (FEH)",
           "; geschaltet und auf die Taste 'LAUF' am Geraet gewartet (KOM, PIO B Bit 2).  Zellen:",
           ";   0DF5 BLN, 0DF7 BLZ (1 Byte), 0DF8 RAM, 0DFA SPH-1, 0DFB SPH, 0DFC SPH+1 Pufferzeiger,",
           ";   0DFE SPH+3 CRC; Puffer 0E00 (SPH+5)=00, 0E01 BLN, 0E03 Daten, 0E83 CRC, 0E85 AAH",
           ";   (Blatt 29 'LEGENDE ZUM MB-BSY').  Blockformat und CRC-Routine wie in ver2.",
           "; " + "=" * 76]
    for a in sorted(o_kom.keys() | o_marken.keys()):
        if a < 0x05B6:
            continue
        m = o_marken.get(a, "")
        k = o_kom.get(a, "")
        anh.append(f";  {a:04X}  {m + (':' if m else ''):6s} {k}".rstrip())
    anh += ["", "\tEND"]

    text = KOPF.replace("{vergleich}", "\n".join(vergleich())) + "\n\tORG\t0000H\n" \
        + "\n".join(out) + "\n".join(anh) + "\n"
    return text, mem


BYTE_RE = re.compile(r'^([0-9A-F]{4})  ((?:[0-9A-F]{2} ?)+?)\s*\t')
DS_RE = re.compile(r'^\tDS\t(\d+),0FFH\t\t; bis ([0-9A-F]{4})H')


def pruefe(text, mem):
    belegt = {}
    letzte = None
    for z in text.splitlines():
        m = BYTE_RE.match(z)
        if m:
            a = int(m.group(1), 16)
            hs = m.group(2).split()
            for i, h in enumerate(hs):
                assert a + i in mem, f"Byte {a + i:04X} ausserhalb der Abzuege"
                assert a + i not in belegt, f"Byte {a + i:04X} doppelt"
                assert int(h, 16) == mem[a + i], f"Byte {a + i:04X} weicht ab"
                belegt[a + i] = True
            letzte = a + len(hs)
            continue
        m = DS_RE.match(z)
        if m:
            n, bis = int(m.group(1)), int(m.group(2), 16)
            assert letzte + n - 1 == bis
            for i in range(letzte, bis + 1):
                assert mem[i] == 0xFF and i not in belegt
                belegt[i] = True
    fehlt = sorted(set(mem) - set(belegt))
    assert not fehlt, f"nicht dargestellt: {fehlt[0]:04X} ({len(fehlt)} Byte)"
    for t in ("BSN:", "TST:", "BSK:", "MRD:", "MWR:", "LES0:", "ISB:", "MBM:", "TTA:", "TII:"):
        assert re.search(rf'^{t}$', text, re.M), f"Marke {t} fehlt"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="nichts schreiben: Abdeckung jedes ROM-Bytes und Gleichheit mit dem "
                         "eingecheckten .prn pruefen (Exit 1 bei Abweichung)")
    args = ap.parse_args()
    text, mem = erzeuge()
    text.encode("ascii")
    pruefe(text, mem)
    if args.check:
        alt = open(ZIEL, encoding="ascii").read() if os.path.exists(ZIEL) else None
        if alt != text:
            print(f"ABWEICHUNG: {os.path.relpath(ZIEL, ROOT)} ist nicht die Ausgabe des Generators "
                  f"(python3 tools/gen_mc8020_bsys_prn.py ausfuehren)")
            return 1
        print(f"ok: bsys_2x.prn bytegleich, {len(mem)} Byte abgedeckt")
        return 0
    open(ZIEL, "w", encoding="ascii").write(text)
    print(f"geschrieben: {os.path.relpath(ZIEL, ROOT)} ({len(text.splitlines())} Zeilen)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
