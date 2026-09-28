#!/usr/bin/env python3
"""
Baut doc/EPROMS/K8915/k8915_zre.prn aus dem rohen Disassemblat (tools/z80_disasm2.py)
+ Handkommentaren, analog tools/gen_zre_prn.py fuer den A5120.

Quelle der Kommentare: doc/design/16_k8915.md §4 (2026-09-27, Stromlaufplan + ROM-Verhalten).
STATISCHE Analyse -- kein K8915-Emulator existiert noch, also keine boot_trace/k1520dbg-
Kontrollfluss-Verifikation wie bei zre.prn (dort per --coverage cpu,pc bestaetigt). Deshalb
KEIN [ZVE1]/[ZVE2]-Tag (es gibt nur eine CPU), stattdessen ein [STATISCH]-Hinweis im Kopf,
und Kommentare nur dort, wo Stromlaufplan+ROM-Ablauf schon zusammenpassen (Etappe 0/1).
"""
import re
import subprocess
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ROM = os.path.join(ROOT, "doc/EPROMS/K8915/k8915_boot_2732.bin")
OUT = os.path.join(ROOT, "doc/EPROMS/K8915/k8915_zre.prn")
TOOL = os.path.join(ROOT, "tools/z80_disasm2.py")

data = open(ROM, "rb").read()
assert len(data) == 4096

LINE_RE = re.compile(r'^([0-9A-Fa-f]{4})\s+[0-9A-Fa-f ]*\t(.*?)\s*$')
LABEL_RE = re.compile(r'^(\S+):$')


def disasm(args):
    out = subprocess.run([sys.executable, TOOL] + args + [ROM],
                          capture_output=True, text=True, check=True).stdout
    entries = {}  # addr(int) -> (label_or_None, mnemonic)
    pending_label = None
    for line in out.splitlines():
        m = LABEL_RE.match(line)
        if m:
            pending_label = m.group(1)
            continue
        m = LINE_RE.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        mnem = m.group(2)
        entries[addr] = (pending_label, mnem)
        pending_label = None
    return entries


# ---- drei Disassemblier-Durchgaenge, je im passenden Adressraum ----------
low = disasm(["--org", "0", "--entry", "0x0000", "--entry", "0x001B", "--entry", "0x0400",
              "--entry", "0x0428", "--entry", "0x0FFA", "--range", "0x09A0:0x0C00"])
selftest = disasm(["--org", "0xF000", "--entry", "0xF0D0", "--range", "0xF0D0:0xF400"])
helpers = disasm(["--org", "0xF000", "--entry", "0xFC00", "--range", "0xFC00:0xFD00"])

# ---- der 23-Byte-Stub, von Hand disassembliert (Quelle 0987-099DH, Ziel FFE0H) ----
stub_bytes = data[0x0987:0x099E]
assert stub_bytes == bytes.fromhex("3e87 d3a8 3a00 00fe c320 053a 0500 fec3"
                                    "3e06 d3a8 c328 04".replace(" ", "")), "Stub-Bytes veraendert?"
stub_entries = {
    0xFFE0: (None, "LD A,87H"),
    0xFFE2: (None, "OUT (A8H),A"),
    0xFFE4: (None, "LD A,(0000H)"),
    0xFFE7: (None, "CP C3H"),
    0xFFE9: (None, "JR NZ,FFF0H"),
    0xFFEB: (None, "LD A,(0005H)"),
    0xFFEE: (None, "CP C3H"),
    0xFFF0: (None, "LD A,06H"),
    0xFFF2: (None, "OUT (A8H),A"),
    0xFFF4: (None, "JP 0428H"),
}

# ---- Kommentare je Adresse (Quelle: doc/design/16_k8915.md §4) -----------
C = {}


def c(addr, text):
    C[addr] = text


c(0x0000, "[RESET-EINSPRUNG] DI/IM2/SP=F000H, dann OUT(61H),FFH (ATS-Steuerlatch? [?], "
          "vgl. Design-Doc §3.2 Port 61H) und OUT(A8H),8EH = Bankregister: ROM an, RAM "
          "darueber (Design-Doc §4.2)")
