#!/usr/bin/env python3
"""gen_k8915_format_prn.py — kommentiertes .prn von FORMAT.COM des K8915 (SCPX 8915 V5.3).

Erzeugt ein via `k1520dbg -l` / `boot_trace -l` ladbares Listing der Teile von
FORMAT.COM (V 1.7, 14.10.1988, "REZ 14.10.1988 FORMAT V 1.7  K5122*"), die das
Arbeitspaket AP-E4e (doc/design/16_k8915.md §8a) fuer den Vollspur-Schreibpfad
im /WAIT-Betrieb (AP-E4f) braucht: die Stelle, an der FORMAT.COM die K5122
DIREKT programmiert (nicht ueber den BIOS-Treiber E2E1H — anders als DISGEN.COM,
das nur ueber die normale BIOS-WRITE-Funktion schreibt, s. §4.4).

FORMAT.COM ist ein gewoehnliches .COM (TPA ab 0100H) — anders als das BIOS
(gen_k8915_bios_prn.py) braucht es KEINEN RAM-Dump, die Datei wird direkt
disassembliert. Quelle: `tests/fixtures/disks/k8915scpx_boot1.hfe` (Diskette 901;
FORMAT.COM ist auf 900/901/904 byteidentisch, AP-B2), herausgeholt mit

    tools/dev.sh tool k1520disktool get tests/fixtures/disks/k8915scpx_boot1.hfe \\
        FORMAT.COM --to /tmp/k8915fmt

Aufruf:  python3 tools/gen_k8915_format_prn.py [<FORMAT.COM>] [<ziel.prn>]

Der Disassembler (tools/z80_disasm2.py) wird als Subprozess je Region aufgerufen
(analog gen_scpx_readpath_prn.py): Regionen mit `mode="entry"` laufen als
rekursiver Abstieg ab `entry` (folgt CALL/JP, findet Verzweigungen selbst),
Regionen mit `mode="range"` erzwingen lineare Dekodierung von lo..hi — noetig
fuer den Schreibstrom-Kern (1AEDH ff.), den der rekursive Abstieg von 0100H aus
nicht erreicht (er wird nur ueber einen berechneten Sprung angesprungen).
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
DEFAULT_SRC = ROOT / "tests" / "fixtures" / "disks" / "_extracted_not_present"
DEFAULT_OUT = ROOT / "doc" / "EPROMS" / "K8915" / "format_k8915.prn"

ORG = 0x100

# Regionen: (entry_fuer_disassembler, lo, hi, mode)
REGIONS = [
    (0x100, 0x100, 0x124, "entry"),   # Kopf: CRC-Sprung + Klartext-ID
    (0x100, 0x1946, 0x1A0A, "entry"),  # Auswahl-Dispatch, Feldpositionierung
    (0x100, 0x1A0B, 0x1A1A, "range"),  # Index-ISR (Vektor 20H), setzt (3580H)
    (0x100, 0x1A1B, 0x1AEC, "entry"),  # Vorbereitung, /WP-Pruefung, Warten
    (0x100, 0x1AED, 0x1B5B, "range"),  # Schreibkern = Index-ISR (Vektor 24H; nur ueber die
                                        # IM-2-Tabelle erreicht, der Abstieg findet ihn nicht)
    (0x100, 0x1B5C, 0x1BD9, "range"),  # Pruef-Lesen = Index-ISR (Vektor 22H, AP-E4f)
]

COMMENTS = {
    0x100: "[Einsprung] JP ueber den Klartext-Kopf; ID-Text 'REZ 14.10.1988 FORMAT V 1.7  "
           "K5122*' (0103H ff.) belegt: FORMAT.COM programmiert die K5122 DIREKT (anders als "
           "DISGEN.COM, das nur ueber die normale BIOS-WRITE-Funktion schreibt, s. Design-Doc "
           "§4.4 Fassungsvergleich/DISGEN-Abschnitt)",
    0x1946: "[Feldkopie] Parameterblock (IY) in Arbeitsvariablen 3500H ff. umkopieren "
            "(Tabelle 1BECH: Feldlaengen+Zielzeiger je Byte)",
    0x1973: "L1973/sub_1977/L1992/199F/19A3: waehlen den Operationscode (3599H) nach den "
            "Schaltern 3579H/357BH — 21H/22H/23H/03H/04H je nach Lesen/Schreiben/Formatieren "
            "und Sektorgroesse [?] (Bedeutung der einzelnen Codes nicht abschliessend geklaert)",
    0x1977: "sub_1977: 11 Byte ab 355FH bitweise setzen/loeschen (Bit 2) — vermutlich eine "
            "Sektor-Belegungs-/Attributtabelle fuer die kommende Spur, abhaengig von (3579H) "
            "[?]",
    0x19BE: "CALL sub_1EE6 (Bildschirmausgabe/Status), dann IX=(3503H): Parameterblock des "
            "aktuellen Laufwerks/Formats",
    0x19C9: "CALL sub_1A01 — PIO-Modus setzen (s. dort), dann zurueck in den Menue-Dispatch "
            "(L129AH)",
    0x19CF: "sub_19CF/sub_19EE: Dispatch nach Menuecode (3586H: 'L'/'J'/'S' = sub_1AB8, "
            "'N'/'S'/'J' = sub_1A2F) [?] — welcher Buchstabe zu welchem Menuepunkt gehoert, "
            "ist aus dem Code allein nicht sicher (kein String-Vergleich im erreichten Bereich)",
    0x1A01: "sub_1A01: DI; A=03H -> OUT(11H) = Steuer-PIO Tor A, Modus-Kontrollwort "
            "(Bit1:0=11B => Mode-0/Ausgabe-Festlegung — die Steuerausgaenge /WE, MK, /FR, "
            "/STR, MK1, MR/SD, /HL, /ST sind reine Ausgaenge, s. Design-Doc §4 in "
            "07_k5122_afs.md); danach CALL sub_1ED8 (weitere Initialisierung, hier nicht "
            "verfolgt) und EI",
    0x1A2F: "sub_1A2F: HL=(3511H) = Zeiger auf den Track-/Sektor-Deskriptor (Sync-Laenge, "
            "3-fache Pruefschleife auf ein Erwartungsbyte); baut die Zeiger (3523H Byte-Zahl, "
            "3527H Datenzeiger) fuer den kommenden Schreib-/Lesevorgang auf",
    0x1A58: "IN A,(16H): Lesedatenport EINMAL abgefragt/geleert (Alt-Inhalt verwerfen), dann "
            "OUT(10H) zweimal — erst ein Steuerwort mit geloeschtem Bit 2 (RES 2,A auf "
            "(3569H)), dann das ungeaenderte (3567H): vermutlich /FR (Seitenwahl, Bit 2) "
            "kurz umschalten und zurueck, um den Kopf/die Seite an der /STR-Flanke neu zu "
            "latchen (Design-Doc §4 in 07_k5122_afs.md: /FR wird NUR an der /STR-Flanke "
            "uebernommen)",
    0x1A66: "IN A,(12H); BIT 5,A — Status-PIO Tor B Bit 5 = /WP (Write Protect); "
            "Design-Doc §5 in 07_k5122_afs.md: 0 = schreibgeschuetzt. Bit=0 (Z gesetzt) "
            "-> L1A83 (Fehlercode 11H nach 3599H, kein Schreibzugriff); sonst weiter mit dem "
            "Schreibvorgang. FORMAT.COM prueft den Schreibschutz also SELBST, bevor es "
            "die Spur programmiert — nicht erst der BIOS-Treiber",
    0x1A75: "L1A75/1A77: DI, kurze Verzoegerungsschleife (12H = 18 Durchlaeufe), dann "
            "OUT(11H)=A: 24H = INTERRUPTVEKTOR der Steuer-PIO Tor A (Bit0 = 0) — der naechste "
            "Index-Interrupt springt ueber FF24H in den Schreibkern 1AEDH (26H im Vorzweig "
            "1A6E); dann CALL sub_1A89 = warten, bis der ISR fertig meldet (am Lauf "
            "bestaetigt, AP-E4f)",
    0x1A0B: "[Index-ISR, Vektor 20H] (3581H)++, (3580H)=1: 'Index gesehen' — beendet die "
            "Schreibschleife im Kern (1B2CH) nach einer Umdrehung; EI; RETI",
    0x1A83: "L1A83: Fehlerpfad 'DISK READ/ONLY' — Fehlercode 11H nach (3599H), kein Zugriff "
            "auf die K5122",
    0x1A89: "sub_1A89: Wartet auf (359AH) Bit 1 (ISR fertig) oder Bit 7 (Timeout-Flag, per "
            "DE-Abwaertszaehler FFFFH gesetzt, wenn nichts passiert); im Timeout-Fall (L1AA7) "
            "Vektor 20H, /STR = 1, Daten-PIO leeren",
    0x1AB8: "sub_1AB8: baut Zeiger (352FH Ende, 3523H Byte-Zahl, 3527H->3511H "
            "Anfangszeiger) fuer den GESAMTEN zu schreibenden Bereich einer Operation "
            "(HL+=BC*n je nach Feldgroesse); OUT(14H)=00H + IN(16H) + OUT(10H) als "
            "Vorbereitung, dann OUT(11H)=22H (PIO scharf) und CALL sub_1A89 (auf Bereitschaft "
            "warten) — OUT(11H)=22H ist der VEKTOR fuer das Pruef-Lesen: der naechste Index "
            "springt ueber FF22H in 1B5CH (s. u., AP-E4f)",
    0x1AED: "[SCHREIBKERN = Index-ISR, Vektor 24H, /WAIT-Betrieb] PUSH AF; OUT(10H)=Schreib-"
            "steuerwort (das 00H ist ein PLATZHALTER, 1A55H setzt das erste Steuerwort der "
            "Tabelle ein: 90H = /WE 0, /STR 0, Kopf 1 bzw. 94H = Kopf 0); OUT(14H)=00H",
    0x1AF9: "HL=(3523H) = Zeiger auf den vorgebauten ROHSTROM der Spur (Luecken, C2/A1, "
            "Kennfelder samt CRC, Daten E5), DE=(3527H) = Tabelle aus Paaren (ANZAHL, "
            "STEUERWORT) — je Lauf gleicher Steuerung ein Paar (am Lauf: 57H/90H, 03H/92H "
            "(C2 C2 C2), 3FH/90H, … ; Anzahl 00H = 256). C=14H fuer OUTI",
    0x1B03: "dreifaches OUT(C),B (=OUT(14H),00H) als Anlauf, dazwischen OUT(11H)=20H "
            "(Vektor 20H: der naechste Index geht an 1A0BH) und CALL sub_1A18 (EI; RETI — "
            "sub_1A18 wird sowohl als echtes ISR-Ende ALS AUCH hier als gewoehnliches "
            "'EI + Ruecksprung' benutzt, RETI wirkt bei CALL wie RET); (3580H)=00H, "
            "(3581H) inkrementiert (Versuchs-/Durchlaufzaehler)",
    0x1B1C: "HAUPTSCHLEIFE (am Lauf berichtigt, AP-E4f): B=(DE) = ANZAHL, A=(DE+1) = "
            "STEUERWORT; OUTI schreibt ein Byte des Rohstroms (HL) an 14H (jedes OUT wartet "
            "per /WAIT auf sein Bytefenster), dann OUT(10H)=Steuerwort (92H = MK 1 fuer die "
            "Synchronbytes C2/A1, 90H sonst); weitere OUTI, bis B = 0, dann das naechste "
            "Paar. Ab 1B2CH wird nach jedem Byte (3580H) abgefragt: setzt der Index-ISR "
            "1A0BH es, endet der Strom (eine Umdrehung, die Luecke laeuft bis zum Index)",
    0x1B38: "Abschluss: OUT(10H)=(3567H) (z. B. BBH: /WE = /STR = 1 — hier geht die Spur "
            "auf die Scheibe); IN A,(16H) (Daten-PIO leeren); Bit 1 in (359AH) = fertig "
            "(sub_1A89); Restlaenge nach (352BH). Nachgeprueft wird im NAECHSTEN "
            "Index-ISR (1B5CH)",
    0x1B5C: "[PRUEF-LESEN = Index-ISR, Vektor 22H, AP-E4f] HL = Rohstrom (3523H), IX = "
            "Tabelle (352FH), je Eintrag 6 Byte: (IX-2) Abstand im Rohstrom, (IX+0)/(IX+1) "
            "Anzahl (B + 256*(E-1)), (IX+2) Frist fuer MKE, (IX+3) Lesesteuerwort; "
            "Tabellenende = FFH/FFH",
    0x1B87: "OUT(10H)=Lesesteuerwort: 83H/87H (MK = 1: Markenerkennung MFM-INDEXMARKE C2) "
            "fuer den ersten Eintrag, 81H/85H (MK = 0: A1) fuer Kennfeld und Datenfeld",
    0x1B8C: "auf MKE warten (12H Bit1), hoechstens D Abfragen (FFH ≈ 130 Bytezeiten), sonst "
            "1BB7H: Fehler C0H",
    0x1B95: "VERGLEICH Byte fuer Byte: IN A,(16H) mit dem Rohstrom (HL) — das erste Byte ist "
            "das erkannte Sync-Byte selbst (C2 bzw. A1), dann die Marke, das Feld samt CRC, "
            "Luecke 2 (22 x 4E) und 6 Byte 00. Ungleich ⇒ 1BBBH: Fehler A0H",
    0x1BA0: "Feld fertig: OUT(10H)=(3566H) (MR = 1, Marken-FF zurueck), OUT(10H)=(3567H) "
            "(/STR = 1), naechster Eintrag",
    0x1BB3: "alle Eintraege gleich: A = 02H nach (359AH) = Spur in Ordnung",
    0x1BCC: "Vektor 20H zurueck, CALL 1A0BH (Index-Zaehler), RET — Ergebnis in (359AH): 02H "
            "gut, A0H Abweichung, C0H keine Marke ⇒ 'ERROR ===> BAD TRACK'",
}

LINE_RE = re.compile(r"^([0-9A-Fa-f]{4})\s+((?:[0-9A-Fa-f]{2} )+)\s*\t?(.*)$")


def disasm(path: str, entry: int, rng=None):
    args = [sys.executable, str(HERE / "z80_disasm2.py"), path,
            "--org", hex(ORG), "--entry", hex(entry), "--no-strings"]
    if rng:
        args += ["--range", f"{rng[0]:#x}:{rng[1]:#x}"]
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    res = {}
    for line in out.splitlines():
        m = LINE_RE.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        res.setdefault(addr, (m.group(2).strip(), m.group(3).strip()))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("src", nargs="?", help="FORMAT.COM (z.B. aus k1520disktool get)")
    ap.add_argument("out", nargs="?", default=str(DEFAULT_OUT))
    args = ap.parse_args()
    if not args.src:
        print("Fehler: Pfad zu FORMAT.COM angeben (z.B. per "
              "'tools/dev.sh tool k1520disktool get tests/fixtures/disks/"
              "k8915scpx_boot1.hfe FORMAT.COM --to <ordner>')", file=sys.stderr)
        return 1

    lines = [
        "; ============================================================================",
        "; format_k8915.prn  -  FORMAT.COM V1.7 (K8915, SCPX 8915 V5.3, 900/901/904 "
        "byteidentisch)",
        ";",
        "; Kommentiertes Teil-Listing fuer k1520dbg/boot_trace (-l). Erzeugt von",
        "; tools/gen_k8915_format_prn.py direkt aus FORMAT.COM (TPA 0100H, kein RAM-Dump",
        "; noetig). Arbeitspaket AP-E4e, doc/design/16_k8915.md §8a/§4.4.",
        ";",
        "; Kern: FORMAT.COM programmiert die K5122 DIREKT (Ports 10H/11H/12H/14H/16H/18H,",
        "; Design-Doc 07_k5122_afs.md §3) -- der Schreibkern ab 1AEDH (Index-ISR, Vektor",
        "; 24H) schreibt einen vorgebauten Rohstrom der Spur per OUTI mit /WAIT, gesteuert",
        "; von (Anzahl,Steuerwort)-Paaren; das Pruef-Lesen ab 1B5CH (naechster Index,",
        "; Vektor 22H) vergleicht die Spur Byte fuer Byte (am Lauf berichtigt, AP-E4f).",
        "; DISGEN.COM dagegen bleibt beim normalen",
        "; BIOS-WRITE (nur EIN OUT im ganzen Programm, ein PIO-Reset an 17H) und braucht",
        "; keinen neuen /WAIT-Schreibpfad, s. §4.4.",
        "; ============================================================================",
        "",
    ]

    total = commented = 0
    for entry, lo, hi, mode in REGIONS:
        decoded = disasm(args.src, entry, (lo, hi) if mode == "range" else None)
        lines.append(f";  ---- {lo:04X}..{hi:04X} ----")
        addr = lo
        while addr <= hi:
            ent = decoded.get(addr)
            if ent is None:
                addr += 1
                continue
            by, mnem = ent
            comment = COMMENTS.get(addr, "")
            src = f"{mnem}\t\t;{comment}" if comment else mnem
            if comment:
                commented += 1
            lines.append(f"{addr:04X}  {by:<12}\t{src}")
            total += 1
            addr += 1 + by.count(" ")
        lines.append("")

    Path(args.out).write_text("\n".join(lines) + "\n")
    print(f"geschrieben: {args.out}  ({total} Code-Zeilen, {commented} kommentiert)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
