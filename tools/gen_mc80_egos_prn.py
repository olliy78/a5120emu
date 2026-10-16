#!/usr/bin/env python3
"""
Baut doc/mc80/egos.prn: das kommentierte Listing des MC80.3x-Betriebssystems EGOS
(ZVE-EPROMs 0000H-1FFFH + SPE-2-EPROMs 2000H-27FFH, MAME-Satz mc8030), AP M1
(doc/design/32_arbeitspakete_kassette_a5120alt_mc80.md Par. 6).

Quellen: doc/EPROMS/MC80/mc8030_mame/zve_1..4.rom, spe_1..2.rom (zusammen 10 KB),
rohes Disassemblat von tools/z80_disasm2.py, Handkommentare aus der Systembeschreibung
EGOS 30.1 Teil 1+2 (doc/original_docs/mc80/mc80.3x-egos.pdf) und doc/mc80/hardware_3x.md.
Auswertung und Fassungsvergleich: doc/mc80/egos.md.

STATISCHE Analyse (es gibt noch keinen MC80-Emulator).  Belegbar ist, was im Code steht;
was nach Unterlage oder Vermutung gedeutet ist, traegt [?].  [EGOS S.n] = gedruckte Seite der
Systembeschreibung Teil 1, [EGOS2 S.n] = Teil 2 (Magnetband).  Die Ausgabe ist reines ASCII
(die Listing-Leser der Werkzeuge, tools/prn_listing.h, erwarten das).

Aufruf:
    python3 tools/gen_mc80_egos_prn.py            # doc/mc80/egos.prn schreiben
    python3 tools/gen_mc80_egos_prn.py --check    # Waechter: nichts schreiben, pruefen
                                                  #  (MD5 der Abzuege, jedes Byte genau einmal,
                                                  #   Sprungleisten-Einspruenge benannt,
                                                  #   bytegleich zum eingecheckten .prn)
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
EPROMS = os.path.join(ROOT, "doc/EPROMS/MC80/mc8030_mame")
ZIEL = os.path.join(ROOT, "doc/mc80/egos.prn")

# (Datei, Ladeadresse, MD5) -- Reihenfolge = Adressfolge
ABZUEGE = [
    ("zve_1.rom", 0x0000, "ccb6a95a333f01b68dc87ca02047c667"),
    ("zve_2.rom", 0x0800, "0675cd564ae3c8f29e43d4db50145adc"),
    ("zve_3.rom", 0x1000, "2868208bc3eb19872bd0e82e35cb2cfd"),
    ("zve_4.rom", 0x1800, "5d631a93788dc655da6aa2411f714d1d"),
    ("spe_1.rom", 0x2000, "b889d5c848849b4dd1203d6adc333ac0"),
    ("spe_2.rom", 0x2400, "637a0184a0c970d0cd3aab1e0cab7d7a"),
]
ENDE = 0x2800

# Einspruenge, die die Rekursion des Disassemblers nicht findet (Interruptroutinen ueber
# RAM-Vektoren, Sprungleisten, Rueckruf-Adressen, die als Zahl in HL uebergeben werden,
# Programmanfaenge hinter den Kennbyte-Koepfen der ROM-Kette).
EINSPRUENGE = [
    0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038, 0x0066, 0x003B, 0x0050, 0x0069, 0x02DE, 0x044B, 0x046F, 0x07B0, 0x0ADE, 0x0B05, 0x0B10, 0x0B17,
    0x1008, 0x13CD, 0x1389, 0x142A, 0x145D, 0x146D, 0x183D, 0x18F6, 0x1AFA, 0x1B07, 0x1B27,
    0x1B31, 0x1B47, 0x1B69, 0x1BAE, 0x1BE8, 0x1CB4, 0x1CC7, 0x1CDF, 0x1CF9, 0x1E2B, 0x1E8A,
    0x1F24, 0x1F4F, 0x2209, 0x2298, 0x22A8, 0x22D6, 0x232C, 0x2337, 0x2408, 0x2491, 0x250D,
    0x2558, 0x25BD, 0x2611, 0x2626, 0x26D7, 0x20D4, 0x21B6,
] + list(range(0x0BB5, 0x0BFD, 3)) + [0x1FF7, 0x1FFA, 0x1FFD] + [
    a for a in range(0x2004, 0x2079, 3) if a not in (0x2016, 0x2019, 0x2067, 0x2070, 0x2076)]

# --------------------------------------------------------------------------------------
# Datenbereiche (Anfang, Ende exklusiv, Art, Kommentar).  Alles andere ist Code.
# Arten: fuell (unbelegt), hex (Bytes, 16 je Zeile), text (ASCII), kette (Kopf eines
# Objekts der ROM-RAM-Kette: Kennbyte, Distanz, Name, Endebyte), zgtab (Zeigertabelle
# des Zeichengenerators), zg (Spaltenmuster), sprung (unbelegte Sprungleistenplaetze).
# --------------------------------------------------------------------------------------
DATEN = [
    (0x0003, 0x0008, "fuell", "Luecke bis RST 08H"),
    (0x000B, 0x0010, "fuell", ""),
    (0x0013, 0x0018, "fuell", ""),
    (0x001B, 0x0020, "fuell", ""),
    (0x0023, 0x0028, "fuell", "(0027H = FFH)"),
    (0x002B, 0x0030, "fuell", ""),
    (0x0033, 0x0038, "fuell", "in den ycdt-Fassungen 30E steht hier 'LD (HL),0 / JP 0C42H' "
                              "(NMI-Ziel, s. egos.md)"),
    (0x0063, 0x0066, "fuell", ""),
    (0x0075, 0x0082, "fuell", "unbelegt; ycdt-30E: NMI-Routine 'LD HL,0AAFDH / LD (4001H),HL / "
                              "LD HL,4000H / JP 0033H'"),
    (0x041B, 0x042B, "hex", "TAB_VIS: 16 Byte -> FB40H..FB4FH (CON-Init 03D9H): FB40/41 = 0CA4H "
                            "Zeichengenerator, FB42 = 00 Status, FB43 = 0AH Zeilenhoehe, FB44/45 = "
                            "D7H (CTC-ASP K3) / FFH Zeichen, FB46/47 = C1H/C0H Port 'hell'/'dunkel' "
                            "(Vorder-/Hintergrund), FB48/49 = FFFFH, FB4A = 00 Rollzaehler, FB4B = "
                            "C0H Bildhintergrund (VIS-Grundadresse), FB4C/4D = F600H lok. Stack, "
                            "FB4E/4F = 00FFH"),
    (0x042B, 0x043B, "hex", "TAB_TAST: 16 Byte -> FB60H..FB6FH: FB60 = 87H Tastaturstatus, "
                            "FB61..64 = 0, FB65.. = Tabulatoren 20H,40H,..,E0H (Pixel/2), Rest 0"),
    (0x043B, 0x043D, "hex", "TAB_CTC: -> D4H (CTC-ASP K0): 47H Zaehler, ZK folgt; 04H "
                            "Zeitkonstante (SIO-A-Takt -> 9600 Bd, hardware_3x.md 6.2)"),
    (0x043D, 0x0442, "hex", "TAB_SIO_B: -> D3H: 18H Kanalreset, WR2 = F0H (Vektorbasis "
                            "FFF0H), WR1 = 04H (Status beeinflusst Vektor)"),
    (0x0442, 0x044B, "hex", "TAB_SIO_A: -> D1H: 18H Reset, 14H/4DH WR4 (x16, 2 Stoppbit, Paritaet "
                            "ungerade), 03H/61H WR3 (Rx 7 Bit, Auto-Enables, Rx ein), 05H/AAH WR5 "
                            "(Tx 7 Bit, DTR, RTS, Tx ein), 01H/18H WR1 (Rx-Interrupt je Zeichen)"),
    (0x08CD, 0x08CF, "text", "Name 'T' (+FFH): BASIC-Bereich (Kennbyte 1CH), der eine Zeile "
                             "vorverarbeitet [?] (0829H)"),
    (0x08CF, 0x08D1, "text", "Name 'B' (+FFH): BASIC-Bereich, dem eine Zeile mit 'B'-Kennung "
                             "uebergeben wird [?] (085EH)"),
    (0x08D1, 0x08D9, "text", "Fehlerausschrift: 07H (Piep) + 'ERROR %' -- zugleich NAME der "
                             "ASCII-Datei (Kennbyte 07H) mit Fehlertexten, die 0878H sucht"),
    (0x08D9, 0x08DA, "text", "'>' Eingabeaufforderung (letzte Zeile)"),
    (0x08DA, 0x08DE, "text", "'OK' + 07H (Piep) + 19H (Zeilenrest loeschen), vorletzte Zeile"),
    (0x0AFE, 0x0B05, "kette", "KETTE_ANFANG: hier beginnt JEDE Suche in der Kette (0924H, CAT "
                              "262FH, RESET 078EH).  Kennbyte 02 (Treiber) 'CON', Distanz 7"),
    (0x0B08, 0x0B10, "kette", "Kennbyte 01 (Kommando) 'INIT'"),
    (0x0B13, 0x0B17, "kette", "Kennbyte 03 (RESET-Routine, ohne Namen), naechstes Objekt 1000H"),
    (0x0B41, 0x0B43, "text", "LEERZEILE: 0DH, FFH (fuer LZOU)"),
    (0x0B43, 0x0BB5, "fuell", "unbelegt"),
    (0x0BFD, 0x0C00, "fuell", ""),
    (0x0C71, 0x0C82, "hex", "ADT_VORGABE (17 Byte -> FC00H, INIT): 81 01 0AFE / 82 01 0AFE / "
                            "83 01 0AFE = CON als log. Geraet 1, 2, 3 (Segment 1); 84 01 1000 = KLOG "
                            "log. Geraet 4; 00 = Tabellenende [EGOS S.10, S.17]"),
    (0x0C82, 0x0C86, "fuell", ""),
    (0x0C95, 0x0CA4, "hex", "IY_VORGABE: 15 Byte -> F5F1H..F5FFH, IY = F5F1H (lokaler Stack im "
                            "Grundzustand).  Enthaelt 086FH (Rueckkehr in die Kommandoschleife), "
                            "F5F1H, F880H; Bedeutung der Felder im Einzelnen [?]"),
    (0x0CA4, 0x0D66, "zgtab", "ZG_ZEIGER: 97 Zeiger (Zeichen 20H..7FH + Endezeiger), FB40H zeigt "
                              "nach RESET hierher ([EGOS S.6] schreibt 'bei RESET 0C00H').  "
                              "Zeichenbreite = Abstand zum naechsten Zeiger (Proportionalschrift)"),
    (0x0D66, 0x1000, "zg", "ZG_MUSTER: Spaltenbytes, je Spalte 8 Bildpunkte, Bit 7 = OBEN, "
                           "Bit 0 = Unterlaenge (z. B. '0' = 00 7C 8A 92 A2 7C)"),
    (0x1000, 0x1008, "kette", "Kennbyte 02 (Treiber) 'KLOG' = logischer Kassettentreiber, "
                              "log. Geraet 4"),
    (0x1417, 0x1421, "text", "'DIRECTORY' + FFH: Name des Verzeichnisses auf der Kassette "
                             "(erster Block nach FORMAT/KINIT)"),
    (0x1421, 0x142A, "kette", "Kennbyte 01 'KINIT' (= Bedienkommando FORMAT [EGOS2 S.5])"),
    (0x1455, 0x145D, "kette", "Kennbyte 01 'READ'"),
    (0x1464, 0x146D, "kette", "Kennbyte 01 'WRITE'; Distanz 0B8CH reicht ueber den physischen "
                              "Treiber hinweg bis 1FF3H"),
    (0x1590, 0x1595, "hex", "nicht erreichbar (Rest einer aelteren Fassung? [?])"),
    (0x1779, 0x177B, "text", "Name 'E' (+FFH): Arbeitsbereich Typ A, Kennbyte 13H "
                             "(Assembler/Objektcodeeditor) [EGOS2 S.6]"),
    (0x177B, 0x177D, "text", "Name 'F' (+FFH): Arbeitsbereich Typ B, Kennbyte 1CH (BASIC) [?]"),
    (0x177D, 0x17C4, "fuell", "unbelegt; MC80.33 (PK3-Treiber) weicht ab 17C4H ab"),
    (0x1F4B, 0x1F4F, "hex", "CTC_ZEIT: je 2 Byte -> D5H (CTC-ASP K1): A5H = Zeitgeber, Vorteiler "
                            "256, Interrupt, ZK folgt; 60H -> 96*256/2,4576 MHz = 10 ms je Takt "
                            "(MB_WARTEN 1F05H) bzw. 00H -> 26,7 ms (MB_WARTEN_LANG 1F1DH)"),
    (0x1FD1, 0x1FD3, "hex", "-> D6H (CTC-ASP K2): 47H Zaehler, ZK 02H -- Bittakt fuer die "
                            "K5020 aus dem ASP-CTC (Umbau fuer LW 1200, Entwurf 29) [?]"),
    (0x1FD3, 0x1FD8, "hex", "-> 37H Steuer-PIO Tor B: Vektor B6H, Betriebsart 3, E/A-Maske FDH "
                            "(nur Bit 1 = AUF Ausgang), 17H Interruptsteuerwort (aus, LOW, Maske "
                            "folgt), Maske EFH (nur Bit 4 = /NIB1 ueberwacht)"),
    (0x1FD8, 0x1FDD, "hex", "-> 36H Steuer-PIO Tor A: Vektor B8H, Betriebsart 3, Maske 80H "
                            "(Bit 7 = AEB Eingang), 17H, Maske 7FH (nur AEB ueberwacht)"),
    (0x1FDD, 0x1FDE, "hex", "-> 35H Steuer-PIO Tor B Daten: 00H (AUF aus)"),
    (0x1FDE, 0x1FDF, "hex", "-> 34H Steuer-PIO Tor A Daten: 03H (kein KMBG reserviert)"),
    (0x1FDF, 0x1FE2, "hex", "-> 33H Daten-PIO Tor B: Vektor B4H, 7FH Betriebsart 1 (Eingabe), "
                            "03H Interrupt aus (wird auch von MB_PIO_AUS 1EE7H benutzt)"),
    (0x1FE2, 0x1FE5, "hex", "-> 32H Daten-PIO Tor A: Vektor B0H, 0FH Betriebsart 0 (Ausgabe), "
                            "03H Interrupt aus"),
    (0x1FE5, 0x1FE7, "hex", "-> FFEAH: Vektor CTC-ASP K1 = 1F24H (MB_ZEIT_ISR)"),
    (0x1FE7, 0x1FF3, "hex", "-> FFB0H..FFBBH (AKB-Vektoren [EGOS S.5]): B0 1B69H Schreiben, "
                            "B2 1BAEH Mitlesen beim Schreiben, B4 1BE8H Lesen, B6 1CC7H /NIB, "
                            "B8 1CDFH AEB, BA 1CF9H (nur RETI)"),
    (0x1FF3, 0x1FF7, "kette", "Kennbyte 03 (RESET-Routine): MB_INIT ueber 1FF7H"),
    (0x2000, 0x2004, "kette", "Kennbyte 1BH 'gesperrter Bereich fuer feste Programme' [EGOS S.8], "
                              "Distanz 01FDH bis 2200H; Endebyte B0H (jedes Byte >= 7FH beendet "
                              "einen Namen, 0958H)"),
    (0x2016, 0x201C, "sprung", "unbelegte Plaetze (JP 0000H)"),
    (0x2067, 0x206A, "sprung", "unbelegt (JP 0000H)"),
    (0x2070, 0x2073, "sprung", "unbelegt (JP 0000H)"),
    (0x2076, 0x207C, "sprung", "unbelegt (JP 0000H)"),
    (0x21F0, 0x2200, "hex", "unbelegt (FFH, 00H)"),
    (0x2200, 0x2209, "kette", "Kennbyte 01 'RELAD' (Endebyte B0H)"),
    (0x225E, 0x2298, "fuell", "00H -- in der Fassung 3s steht hier ein Zusatz (2260H ff.); die "
                              "von [EGOS S.19] genannte Adresse VERA = 2290H liegt in diesem "
                              "Leerraum, VERA beginnt im Abzug bei 22A8H [Befund]"),
    (0x232D, 0x2330, "fuell", ""),
    (0x2330, 0x2337, "kette", "Kennbyte 01 'DIR' (Endebyte B0H)"),
    (0x23B2, 0x2400, "fuell", "unbelegt; Fassung 3s: JP 0B51H bei 23F3H, JP 225FH bei 23FCH"),
    (0x2400, 0x2408, "kette", "Kennbyte 01 'LADT'"),
    (0x2486, 0x2487, "fuell", ""),
    (0x2487, 0x2491, "kette", "Kennbyte 01 'DEFINE'"),
    (0x2506, 0x250D, "kette", "Kennbyte 01 'ACT'"),
    (0x254B, 0x254F, "hex", "(LD A,0CCH / JR 253FH -- nicht erreichbar: 'logische Funktion "
                            "bereits aktiv' [EGOS2 S.7] [?])"),
    (0x254F, 0x2558, "kette", "Kennbyte 01 'DEACT'"),
    (0x25B8, 0x25B9, "fuell", ""),
    (0x25B9, 0x25BD, "kette", "Kennbyte 03 (RESET-Routine): Einschaltmeldung"),
    (0x25E4, 0x25FB, "text", "MELDUNG_1: 'mc 80.30. microcomputer' (23 Zeichen, doppelt hoch)"),
    (0x25FB, 0x260A, "text", "MELDUNG_2: 'Version 3.0/1,2' (15 Zeichen)"),
    (0x260A, 0x2611, "kette", "Kennbyte 01 'CAT'"),
    (0x261E, 0x2626, "kette", "Kennbyte 01 'CATA'"),
    (0x26CF, 0x26D7, "kette", "Kennbyte 01 'RKET', Distanz 012EH -> naechstes Objekt bei 2800H "
                              "(Erweiterungs-ROM der SPE 2; ohne Inhalt Kettenende, s. egos.md)"),
    (0x273D, 0x2800, "fuell", "unbelegt"),
]

# --------------------------------------------------------------------------------------
# Namen.  Nicht genannte Ziele behalten den Namen Lxxxx/sub_xxxx des Disassemblers.
# --------------------------------------------------------------------------------------
NAMEN = {
    0x0000: "KALTSTART", 0x0008: "RST08", 0x0018: "RST18", 0x0028: "RST28", 0x0038: "RST38",
    0x003B: "CLB", 0x0050: "ROL", 0x0069: "ROU", 0x0082: "BFE",
    0x008C: "ZEICHENBREITE", 0x0101: "ZEICHEN_AUS", 0x0133: "ZEICHEN_NORMAL",
    0x0170: "ZEICHEN_DOPPELT", 0x01BB: "ZEILE_ZU_PIXEL", 0x01F5: "KURSOR_EIN",
    0x01FB: "KURSOR_AUS", 0x0226: "VIS_KOMMANDO", 0x0237: "MUL8",
    0x024D: "CON_LESEN", 0x024F: "CON_EINGABE", 0x02E1: "WBN", 0x02E6: "OUT_ZS", 0x02E9: "WLN",
    0x0325: "ZEICHEN_LOESCHEN", 0x0351: "CON", 0x03D3: "CON_INIT",
    0x044B: "SIO_A_ISR", 0x046F: "TASTE_ISR", 0x0599: "EDITOR", 0x05BE: "ED_EINFUEGEN",
    0x05EA: "ED_ZEICHEN", 0x0639: "ED_ZEILENANFANG", 0x067A: "ED_RECHTS", 0x06A3: "ED_TAB_SUCHEN",
    0x06E8: "ED_REST_ZEIGEN", 0x071B: "ED_GRENZEN", 0x0755: "TST_STRG", 0x0761: "TST",
    0x0776: "RESET", 0x07BE: "KOMMANDO_NEU", 0x07D4: "KOMMANDO_SCHLEIFE",
    0x0878: "FEHLER_ANZEIGEN", 0x08DE: "NAME_PUFFERN", 0x08FC: "KOMMANDO_STARTEN",
    0x0914: "LIY", 0x0924: "RSU", 0x092E: "RS0", 0x098B: "SEGC_EIN", 0x099E: "RSF",
    0x0A0D: "RLL", 0x0A2E: "IST_NAMENSZEICHEN", 0x0A43: "HBS", 0x0A4C: "HEX_ZIFFER",
    0x0A59: "ZLE", 0x0A98: "NIXE", 0x0AE5: "LOUT", 0x0B17: "KERN_RESET", 0x0B32: "LZOU",
    0x0BB5: "SL_KOMMANDO", 0x0BB8: "SL_ZEICHEN", 0x0BBB: "SL_RLL", 0x0BBE: "SL_OUT",
    0x0BC1: "SL_WBN", 0x0BC4: "SL_RSF", 0x0BC7: "SL_RSU", 0x0BCA: "SL_RS0", 0x0BCD: "SL_TST",
    0x0BD0: "SL_ZLE", 0x0BD3: "SL_WLN", 0x0BD6: "SL_BFE", 0x0BD9: "SL_CLB", 0x0BDC: "SL_ROL",
    0x0BDF: "SL_ROU", 0x0BE2: "SL_TST_STRG", 0x0BE5: "SL_FEHLER", 0x0BE8: "SL_LIY",
    0x0BEB: "SL_HBS", 0x0BEE: "SL_CON", 0x0BF1: "SL_NIXE", 0x0BF4: "SL_LOUT", 0x0BF7: "SL_LZOU",
    0x0BFA: "SL_IY_GRUND", 0x0C00: "INIT", 0x0C42: "INIT_KETTE", 0x0C86: "IY_GRUNDZUSTAND",
    0x1008: "KLOG", 0x107B: "KLOG_INIT", 0x1089: "KLOG_CLOSE", 0x1094: "KLOG_OPEN",
    0x111C: "KLOG_FORMAT", 0x1239: "KLOG_SCHREIBEN", 0x1268: "KLOG_LESEN",
    0x128F: "NAMEN_VERGLEICH", 0x12A1: "VERZ_SCHREIBEN", 0x12D4: "VERZ_SUCHEN",
    0x1335: "LAENGE_RUNDEN", 0x135E: "MB_FEHLERPRUEFUNG", 0x1371: "MB_RESERVIEREN",
    0x1378: "MB_FREIGEBEN", 0x137C: "MB_BLOCK_LESEN", 0x1380: "MB_RUECKSPULEN",
    0x1397: "MB_BM_VOR", 0x139E: "MB_BM_RUECK", 0x13A5: "MB_BM_SCHREIBEN",
    0x13A9: "MB_SCHLUSSLUECKE", 0x13AD: "MB_BLOCK_SCHREIBEN", 0x13BB: "MB_AUFTRAG",
    0x13CD: "MB_FERTIG", 0x142A: "KINIT", 0x145D: "READ", 0x146D: "WRITE",
    0x15D2: "BEDIEN_AUSFUEHREN",
    0x17C4: "MB_TREIBER", 0x183D: "MB_WEITER", 0x189B: "MB_SCHREIBEN", 0x18CD: "MB_SCHREIB_START",
    0x1904: "MB_LESEN", 0x1939: "MB_RES_EIN", 0x196A: "MB_RES_AUS", 0x196F: "MB_UMSPULEN",
    0x19C9: "MB_BANDMARKE", 0x19E0: "MB_SCHLUSS", 0x19F5: "MB_BM_SUCHEN",
    0x1B5F: "MB_FEHLER", 0x1B69: "MB_AUS_ISR", 0x1BAE: "MB_MITLESEN_ISR", 0x1BE8: "MB_EIN_ISR",
    0x1CC7: "MB_NIB_ISR", 0x1CDF: "MB_AEB_ISR", 0x1CFB: "MB_ENDE", 0x1D15: "PIEP",
    0x1D1C: "MB_STATUS", 0x1D41: "MB_RETTEN", 0x1D6D: "MB_HOLEN", 0x1DAC: "MB_PUFFER",
    0x1DD1: "CRC16", 0x1DF4: "PIO_INT_EIN", 0x1E21: "MB_WARTEN_10MS", 0x1E64: "MB_LAENGE_PRUEFEN",
    0x1E8A: "MB_AUF_EIN", 0x1EA2: "MB_KMBG_RES", 0x1ECB: "MB_KMBG_FREI", 0x1EE7: "MB_PIO_AUS",
    0x1F05: "MB_WARTEN", 0x1F1D: "MB_WARTEN_LANG", 0x1F24: "MB_ZEIT_ISR", 0x1F4F: "MB_WIEDERHOLEN",
    0x1F76: "MB_INIT", 0x1FF7: "SL_MB_INIT", 0x1FFA: "SL_MB_STATUS", 0x1FFD: "SL_MB_TREIBER",
    0x2004: "JHL", 0x2007: "JHD", 0x200A: "JPD", 0x200D: "CHL", 0x2010: "CHD", 0x2013: "CAD",
    0x201C: "SL_201C", 0x2037: "SEG_LDIR", 0x203A: "SEG_LDDR", 0x203D: "L0L", 0x2040: "L0E",
    0x2043: "L0X", 0x2046: "L0Y", 0x2049: "L1L", 0x204C: "L1E", 0x204F: "L1X", 0x2052: "L1Y",
    0x2055: "L2L", 0x2058: "L2E", 0x205B: "L2X", 0x205E: "L2Y", 0x2061: "S1L", 0x2064: "S1X",
    0x206A: "S2L", 0x206D: "S2X", 0x2073: "SL_2073", 0x2189: "SEG_AUSFUEHREN",
    0x2209: "RELAD", 0x2298: "VERA_DIFF", 0x22A8: "VERA", 0x2337: "DIR", 0x2408: "LADT",
    0x2446: "ADT_ZEILE", 0x2491: "DEFINE", 0x250D: "ACT", 0x2558: "DEACT",
    0x25BD: "EINSCHALTMELDUNG", 0x2611: "CAT", 0x2626: "CATA", 0x262C: "CAT_AUSGABE",
    0x26D7: "RKET",
}

# --------------------------------------------------------------------------------------
# Abschnittsueberschriften
# --------------------------------------------------------------------------------------
ABSCHNITTE = {
    0x0000: "Restart-Vektoren: RESET, RST 08H..38H und NMI springen in die RAM-Aussprungleiste "
            "FEE8H-FEFFH [EGOS S.5]",
    0x003B: "VIS-Grundfunktionen CLB/ROL/ROU (Bildkommandos der VIS 2a), BFE",
    0x008C: "Zeichenausgabe auf die Vollgrafik: Zeichenbreite, Steuerzeichen, Zeichen zeichnen",
    0x01BB: "Zeile/Spalte -> Bildpunkt, Kursor, VIS-Bildkommando, Multiplikation",
    0x024D: "CON: Zeileneingabe einrichten, Textausgabe WBN/OUT/WLN",
    0x0351: "CON -- Konsolentreiber mit RIO-Schnittstelle (IY-Vektor) [EGOS S.14-16]",
    0x041B: "Initialisierungstabellen (VIS-/Tastaturzellen, ASP-CTC, ASP-SIO)",
    0x044B: "Tastatur: SIO-A-Empfangsinterrupt + nachgeordneter CTC-K3-Interrupt (Protokoll)",
    0x0599: "Zeileneditor (laeuft im Tastaturinterrupt, solange FB42H Bit 7 = Eingabe aktiv)",
    0x0755: "TST -- Tastaturabfrage (polling) [EGOS S.13]",
    0x0776: "RESET: Hochfahren, RESET-Routinen (Kennbyte 03) abarbeiten, Kommandoschleife",
    0x0878: "Fehleranzeige, Kommandoaufruf ueber die Kette",
    0x0914: "Kettenverwaltung: LIY, RSU/RS0, RSF, RLL; Zahlen lesen/schreiben (ZLE/HBS)",
    0x0A98: "NIXE -- Treiberverwaltung ueber die Active Device Table FC00H [EGOS S.15-17]",
    0x0AFE: "ROM-Kette, Teil 1: CON, INIT, Kern-RESET-Routine",
    0x0B32: "LZOU, Leerzeile",
    0x0BB5: "SPRUNGLEISTE 0BB5H-0BFCH = Softwareschnittstellen [EGOS S.12-15]",
    0x0C00: "INIT -- RAM loeschen und ketten, Directory, ADT [EGOS S.9]",
    0x0C86: "IY-Grundzustand",
    0x0CA4: "Zeichengenerator (Software, Proportionalschrift, 96 Zeichen 20H-7FH)",
    0x1000: "MAGNETBAND 1000H-1FFFH, Teil 1: logischer Treiber KLOG (IY-Schnittstelle, "
            "log. Geraet 4) [EGOS2 S.4]",
    0x1417: "Magnetband, Teil 2: Bedienprogramme KINIT (=FORMAT), READ, WRITE [EGOS2 S.5-6]",
    0x17C4: "Magnetband, Teil 3: physischer Treiber fuer die AKB K5020 an 30H-37H "
            "(IX-Schnittstelle) [EGOS2 S.3-4]",
    0x1B69: "Magnetband: Interruptroutinen (Daten-PIO Aus/Ein, Mitlesen, /NIB, AEB) und "
            "Rueckkehr zum Auftraggeber",
    0x1D41: "Magnetband: Registerrettung, CRC, PIO-/Zeitgeber-Hilfen",
    0x1F76: "Magnetband: Initialisierung (RESET-Routine, Kennbyte 03) und Sprungleiste 1FF7H",
    0x2000: "SPE-2-ROM 2000H-23FFH: Segmentumschaltprogramme [EGOS S.17-19]",
    0x2200: "RELAD und VERA (Relativlader, Verschiebeadresstabelle) [EGOS S.10, S.19-20]",
    0x2330: "DIR [EGOS S.11]",
    0x2400: "SPE-2-ROM 2400H-27FFH: Geraeteverwaltungskommandos LADT, DEFINE, ACT, DEACT "
            "[EGOS S.10-11]",
    0x25B9: "Einschaltmeldung (RESET-Routine, Kennbyte 03)",
    0x260A: "CAT, CATA [EGOS S.9-10]",
    0x26CF: "RKET [EGOS S.11-12]",
}

# --------------------------------------------------------------------------------------
# Kommentare je Befehlsadresse
# --------------------------------------------------------------------------------------
C = {
    # ---- Restart-Vektoren
    0x0000: "[RESET] Kaltstart -> RESET 0776H",
    0x0008: "RST 08H -> Aussprung FEEBH (RAM, schreibgeschuetzt; INIT fuellt FFH)",
    0x0010: "RST 10H -> FEEEH",
    0x0018: "RST 18H -> FEF1H",
    0x0020: "RST 20H -> FEF4H",
    0x0028: "RST 28H -> FEF7H",
    0x0030: "RST 30H -> FEFAH",
    0x0038: "RST 38H -> FEFDH; INIT legt dort 18H,FEH = 'JR $' ab (Halteschleife, 0C53H)",
    0x0066: "[NMI] -> FEE8H; INIT legt dort EDH,45H = RETN ab (0C59H): die NMI (Netzausfall, "
            "MST 2) wird im Grundzustand uebergangen",
    # ---- CLB/ROL/ROU/BFE
    0x003B: "[CLB 0BD9H] Bild dunkel loeschen [EGOS S.14]",
    0x003E: "B = Opcode 00H: 'Bild loeschen, Rollzaehler zuruecksetzen' (C0 aus FB4BH: "
            "0 = dunkel, 1 = hell) [BDA S.20]",
    0x0043: "Bildfenster-Anfang = F800H (D = F8H = oberste Textzeile, E = 0)",
    0x0049: "Rollzaehler FB4AH = 0",
    0x0050: "[ROL 0BDCH] Bild um eine Textzeile nach oben rollen",
    0x0053: "B = 1AH: Opcode 1/n = 'n Zeilen nach oben rollen', n = 0AH = 10 Bildzeilen",
    0x0058: "Rollzaehler -= 10 (F6H)",
    0x005A: "Rollzaehler FB4AH += A -- verschiebt alle Bildpunkt-Y um den Rollweg "
            "(ZEILE_ZU_PIXEL 01E7H)",
    0x0069: "[ROU 0BDFH] Bild um eine Textzeile nach unten rollen",
    0x006C: "B = 2AH: Opcode 2/n = n Zeilen nach unten, n = 10",
    0x0071: "Rollzaehler += 10",
    0x0082: "[BFE 0BD6H] Bildfenster festlegen: DE = Zeile/Spalte -> Bildpunkte -> FB58H",
    # ---- Zeichenbreite
    0x008C: "[ZEICHENBREITE] A = Zeichen, DE = Position (D = Y, Bit 0 von D = rechte Bildhaelfte, "
            "E = X 0..255).  Rueckgabe A = Breite, CY = 1: passt nicht in die Zeile",
    0x0090: "CR: Breite 0, Zeilenwechsel (00D6H)",
    0x0093: "TAB (09H): Abstand bis zum naechsten Tabulator FB65H.. (Pixel/2)",
    0x00A9: "nicht darstellbar: A = 0, CY",
    0x00AD: "druckbares Zeichen: Zeiger in ZG_ZEIGER ((FB40H) + 2*(Zeichen-20H))",
    0x00BA: "C/B = Zeiger auf die Spaltenmuster, A = naechster Zeiger (low) -> Breite = A - C",
    0x00C1: "passt die Breite noch in die aktuelle Haelfte (E + Breite < 256)?",
    0x00C5: "schon in der rechten Haelfte (D Bit 0)?  Nein: Uebertrag in die rechte Haelfte erlaubt",
    0x00C8: "rechter Rand erreicht: FB60H Bit 1 = automatischer Zeilenumbruch erlaubt?",
    0x00D0: "Rest der Zeile loeschen (B = FFH Spalten)",
    0x00D6: "[Zeilenwechsel] neues Y = Y - Zeilenhoehe, Pruefung gegen den Rollzaehler",
    0x00DC: "Y -= Zeilenhoehe (FB43H): Y zaehlt nach OBEN, die naechste Zeile liegt tiefer",
    0x00E3: "unter der untersten Zeile?",
    0x00ED: "ja: FB60H Bit 0 = Rollen gesperrt?  Sonst ROL (Bild rollt, Kursor bleibt unten)",
    # ---- ZEICHEN_AUS
    0x0101: "[ZEICHEN_AUS 0BB8H] A = Zeichen an Position DE zeichnen (Vordergrund FB46H, Hintergrund "
            "FB47H); DE danach hinter dem Zeichen.  19H = Rest der Zeile loeschen",
    0x010B: "Breite holen; CY -> nicht darstellbar",
    0x0117: "IX = Zeiger auf die Spaltenmuster (aus ZEICHENBREITE in BC)",
    0x011C: "H/L = Portadresse dunkel/hell (FB47H/FB46H, Vorgabe C0H/C1H = VIS 'Punkt dunkel'/'Punkt "
            "hell'); rechte Haelfte: Bit 3 der Portadresse setzen [BDA S.19]",
    0x0129: "B = Y (geht bei OUT (C),r auf A8-A15)",
    0x012B: "FB42H Bit 4: doppelte Zeichenhoehe?",
    0x0133: "[ZEICHEN_NORMAL] je Spalte ein Musterbyte; Bit 0 zuerst = UNTERSTE Bildzeile, Y waechst "
            "je Bit -> die Y-Achse zeigt nach OBEN (Y = 0 unten) [Befund]",
    0x0139: "Bit herausschieben; Z nach 8 Bit (Endemarke 1 von SCF)",
    0x013D: "Bit = 1 -> L-Port (hell), 0 -> H-Port (dunkel): jede Spalte wird ganz geschrieben",
    0x0141: "VIS 2a: OUT (C),E mit C = Funktion/Haelfte, B = Y, E = X [BDA S.19]",
    0x0147: "naechste Spalte: Y zurueck (B -= 8), X += 1",
    0x014C: "X-Ueberlauf 255 -> 0: weiter in der rechten Haelfte (Bit 3 der Ports)",
    0x015D: "Rueckgabe D = Y, Bit 0 = rechte Haelfte",
    0x0170: "[ZEICHEN_DOPPELT] wie oben, jeder Bildpunkt 2 x 2",
    0x018F: "Leerspalte doppelt breit",
    # ---- ZEILE_ZU_PIXEL usw.
    0x01BB: "[ZEILE_ZU_PIXEL] DE = Textzeile/-spalte -> D = Y-Bildpunkt, E = X: Spalte * Breite des "
            "Zeichens 20H (ZG), Zeile * 10, Y = F8H - 10*Zeile + Rollzaehler",
    0x01F5: "[KURSOR_EIN] Kursorstrich unter der Kursorposition FB5CH in Vordergrundfarbe",
    0x01FB: "[KURSOR_AUS] ebenso in Hintergrundfarbe",
    0x0201: "DE = Kursor in Bildpunkten (FB5CH)",
    0x0210: "6 Bildpunkte breiter waagerechter Strich in der Zeile Y-1",
    0x0226: "[VIS_KOMMANDO] B = Opcode an C = FB4BH | 04H (Funktion 1xx = Bildkommando) [BDA S.20]",
    0x022E: "Wartezeit 32*256 DJNZ (ca. 43 ms): die VIS 2a fuehrt Loeschen/Rollen selbst aus; "
            "im Emulator genuegt sofortige Ausfuehrung [?]",
    0x0237: "[MUL8] HL = HL * A (8 Bit), Ergebnis in H:L:A",
    # ---- CON lesen
    0x024D: "[CON_LESEN] READ BINARY / READ LINE: Zeile ab leerem Puffer",
    0x024F: "[CON_EINGABE] INPUT (48H): Puffer HL, Laenge C; Eingabe laeuft im Tastaturinterrupt",
    0x0253: "FB42H Bit 7 = Zeileneingabe aktiv; war es schon gesetzt: Fehler C2H (Geraet belegt)",
    0x0261: "IY des Auftraggebers merken (FB4EH); die Interruptroutine meldet ueber (IY+10)",
    0x0269: "Textpuffer: Anfang FB50H, Kursor FB54H, max. Ende FB52H = Anfang + Laenge",
    0x026F: "FB7DH = FFH: kein Zeichen fuer TST anstehend",
    0x027B: "Bildfenster-Anfang FB58H -> Fensterende/Kursor/aktuelles Ende",
    0x0288: "FB60H Bit 0/1 (Rollen/Umbruch gesperrt) loeschen",
    0x0290: "C = (IY+11) = Spalte, auf die der Kursor nach der Vorgabe gestellt wird",
    0x0295: "Vorgabetext des Puffers anzeigen (bis zum ersten Steuerzeichen)",
    0x02CA: "Kursor zeigen, FB42H Bit 0 setzen",
    0x02D2: "(IY+1) Bit 0 = 1: Interruptbetrieb, sofort zurueck; sonst warten auf (IY+10) Bit 7",
    0x02DE: "(nicht angesprungen [Befund]) BFE + WBN",
    # ---- WBN/OUT/WLN
    0x02E1: "[WBN 0BC1H] Text HL, max. BC Zeichen ab Kursor, Steuerzeichen werden ausgefuehrt "
            "(07H Piep, 0CH Bild loeschen, 0DH CR, 19H Zeilenrest loeschen); Ende FFH",
    0x02E6: "[OUT 0BBEH] wie WLN, aber Position DE = Zeile/Spalte vorgeben [EGOS S.12]",
    0x02E9: "[WLN 0BD3H] wie WBN, endet zusaetzlich an 0DH",
    0x02EE: "DE = aktuelle Ausgabeposition FB58H (Bildpunkte)",
    0x02FF: "0CH = Bild loeschen (CLB)",
    0x0304: "07H -> OUT (D0H): Piep der Tastatur (SIO A Senden)",
    0x030C: "Endezeichen (D vom Aufrufer: FFH oder 0DH) erreicht?",
    0x0316: "Position merken; BC = geschriebene Zeichen",
    0x0325: "[ZEICHEN_LOESCHEN] BC Bildpunktspalten ab DE in Hintergrundfarbe (8 Bildzeilen)",
    # ---- CON
    0x0351: "[CON 0BEEH] Konsolentreiber; IY = RIO-Vektor, (IY+1) = Anforderung (Bit 0 = "
            "Interruptbetrieb wird ausgeblendet)",
    0x0359: "DE = (IY+2/3) Datenadresse, BC = (IY+4/5) Laenge -> HL = Daten",
    0x0361: "00 INITIALIZE",
    0x0365: "02..07: nichts zu tun (80H); 08: ungueltig",
    0x036B: "0AH READ BINARY / 0CH READ LINE",
    0x0374: "0EH WRITE BINARY",
    0x0378: "10H WRITE LINE",
    0x037C: "40H READ STATUS, 42H WRITE STATUS, 48H INPUT; Rest ungueltig (C1H)",
    0x038B: "Fertigstellungscode C1H 'Operation ungueltig'",
    0x0390: "INITIALIZE: Zellen + ASP neu einrichten, Bild loeschen",
    0x0398: "WRITE BINARY = WBN; Laenge zurueck nach (IY+4/5)",
    0x03A1: "Fertigstellungscode 80H 'fehlerfrei'",
    0x03A6: "WRITE LINE = WLN",
    0x03AB: "READ STATUS: (IY+4) Byte ab FB60H (Tastatur- und VIS-Zellen) in den Puffer",
    0x03B8: "WRITE STATUS: Puffer -> FB60H.., hoechstens 55H Byte; danach Kursor aus FB63H/64H neu",
    0x03D3: "[CON_INIT] Vektor SIO-A-Empfang FFFCH = 044BH (FFF0H + 0CH, Status beeinflusst Vektor)",
    0x03D9: "TAB_VIS -> FB40H.., TAB_TAST -> FB60H..",
    0x03EB: "CTC ASP K0 (D4H): Zaehler /4 -> SIO-A-Takt",
    0x03F1: "SIO ASP Kanal B (D3H)",
    0x03F7: "SIO ASP Kanal A (D1H) = Tastatur 9600 Bd, 7 Bit, ungerade, 2 Stoppbit",
    0x03FD: "FB7EH/FB7FH = 90H: Tastaturzustand (gemeldet/gewuenscht), s. TASTE_ISR",
    0x0405: "Vektor CTC ASP K3 FFEEH = 046FH",
    0x040B: "CTC ASP Vektorbasis E8H (FFE8H-FFEFH)",
    0x0412: "CTC ASP K3 (D7H): 07H Zeitgeber, Vorteiler 16, Interrupt AUS, ZK = 01H",
    # ---- Tastatur
    0x044B: "[SIO_A_ISR] Zeichen von der Tastatur (Vektor FFFCH)",
    0x044D: "IN (D0H), 7 Bit",
    0x0451: ">= 20H: Zeichen; 1CH..1FH: PRAEFIX -> FB7CH (gilt fuer alle folgenden Zeichen bis zum "
            "naechsten Praefix); 00H..1BH: Zeichen",
    0x045F: "Zeichen -> FB45H; CTC ASP K3 mit 91H (Interrupt ein, Zeitgeber, ohne neue ZK) anstossen: "
            "die eigentliche Verarbeitung laeuft im niedriger priorisierten CTC-Interrupt [EGOS S.21]",
    0x046F: "[TASTE_ISR] CTC ASP K3 (Vektor FFEEH): Zeichen auswerten.  B = Zeichen (FB45H), "
            "FB7CH = Praefix, FB7FH = Tastaturzustand",
    0x0473: "CTC K3: Interrupt wieder aus (01H)",
    0x047E: "Praefix 1FH: Zeichen | 80H (Funktions-/Kursortasten [?]); FB7FH Bit 3 sperrt (-> 1BH)",
    0x0489: "gesperrt: statt des Zeichens 1BH liefern [?]",
    0x0491: "Praefix 1CH..1EH: Nummer 0..2",
    0x049B: "Praefix 1EH = SONDERTASTE: B ist eine Bitmaske; n = 40H -> 0, 20H -> 1, .. 01H -> 6, "
            "00H -> 7; FB7FH Bit 1 sperrt",
    0x04AC: "Aussprung FED0H + 3n nur, wenn dort C3H (JP) steht [EGOS S.21-22]; RET kehrt nach "
            "051EH zurueck",
    0x04BD: "Praefix 1CH (1. Ebene [?]): FB7FH Bit 2 sperrt; Bit 4 = Gross/Klein tauschen "
            "(Vorgabe an, FB7FH = 90H)",
    0x04DE: "Praefix 1DH (2. Ebene [?]): FB7FH Bit 0 sperrt",
    0x04E4: "Praefix 1FH: Zeichen | 80H",
    0x04EB: "Tastaturzustand abgleichen: hat sich FB7FH gegenueber FB7EH geaendert?",
    0x04F7: "Bit 6 geaendert -> an die Tastatur 14H (Bit 6 = 1) bzw. 12H (Bit 6 = 0) senden "
            "(Anzeige/LED der Tastatur [?])",
    0x0507: "Bit 5: ausgebaut (NOPs)",
    0x0515: "FB7FH Bit 7 = 0: jede Taste mit Piep (07H an die Tastatur) quittieren",
    0x051E: "FB62H = letztes Zeichen",
    0x0522: "keine Zeileneingabe aktiv (FB42H Bit 7 = 0): Zeichen fuer TST nach FB7DH",
    0x0530: "Zeileneingabe aktiv: Zeichen in den Editor (IY des Auftraggebers aus FB4EH)",
    0x0545: "Zeile fertig (CR/Endezeichen): Kursor ans Ende, Laenge -> (IY+4/5)",
    0x0566: "INPUT (48H): (IY+12) = Endezeichen, (IY+11) = Kursorspalte",
    0x057B: "(IY+10) = 80H fertig; FB42H Bit 7/6 zuruecksetzen",
    0x058A: "Interruptbetrieb ((IY+1) Bit 0): Ruecksprung (IY+6/7) anspringen",
    # ---- Editor
    0x0599: "[EDITOR] A = Zeichen: 20H..7EH einfuegen/ueberschreiben, sonst Steuerzeichen; CY = "
            "Zeile beendet",
    0x05BE: "[ED_EINFUEGEN] (0BH) Leerzeichen an der Kursorposition einfuegen",
    0x05EA: "[ED_ZEICHEN] Zeichen an der Kursorposition ablegen (max. 46H = 70 Zeichen)",
    0x0614: "Steuerzeichen: 0CH Kursor rechts, 08H links, 7FH Zeichen loeschen, 09H Tabulator, "
            "0BH einfuegen, 89H Tab-Einfuegen, 0DH Zeilenanfang; jedes andere beendet die Eingabe "
            "(CY) und wird Endezeichen (11H ENTER, 13H [?])",
    0x0639: "[ED_ZEILENANFANG] Kursor an den Pufferanfang",
    0x0646: "7FH: Zeichen unter dem Kursor loeschen, Rest nachziehen",
    0x0661: "08H: Kursor ein Zeichen nach links (vom Anfang neu abzaehlen)",
    0x067A: "[ED_RECHTS] Kursor ein Zeichen nach rechts",
    0x06A3: "[ED_TAB_SUCHEN] naechster Tabulator rechts von E (Tabelle FB65H, Pixel/2)",
    0x06B8: "09H: bis zum naechsten Tabulator rechts",
    0x06D9: "89H: nur wenn FB60H Bit 7: Leerzeichen einfuegen + TAB",
    0x06E8: "[ED_REST_ZEIGEN] Puffer ab Kursor neu zeichnen (FB60H Bit 2 = Anzeige ein)",
    0x071B: "[ED_GRENZEN] DE = Kursor im Puffer, auf Anfang..Ende begrenzen; BC = Ende",
    # ---- TST
    0x0755: "[TST_STRG 0BE2H] wie TST, Codes >= 80H auf 00H..1FH abgebildet [?]",
    0x0761: "[TST 0BCDH] FB7DH = anstehendes Zeichen (FFH = keins).  Rueckgabe: A = Code; keine "
            "Taste: A = 0, CY = 0, Z = 1; Taste: CY = 1, Z = 0 [EGOS S.13 nennt auch 'repetierend' "
            "-- die Wiederholung macht die Tastatur selbst]",
    # ---- RESET
    0x0776: "[RESET] Stapel F800H (CPU-Stack F601H-F800H [EGOS S.4])",
    0x0779: "I = FFH, IM 2: Vektortabelle FF00H-FFFFH",
    0x0780: "256 x OUT (C),B mit C = 8CH und B = 00H, FFH, FEH .. 01H (B liegt auf A8-A15 UND auf "
            "dem Datenbus): RAM-Steuerung 'Schreibschutz FC00H' [EGOS S.5].  Danach beschreibt das "
            "ROM FC00H-FFFFH ohne weiteres (FFFCH, FFEEH, FC00H, FEE8H) und schreibt NIE mehr nach "
            "8CH/8DH -> der Schutz ist nach dieser Folge AUS [Befund; Wirkung im Einzelnen [?]]",
    0x0787: "ZVE-PIO Tor B (87H): Betriebsart 0 = Ausgabe -> Segmentregister (85H)",
    0x078B: "IY = F5F1H (lokaler Stack, Grundzustand)",
    0x078E: "alle RESET-Routinen (Kennbyte 03) der Kette ab KETTE_ANFANG, Segment 1, abarbeiten "
            "[EGOS S.20]: KERN_RESET 0B17H, MB_INIT 1FF7H, EINSCHALTMELDUNG 25BDH, dann die im RAM",
    0x0799: "RS0: naechstes Objekt mit Kennbyte 03 suchen (DE = 0: ohne Namen)",
    0x07A0: "SEGC = SEGD = Segment des Objekts, einschalten",
    0x07AA: "Rueckkehr nach 07B0H; JP (HL+1) = Code hinter dem Endebyte",
    0x07B0: "zurueck: SEGD wiederherstellen, ueber die Distanz zum naechsten Objekt, weitersuchen",
    0x07BD: "Kette zu Ende",
    0x07BE: "[KOMMANDO_NEU] 'OK' + Piep in der vorletzten Zeile (Zeile 23) [EGOS S.9]",
    0x07C3: "Eingabepuffer FB80H mit 19H vorbelegen",
    0x07D4: "[KOMMANDO_SCHLEIFE 0BB5H] '>' in Zeile 24, Spalte 0",
    0x07E3: "IY -= 13: eigener RIO-Vektor auf dem lokalen Stack",
    0x07EA: "CON INPUT (48H): Zeile nach FB80H, max. 51H Zeichen",
    0x07FB: "Endezeichen (IY+12): 13H -> neu 'OK'; nur 11H fuehrt aus",
    0x0808: "erstes Zeichen muss ein Namenszeichen sein",
    0x080F: "'OK'-Zeile loeschen (19H)",
    0x081D: "IX = F880H (BZPU-BASIC-Zwischencodepuffer [EGOS S.4])",
    0x0821: "NAME_PUFFERN: Kommando (Kennbyte 01) mit diesem Namen in der Kette?",
    0x0826: "nein: BASIC-Bereich 'T' (Kennbyte 1CH) suchen und die Zeile uebersetzen lassen [?]",
    0x0840: "Puffer F880H mit FFH abschliessen",
    0x0847: "nichts gefunden: Fehlerstelle (Spalte) -> (IY-2), Fehler anzeigen",
    0x0853: "F880H beginnt mit B1H = Kommando: KOMMANDO_STARTEN",
    0x085B: "sonst BASIC-Bereich 'B' (Kennbyte 1CH) anspringen (Direktmodus [?]); fehlt er: "
            "Fehler C7H",
    0x086F: "Rueckkehr der Kommandos: CY = 0 -> 'OK', CY = 1 -> Fehlercode A anzeigen",
    # ---- Fehleranzeige
    0x0878: "[FEHLER_ANZEIGEN 0BE5H] A = Code: '<07>ERROR %xx' in Zeile 23, Piep",
    0x089D: "ASCII-Datei (Kennbyte 07) mit dem Namen ab 08D1H suchen ('ERROR' mit vorangestelltem "
            "07H [?]); gefunden: Zeile, die mit dem Code (2 Hexziffern) beginnt, ab Spalte 0BH "
            "zeigen -- Fehlertexte sind nachladbar",
    0x08DE: "[NAME_PUFFERN] Kommando DE in der Kette suchen; gefunden: B1H + Zeile nach (IX)",
    0x08FC: "[KOMMANDO_STARTEN] Objekt suchen, SEGD einschalten, Leerzeichen ueberspringen, "
            "JP (HL+1) mit DE = Parameter",
    # ---- Kette
    0x0914: "[LIY 0BE8H] (IY+1) = A Anforderung, (IY+2/3) = HL Daten, (IY+4/5) = BC Laenge",
    0x0924: "[RSU 0BC7H] Suche ab KETTE_ANFANG 0AFEH in Segment 1 (FBFEH = 01H)",
    0x092E: "[RS0 0BCAH] Suche ab HL im Segment FBFEH; A = Kennbyte, DE = Name (0 = nur Typ) "
            "[EGOS S.13]",
    0x0930: "Segment des Kettenteils einschalten (die Kette darf Segmente wechseln)",
    0x0937: "Kennbyte >= 1DH: Ende (FFH) bzw. Segmentwechsel (FEH) bzw. Kettenfehler",
    0x093B: "naechstes Objekt = Adresse des Namens + Distanz [EGOS S.7]",
    0x094B: "Name vergleichen: Kettenseite (DE) im Segment SEGD, Suchname (HL) im Segment SEGE",
    0x0958: "Namensende in der Kette = erstes Byte >= 7FH (FFH laut [EGOS S.6], im ROM auch B0H)",
    0x0966: "Suchname muss dort auch zu Ende sein (kein Namenszeichen mehr)",
    0x096C: "gefunden: HL = 2. Kennbyte (Endebyte), BC = 1. Kennbyte, A = Segment, CY = 0",
    0x097D: "FEH: Segmentwechsel 'FE adrL adrH seg' -> weiter bei adr im Segment seg",
    0x0981: "FFH -> C7H 'nicht gefunden', sonst 48H 'Kettungsfehler'",
    0x098B: "[SEGC_EIN] Segment SEGC (FBFBH) wieder einschalten",
    0x099E: "[RSF 0BC4H] Freibereich der Laenge BC suchen [EGOS S.13]: freie Bloecke (Kennbyte 00) "
            "zusammenfassen und teilen",
    0x0A07: "kein Platz: A = FDH, CY",
    0x0A0D: "[RLL 0BBBH] Objekt HL loeschen: Kennbyte 00, Inhalt FFH [EGOS S.12]",
    0x0A2E: "[IST_NAMENSZEICHEN] CY = 1 fuer 0-9, A-Z, 5FH..7AH",
    0x0A43: "[HBS 0BEBH] A als zwei Hexziffern nach (HL)",
    0x0A59: "[ZLE 0BD0H] Zahl ab (DE) lesen: A Bit 0-4 Stellenzahl, Bit 7 = hex [EGOS S.14]",
    # ---- NIXE
    0x0A98: "[NIXE 0BF1H] (IY+0) = logische Geraetenummer in der ADT FC00H suchen",
    0x0AB6: "nicht in der ADT: C7H",
    0x0ABB: "Aktivbit (Bit 7) fehlt: C2H 'nicht bereit'",
    0x0AC0: "Segment + Adresse des Treiberkopfes; Rueckkehr ueber 0ADEH stellt SEGC wieder her",
    0x0AD2: "Namen ueberspringen bis FFH, dann JP auf den Treibercode",
    0x0ADE: "Rueckkehr vom Treiber: altes Segment",
    0x0AE5: "[LOUT 0BF4H] Zeile HL auf log. Geraet 3 (WRITE LINE ueber NIXE)",
    0x0B05: "CON-Treiber -> 0351H",
    0x0B10: "Kommando INIT -> 0C00H",
    0x0B17: "[KERN_RESET] RESET-Routine des Kerns: CON INITIALIZE, Segmentzellen FBFBH-FBFFH = 01H",
    0x0B32: "[LZOU 0BF7H] Leerzeile auf log. Geraet 3",
    # ---- Sprungleiste
    0x0BB5: "Kommandoschleife (Rueckkehr der Kommandos ohne 'OK')",
    0x0BB8: "Zeichen A an Position DE zeichnen",
    0x0BBB: "RLL", 0x0BBE: "OUT", 0x0BC1: "WBN", 0x0BC4: "RSF", 0x0BC7: "RSU", 0x0BCA: "RS0",
    0x0BCD: "TST", 0x0BD0: "ZLE", 0x0BD3: "WLN", 0x0BD6: "BFE", 0x0BD9: "CLB", 0x0BDC: "ROL",
    0x0BDF: "ROU",
    0x0BE2: "TST mit Abbildung der Codes >= 80H (nicht in [EGOS S.12-15])",
    0x0BE5: "Fehler A anzeigen (nicht in [EGOS])",
    0x0BE8: "LIY", 0x0BEB: "HBS", 0x0BEE: "CON", 0x0BF1: "NIXE", 0x0BF4: "LOUT", 0x0BF7: "LZOU",
    0x0BFA: "IY-Grundzustand (nicht in [EGOS])",
    # ---- INIT
    0x0C00: "[INIT] Parameter: letztes Segment als Einsernummer (2 Hexziffern, Vorgabe 01H)",
    0x0C07: "je Segment: 4000H = Kennbyte 00 (frei), Distanz 7FF9H, FFH, Rest FFH ...",
    0x0C1E: "... BFFCH = 'FE 00 40 seg*2': Weiterkettung auf 4000H des naechsten Segments",
    0x0C2B: "naechstes Segment, bis L leer",
    0x0C2F: "letztes Segment: BFFCH-Kettung auf C000H (Segment 01) [EGOS S.6-7]",
    0x0C3B: "C000H-FFFFH mit FFH fuellen -- auch Stapel und FC00H-FFFFH (Schreibschutz muss aus sein)",
    0x0C42: "[INIT_KETTE] Segment 1: 4000H frei bis BAF8H (Distanz 7AF5H)",
    0x0C48: "BAF8H: Directory (Kennbyte 04, Distanz 0501H -> BFFCH) [EGOS S.11]",
    0x0C53: "FEFDH = 18H,FEH (JR $) fuer RST 38H",
    0x0C59: "FEE8H = EDH,45H (RETN) fuer NMI",
    0x0C5F: "FBF6H = F000H [?] (BASIC-Systemzelle)",
    0x0C65: "ADT -> FC00H",
    0x0C70: "RST 0: Neustart wie RESET [EGOS S.9]",
    0x0C86: "[IY_GRUNDZUSTAND 0BFAH] IY_VORGABE -> F5F1H.., IY = F5F1H",
    # ---- KLOG
    0x1008: "[KLOG] logischer Kassettentreiber (ueber NIXE, IY = Vektor): Register retten",
    0x101C: "(IY+1): 00 INITIALIZE, 04 OPEN, 06 CLOSE, 0AH READ BINARY, 0EH WRITE BINARY "
            "[EGOS S.17]; sonst C1H",
    0x103A: "noch reserviert? dann freigeben (41H)",
    0x1043: "Rueckkehr: Register zurueck; Fehler (IY+10 Bit 6) -> Fehlerrueckkehr (IY+8/9), "
            "Interruptbetrieb -> (IY+6/7)",
    0x107B: "[KLOG_INIT] INITIALIZE = MB_INIT",
    0x1089: "[KLOG_CLOSE] reserviert: zurueckspulen, freigeben",
    0x1094: "[KLOG_OPEN] Folgevektor (IY+11/12) -> FA2AH: +0 Art (00 Eingabe, 02 neue Datei), "
            "+1 Laufwerk, +2 Namenslaenge, +3.. Name [?]",
    0x109E: "neue Datei 'DIRECTORY' = Kassette formatieren (KLOG_FORMAT)",
    0x10B0: "Laufwerk reservieren, zurueckspulen, ersten Block lesen und als Verzeichnis pruefen",
    0x10CD: "Directory (Kennbyte 04) im RAM suchen und vom Band laden (1 + 9 Bloecke zu 128 Byte)",
    0x10FC: "Kennung 'DIRECTORY' im ersten Block?  Sonst D1H 'Identifizierungsfehler Kassette'",
    0x110C: "Eingabe -> Datei suchen (11F7H), neue Datei -> anlegen (1177H), sonst D9H",
    0x111C: "[KLOG_FORMAT] reservieren, zurueckspulen, Block 0 = 89H 'DIRECTORY' 00 01 FFH + 0..0 "
            "(128 Byte), dann 14 x 128 Byte (FFH + 7FH x 00) = leeres Verzeichnis, Bandmarke, "
            "Schlussluecke [EGOS2 S.5]",
    0x1177: "neue Datei: Name darf nicht schon im Verzeichnis stehen (D0H), Eintrag anlegen, "
            "an das Ende (Bandmarken zaehlen) fahren",
    0x11F7: "Eingabe: Name im Verzeichnis suchen (D4H), zur n-ten Bandmarke fahren, ersten "
            "Block (Kopf) nach FA64H",
    0x1239: "[KLOG_SCHREIBEN] WRITE BINARY: Laenge auf 128er-Bloecke runden, Bloecke schreiben, "
            "Bandmarke + Schlussluecke, Verzeichnis zurueckschreiben",
    0x1268: "[KLOG_LESEN] READ BINARY: Bloecke lesen und aus dem Treiberpuffer F911H kopieren",
    0x128F: "[NAMEN_VERGLEICH] B Byte (HL) mit (DE); CY = verschieden",
    0x12A1: "[VERZ_SCHREIBEN] Verzeichnis zurueckspulen und neu schreiben (bis zu 7 Versuche)",
    0x12D4: "[VERZ_SUCHEN] Verzeichniseintrag mit Namen (HL) suchen; Eintrag = Laenge|Name..; "
            "CY = gefunden",
    0x1335: "[LAENGE_RUNDEN] (IY+4/5) auf ein Vielfaches der Blocklaenge (F920H) runden, DE = Bloecke",
    0x135E: "[MB_FEHLERPRUEFUNG] Fehler 18H (Bandmarke beim Kontrolllesen) -> eine Bandmarke weiter",
    0x1371: "[MB_RESERVIEREN] Kommando 31H, (IX+3) = Laufwerk",
    0x1378: "[MB_FREIGEBEN] Kommando 41H",
    0x137C: "[MB_BLOCK_LESEN] Kommando 02H",
    0x1380: "[MB_RUECKSPULEN] Kommando 21H; danach warten, bis Bandanfang (Status Bit 3) oder "
            "nicht bereit (Bit 4)",
    0x1397: "[MB_BM_VOR] Kommando 71H, A = n",
    0x139E: "[MB_BM_RUECK] Kommando 75H, A = n",
    0x13A5: "[MB_BM_SCHREIBEN] Kommando 51H",
    0x13A9: "[MB_SCHLUSSLUECKE] Kommando 61H",
    0x13AD: "[MB_BLOCK_SCHREIBEN] Kommando 00H: HL = Daten, BC = Laenge (B = H-Teil, C = L-Teil)",
    0x13BB: "[MB_AUFTRAG] (IX+4) = Kommando, Eintrittspunkt (IX+5/6) = MB_FERTIG, physischen Treiber "
            "starten und auf den Eintrittspunkt warten (JR $ -- der Treiber kehrt aus dem "
            "Interrupt dorthin zurueck)",
    0x13CD: "[MB_FERTIG] Status (IX+0): Fehler (Bit 7) oder nicht bereit (Bit 4)? Fehlercode (IX+1) "
            "auf den logischen Code abbilden",
    0x13E4: "11H/<12H -> C2H, 12H -> CEH 'Laufwerk ungueltig', 13H -> C2H, 14H -> C9H 'EOF', "
            "15H -> C2H, 16H -> C9H, 17H -> C5H, 18H -> C6H, 1AH -> C2H, sonst C6H",
    # ---- Bedienprogramme
    0x142A: "[KINIT] = FORMAT lwnr [EGOS2 S.5]: OPEN fuer neue Datei 'DIRECTORY' auf Laufwerk lwnr",
    0x145D: "[READ] READ lwnr,Dateiname[,R] [EGOS2 S.5-6]",
    0x146D: "[WRITE] WRITE lwnr,Dateiname[,typ | ,seg,rama,rame] [EGOS2 S.6]",
    0x1474: "IY -= 13, Folgevektor FA40H; lwnr (Ziffer, Vorgabe 1) -> FA41H",
    0x149C: "Dateiname (Buchstabe, dann bis ',', '.', ' ' oder FFH) -> FA42H (Laenge) + FA43H..",
    0x14D0: "nach ',': Segment (Einsernummer, genau ein Bit) oder 'R'/'A'/'B'",
    0x1529: "seg,rama,rame: Bereich (FA24H/FA26H)",
    0x154C: "Fehler 44H 'Operand fehlend oder ungueltig', Fehlerstelle -> (IY+11)",
    0x156D: "Typ A: Arbeitsbereich Kennbyte 13H 'E' suchen; Typ B: 1CH 'F' [?]",
    0x15D2: "[BEDIEN_AUSFUEHREN] KLOG ueber NIXE: OPEN, dann READ BINARY bzw. WRITE BINARY, CLOSE",
    0x15DD: "IX = physischer Vektor (F900H)",
    0x15FB: "WRITE: Dateikopf FA64H.. (Satzlaenge 1, B0H, 0100H, Bereichslaenge in 256-Byte-"
            "Einheiten FA71H, Ladeadresse FA78H) [?]",
    0x1653: "READ: Laenge aus dem Kopf (F91EH Bloecke x F920H), Ladeadresse F925H (0 = freien RAM "
            "suchen, RSF) bzw. RAM-Kette durchsuchen",
    0x170B: "CLOSE",
    0x1724: "IY -= 13, (IY+0) = 4 (KLOG), (IY-1) = 01 (Segment), Folgevektor FA40H",
    # ---- physischer Treiber
    0x17C4: "[MB_TREIBER 1FFDH] IX = Vektor: +0 Status, +1 Fehler, +2 Basis (30H), +3 Laufwerk "
            "(1/2), +4 Kommando, +5/6 Eintrittspunkt, +7/8 Blockadresse, +9/10 Blocklaenge H/L "
            "bzw. n [EGOS2 S.3].  C = Basis + 4 = 34H (Steuer-PIO Tor A)",
    0x17C8: "noch besetzt (Bit 0) -> Fehler 15H",
    0x17D0: "Status: Fehlerbit loeschen, besetzt setzen, Fehlercode 0",
    0x17DC: "NUR Laufwerk 1 ist ausprogrammiert: Laufwerk <> 1 -> Fehler 12H [Befund]",
    0x17E6: "31H Reservieren ein / 41H aus",
    0x17F5: "Steuer-PIO A (34H): Bit 1..0 muss Laufwerk 1 reserviert zeigen (/RES1 = 0, /RES2 = 1) "
            "-> sonst 11H 'nicht reserviert'; Bit 3 /AWA1 muss 1 sein",
    0x1803: "Steuer-PIO B (35H) Bit 4 /NIB1 = 1 (bereit)?  sonst 1AH",
    0x180D: "Tor A Bit 4 TRV = 1: Band laeuft noch (vorheriger Auftrag) -> nur merken, warten",
    0x1812: "Kommando (Bit 3 ausgeblendet): 00 Schreiben, 02 Lesen, 2xH Rueckspulen, 5xH Bandmarke, "
            "6xH Schlussluecke, 7xH Bandmarke suchen (Bit 2 = rueckwaerts); 11H/15H (Block vor/"
            "rueck, K5200) fehlen -> 10H 'unerlaubter Kommandocode' [Befund]",
    0x183D: "[MB_WEITER] Rueckruf nach einer Wartezeit: angefangenen Auftrag fortsetzen",
    0x189B: "[MB_SCHREIBEN] Laenge pruefen (20H..100H), Schreibschutz, Vorluecke",
    0x18A1: "Daten (IX+7/8) -> F911H.., CRC dahinter (erst low, dann high), dann AAH Postambel",
    0x18BA: "CRC-16/ARC ueber die Daten (CRC16 1DD1H)",
    0x18CD: "[MB_SCHREIB_START] Vorluecke abwarten (F90CH)",
    0x18DB: "Daten-PIO B Vektor B2H (Mitlesen) + Blindeingabe, Daten-PIO A Interrupt ein, "
            "erstes Byte AAH = Praeambel an 30H",
    0x18F6: "Rueckruf aus MB_WARTEN_LANG: Vorluecke vorbei",
    0x1904: "[MB_LESEN] Lesen: Puffer F910H, CRC-Zelle F90DH = 0",
    0x191C: "warten, bis BMS (Tor B Bit 5) 255 Abfragen lang 0 ist = in einer Luecke",
    0x1926: "Daten-PIO B: Vektor B4H, Interrupt ein, Blindeingabe; Zeitueberwachung 1B31H",
    0x1939: "[MB_RES_EIN] KMBG 1 reservieren und anwaehlen (MB_KMBG_RES)",
    0x193E: "Schreibschutz pruefen: AUF = 1 setzen und STA (Tor B Bit 6) lesen -- STA = 1 bei "
            "'Aufzeichnen verboten' + AUF (K5020 BD S.10) -> Status Bit 2",
    0x1957: "Status: bereit, reserviert",
    0x196A: "[MB_RES_AUS] freigeben",
    0x196F: "[MB_UMSPULEN] Rueckspulen: TRV+TRR (Umspulen), am Bandanfang (AEB) stoppen, Vorspann "
            "abfahren; Status Bit 3 = Bandanfang",
    0x19C9: "[MB_BANDMARKE] Bandmarke = Block OHNE Daten: AAH, CRC 00 00, AAH (Laenge 0)",
    0x19E0: "[MB_SCHLUSS] Schlussluecke: 22H x 250 ms mit AUF laufen lassen",
    0x19F5: "[MB_BM_SUCHEN] n (IX+10) Bandmarken suchen; 71H vorwaerts schnell (Tor A 50H = TRV+HGE), "
            "75H rueckwaerts (60H = TRR+HGE)",
    0x1A48: "Bloecke zaehlen ueber BMS: ein Block ist eine Bandmarke, wenn BMS bei 159 Abtastungen "
            "hoechstens 13H mal 1 ist (kurzer Block) [Deutung]",
    0x1B1F: "Fehler 12H unerlaubte Laufwerksnummer",
    0x1B23: "Fehler 11H Geraet nicht reserviert",
    0x1B2D: "Fehler 13H unerlaubte Blocklaenge",
    0x1B31: "Zeitueberwachung beim Lesen abgelaufen: Fehler 14H Aufzeichnungsende",
    0x1B38: "Fehler 15H Geraet besetzt",
    0x1B43: "Fehler 10H unerlaubter Kommandocode",
    0x1B47: "Fehler 18H Bandmarke beim Kontrolllesen",
    0x1B4C: "Fehler 19H Aufzeichnen verboten",
    0x1B50: "Fehler 17H unerlaubte Blocklaenge beim Lesen (Pufferueberlauf)",
    0x1B5F: "[MB_FEHLER] (IX+1) = Fehlercode, Status Bit 7",
    0x1B69: "[MB_AUS_ISR] Vektor FFB0H: Daten-PIO A will das naechste Byte",
    0x1B78: "naechstes Byte aus dem Puffer an 30H",
    0x1B86: "alles ausgegeben: Interrupt aus, Nachluecke 110 ms, AUF aus, Piep",
    0x1B9B: "Vergleichsfehler beim Mitlesen -> Wiederholen (1F4FH)",
    0x1BAE: "[MB_MITLESEN_ISR] Vektor FFB2H: beim Schreiben gleichzeitig zurueckgelesenes Byte "
            "mit dem Puffer vergleichen (K5200 liest hinter dem Schreibkopf)",
    0x1BC5: "beim ERSTEN Byte (Praeambel) Bit 0 ausblenden: gelesen wird ABH statt AAH [Befund]",
    0x1BCE: "ungleich: F90FH Bit 4",
    0x1BE8: "[MB_EIN_ISR] Vektor FFB4H: Byte vom Band",
    0x1BF0: "AAH: Postambel oder Datenbyte -> 1C3AH prueft BMS",
    0x1BFF: "erstes Byte muss ABH sein (die Praeambel AAH kommt mit gesetztem Bit 0 an), sonst "
            "verwerfen [Befund]",
    0x1C06: "Byte ablegen; Puffer bis FA13H (max. 256 Daten + CRC + AAH), sonst 17H",
    0x1C24: "CRC um 3 Byte verzoegert mitrechnen (die letzten 3 sind CRC und Postambel)",
    0x1C3A: "AAH: kurz warten; BMS noch 1 -> war ein Datenbyte; BMS 0 -> Blockende",
    0x1C5D: "Blocklaenge = Bytes - 3; 0 = Bandmarke (IX+7..10 = 0)",
    0x1C7B: "gelesene CRC (low, high) mit der gerechneten vergleichen; falsch -> F90FH Bit 3",
    0x1C9A: "Fehler -> Wiederholen, sonst fertig; Daten bleiben in F911H (IX+7/8 zeigt darauf)",
    0x1CC7: "[MB_NIB_ISR] Vektor FFB6H: /NIB1 -> 0 = Laufwerk nicht mehr bereit: 1AH",
    0x1CDF: "[MB_AEB_ISR] Vektor FFB8H: Bandende/-anfang (AEB); Richtung aus Tor A Bit 5 (TRR): "
            "rueckwaerts -> Status Bit 3 Anfang, sonst Bit 1 Ende",
    0x1CF9: "Vektor FFBAH: nur RETI",
    0x1CFB: "[MB_ENDE] besetzt loeschen, Register zurueck, in den Eintrittspunkt (IX+5/6) "
            "des Auftraggebers zurueckkehren",
    0x1D15: "[PIEP] 07H an die Tastatur (OUT D0H)",
    0x1D1C: "[MB_STATUS 1FFAH] Status Laufwerk (IX+3) lesen [?]",
    0x1D41: "[MB_RETTEN] Register auf eigenen Stapel (Schachtelung FA17H, bis 4), IX = (F900H), "
            "C = Basis (30H), A = Basis + 4",
    0x1D6D: "[MB_HOLEN] Gegenstueck zu MB_RETTEN",
    0x1DAC: "[MB_PUFFER] Zaehler: Ausgabe = Laenge + 4, Zeiger F910H, (F910H) = AAH",
    0x1DD1: "[CRC16] DE = CRC ueber B Byte ab HL: Polynom 8005H gespiegelt (A001H), Start 0000H, "
            "Bit 0 zuerst = CRC-16/ARC (Pruefwert '123456789' = BB3DH) [nachgerechnet]",
    0x1DF4: "[PIO_INT_EIN] Vektor BAH, Interrupt ein (83H), kurz EI",
    0x1E21: "[MB_WARTEN_10MS] A x 10 ms warten (CTC ASP K1)",
    0x1E64: "[MB_LAENGE_PRUEFEN] Laenge (IX+9 H, IX+10 L) muss 20H..FFH (H = 0) oder 100H sein; sonst 13H",
    0x1E8A: "[MB_AUF_EIN] Schreibschutz (Status Bit 2) -> 19H; sonst AUF = 1 (Tor B Bit 1)",
    0x1EA2: "[MB_KMBG_RES] Tor A = 01H (KMBG 1 reservieren), 260 ms, 09H (anwaehlen), 260 ms; "
            "/NIB1 = 1 -> Z = 0 und 2,5 s warten; sonst freigeben (03H), Z = 1",
    0x1ECB: "[MB_KMBG_FREI] Tor A = 03H, 2,5 s",
    0x1EE7: "[MB_PIO_AUS] alle Interrupts der K5020 aus (03H an 33H/37H/36H/32H)",
    0x1F05: "[MB_WARTEN] A Takte zu 10 ms (CTC K1 A5H/60H), dann JP (HL) aus dem Interrupt",
    0x1F1D: "[MB_WARTEN_LANG] ebenso mit 26,7 ms (A5H/00H)",
    0x1F24: "[MB_ZEIT_ISR] Vektor FFEAH: FA14H herunterzaehlen; bei 0 CTC aus und in den "
            "Rueckruf (FA15H) springen",
    0x1F4F: "[MB_WIEDERHOLEN] Lesefehler -> 21H, Schreibfehler -> 22H, kein Echo -> 23H [EGOS2 S.4]",
    0x1F76: "[MB_INIT] F900H = FA30H (IX-Vektor), Status 0, Basis 30H",
    0x1F8D: "CTC ASP K2 (D6H) Zaehler /2 [?], K5020: 37H, 36H, 35H, 34H, 33H, 32H",
    0x1FB5: "Vektoren: FFEAH (CTC ASP K1) und FFB0H-FFBBH",
    0x1FF7: "MB_INIT",
    0x1FFA: "MB_STATUS",
    0x1FFD: "MB_TREIBER (physischer Treiber, IX)",
    # ---- Segmentprogramme
    0x2004: "JHL: JP (HL) mit Segment A (SEGC := A) [EGOS S.18]",
    0x2007: "JHD: JP (HL) im Segment SEGD",
    0x200A: "JPD: Segment SEGD einschalten, RET",
    0x200D: "CHL: CALL (HL) mit Segment A",
    0x2010: "CHD: CALL (HL) im Segment SEGD",
    0x2013: "CAD: CALL (SP) im Segment SEGD (Stapelaufbau [EGOS S.18])",
    0x201C: "Befehl (3 Byte hinter dem CALL) im Segment SEGD ausfuehren (SEG_AUSFUEHREN) "
            "(nicht in [EGOS])",
    0x201F: "ebenso, C Byte, Segment SEGD", 0x2022: "ebenso, A Byte, SEGD",
    0x2025: "ebenso, C Byte, SEGE", 0x2028: "ebenso, A Byte, SEGE",
    0x202B: "ebenso, C Byte, Segment FBFDH", 0x202E: "ebenso, A Byte, FBFDH",
    0x2031: "ebenso, C Byte, Segment FBFCH", 0x2034: "ebenso, A Byte, FBFCH",
    0x2037: "LDIR aus SEGD nach SEGE", 0x203A: "LDDR aus SEGD nach SEGE",
    0x203D: "L0L: LD A,(HL) mit Segment A", 0x2040: "L0E: LD A,(DE) Segment A",
    0x2043: "L0X: LD A,(IX) Segment A", 0x2046: "L0Y: LD A,(IY) Segment A",
    0x2049: "L1L: LD A,(HL) SEGD", 0x204C: "L1E", 0x204F: "L1X", 0x2052: "L1Y",
    0x2055: "L2L: LD A,(HL) SEGE", 0x2058: "L2E", 0x205B: "L2X", 0x205E: "L2Y",
    0x2061: "S1L: LD (HL),A SEGD", 0x2064: "S1X: LD (IX),A SEGD",
    0x206A: "S2L: LD (HL),A SEGE", 0x206D: "S2X: LD (IX),A SEGE",
    0x2073: "Befehl (C Byte) im Segment A ausfuehren (nicht in [EGOS])",
    0x207C: "SEGC := SEGD, umschalten, JP (HL)",
    0x207F: "OUT (85H): Segment umschalten -- der SPE-2-ROM, aus dem das hier laeuft, bleibt "
            "sichtbar (haengt also NICHT am Segmentregister) [Befund]",
    0x2093: "altes SEGC (FBFBH) auf den Stapel, Rueckkehr ueber 20D4H stellt es wieder her",
    0x20D4: "Rueckkehr der CALL-Programme: altes Segment (H) einschalten",
    0x2189: "[SEG_AUSFUEHREN] Befehl hinter dem CALL nach F8DFH kopieren, 'JP 21B6H' anhaengen, "
            "Segment F8DCH einschalten, ausfuehren (Bereich F8DCH-F8FFH [EGOS S.4])",
    0x21B6: "zurueck: SEGC einschalten, hinter dem Befehl fortsetzen",
    # ---- RELAD/VERA/DIR
    0x2209: "[RELAD] RELAD Name[,Name..]: Verschiebeadresstabelle (Kennbyte 10H) und Programm "
            "(Kennbyte 01) suchen, jede eingetragene Adresse um die Ladeadresse erhoehen",
    0x225A: "Fehler 44H",
    0x2298: "[VERA_DIFF] HL = (IY-1..0) - (IX-1..0)",
    0x22A8: "[VERA] Verschiebeadresstabelle erstellen [EGOS S.19-20]: zwei verschieden gebundene "
            "Kopien (IX, IY) byteweise vergleichen; Unterschiede = zu verschiebende Adressen",
    0x22D6: "(nicht angesprungen [Befund])",
    0x2337: "[DIR] Directory (Kennbyte 04) zeigen: Name ... '%' Dateinummer; 17H Zeilen je "
            "Seite, weiter mit ENTER (11H)",
    # ---- LADT usw.
    0x2408: "[LADT] ADT FC00H zeigen: 'Name...%Segment.Adresse  log.Nr' [EGOS S.10]",
    0x2446: "[ADT_ZEILE] Zeile 'Name.....%ss.aaaa' nach FB80H",
    0x2491: "[DEFINE] je aktivem ADT-Eintrag: 'Name ...> alte Nummer' zeigen, neue Nummer (2 "
            "Hexziffern) lesen; 13H (OFF) bricht ab [EGOS S.10-11]",
    0x250D: "[ACT] ACT Name[,Name..]: Treiber (Kennbyte 02) suchen und mit log. Nummer 0 in die "
            "ADT eintragen (hoechstens 14H Eintraege, sonst CFH), dann LADT [EGOS S.11]",
    0x2558: "[DEACT] ohne Namen: ADT ab dem 5. Eintrag (FC10H) loeschen; mit Namen: Eintrag "
            "streichen und Tabelle nachziehen [EGOS S.11]",
    0x25BD: "[EINSCHALTMELDUNG] FB60H = 87H; doppelt hoch 'mc 80.30. microcomputer' ab Zeile 9, "
            "Spalte 0DH; normal 'Version 3.0/1,2' ab Zeile 0CH, Spalte 1DH",
    0x2611: "[CAT] CAT [Kennbyte]: ohne Parameter Kennbyte 01",
    0x2626: "[CATA] alle Objekte (C = 0)",
    0x262C: "[CAT_AUSGABE] Kette ab 0AFEH (Segment 1) durchlaufen, FEH-Eintraege folgen; Zeile "
            "'Name....%KB %Segment.Adresse'; 17H Zeilen je Seite [EGOS S.9-10]",
    0x26D7: "[RKET] RKET seg,von,nach: Kettung im Segment von 'von' auf 'nach' fuehren; FFH an "
            "'von' wird freier RAM [EGOS S.12]",
}

# Daten mit Namen fuer Operanden (LD HL,nnnn usw.)
DATENNAMEN = {
    0x041B: "TAB_VIS", 0x044B: "SIO_A_ISR", 0x046F: "TASTE_ISR", 0x08D1: "TXT_ERROR",
    0x08D9: "TXT_PROMPT", 0x08DA: "TXT_OK", 0x08DD: "TXT_ZEILENREST", 0x0AFE: "KETTE_ANFANG",
    0x0B41: "LEERZEILE", 0x0C71: "ADT_VORGABE", 0x0C95: "IY_VORGABE", 0x1417: "TXT_DIRECTORY",
    0x1779: "NAME_E", 0x177B: "NAME_F", 0x1FD1: "MB_TABELLE", 0x1FE5: "MB_VEKTOREN",
    0x25E4: "MELDUNG_1", 0x08CD: "NAME_T", 0x08CF: "NAME_B", 0x1F4B: "CTC_ZEIT",
}


# --------------------------------------------------------------------------------------
# Maschinerie
# --------------------------------------------------------------------------------------
def lade_bild():
    bild = b""
    for datei, adr, md5 in ABZUEGE:
        d = open(os.path.join(EPROMS, datei), "rb").read()
        assert hashlib.md5(d).hexdigest() == md5, f"{datei}: MD5 weicht ab"
        assert len(bild) == adr, f"{datei}: Ladeadresse {adr:04X} passt nicht"
        bild += d
    assert len(bild) == ENDE
    return bild


def kette(bild, anfang=0x0AFE):
    """Die ROM-Kette ab KETTE_ANFANG: Liste (Adresse, Kennbyte, Name, Codeanfang)."""
    res = []
    a = anfang
    while a < len(bild):
        kb = bild[a]
        if kb >= 0x1D:
            break
        dist = bild[a + 1] | bild[a + 2] << 8
        n = a + 3
        while bild[n] < 0x7F:
            n += 1
        res.append((a, kb, bild[a + 3:n].decode("ascii"), n + 1))
        a = a + 3 + dist
    return res, a


LINE_RE = re.compile(r'^([0-9A-F]{4})  ((?:[0-9A-F]{2} ?)+)\s*\t(.*?)\s*$')
LABEL_RE = re.compile(r'^(\S+):$')


def disasm(pfad):
    args = [sys.executable, TOOL, pfad, "--org", "0", "--no-strings"]
    for e in sorted(set(EINSPRUENGE)):
        args += ["--entry", f"0x{e:04X}"]
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    befehle, labels, pending = {}, {}, None
    for z in out.splitlines():
        m = LABEL_RE.match(z)
        if m:
            pending = m.group(1)
            continue
        m = LINE_RE.match(z)
        if not m:
            continue
        a = int(m.group(1), 16)
        mn = m.group(3).replace("\t", " ")
        if pending:
            labels[a] = pending
            pending = None
        if mn.startswith("DB "):
            continue
        befehle[a] = (len(m.group(2).split()), mn)
    return befehle, labels


def datenbereich(a):
    for d in DATEN:
        if d[0] <= a < d[1]:
            return d
    return None


def hexb(bild, a, n):
    return " ".join(f"{b:02X}" for b in bild[a:a + n])


def zeile(bild, a, n, mn, k=""):
    return f"{a:04X}  {hexb(bild, a, n):<14}\t{mn}" + (f"\t\t;{k}" if k else "")


def db_liste(bs):
    return ",".join(f"{b:02X}H" if b < 0xA0 else f"0{b:02X}H" for b in bs)


def db_text(bs):
    teile, s = [], ""
    for b in bs:
        if 0x20 <= b < 0x7F and b != 0x27:
            s += chr(b)
        else:
            if s:
                teile.append(f"'{s}'")
                s = ""
            teile.append(f"{b:02X}H" if b < 0xA0 else f"0{b:02X}H")
    if s:
        teile.append(f"'{s}'")
    return ",".join(teile)


def zeichenname(c):
    return f"'{chr(c)}'" if c != 0x27 else "Apostroph"


def daten_zeilen(bild, d):
    lo, hi, art, kom = d
    z = []
    if art == "kette":
        n = lo + 3
        while bild[n] < 0x7F:
            n += 1
        z.append(zeile(bild, lo, 3, "DB\t" + db_liste(bild[lo:lo + 3]), kom))
        if n > lo + 3:
            z.append(zeile(bild, lo + 3, n - lo - 3, "DB\t" + db_text(bild[lo + 3:n]), "Name"))
        z.append(zeile(bild, n, 1, "DB\t" + db_liste(bild[n:n + 1]), "Endebyte"))
        assert n + 1 == hi, f"Kettenkopf {lo:04X}: Ende {n + 1:04X} statt {hi:04X}"
        return z
    if art == "text":
        z.append(zeile(bild, lo, hi - lo, "DB\t" + db_text(bild[lo:hi]), kom))
        return z
    if art == "zgtab":
        erst = True
        for a in range(lo, hi, 16):
            e = min(hi, a + 16)
            ws = [bild[i] | bild[i + 1] << 8 for i in range(a, e, 2)]
            c0 = 0x20 + (a - lo) // 2
            c1 = c0 + len(ws) - 1
            k = f"Zeichen {c0:02X}H-{min(c1, 0x7F):02X}H" + (" + Endezeiger" if c1 > 0x7F else "")
            if erst:
                k = kom + ".  " + k
                erst = False
            z.append(zeile(bild, a, e - a, "DW\t" + ",".join(f"0{w:04X}H" if w >= 0xA000
                                                              else f"{w:04X}H" for w in ws), k))
        return z
    if art == "zg":
        tab = [bild[i] | bild[i + 1] << 8 for i in range(0x0CA4, 0x0D66, 2)]
        assert tab[0] == lo, "Zeichengenerator: erster Zeiger"
        z.append(f"; {kom}")
        for c in range(0x20, 0x80):
            a, e = tab[c - 0x20], tab[c - 0x1F]
            assert lo <= a <= e <= hi, f"Zeichen {c:02X}: Zeiger {a:04X}-{e:04X}"
            if e > a:
                z.append(zeile(bild, a, e - a, "DB\t" + db_liste(bild[a:e]),
                               f"{c:02X}H {zeichenname(c)}, Breite {e - a}"))
        rest = tab[-1]
        for a in range(rest, hi, 16):
            e = min(hi, a + 16)
            z.append(zeile(bild, a, e - a, "DB\t" + db_liste(bild[a:e]),
                           "hinter dem Endezeiger (unbenutzt)" if a == rest else ""))
        return z
    # fuell/hex/sprung: 16 Byte je Zeile, Kommentar an der ersten
    for a in range(lo, hi, 16):
        e = min(hi, a + 16)
        k = kom if a == lo else ""
        if art == "fuell" and not k:
            k = ""
        z.append(zeile(bild, a, e - a, "DB\t" + db_liste(bild[a:e]), k))
    if art == "fuell" and not kom:
        pass
    return z


def operand(mn, a):
    """Tool-Labels in Operanden: bei Spruengen die eigenen Namen, sonst Hexzahl/Datenname."""
    op = mn.split()[0]

    def sub(m):
        adr = int(m.group(2), 16)
        if op in ("JP", "JR", "CALL", "DJNZ"):
            return NAMEN.get(adr, m.group(0))
        if adr in DATENNAMEN:
            return DATENNAMEN[adr]
        return f"0{adr:04X}H" if adr >= 0xA000 else f"{adr:04X}H"
    mn = re.sub(r'\b(sub_|L)([0-9A-F]{4})\b', sub, mn)
    if op == "LD":
        m = re.match(r'^(LD (?:HL|DE|BC|IX|IY),)([0-9A-F]{4})H$', mn)
        if m and int(m.group(2), 16) in DATENNAMEN:
            mn = m.group(1) + DATENNAMEN[int(m.group(2), 16)]
    return mn


def formatiere(bild):
    befehle, tlabels = disasm(ZWISCHEN)
    zeilen, benutzt = [], set()
    ziele = set()
    for a, (n, mn) in befehle.items():
        for m in re.finditer(r'\b(?:sub_|L)([0-9A-F]{4})\b', mn):
            ziele.add(int(m.group(1), 16))
    a = 0
    while a < ENDE:
        if a in ABSCHNITTE:
            zeilen.append("")
            zeilen.append(f"; ---- {ABSCHNITTE[a]} ".ljust(96, "-"))
        d = datenbereich(a)
        if d:
            assert d[0] == a, f"Datenbereich {d[0]:04X} von Code ueberlappt (bei {a:04X})"
            nm = DATENNAMEN.get(a) or NAMEN.get(a)
            if nm:
                zeilen.append(f"{nm}:")
            zeilen += daten_zeilen(bild, d)
            for x in C:
                if d[0] <= x < d[1]:
                    raise AssertionError(f"Kommentar {x:04X} liegt in Daten {d[0]:04X}")
            a = d[1]
            continue
        assert a in befehle, f"{a:04X}: kein Befehl decodiert (Einsprung fehlt?)"
        n, mn = befehle[a]
        for i in range(1, n):
            assert datenbereich(a + i) is None, f"Befehl {a:04X} ragt in Daten"
        if a in NAMEN:
            zeilen.append(f"{NAMEN[a]}:")
        elif a in tlabels or a in ziele:
            zeilen.append(f"{tlabels.get(a, f'L{a:04X}')}:")
        k = C.get(a, "")
        if k:
            benutzt.add(a)
        zeilen.append(zeile(bild, a, n, operand(mn, a), k))
        a += n
    verwaist = sorted(set(C) - benutzt)
    assert not verwaist, "Kommentare ohne Befehl bei " + ", ".join(f"{x:04X}" for x in verwaist)
    zeilen.append("")
    zeilen.append("\tEND")
    return zeilen


def kopf(bild):
    k, ende = kette(bild)
    kz = "\n".join(f";   {a:04X}  KB {kb:02X}  {name or '(ohne Namen)':10s} Code ab {c:04X}"
                   for a, kb, name, c in k)
    md5s = "\n".join(f";          {d:10s} {adr:04X}H  MD5 {m}" for d, adr, m in ABZUEGE)
    return f"""; ============================================================================================