c(0x000E, "CALL sub_00C0: kopiert ROM 00D0H-0FFEH (BC=0F2FH Bytes) nach F0D0H-FFFEH -- "
          "'waehrend das ROM bei 0000 sichtbar ist' (§4.2); danach existiert derselbe "
          "Code-Bereich doppelt: als ROM unten UND als RAM-Spiegel oben")
c(0x0014, "OUT(A8H),06H = 'ROM an' (Normalbetrieb); der Lader ab 0400H laeuft direkt aus "
          "dem ROM (Design-Doc §4.2 Zeile 2)")
c(0x0018, "JP 0400H: erster Sprung in den Diskettenlader, noch OHNE Selbsttest")
c(0x001B, "[PRUEFSUMMEN-EINSPRUNG L001B] Zweiter Einstieg, erreicht ueber JP 0FFAH -> "
          "JP 001BH am Ende des Ladervorlaufs (§4.3 Schritt 3), wenn KEIN Fremdsystem im "
          "RAM gefunden wurde. SP neu setzen, Bildspeicher 17FFH loeschen (Cursor-Byte?), "
          "Statuszeile mit Leerzeichen fuellen (FCF8H), ROM erneut nach F0D0H kopieren.")
c(0x002D, "HL=FC77H: Zeiger auf Init-Tabelle fuer die Statuszeile 'MROM RAM SIO KEY CTC' "
          "(sub_FC19 kopiert 10 Bytes davon nach 1740H, vgl. FC77H-Bereich im Helper-Block)")
c(0x0037, "24-Bit-Pruefsummen-Vergleich: IY=0FFDH zeigt auf die im ROM abgelegte Summe "
          "0FFDH-0FFFH (=09AB70H, Design-Doc §1/§4.1), HL/DE-Schleife (0044H) bildet die "
          "laufende Summe ueber 0000H-0FFBH")
c(0x0044, "24-Bit-Additionsschleife: A+=[HL], Carry->DE++ (Ueberlauf in DE:A), CPI zaehlt "
          "BC (=0FFCH Bytes) herunter; PE (BC!=0) haelt die Schleife")
c(0x004D, "Vergleich laufende Summe (DE:A) gegen die abgelegte 24-Bit-Pruefsumme (IY) -- "
          "bei Gleichheit (Z) weiter zu L0083 (ROM-Test bestanden)")
c(0x005A, "[ROM-FEHLER] Fehlerpfad: OUT(61H),7FH, Statuszeile 'ROM' auf Fehler (46H='F'?), "
          "9 Zeichen Verzoegerungsschleife -> BEL-Sturm (F3C0H, ausserhalb -- 64x BEL "
          "gemaess §4.3 letzter Punkt vermutlich in der noch nicht disassemblierten "
          "F3C0H-Region)")
c(0x0083, "[ROM OK] Statuszeile 1770H+2 = 'A' (ROM-Test bestanden), danach RAM-Test: "
          "4000H-BFFFH mit 55H fuellen (LDIR), dann mit AAH/00H vergleichend gegenlesen "
          "(§4.3 'RAM: Muster 55H/AAH/00H ueber alle Baenke')")
c(0x00BA, "Test dieser 16-KB-Seite (4000H-7FFFH) fertig -> erneut kopieren + Sprung in "
          "den RAM-Spiegel F0D0H: ab hier laeuft der Code aus der KOPIE, nicht mehr aus "
          "dem Alias bei 0000H (wichtig fuer den Bankwechsel A8H=44/54/64/74H, der ja "
          "gerade die niedrigen Adressen NICHT betrifft -- nur das Fenster 4000H-7FFFH)")
c(0x00C0, "[KOPIERROUTINE] LDIR ROM(00D0H..0FFEH) -> RAM(F0D0H..FFFEH), BC=0F2FH=3887 "
          "Bytes. Deckt Selbsttest UND Diskettenlader UND Hilfsroutinen gleichermassen ab "
          "(ein einziger Bulk-Copy fuer den GESAMTEN oberen ROM-Teil); ob eine Stelle "
          "danach aus dem ROM (niedrige Adresse, A8H=06H) oder aus der Kopie (hohe "
          "Adresse, A8H=87H='ROM aus') laeuft, entscheidet einzig der jeweils aktive "
          "Sprung/Call-Zieladresse.")

