#!/usr/bin/env python3
"""
Baut doc/EPROMS/K8915/scpx8915_v53_bios.prn -- das kommentierte Listing des BIOS
SCPX 8915 V5.3 (D600H-EFEFH) -- aus dem eingecheckten Speicherbild
doc/EPROMS/K8915/scpx8915_v53_sys.bin (C000H-EFEFH) und den Handkommentaren unten.

Herkunft des Speicherbilds (Arbeitspaket AP-B1, doc/design/16_k8915.md §8a):
    python3 tools/k8915_sysload.py disks/k8915scpx_boot1.hfe --out /tmp/x.bin
    -> Bytes C000H..EFEFH daraus = scpx8915_v53_sys.bin
Mit --neu <abbild> erzeugt dieses Skript das .bin selbst (braucht libk1520disk).

STATISCHE Analyse wie bei tools/gen_k8915_zre_prn.py: es gibt noch keine
K8915Machine, also keine Laufzeitbestaetigung. Rekursiver Abstieg ab den 18
BIOS-Einspruengen, den IM-2-Vektoren (Tabelle D9D5H, zur Laufzeit bei FFD0H)
und den weiteren Einspruengen in EINSPRUENGE; was dabei nicht erreicht wird,
erscheint als DB -- CCP/BDOS (C000H-D5FFH) werden nur abgegrenzt.

Aufruf:  python3 tools/gen_k8915_bios_prn.py [--neu <abbild>] [--ports]
  --ports  druckt nur die Portzugriffe im erreichten Code (Arbeitshilfe).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.setrecursionlimit(20000)   # recursive_decode steigt je Verzweigung eine Ebene ab
from z80_disasm2 import Z80Disassembler  # noqa: E402

BIN = os.path.join(ROOT, "doc/EPROMS/K8915/scpx8915_v53_sys.bin")
OUT = os.path.join(ROOT, "doc/EPROMS/K8915/scpx8915_v53_bios.prn")
ORG = 0xC000
LEN = 12272            # C000H..EFEFH, s. Ladekopf (§4.4)
BIOS = 0xD600

SPRUNGLEISTE = ["BOOT", "WBOOT", "CONST", "CONIN", "CONOUT", "LIST", "PUNCH", "READER",
                "HOME", "SELDSK", "SETTRK", "SETSEC", "SETDMA", "READ", "WRITE", "LISTST",
                "SECTRAN",
                "BIOS18"]      # D633H: JP E292H -- 18. Eintrag, SCPX-Erweiterung (s. dort)


def neu(abbild):
    """Speicherbild ueber k8915_sysload.py erzeugen (nur lesend am Abbild)."""
    import subprocess
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        tmp = os.path.join(d, "bild.bin")
        subprocess.run([sys.executable, os.path.join(HERE, "k8915_sysload.py"), abbild,
                        "--out", tmp], check=True)
        bild = open(tmp, "rb").read()
    open(BIN, "wb").write(bild[ORG:ORG + LEN])
    print(f"geschrieben: {os.path.relpath(BIN, ROOT)} ({LEN} Byte ab {ORG:04X}H)")


# ---- Einsprunge ausser der Sprungleiste -------------------------------------
# Adresse -> Name. Wird beim Kommentieren ergaenzt.
EINSPRUENGE = {
    0xEBC7: "ISR_F0", 0xE965: "ISR_F2", 0xEBEF: "ISR_FE",   # gesetzt in E817H
    0xE752: "VGL_LESEN",   # E7BAH: CALL 0000H wird auf E752H (Lesen) bzw. E76DH (Schreiben) gepatcht
    0xD8F8: "WBOOT_F7FB",  # BOOT legt bei F7FBH 'JP D8F8H' ab; kein Aufrufer in ROM oder BIOS
    0xE955: "E955", 0xE958: "E958", 0xE962: "E962",        # Sprungleiste im Treiber
}

# ---- Code, den nur gepatchte Spruenge erreichen (Varianten je Format, s. E4C2H/E8D8H) --
LINEAR = [(0xE9EB, 0xEA1D), (0xEB16, 0xEBC7), (0xEBD1, 0xEBEF)]

# ---- Datenbereiche im BIOS, die NICHT als Code gelten -----------------------
# (start, ende_exklusiv, beschreibung); Text wird als ASCII gezeigt, sonst DB-Hex.
DATEN = [
    (0xD613, 0xD615, "Rest von PUNCH (RET + 2 Fuellbytes)"),
    (0xD639, 0xD641, "Konfiguration (DISGEN): D639H <>0 = Laufwerk E: (RAM-Disk) zugelassen, "
                     "setzt RADE; D63AH Zeiger F150H (Tastaturpuffer); D63CH Laufwerkszahl (2); "
                     "D63DH Versionsfeld '5.3M' (BIOS+3DH, von RADE geprueft)"),
    (0xD641, 0xD681, "Autostart: 40H Byte, beim Kaltstart nach F150H = TASTATURPUFFER kopiert "
                     "(Zaehler 05, 'rade',CR) -- der CCP 'liest' das Kommando ueber CONIN"),
    (0xD681, 0xD6AD, "F-Tasten-Belegung: Code 91H..9CH, danach Text bis zum naechsten Byte mit "
                     "Bit7; FFH = Ende. F1 'DISGEN53'CR, F7 'M80 =', F8 'LINK', F9 'TP'CR, "
                     "F10 'NSWP'CR, Umsch+BildAuf 'POWER'CR, Umsch+BildAb 'DIR'CR"),
    (0xD6AD, 0xD701, "frei (00H)"),
    (0xD701, 0xD751, "5 DPH (A:..E:), je 16 Byte: XLT=0, DIRBUF EF2AH, DPB, CSV, ALV"),
    (0xD751, 0xD7FF, "5 DPB + Treiberangaben, je 23H Byte (SELDSK: DPH+10 -> IY). A: 5x1024 "
                     "(SPT 80, DSM 389, DRM 127, OFF 2) = cpa800; B: 16x256 (SPT 64, DSM 311); "
                     "C:/D:/E: wie A:. IY+15 Sektorgroesse 0/1/2/3=128..1024, IY+20 Sektoren je "
                     "Spur, IY+24 Laufwerksnummer, IY+21 Seiten"),
    (0xD7FF, 0xD819, "Sektoruebersetzung 26 Sektoren, Versatz 6 (8-Zoll-Standard) -- kein DPH "
                     "verweist darauf (alle XLT=0)"),
    (0xD819, 0xD8A7, "Texte: Einschaltmeldung '>>>>> Konfigurierbare Datenstation K 8915 <<<<<', "
                     "'SCPX 8915 V 5.3 Anpassung: V24 (XON/XOFF)', 'System ?'"),
    (0xD9C7, 0xD9D5, "Init CTC2-K2 (5AH: 07H,01H = 9600 Bd) und SIO2-B (53H: 00,18H Kanalreset, "
                     "WR4=44H, WR3=C1H, WR5=EAH, WR1=17H, WR2=D0H Vektor)"),
    (0xD9D5, 0xD9DD, "IM-2-Tabelle -> FFD0H (I=FFH): SIO2-B Tx-leer D9DDH, Ext/Status D9ECH, "
                     "Rx-Zeichen DA19H, Rx-Sonderfall D9FBH ('status affects vector')"),
    (0xDBE6, 0xDC0F, "Befehle an die K7672, '$'-begrenzt: DBE6H Kaltstart 'ESC[?11l ESC[?19h "
                     "ESC[?22h ESC[?18l' (22h = DCP-Modus), DBF8H 'ESC[?18l', DBFFH BEL, "
                     "DC01H 'ESC[?13h', DC08H 'ESC[?13l' (LED)"),
    (0xDC0F, 0xDC1C, "Tastentabelle Strg (Scancode->Zeichen, Format: Anzahl, Paare)"),
    (0xDC1C, 0xDCA5, "Tastentabelle Grundbelegung, 68 Paare Scancode(Satz 1)->Zeichen; "
                     "3BH..44H (F1..F10) -> 91H..9AH; DIN-Belegung mit [\\] fuer Ae/Oe/Ue"),
    (0xDCA5, 0xDCC2, "Umschalttabelle, nach ZEICHEN geschluesselt ('<'->'>', '3'->'@', "
                     "'7'->'/', '['->'{' ...)"),
    (0xDCC2, 0xDCD5, "Tastentabelle Umschalt, Scancode: Ziffernblock als Cursortasten "
                     "(^H ^X ^D ^E), BildAuf/Ab -> 9BH/9CH"),
    (0xDCD5, 0xDCF2, "Tastentabelle Ziffernblock (ohne Umschalt): Ziffern"),
    (0xDE90, 0xDE99, "Init CTC1-K2 (4AH: 05H,01H) und SIO1-B/Drucker (43H: 18H, WR4=44H, "
                     "WR3=C1H, WR5=68H)"),
    (0xDEB9, 0xDF3C, "frei (00H)"),
    (0xE28A, 0xE292, "toter Code (LD (IX+0),50H / POP IY / JR ...)"),
    (0xE2E3, 0xE2F3, "Autor/Datum des Treibers"),
    (0xE2F3, 0xE2FD, "Konstanten des Treibers [?]"),
    (0xE95B, 0xE962, "Arbeitsbytes des Lese-ISR [?]"),
    (0xEA3A, 0xEA42, "Fuellbytes + JP E9FFH (Sprungziel einer Patchvariante)"),
    (0xEC3A, 0xEC54, "Init-Liste (Port,Wert; FFH = Ende): CTC-ZRE 80H Vektor F8H (K3 -> FEH); "
                     "K5122-PIO1 A (11H) Vektor F0H, Mode 0; PIO1 B (13H) Vektor F2H, Mode 3, "
                     "E/A-Maske F3H, Int.-Steuerwort 37H + Maske FDH (nur Bit1 = Marke); "
                     "PIO2 A (15H) Mode 0 = Schreibdaten, PIO2 B (17H) Mode 1 = Lesedaten"),
    (0xEC54, 0xEC5E, "Init-Liste Zeitgeber: CTC-ZRE K3 (83H) Reset, B7H (Int., Zeitgeber, "
                     "Vorteiler 256), ZK FFH => 2,4576 MHz/256/255 = 37,6 Hz; PIO1 A Int. ein"),
    (0xEC5E, 0xEC8A, "Patchadressen (0-begrenzt) fuer E8D8H: die Code-Bytes, die je Format "
                     "umgeschrieben werden (Lese-ISR E96FH/E9CFH/..., Schreibpfad EAAEH/...)"),
    (0xEC8A, 0xEC92, "Zeiger auf die Patchwerte je Format (IX+8 = 0..3)"),
    (0xEC92, 0xECD1, "Patchwerte je Format (zu EC5EH)"),
    (0xECD1, 0xECE3, "Patchliste Lesen/Schreiben: E9C2H/E9C3H, E7BBH/E7BCH; Werte ECDBH "
                     "(Lesen: CALL E752H) bzw. ECDFH (Schreiben: CALL E76DH)"),
    (0xECE3, 0xECFF, "Patchlisten Kopf 0/1 (E61AH) [?]"),
    (0xECFF, 0xED03, "Wert fuer Port 12H je Format (04H, 88H, 00H, 8CH)"),
    (0xED03, 0xEFF0, "frei (00H) -- Arbeitsbereich; EF00H SP-Rettung der ISRs, EF2AH ISR-Stapel "
                     "und DIRBUF, EFAAH/EFDDH ALV/CSV von A:"),
]

# ---- Kommentare je Adresse --------------------------------------------------
C = {}


def c(addr, text):
    C[addr] = text


# Sprungleiste / Kaltstart / Warmstart
c(0xD612, "PUNCH: nur RET")
c(0xD615, "READER: liefert immer 1AH (^Z = Dateiende)")
c(0xD633, "18. Eintrag (SCPX): JP E292H = Diskettentreiber zuruecksetzen, gerufen von WBOOT")
c(0xD636, "Einhaengepunkt RAM-Disk E: (Laufwerksnummer 4, s. E019H/E225H): hier 'LD A,FFH / "
          "RET' = Fehler; RADE.COM patcht es auf seinen Treiber bei EE00H")
c(0xD8A7, "[BOOT] A8H=87H: Seiten 0-3 RAM, ROM aus (Design-Doc §4.2a)")
c(0xD8AE, "IOBYTE (0003H) = 95H")
c(0xD8B3, "F7E0H..F7FAH loeschen und bei F7FBH 'JP D8F8H' ablegen (IOBYTE setzen + WBOOT) -- "
          "ein fester Einsprung fuer Fremdprogramme; weder ROM noch BIOS rufen F7FBH [?]")
c(0xD8F8, "[Einsprung ueber F7FBH] IOBYTE = 95H, weiter in WBOOT")
c(0xD8C5, "Laufwerk/Nutzer (0004H) = 0, Zaehler Cursoradressierung (F1B7H) = 0")
c(0xD8CC, "Port 61H = B0H: Anzeige 'bereit' (s. E011H)")
c(0xD8D0, "I = FFH, IM 2: Vektortabelle ab FF00H, SIO2-B-Eintraege bei FFD0H (s. D9ACH)")
c(0xD8D7, "Bild loeschen (0CH), Einschaltmeldung ab D81AH ausgeben")
c(0xD8E2, "Drucker-Schnittstelle SIO1-B einrichten (DE82H)")
c(0xD8E5, "Tastatur einrichten (D997H): Puffer leeren, SIO2-B + IM-2-Tabelle, K7672-Kaltstartbefehle")
c(0xD8E8, "Polaritaet von MKE (Port 12H Bit1) erkennen und den Treiber darauf einstellen (E2B6H)")
c(0xD8EB, "Autostart: 40H Byte ab D641H in den Tastaturpuffer F150H ('rade' CR)")
c(0xD8FD, "[WBOOT] DI, A8H=06H (ROM + Bildspeicher sichtbar), SP=F7E0H")
c(0xD905, "03H an CTC-ZRE K3 (83H) und K5122-PIO1 A/B (11H/13H): Zeitgeber und "
          "Disketten-Interrupts aus, bevor der ROM-Lader laeuft")
c(0xD90F, "CALL 0406H = Diskettenlader im Boot-ROM laedt CCP+BDOS neu; A = Ergebnis (0 = gut)")
c(0xD913, "A8H=87H (ROM aus), EI")
c(0xD91F, "Ladefehler: Cursor an den Anfang der letzten Zeile (1730H = 1000H + 23*80)")
c(0xD925, "SIO2-B und IM-2-Tabelle neu (D9ACH)")
c(0xD928, "Diskettentreiber zuruecksetzen (E292H), dann Seite 0 einrichten: JP D603H bei 0000H, "
          "JP C806H (BDOS) bei 0005H, DMA 0080H")
c(0xD950, "JP C000H = CCP, C = Laufwerk/Nutzer")
c(0xD953, "Text ab HL bis '$' ueber CONOUT")
c(0xD960, "[CONST] zehn NOP = Patchplatz; FFH, wenn der Tastaturpuffer F150H nicht leer ist")
c(0xD972, "[CONIN] zehn NOP = Patchplatz; wartet auf F150H<>0, nimmt F151H und schiebt den "
          "Puffer nach (LDIR) -- alles andere macht die Empfangs-ISR DA19H")
c(0xD997, "Tastatur-Init: Puffer und Umschaltzustand F14FH loeschen, SIO2-B einrichten, "
          "Kaltstartbefehle DBE6H an die K7672 senden")
c(0xD9AC, "IM-2-Tabelle D9D5H nach (I)<<8|D0H = FFD0H kopieren, dann Init-Tabelle D9C7H: "
          "2 Byte an 5AH (CTC2-K2), 12 Byte an 53H (SIO2-B)")

# ISRs SIO2-B (Tastatur)
c(0xD9DD, "[ISR SIO2-B Tx leer] WR0 = 28H (Tx-Interrupt zuruecksetzen); eigener Stapel EF2AH")
c(0xD9EC, "[ISR SIO2-B Ext/Status] WR0 = 10H (Ext/Status zuruecksetzen)")
c(0xD9FB, "[ISR SIO2-B Rx-Sonderfall] WR0 = 30H (Fehler zuruecksetzen)")
c(0xDA19, "[ISR SIO2-B Rx-Zeichen = TASTATUR] Scancode (PC/XT Satz 1) von der K7672 im "
          "DCP-Modus; 00H wird verworfen. F14FH = Zustand: Bit0 Strg, Bit1 Feststell, "
          "Bit2 Umschalt, Bit3 XOFF")
c(0xDA2F, "1DH/9DH Strg druecken/loslassen, 2AH/36H bzw. AAH/B6H Umschalt links/rechts, "
          "3AH Feststell (wechselt Bit1), 7EH sendet 'ESC[?18l' an die Tastatur")
c(0xDA57, "keine Umschaltung aktiv -> DB5EH (Loslassen-Codes mit Bit7 werden dort verworfen)")
c(0xDA65, "[Grundbelegung] erst Ziffernblock DCD5H, dann DC1CH; Kleinbuchstaben -> GROSS "
          "(ohne Umschalt liefert die Tastatur Grossbuchstaben)")
c(0xDA88, "[Umschalt] 4FH (Zif.-1) = Drucker-Schnittstelle neu (DE82H); 47H (Zif.-7) ruft "
          "DEABH -- mitten in LISTST, vermutlich veraltete Adresse [?]")
c(0xDAA1, "Umschalttabelle DCC2H (Cursor), sonst DC1CH; Ziffern 1..9 ausser 3/7 -> AND EFH "
          "('1'->'!'), Buchstaben gross ausser bei Feststell, Rest ueber DCA5H")
c(0xDAEF, "[Strg] 45H = Strg+Pause: XOFF-Bit3 wechseln, LED 'ESC[?13h/l' und DC3/DC1 ins "
          "Puffer; Tabellen DC0FH/DCD5H/DC1CH; Buchstaben -> AND 9FH (Steuerzeichen)")
c(0xDB77, "[Zeichen ablegen] 91H..9CH = F-Taste: Text aus D681H; sonst 1 Zeichen. Puffer "
          "F150H (Zaehler) + F151H.. max. 100 Zeichen, bei Ueberlauf BEL an die Tastatur")
c(0xDBC7, "Text bis '$' an die K7672 senden: SIO2-B RR0 Bit2 (Tx leer) pollen, Daten an 52H")
c(0xDBD9, "Tabellensuche: (HL) = Anzahl, dann Paare (Schluessel, Wert); Z + A = Wert bei Treffer")

# CONOUT
c(0xDCF2, "[CONOUT] zehn NOP = Patchplatz. Cursor = Bit7 im Bildspeicher an F1B5H; jeder "
          "VRAM-Zugriff unter DI mit A8H=06H (Seite 0: ROM + K7024 bei 1000H), danach 87H")
c(0xDD1E, "F1B7H <> 0: Cursoradressierung laeuft (ESC, Zeile+80H, Spalte+80H); Zeile >= 18H "
          "= Bild loeschen, Spalte > 4FH = 4FH")
c(0xDD52, "Bit7 des Zeichens loeschen (ist im VRAM das Cursorbit), Steuerzeichen < 20H und "
          "7FH gesondert")
c(0xDD6F, "Steuerzeichen: 0DH CR, 0AH LF (rollt), 08H links, 15H rechts, 18H Cursor nach "
          "1000H, 0CH Bild loeschen, 16H Zeilenrest loeschen, 14H Bildrest loeschen, 1AH hoch, "
          "1BH Cursoradressierung, 7FH links + loeschen, 07H (BEL) wird VERSCHLUCKT; sonst wird "
          "das Zeichen dargestellt")
c(0xDDC2, "Bild loeschen: 06H nach 1000H, Wartezeit (21H*256 Schleifen, ~0,1 s), dann "
          "1000H..177FH (BC=0880H: 80H + 7*256 = 780H Byte) mit 20H")
c(0xDE02, "Cursor weiter; ab 1780H rollen: 1050H..177FH nach 1000H (LDIR), letzte Zeile leeren")
c(0xDE82, "Drucker-Schnittstelle: 2 Byte an 4AH (CTC1-K2), 7 Byte an 43H (SIO1-B)")
c(0xDE99, "[LIST] wartet auf LISTST, Zeichen an SIO1-B (42H)")
c(0xDEA2, "[LISTST] XON/XOFF: liest das LETZTE empfangene Byte aus 42H (ohne RR0-Pruefung); "
          "13H (XOFF) oder 14H = nicht bereit, sonst RR0 Bit2 (Tx leer)")

# Diskette: Sprungleiste, Blockung
c(0xDF3C, "[HOME] Spur 0; Schreibpuffer nur verwerfen, wenn er nicht mehr zu schreiben ist")
c(0xDF4B, "[SELDSK] C < (D63CH) oder C = 4 bei (D639H) <> 0 (RAM-Disk); HL = D701H + 16*C")
c(0xDF87, "[READ] IY = DPB (E2A4H). IY+15 = 0 (128-B-Sektoren): direkt lesen (DFA9H, "
          "Befehl 70H); sonst Blockung ueber den Hostpuffer F1E0H (E0D2H, Digital-Research-"
          "Verfahren)")
c(0xDFB8, "Parameterblock IX = F1B9H fuellen: +0 Befehl (70H Lesen, 50H Schreiben, 80H "
          "Ruecksetzen), +1 Laufwerk (IY+24), +2 Spur, +3 Kopf, +4 Sektor, +5..+9 aus dem DPB, "
          "+10/11 Puffer, +12/13 Zeiger, +14 = Fehlerbuchstabe (Rueckgabe). F1C8H = 5 Versuche")
c(0xE00F, "Port 61H: E0H vor Lesen, D0H vor Schreiben, B0H danach; bei Fehler 60H (Lesen) "
          "bzw. 50H (Schreiben). => Anzeigefeld, aktiv low: Bit4 Lesen, Bit5 Schreiben, "
          "Bit6 bereit, Bit7 Fehler [?]")
c(0xE013, "Laufwerk 4 (E:) -> RAM-Disk D636H, sonst K5122-Treiber E2E1H")
c(0xE03C, "nur Fehler 'K' (4BH) wird wiederholt (bis 5 Mal), jeder andere -> A = FFH")
c(0xE049, "[WRITE] wie READ; C = Schreibart (0 normal, 1 Verzeichnis, 2 unbelegter Block)")
c(0xE171, "Hostpuffer schreiben (50H) bzw. lesen (70H) -- eigener Stapel F692H")
c(0xE292, "[BIOS18] Blockungszustand loeschen, Befehl 80H = Treiber ruecksetzen (Laufwerke "
          "auf Spur 0, E332H)")
c(0xE2A4, "IY := DPB des Laufwerks A (Wort bei D70BH + 16*A)")
c(0xE2B6, "PIO1 B (13H) Mode 3, Maske F7H; liest Port 12H Bit1 = MKE (Marke erkannt) im "
          "Ruhezustand: 0 => Maske F3H, Interruptwort 37H (bei high), JP NZ (C2H); 1 => F7H, "
          "17H (bei low), JP Z (CAH). Eingetragen in EC49H/EC4BH (Init-Liste) und "
          "ECA0H/ECB5H/ECCAH (Patchwerte) -- das BIOS passt sich der Polaritaet von MKE an")

# K5122-Treiber
c(0xE2E1, "[K5122-TREIBER] Einsprung mit IX = Parameterblock; Autor 'M.Schurz 07.04.87'")
c(0xE2FD, "Parameterblock nach F77AH (12 Byte) und 10 Byte Formatangaben nach F786H; Fehler "
          "F775H = 0; EBF0H = 0")
c(0xE32A, "Befehl 80H: Spurtabelle F694H.. mit FFH (Position unbekannt), Laufwerke abwaehlen "
          "(18H = FFH), Interrupts ein (E7D8H/E840H), 'bereit' der Laufwerke erfassen")
c(0xE3A7, "Ende: EBFDH-Pfad (E925H), Fehlerbuchstabe aus F775H nach IX+14, zurueck")
c(0xE3EC, "[Lesen/Schreiben] 3 Versuche Positionieren (F766H..F768H), Spur-/Kopfpruefung; "
          "Kopf >= 2 -> Fehler 'Z' (5AH)")
c(0xE42E, "Laufwerk waehlen: Motor-/Select-Maske F76DH/F75DH, Port 18H (High-Nibble /SE, "
          "Low-Nibble /LCK), s. CLAUDE.md 'Drive select'")
c(0xE4A9, "Port 12H: Formatwert aus ECFFH+IX+8 (Dichte/Laufwerkstyp); Patchwerte fuer den "
          "Lese-ISR nach Format einspielen (E8D8H mit EC5EH)")
c(0xE4EB, "Liste der erwarteten Sektorkoepfe ab F694H aufbauen (Spur, Kopf, Sektor, Laenge) "
          "und je Eintrag die ID-CRC vorausrechnen (E727H) -- der Lese-ISR vergleicht dann nur")
c(0xE51F, "Lesen: CALL-Ziel E7BBH := E752H, Schreiben: := E76DH (Patch ECD1H)")
c(0xE552, "Fehlerbuchstaben F775H: 'W' 57H (Sektor nicht gefunden) [?], 'C' 43H (Daten-CRC), "
          "'K' 4BH (wiederholbar) [?], 'V' 56H (Spur falsch), 'R' 52H (Zeitueberlauf: kein "
          "Index), 'S' 53H (schreibgeschuetzt), 'Z' 5AH (Kopf ungueltig)")
c(0xE5DB, "Rekalibrieren: Motor-Nachlauf F765H, auf Spur 0 fahren")
c(0xE601, "[Schreiben] Port 12H Bit5 = 0 (/WP) -> 'S' (schreibgeschuetzt)")
c(0xE6AE, "[Transfer starten] PIO1 B Interrupt ein (13H = 83H), Lese-Steuerwort F755H mit "
          "Kopfbit nach 10H (/STR=0) -- ab hier arbeitet der ISR E965H je Sektorkopf; die "
          "CPU wartet in der Schleife E6DCH auf F758H Bit0 (fertig) oder Zeitueberlauf")
c(0xE727, "CRC-CCITT (tabellenlos, 4-Bit-Schritte) ueber B Byte ab HL, Startwert F756H/F757H")
c(0xE752, "[Vergleich beim Lesen] gepatchtes CALL-Ziel von E7BAH [?]")
c(0xE76D, "[Schreiben] Kopfeintrag ablegen, IY += 4")
c(0xE7D8, "DI, Interrupts aus: 03H an PIO1 A/B (11H/13H) und CTC-ZRE K3 (83H)")
c(0xE7F3, "Warteschleife A Millisekunden (BDH*13 Takte = 1 ms bei 2,4576 MHz); bricht mit 'R' ab, wenn der Index-Zeitgeber "
          "F764H abgelaufen ist")
c(0xE809, "Port 18H setzen (merkt F76BH), Steuerport 10H in Grundstellung (BBH + /FR)")
c(0xE817, "Vektoren eintragen: (I)FEH -> EBEFH (CTC-ZRE K3 = Zeitgeber), (I)F0H -> EBC7H "
          "(PIO1 A = Index), (I)F2H -> E965H (PIO1 B Bit1 = Marke erkannt); F764H = 0BH "
          "Index-Frist, F765H = 60H Motornachlauf; Init-Liste EC54H")
c(0xE845, "Init-Liste ab HL: (Port, Wert) bis FFH, per OUTI")
c(0xE857, "Positionieren: Differenz Soll-/Ist-Spur, Richtung F758H Bit5, Schritte E88AH, "
          "Beruhigungszeit IX+16")
c(0xE88A, "ein Schritt: 10H = 8BH | DIR(Bit5) | /FR, dann IX+9 Impulse (DPB+19: 1, bei "
          "Doppelschritt 2) ueber E8A6H")
c(0xE8A6, "Schrittimpuls: bei DIR=aus und /TO (12H Bit7) = 0 kein Schritt mehr (Spur 0); "
          "sonst Bit7 (/ST) 0 -> 1, Wartezeit IX+14")
c(0xE8E6, "Transfer beenden: PIO1 B Interrupt aus, /STR=1 (10H), Daten-PIO leeren "
          "(IN 16H, OUT 14H)")
c(0xE925, "Ende eines Auftrags -> EBFDH (Motor-Nachlauf/Abwahl), Rueckkehr ueber E3AAH")
c(0xE946, "Port 12H Bit0 lesen, waehrend Bit2 kurz 0 ist: 'bereit' des gewaehlten Laufwerks")

# Lese-ISR und Schreibpfad
c(0xE955, "Sprungleiste des ISR: E955H -> EA42H (Kopf passt nicht), E958H -> EA94H "
          "(Schreiben), E962H -> E9D1H (Datenfeld lesen)")
c(0xE965, "[ISR PIO1 B = MARKE ERKANNT] liest den Sektorkopf AUSGEROLLT, ohne jede "
          "Statusabfrage: A1 (weitere A1 ueberspringen), FE, Spur, Kopf, Sektor, Laenge, "
          "CRC -- verglichen mit dem erwarteten Eintrag (F692H). Das geht nur, wenn "
          "'IN A,(16H)' die CPU per /WAIT bis zum naechsten Byte anhaelt: die K5122 laeuft "
          "im K8915 im WAIT-Betrieb (Doku K5122 §5.6, Decoder-Ausgang 00), nicht mit /BUSRQ")
c(0xE96E, "JP NZ,0000H: Ziel wird je Format gepatcht (Patchliste EC5EH)")
c(0xE9C2, "Patchstelle (E656H setzt 2BH,DBH = DEC HL / IN A,(16H) fuer den Vergleichslauf)")
c(0xE9D1, "Datenfeld: Lueckenbytes zaehlen, auf naechste Marke warten (12H Bit1), dann die "
          "Daten per INI/IN A,(16H) nach (F74DH) -- Variante je Format ab E9EBH")
c(0xE9EB, "[Variante, nur ueber Patch erreichbar] Datenfeld lesen: A1-Sync ueberspringen, "
          "FBH erwarten, 128 Byte per INI (7EH+2) je Durchlauf")
c(0xEA1D, "Transfer fertig: F758H Bit0 = 1, RETI")
c(0xEA42, "Kopf passt nicht: gelesene Spur/Sektor nach F749H, ID-CRC pruefen, Spurfehler "
          "merken (F758H Bit6)")
c(0xEA94, "[Schreiben] nach dem Kopf: Luecke abzaehlen, /WE (10H), dann per OUT (C),D an "
          "14H: 00H-Luecke, 3 x A1H, FBH, die Daten per OUTI und 2 CRC-Bytes -- ebenfalls "
          "ohne Statusabfrage, also /WAIT auch beim Schreiben")
c(0xEB16, "[Variante, nur ueber Patch erreichbar] Schreib-/Lese-Varianten je Format")
c(0xEBC7, "[ISR PIO1 A = INDEX] Index-Frist F764H neu auf 0BH")
c(0xEBD1, "[Variante] ISR mit eigenem Stapel F744H: Motor-Nachlauf F765H zaehlen [?]")
c(0xEBEF, "[ISR CTC-ZRE K3 = ZEITGEBER 37,6 Hz] F764H (Index-Frist, 11 Takte = 290 ms) "
          "und F765H (Motornachlauf) herunterzaehlen; abgelaufen -> EBFDH")
c(0xEBFD, "Frist abgelaufen: Motoren aus (18H: Low-Nibble /LCK = 1), bei laufendem Auftrag "
          "Laufwerke abwaehlen, F758H Bit1 (Abbruch), Interrupts neu (E7C1H)")


def lade():
    data = open(BIN, "rb").read()
    assert len(data) == LEN, f"{BIN}: {len(data)} statt {LEN} Byte"
    return data


def disassembliere(data):
    dis = Z80Disassembler(data, org=ORG)
    namen = {}
    for i, name in enumerate(SPRUNGLEISTE):
        a = BIOS + 3 * i
        namen[a] = f"J{name}"
        if data[a - ORG] == 0xC3:
            namen.setdefault(data[a - ORG + 1] | data[a - ORG + 2] << 8, name)
        # PUNCH (D612H) und READER (D615H) sind kein JP, sondern stehen an Ort und Stelle
        dis.recursive_decode(a)
    # IM-2-Tabelle D9D5H (8 Byte, zur Laufzeit bei FFD0H)
    for i in range(4):
        v = data[0xD9D5 - ORG + 2 * i] | data[0xD9D5 - ORG + 2 * i + 1] << 8
        if ORG <= v < ORG + LEN:
            namen.setdefault(v, f"ISR_FFD{2 * i:X}")
            dis.recursive_decode(v)
    for a, n in EINSPRUENGE.items():
        namen[a] = n
        dis.recursive_decode(a)
    for t in sorted(dis.jump_targets | dis.call_targets):
        if t not in dis.instructions and BIOS <= t < ORG + LEN:
            dis.recursive_decode(t)
    for a, e in LINEAR:
        dis.linear_decode(a, e)
    return dis, namen


def ports(dis):
    zeilen = []
    for a in sorted(dis.instructions):
        m, _, _ = dis.instructions[a]
        if a >= BIOS and (m.startswith(("IN ", "OUT ")) or "(C)" in m
                          or m in ("INI", "INIR", "OTIR", "OUTI", "IND", "INDR", "OTDR", "OUTD")):
            zeilen.append(f"{a:04X}  {m}")
    return zeilen


HDR = """; ============================================================================
; scpx8915_v53_bios.prn  -  BIOS SCPX 8915 V5.3 (K8915 V3), D600H-EFEFH
;
; Quelle: doc/EPROMS/K8915/scpx8915_v53_sys.bin = Systemspuren der eigenen
; Bootdiskette (disks/k8915scpx_boot1.hfe), geladen wie vom ROM-Lader
; (tools/k8915_sysload.py): 12 272 Byte nach C000H-EFEFH, Einsprung D600H.
;   C000H  CCP   'SCPX V0/2 VEB ROBOTRON ELEKTRONIK ZELLA-MEHLIS (REZ), 1988';
;                beginnt mit JP C31DH / JP C319H -- nicht das DR-Layout
;   C800H  BDOS  Einsprung C806H (JP C811H + vier Fehlervektoren wie bei DR),
;                Texte 'SCPX ERR ON  : $BAD SECTOR$SELECT$FILE R/O$'
;   D600H  BIOS  (dieses Listing)
; CCP/BDOS sind nur abgegrenzt. Gegen das A5120-SCPX 1526 V1.7
; (disks/scpx17_cpa780_k5601.hfe) gibt es keinen verschoben gleichen Abschnitt
; ueber 85 Byte -- ein anderer Stand, fuer die Nachbildung ohne Belang.
;
; STATISCHES Disassemblat (tools/z80_disasm2.py, rekursiver Abstieg ab den 18
; Eintraegen der Sprungleiste, den IM-2-Vektoren und den zur Laufzeit gesetzten
; Vektoren, dazu linear die nur ueber Patches erreichbaren Varianten) +
; Handkommentare [STATISCH]. Keine Laufzeitbestaetigung -- es gibt noch keine
; K8915Machine. Erzeugt von tools/gen_k8915_bios_prn.py; Befunde im
; Design-Doc doc/design/16_k8915.md §4.4.
;
; Was der Kern fuer Etappe 3 daraus braucht (Kurzfassung):
;   - K5122 im /WAIT-Betrieb: der Lese-ISR E965H und der Schreibpfad EA94H
;     lesen/schreiben 16H/14H ausgerollt OHNE Statusabfrage.
;   - Interrupts (IM 2, I = FFH): SIO2-B (Tastatur) D0H..D6H, K5122-PIO1 A
;     (Index) F0H, PIO1 B Bit1 (Marke) F2H, CTC-ZRE K3 (Zeitgeber 37,6 Hz) FEH.
;   - Tastatur: K7672 im DCP-Modus, Scancodes Satz 1, Tabellen DC0FH..DCF1H.
;   - Port 61H = Anzeigefeld (E0H Lesen, D0H Schreiben, B0H bereit, 60H/50H Fehler).
;   - A8H nur 87H (Betrieb) und 06H (Bildspeicher, Warmstart ueber ROM 0406H).
; ============================================================================
"""


def fmt_daten(data, a, e, text):
    roh = data[a - ORG:e - ORG]
    zeilen = []
    for i in range(0, len(roh), 16):
        stk = roh[i:i + 16]
        if all(0x20 <= b < 0x7F for b in stk) and len(stk) > 3:
            db = "'" + stk.decode("ascii").replace("'", "''") + "'"
        else:
            db = ",".join(f"{b:02X}H" for b in stk)
        kom = f"\t\t;{text}" if i == 0 and text else ""
        zeilen.append(f"{a + i:04X}  {'':<14}\tDB\t{db}{kom}")
    return zeilen


def rendere(data, dis, namen):
    out = [HDR.rstrip("\n"), "", "\tORG\tC000H", ""]
    out.append(f"{ORG:04X}  {'':<14}\tDS\t{0xC800 - ORG}\t\t;CCP (abgegrenzt, s. Kopf)")
    out.append(f"{0xC800:04X}  {'':<14}\tDS\t{BIOS - 0xC800}\t\t;BDOS, Einsprung C806H (abgegrenzt)")
    out.append("")
    daten = {a: (e, t) for a, e, t in DATEN}
    addr = BIOS
    end = ORG + LEN
    while addr < end:
        if addr in namen:
            out.append(f"{namen[addr]}:")
        if addr in daten:
            e, t = daten[addr]
            out += fmt_daten(data, addr, e, t)
            addr = e
            continue
        if addr in dis.instructions:
            m, ln, raw = dis.instructions[addr]
            hexb = " ".join(f"{b:02X}" for b in raw)
            kom = C.get(addr, "")
            out.append(f"{addr:04X}  {hexb:<14}\t{m}" + (f"\t\t;{kom}" if kom else ""))
            addr += ln
            continue
        # nicht erreichte Bytes bis zur naechsten bekannten Stelle als DB sammeln
        e = addr + 1
        while (e < end and e not in dis.instructions and e not in daten
               and e not in namen and e - addr < 16):
            e += 1
        out += fmt_daten(data, addr, e, C.get(addr, "[nicht erreicht]" if addr not in C else ""))
        addr = e
    out += ["", "\tEND"]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--neu", metavar="ABBILD")
    ap.add_argument("--ports", action="store_true")
    a = ap.parse_args()
    if a.neu:
        neu(a.neu)
    data = lade()
    dis, namen = disassembliere(data)
    if a.ports:
        print("\n".join(ports(dis)))
        return
    zeilen = rendere(data, dis, namen)
    open(OUT, "w", encoding="utf-8").write("\n".join(zeilen) + "\n")
    print(f"geschrieben: {os.path.relpath(OUT, ROOT)}  ({len(zeilen)} Zeilen)")


if __name__ == "__main__":
    main()