; egos.prn  -  MC80.3x Betriebssystem EGOS 30.1 ('mc 80.30. microcomputer Version 3.0/1,2')
;              ZVE-EPROMs 0000H-1FFFH (4 x 2716) + SPE-2-EPROMs 2000H-27FFH (2 x 2708), 10 KB
;
; Quelle : doc/EPROMS/MC80/mc8030_mame/ (MAME-Satz mc8030):
{md5s}
; Erzeugt: tools/gen_mc80_egos_prn.py (rohes Disassemblat tools/z80_disasm2.py + Handkommentare)
; Doku   : doc/mc80/egos.md (Auswertung, Fassungsvergleich), doc/mc80/hardware_3x.md (Hardware),
;          [EGOS S.n] = mc80.3x-egos.pdf Teil 1, [EGOS2 S.n] = Teil 2 (Magnetband), gedruckte Seite
;
; STATISCHE Analyse -- es gibt noch keinen MC80-Emulator.  [?] = Deutung nach Unterlage oder
; Vermutung; [Befund] = steht so im Code, weicht aber von einer Unterlage ab oder ist neu.
;
; ---- Speicher ------------------------------------------------------------------------------
;   0000-0FFF Kern, VIS-Treiber, Tastatur, Zeichengenerator   1000-1FFF Magnetband
;   2000-27FF SPE-2-ROM (Segmentprogramme, Kommandos)          2800-3FFF Erweiterungs-ROM (leer)
;   4000-BFFF Segmentfenster (OUT 85H, Einsernummer)            C000-FFFF ZVE-RAM
;   RAM-Zellen: F5F1 IY-Grundzustand, F800 Stapel, F880 BASIC-Puffer, F8DC Segmentausfuehrung,
;   F900-FB3F Magnetband (F900 -> IX-Vektor FA30, F910 Blockpuffer), FB40-FB7F VIS/Tastatur,
;   FB80-FBDF Eingabepuffer, FBFB SEGC, FBFC/FBFD Zusatzsegmente, FBFE SEGD, FBFF SEGE,
;   FC00 ADT, FED0-FEE7 Sondertasten, FEE8-FEFF NMI/RST-Aussprung, FF00-FFFF Vektoren (I = FFH)
;
; ---- Die ROM-Kette (Objekte mit Kennbyte, Distanz, Name, Endebyte >= 7FH [EGOS S.6-8]) --------
; Jede Kommando-, Treiber- und RESET-Suche beginnt bei 0AFEH im Segment 1.  Im ROM:
{kz}
;   danach Objekt bei {ende:04X}H (Erweiterungs-ROM); dort FFH = Ende der Kette
;
; ---- E/A, wie EGOS sie benutzt ------------------------------------------------------------
;   30H-37H K5020 (AKB): 30 Daten aus, 31 Daten ein, 32/33 deren Steuerworte, 34/35 Steuer-PIO
;           A/B (Bitbelegung doc/kassette/k5020.md 3), 36/37 deren Steuerworte; NUR Laufwerk 1
;   85H/87H ZVE-PIO Tor B = Segmentregister;  8CH 256-mal im RESET (Schreibschutz, s. 0780H)
;   C0H-CFH VIS 2a: OUT (C),E mit B = Y (unten 0), E = X, C = C0/C1 dunkel/hell, Bit 3 = rechte
;           Haelfte, C4/C5 + B = Bildkommando (00 loeschen, 1A/2A rollen)
;   D0H-D3H ASP-SIO (A Tastatur 9600 7O2), D4H-D7H ASP-CTC (K0 SIO-Takt, K1 Magnetband-
;           Zeitgeber 10 ms, K2 /2 [?], K3 nachgeordneter Tastaturinterrupt)
;   NICHT benutzt: 80H-84H, 86H, 8DH, B0H-B3H (SPE 2), D8H-DBH (ASP-PIO), E0H-EFH (EPR)
;
; ---- Tastaturprotokoll (SIO A, 044BH/046FH) ------------------------------------------------
;   Tastatur -> Rechner: Zeichen 00H-1BH und 20H-7FH; 1CH-1FH sind PRAEFIXE, die fuer die
;   folgenden Zeichen gelten: 1CH normal (Gross/Klein-Tausch nach FB7FH Bit 4), 1DH 2. Ebene [?],
;   1EH Sondertaste (Folgebyte = Bitmaske 40H..01H,00H -> Taste 0..7 -> JP FED0H+3n),
;   1FH Zeichen | 80H.  Rechner -> Tastatur: 07H Piep, 12H/14H (FB7FH Bit 6 aus/ein, Anzeige [?])
; ============================================================================================