c(0x0400, "[LADEREINSPRUNG] JP 041AH -- indirekter erster Schritt, siehe dort")
c(0x0907, "[MELDUNGSROUTINE, 'CALL 0907H'-Konvention geklaert 2026-09-28] Alle 6 Aufrufer "
          "(043EH/04E7H/0571H/06C8H/0834H/0870H) legen den anzuzeigenden Text NICHT per "
          "Zeiger in HL/DE ab, sondern INLINE direkt hinter dem 'CALL 0907H' im Code; das "
          "letzte Zeichen traegt Bit7 gesetzt (Endemarke). Belegt: '* Coldstart * Disk on "
          "A: read...', 'Drive A: not ready, check', 'Disk-error, change disk', "
          "'No system disk, change disk', 'Loading complete, replace disk...' (2x) -- "
          "deckungsgleich mit den in Design-Doc §4.3 genannten Meldungen. Ablauf: OUT(61H) "
          "mit dem von JEDEM Aufrufer vorher gesetzten A (nur zwei Werte im ROM: B0H vor "
          "'Coldstart'/'Loading complete', 60H vor den drei Fehlermeldungen -- vermutlich "
          "zwei Zustaende einer Anzeige/eines Summers, Design-Doc §6.2 [?] bleibt offen fuer "
          "die genaue Bitbedeutung); Kanal-2-Baudrate der Tastatur-CTC (Port 5AH, 2 Byte aus "
          "096FH) und SIO2B/Tastatur-Steuerwoerter (Port 53H, 10 Byte ab 0971H) neu "
          "ausgeben (derselbe Wertesatz wie sub_FC46/FC9E-FCA8 im Selbsttest-Block, siehe "
          "dort); Bildschirmbereich 1050H-177FH um 50H=80 Byte (1 Zeile) nach 1000H hoch-"
          "scrollen, neue Zeile mit Leerzeichen fuellen.")
c(0x092E, "'POP HL' holt hier NICHT irgendeinen geretteten Wert, sondern die eigene "
          "CALL-Ruecksprungadresse vom Stack -- die zeigt exakt auf den Meldungstext, der "
          "im Aufrufer direkt hinter 'CALL 0907H' liegt (s.o.). Schleife bei 092FH kopiert "
          "byteweise nach [DE] (in die frisch gescrollte Bildzeile) und loescht dabei Bit7 "
          "(RLCA+SRL A) -- bis zu dem Byte, dessen Bit7 GESETZT war (das ist die Endemarke "
          "UND das letzte kopierte Zeichen). PUSH HL (= Adresse hinter der Endemarke, also "
          "die korrekte Fortsetzungsadresse im Aufrufer) + PUSH DE sichern beide fuer den "
          "Ausgang der Routine.")
c(0x093A, "Haengt einen festen 12-Byte-Text '--> <ENTER>>' aus 097BH an die kopierte "
          "Meldung an (Aufforderung zum Weiterdruecken).")
c(0x0942, "Wartet auf ein Tastaturzeichen (Port 53H/52H = SIO2B, Tastatur K7672, Design-"
          "Doc §3.2): CR(0DH)/7CH/1CH/ESC(1BH) beenden direkt, ESC gefolgt von 'c'(63H) "
          "ebenfalls (zweite Schleife ab 095AH) -- passt zu 'ESC c' als Rueckstell-Befehl "
          "der K7672 aus Design-Doc §3.5.")
c(0x0966, "POP HL holt jetzt das bei 093AH gesicherte DE zurueck (Position direkt hinter "
          "der kopierten Meldung, VOR dem angehaengten '--> <ENTER>>'); die folgende "
          "Schleife loescht genau diese 12 Byte wieder (der Eingabe-Hinweis verschwindet, "
          "die Meldung selbst bleibt stehen). Danach 'RET': das holt das bei 092EH "
          "GEPUSHTE HL vom Stack -- der urspruengliche Ruecksprung ist laengst konsumiert, "
          "der Call kehrt also nicht hinter 'CALL 0907H' zurueck, sondern hinter die "
          "inline liegende Meldung im Aufrufer.")
c(0x041A, "Kopiert den 23-Byte-'Warmstart-Stub' (Quelle 0987H, s.u.) nach FFE0H und "
          "springt ihn an. Der Stub liegt bewusst in der IMMER-RAM-Seite oberhalb 0C000H "
          "(Design-Doc §3.1 Seitendekoder D23), damit sein eigener Code-Fetch nicht davon "
          "abhaengt, ob A8H gerade das ROM bei 0000H ein- oder ausblendet -- er kann so "
          "gefahrlos selbst zwischen beiden Zustaenden hin- und herschalten.")
c(0x0428, "Rueckkehrpunkt aus dem Stub (FFF4H: JP 0428H). Z-Flag noch von der letzten "
          "Stub-Pruefung: NZ = kein Fremdsystem im RAM -> JP 0FFAH (Selbsttest); Z = "
          "Fremdsystem gefunden ODER Taste CR -> Coldstart-Meldung + Lader weiter (§4.3 "
          "Schritt 3/4)")
c(0x0987, "[WARMSTART-STUB, Quelltext bei 0987H] Wird nach FFE0H kopiert (s. 041AH) und "
          "NUR DORT ausgefuehrt -- die Bytes hier sind reine Vorlage, kein Sprungziel. "
          "Deckt sich exakt mit §4.1 Zeile 4 (0987H-099DH -> FFE0H, 23 Byte).")
c(0x09A0, "[TOTER CODE -- geklaert 2026-09-28] Byteidentische Kopie von 0921H-097FH "
          "(dem hinteren Teil der Meldungsroutine 0907H, ab 'LD H,D' bis zum Ende inkl. "
          "ihrer eingebetteten Datentabelle und dem '--> <ENTER>>'-Text) -- NICHT der "
          "Vorspann mit den beiden OTIRs/dem Scroll-LDIR (0907H-0920H fehlt hier). Kein "
          "einziges CALL/JP im gesamten ROM zielt auf 09A0H (durchsucht: alle "
          "16-Bit-Little-Endian-Vorkommen von A0 09 mit vorangehendem CD/C3/…) -- die "
          "96 Byte sind unerreichbarer Fuellcode, vermutlich ein Assembler-/Link-Rest "
          "einer frueheren Fassung dieser Routine, ohne Funktion.")
c(0x09C2, "[TASTATURABFRAGE waehrend des Ladevorlaufs] Pollt SIO-Port 53H (Status) / 52H "
          "(Daten) -- das ist laut Design-Doc §3.2 SIO 2 Kanal B = Tastatur K7672. Prueft "
          "gelesene Zeichen gegen 0DH/7CH/1CH/1BH (CR bzw. ESC-artige Codes -- passt zum "
          "K7672-Protokoll 'ESC c' aus §3.5); zweite Schleife (09DAH) wartet zusaetzlich "
          "auf 63H ('c', Zyklustaste '#'? [?]).")
c(0x0A00, "[UNBELEGT] 0A00H-0BFFH durchgehend FFH (unprogrammierte EPROM-Zellen) -- kein "
          "Code, keine Daten.")
c(0x0C00, "[HILFSROUTINEN, laufen als RAM-Spiegel bei FC00H] Diese ROM-Bytes sind NIE "
          "direkt (aus dem Alias bei 0000H-0FFFH) angesprungen -- FC00H liegt oberhalb "
          "der 16-KB-Umschaltseite (Design-Doc §3.1 Seitendekoder D23, AB14/AB15) und ist "
          "deshalb IMMER die RAM-Kopie, unabhaengig vom A8H-Bankregister. Siehe Block "
          "'HILFSROUTINEN (FC00H-Spiegel)' weiter unten fuer die annotierten Routinen.")
c(0x0D00, "[UNBELEGT] 0D00H-0FF5H durchgehend FFH (unprogrammierte EPROM-Zellen).")
c(0x0FF6, "[IM2-VEKTOR CTC-KANAL 3] Adresse FFF6H = (I=FFH)<<8 | Vektorbyte F6H. F6H = "
          "CTC-Vektorbasis F0H + Kanal 3 * 2 (Design-Doc §4.3: 'Vektor F0H an 80H/48H/58H, "
          "jeweils Kanal 3 als Zeitgeber mit Interrupt'). Inhalt FC64H = ISR-Adresse.")