"""


ZWISCHEN = None


def erzeuge():
    global ZWISCHEN
    bild = lade_bild()
    import tempfile
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        f.write(bild)
        ZWISCHEN = f.name
    try:
        text = kopf(bild) + "\n".join(formatiere(bild)) + "\n"
    finally:
        os.unlink(ZWISCHEN)
    return text, bild


BYTE_RE = re.compile(r'^([0-9A-F]{4})  ((?:[0-9A-F]{2} ?)+) *\t')


def pruefe(text, bild):
    belegt = [False] * len(bild)
    for z in text.splitlines():
        m = BYTE_RE.match(z)
        if not m:
            continue
        a = int(m.group(1), 16)
        for i, h in enumerate(m.group(2).split()):
            assert not belegt[a + i], f"Byte {a + i:04X} doppelt dargestellt"
            assert int(h, 16) == bild[a + i], f"Byte {a + i:04X} weicht ab"
            belegt[a + i] = True
    fehlt = [i for i, b in enumerate(belegt) if not b]
    assert not fehlt, f"nicht dargestellt: {fehlt[0]:04X}.. ({len(fehlt)} Byte)"
    # Abnahme M1: jeder Einsprung der Softwareschnittstellen [EGOS S.12-19] ist benannt
    for adr, name in [(0x0BBB, "SL_RLL"), (0x0BBE, "SL_OUT"), (0x0BC1, "SL_WBN"),
                      (0x0BC4, "SL_RSF"), (0x0BC7, "SL_RSU"), (0x0BCA, "SL_RS0"),
                      (0x0BCD, "SL_TST"), (0x0BD0, "SL_ZLE"), (0x0BD3, "SL_WLN"),
                      (0x0BD6, "SL_BFE"), (0x0BD9, "SL_CLB"), (0x0BDC, "SL_ROL"),
                      (0x0BDF, "SL_ROU"), (0x0BE8, "SL_LIY"), (0x0BEB, "SL_HBS"),
                      (0x0BEE, "SL_CON"), (0x0BF1, "SL_NIXE"), (0x0BF4, "SL_LOUT"),
                      (0x0BF7, "SL_LZOU"), (0x2004, "JHL"), (0x2007, "JHD"), (0x200A, "JPD"),
                      (0x200D, "CHL"), (0x2010, "CHD"), (0x2013, "CAD"), (0x2037, "SEG_LDIR"),
                      (0x203A, "SEG_LDDR"), (0x2061, "S1L"), (0x206D, "S2X")]:
        assert re.search(rf'^{name}:\n{adr:04X}  C3 ', text, re.M), f"Einsprung {name} {adr:04X}"
    k, ende = kette(bild)
    assert [x[2] for x in k] == ["CON", "INIT", "", "KLOG", "KINIT", "READ", "WRITE", "", "",
                                 "RELAD", "DIR", "LADT", "DEFINE", "ACT", "DEACT", "", "CAT",
                                 "CATA", "RKET"], "ROM-Kette"
    assert ende == 0x2800


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true",
                    help="nichts schreiben: bytegleich zum eingecheckten .prn und vollstaendige "
                         "Abdeckung pruefen (Exit 1 bei Abweichung)")
    args = ap.parse_args()
    text, bild = erzeuge()
    pruefe(text, bild)
    text.encode("ascii")
    if args.check:
        alt = open(ZIEL, encoding="ascii").read() if os.path.exists(ZIEL) else None
        if alt != text:
            print(f"ABWEICHUNG: {os.path.relpath(ZIEL, ROOT)} ist nicht die Ausgabe des Generators "
                  f"(python3 tools/gen_mc80_egos_prn.py ausfuehren)")
            return 1
        print(f"ok: egos.prn bytegleich, {ENDE} Byte abgedeckt")
        return 0
    open(ZIEL, "w", encoding="ascii").write(text)
    print(f"geschrieben: {os.path.relpath(ZIEL, ROOT)}  ({len(text.splitlines())} Zeilen)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