c(0x0FF8, "unbekannt/ungenutzt (FFH 3FH) -- keine erkannte Bedeutung")
c(0x0FFA, "[SELBSTTEST-SPRUNG] 'JP 001BH', angesprungen ueber 'JP 0FFAH' am Ladervorlauf-"
          "Ende (§4.3 Schritt 3). Physisch dieselben Bytes wie an FFFAH (RAM-Spiegel); "
          "ausgefuehrt wird hier ueber die NIEDRIGE Adresse, weil A8H zu diesem Zeitpunkt "
          "bereits wieder 06H='ROM an' ist (vom Stub am Ende zurueckgesetzt).")
c(0x0FFD, "[PRUEFSUMME] 24-Bit-Summe ueber 0000H-0FFBH = 09AB70H (§1: 'die ROM-eigene "
          "Pruefsumme stimmt' -- gegen den eigenen Dump verifiziert 2026-09-27).")

# FC00-FCFF Helfer
c(0xFC00, "sub_FC00: Summer AUS (Port 61H = 0)")
c(0xFC04, "sub_FC04: Summer AN (Port 61H = FFH)")
c(0xFC09, "sub_FC09: Bildspeicher 1000H-177FH loeschen (0780H=80x24 Byte) mit dem Fuellwert "
          "aus FCF8H, dann Statuszeile ab 1780H nochmal explizit setzen -- deckt sich mit "
          "VRAM bei 1000H aus Design-Doc §3.3")
c(0xFC19, "sub_FC19: kopiert Text ab HL in die Statuszeile 1770H (3 Byte) + 1740H (10 Byte "
          "ab FC77H) -- die Init-Tabelle fuer 'MROM RAM SIO KEY CTC' liegt direkt danach im "
          "selben Helferblock (ab FC68H/FC77H)")
c(0xFC2C, "sub_FC2C: einfache DJNZ/DEC-C-Verzoegerungsschleife (B*256+C-artig)")
c(0xFC34, "sub_FC34: zweite, laengere Verzoegerungsschleife mit EXX (schont HL/BC des "
          "Aufrufers)")
c(0xFC46, "sub_FC46 -- geklaert 2026-09-28, deckt sich mit Design-Doc §3.2: LD HL,FC9EH; "
          "zwei OTIRs OHNE HL neu zu laden, HL laeuft also durch. Erster OTIR (2 Byte "
          "FC9EH/FC9FH = 07H,01H) an Port 5AH = CTC2-Kanal K2 (Tastatur-Baudrate): "
          "Steuerwort 07H (Zeitgeber, Vorteiler 16), Zeitkonstante 01H -- "
          "2,4576 MHz/16 = 153,6 kHz. Zweiter OTIR (9 Byte, HL jetzt bei FCA0H = "
          "18H,04H,44H,03H,C1H,05H,EAH,01H,00H) an Port 53H = SIO2-Kanal B (Tastatur "
          "K7672, Design-Doc §3.2): Z80-SIO-Zeigerprotokoll ausgewertet -- 18H (WR0, "
          "Pointerbits=0, Reset-artiger Befehl), dann je Zeiger+Wert: WR4=44H "
          "('x16, 1 Stop, keine Paritaet' -- exakt Design-Doc-Wert), WR3=C1H "
          "('Rx enable, 8 Bit' -- exakt Design-Doc-Wert), WR5=EAH (Design-Doc-Wert), "
          "WR1=00H (keine Interrupts). Dieselben elf Bytes (07,01,18,04,44,03,C1,05,EA,01, "
          "mit einer zusaetzlichen 00H) liegen als eingebettete Datentabelle nochmal bei "
          "096FH/0971H -- vom Lader-Meldungsroutine 0907H per eigenem OTIR gelesen "
          "(s. dort); zwei getrennte Kopien derselben Init-Konstanten, keine gemeinsame "
          "Tabelle.")
c(0xFC56, "sub_FC56: kurzer Summerton (Port 52H 1BH/63H mit Verzoegerung dazwischen) -- "
          "ACHTUNG Port 52H ist laut §3.2 SIO2-A Datenport (Tastatur/V.24), hier vermutlich "
          "zweckentfremdet oder Bezeichnung [?] noch zu pruefen")
c(0xFC64, "[CTC-INTERRUPT-ISR, Ziel des Vektors bei FFF6H] INC A; EI; RETI -- die minimale "
          "Zaehl-ISR fuer den CTC-Interrupttest aus §4.3 ('CTC: ... jeweils Kanal 3 als "
          "Zeitgeber mit Interrupt').")
c(0xFC68, "[TEXT-/TABELLENBLOCK FC68H-FCF7H -- geklaert 2026-09-28, war im Plan (§8, "
          "Etappe 0) noch als '[?]' offen] Reiner Datenbereich, vom Disassembler streckenweise "
          "als Fake-Code gezeigt (keine Verstaendnisluecke, nur die Grenze eines blinden "
          "Disassemblers -- s. Design-Doc §8). Fuenf Teile: "
          "(1) FC68H-FC76H Text 'ROMRAMSIOKEYCTC' -- die 5 Spaltenkoepfe der Statuszeile "
          "(sub_FC19 kopiert sie nach 1740H, s. dort); "
          "(2) FC77H-FC9DH Text 'DIAGNOSTIC ENTER: LADER  /  \"#\": ZYKL. ' -- die zweite "
          "Statuszeile; "
          "(3) FC9EH-FCA8H die CTC2-K2/SIO2B-Init-Tabelle von sub_FC46H (s. dort); "
          "(4) FCA9H-FCB0H die Bytes 1BH,5BH,32H,3BH,31H,79H = 'ESC [2;1y' (VT100-DECTST, "
          "Design-Doc §3.5) plus zwei Fuellbytes; "
          "(5) FCB1H-FCE6H eine Spaltenlayout-Tabelle (Breiten/Schrittweiten fuer die 5 "
          "Statuszeilen-Spalten ROM/RAM/SIO/KEY/CTC, inkl. des Trennzeichens 48H), gelesen "
          "vom noch nicht kommentierten Rahmen-Zeichenprogramm bei F2D6H (Selbsttest-Block, "
          "Aufruf ueber sub_FC46+sub_FC09+sub_FC00 ab F2F7H) -- Bedeutung der einzelnen "
          "Tabellenwerte im Detail offen, aber die Adresse und ihr Verbraucher sind jetzt "
          "bekannt, keine 'Fake-Code'-Verwechslung mehr; "
          "(6) FCE7H-FCF7H+ Init-Tabelle 'FCEFH' fuer SIO1-A/B + SIO2-A (V.24/IFSS, Design-"
          "Doc §4.3 'je 200H Muster AAH/55H'): ab FCEFH per Zeigerprotokoll WR3=C1H, "
          "WR4=45H ('x16, 1 Stop, UNGERADE Paritaet' -- exakt Design-Doc-Wert fuer IFSS), "
          "WR5=68H (exakt Design-Doc-Wert), Rest ausserhalb dieses Blocks.")
c(0xF2D6, "[STATUSZEILEN-RAHMEN, laeuft als RAM-Spiegel] Zeichnet die 5-Spalten-Anzeige "
          "'MROM RAM SIO KEY CTC' der Statuszeile (Design-Doc §4.3) mithilfe der "
          "Layout-Tabelle FCB1H-FCE6H (s. FC68H-Kommentar) -- Trennzeichen 48H "
          "wiederholt ueber die Zeilen, danach je Spalte ein Fuellwert aus FCF9H/FCFAH. "
          "Aufgerufen ueber F2F7H (DI; CALL sub_FC46; CALL sub_FC09; CALL sub_FC00) aus "
          "dem RAM-Testabschnitt heraus -- Detailsemantik der einzelnen Tabellenbytes "
          "(FCBBH/FCBDH/FCBFH/FCC0H...) noch nicht Byte-fuer-Byte zugeordnet, aber "
          "Routine und Datenquelle sind jetzt bekannt.")

HDR = """; ============================================================================
; k8915_zre.prn  -  K8915 V3 Boot-EPROM (045-8762 'ZRE fuer K8G'), 2732, 4 KB, 0000H-0FFFH
;
; STATISCHES Disassemblat (tools/z80_disasm2.py) + Handkommentare aus
; doc/design/16_k8915.md Paragraph 4 (Stromlaufplan 1.45.518762 + ROM-Verhalten,
; Stand 2026-09-27). Etappe 0 ('Unterlagen sichern') -- KEINE Laufzeitverifikation:
; es existiert noch kein K8915Machine/Emulator, also keine boot_trace/k1520dbg
; --coverage-Bestaetigung wie bei zre.prn. Vor Etappe 1/2 (Maschinenabstraktion,
; A8H-Logik) gegen das Original pruefen, nicht blind uebernehmen.
;
; Eigener Dump gegen tiffe.de/robotron/K8915/Eprominhalte/k8915-ZVE_2732.bin
; byteidentisch (MD5 19e301bd...4b7); ROM-eigene 24-Bit-Pruefsumme (0000-0FFB
; gegen 0FFD-0FFF) stimmt.
;
; Speicherbild (aus dem Stromlaufplan + ROM-Ablauf, Design-Doc §4.1/§4.2):
;   0000-00CF  laeuft bei 0000H   Reset, Pruefsummentest, Kopierroutine (00C0H)
;   00D0-03FF  laeuft bei F0D0H  Selbsttest (ROM/RAM/SIO/KEY/CTC) -- NUR als RAM-Spiegel
;   0400-09FF  laeuft bei 0400H   Diskettenlader (direkt aus dem ROM)
;   0987-099D  laeuft bei FFE0H  23-Byte-Warmstart-Stub (Quelle hier, Ziel FFE0H)
;   0A00-0BFF  unbelegt (FFH)
;   0C00-0CFF  laeuft bei FC00H  Hilfsroutinen (Bildloeschen/Statuszeile/Verzoegerung/Beep)
;   0D00-0FF5  unbelegt (FFH)
;   0FF6-0FFF  laeuft bei FFF6H  IM2-Vektor Kanal 3 (-> FC64H), Sprung 001BH, Pruefsumme
;
; Bankregister A8H (Design-Doc §4.2, s. Nachtrag unten):
;   8EH  Reset/Kopieren/Bildtest      ROM an, RAM darueber
;   06H  Normalbetrieb (Lader)         ROM an (Lader laeuft bei 0400H)
;   87H  Stub bei FFE0H                ROM AUS (liest 0000H/0005H aus dem RAM)
;   44H/54H/64H/74H  RAM-Test          waehlt eine von vier 16-KB-Baenken ins Fenster
;                                      4000H-7FFFH ein
; Nachtrag 2026-09-28: aus Blatt 3 hergeleitet (Design-Doc §4.2a) -- Bit0 = Seite 0
; RAM statt ROM, Bit6 = Bank 2 ins Fenster 4000H,
; Bit5:4 = welches Viertel von Bank 2, Bit7 = /MEMDI (aktiv bei 1). Am Geraet
; bestaetigt (Brueckenfeld X8-X27, 2026-09-28): Bit2 = Seiten 2 UND 3, Bit3 = /MEMDI1
; (aktiv bei 0), Bit1 = Seite 1.
;
; Bekannte Portbezuege in diesem ROM (Design-Doc §3):
;   61H       vermutlich ATS-Steuerlatch/Summer [?] (§3.2, D3:01)
;   A8H       Bankregister der ZRE (dieses Blatt, §3.1 Blatt 3)
;   52H/53H   SIO2-B = Tastatur K7672 (§3.2), 9600 Bd
; ============================================================================

\tORG\t0000H

"""


def fmt(addr, mnem, comment, org=0):
    nxt = fmt.next_addr_map.get(addr)
    ln = max(1, (nxt if nxt is not None else addr + 1) - addr)
    src = addr - org
    hexb = " ".join(f"{b:02X}" for b in data[src:src + ln])
    return f"{addr:04X}  {hexb:<14}\t{mnem}" + (f"\t\t;{comment}" if comment else "")


def build_block(entries_dict, addr_lo, addr_hi, org=0):
    """entries innerhalb [addr_lo, addr_hi) als sortierte Zeilenliste."""
    addrs = sorted(a for a in entries_dict if addr_lo <= a < addr_hi)
    lines = []
    for i, addr in enumerate(addrs):
        label, mnem = entries_dict[addr]
        nxt = addrs[i + 1] if i + 1 < len(addrs) else addr_hi
        fmt.next_addr_map[addr] = nxt
        if label:
            lines.append(f"{label}:")
        lines.append(fmt(addr, mnem, C.get(addr, ""), org=org))
    return lines


fmt.next_addr_map = {}


def unbelegt_block(addr_lo, addr_hi):
    """Kompakter DB-Block fuer durchgehend FFh (statt tausend Einzelzeilen)."""
    chunk = data[addr_lo:addr_hi]
    assert all(b == 0xFF for b in chunk)
    lines = []
    comment = C.get(addr_lo, "")
    lines.append(f"{addr_lo:04X}  {'FF':<14}\tDS\t{addr_hi - addr_lo}\t\t;{comment}"
                  if comment else
                  f"{addr_lo:04X}  {'FF':<14}\tDS\t{addr_hi - addr_lo}")
    return lines


out_lines = [HDR.rstrip("\n")]

out_lines.append("; ---- Reset / Pruefsumme / Kopierroutine (0000H-00CFH) ".ljust(78, "-"))
out_lines += build_block(low, 0x0000, 0x00D0)

out_lines.append("")
out_lines.append("; ---- Selbsttest, laeuft als RAM-Spiegel bei F0D0H (Quelle 00D0H-03FFH) "
                  .ljust(78, "-"))
out_lines += build_block(selftest, 0xF0D0, 0xF400, org=0xF000)

out_lines.append("")
out_lines.append("; ---- Diskettenlader, laeuft direkt aus dem ROM (0400H-09FFH) ".ljust(78, "-"))
out_lines += build_block(low, 0x0400, 0x0987)
out_lines.append(f"{0x0987:04X}  {'-- 23 Byte --':<14}\tDS\t23"
                  f"\t\t;Warmstart-Stub, reine Vorlage -- wird nach FFE0H kopiert (041AH) "
                  f"und NUR DORT ausgefuehrt; Disassemblat siehe Block 'Warmstart-Stub' "
                  f"am Dateiende. Bytes identisch mit FFE0H-FFF6H.")
out_lines.append(f"{0x099E:04X}  {'00 FF':<14}\tDB\t00H,FFH\t\t;Fuellbytes nach dem Stub")
out_lines += build_block(low, 0x09A0, 0x0A00)

out_lines.append("")
out_lines.append("; ---- Unbelegt (0A00H-0BFFH) ".ljust(78, "-"))
out_lines += unbelegt_block(0x0A00, 0x0C00)

out_lines.append("")
out_lines.append("; ---- Hilfsroutinen, laufen als RAM-Spiegel bei FC00H (Quelle 0C00H-0CFFH) "
                  .ljust(78, "-"))
out_lines += build_block(helpers, 0xFC00, 0xFD00, org=0xF000)

out_lines.append("")
out_lines.append("; ---- Unbelegt (0D00H-0FF5H) ".ljust(78, "-"))
out_lines += unbelegt_block(0x0D00, 0x0FF6)

out_lines.append("")
out_lines.append("; ---- IM2-Vektor / Selbsttest-Sprung / Pruefsumme (0FF6H-0FFFH) "
                  .ljust(78, "-"))
out_lines += build_block(low, 0x0FF6, 0x1000)

out_lines.append("")
out_lines.append("; ---- Warmstart-Stub, Ziel FFE0H (physische Quelle: 0987H-099DH oben) "
                  .ljust(78, "-"))
for addr in sorted(stub_entries):
    nxt_candidates = [a for a in stub_entries if a > addr]
    nxt = min(nxt_candidates) if nxt_candidates else 0xFFF7
    ln = nxt - addr
    src_addr = 0x0987 + (addr - 0xFFE0)
    hexb = " ".join(f"{b:02X}" for b in data[src_addr:src_addr + ln])
    label, mnem = stub_entries[addr]
    out_lines.append(f"{addr:04X}  {hexb:<14}\t{mnem}")

out_lines.append("")
out_lines.append("\tEND")

open(OUT, "w", encoding="utf-8").write("\n".join(out_lines) + "\n")
print(f"geschrieben: {os.path.relpath(OUT, ROOT)}  ({len(out_lines)} Zeilen)")
