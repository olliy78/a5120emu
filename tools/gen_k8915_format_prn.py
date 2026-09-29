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
    (0x100, 0x1946, 0x1AEC, "entry"),  # Auswahl-Dispatch, Feldpositionierung, /WP-Pruefung
    (0x100, 0x1AED, 0x1B60, "range"),  # Schreibstrom-Kern (nur per Sprungtabelle erreicht,
                                        # rekursiver Abstieg findet ihn nicht von selbst)
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
            "OUT(11H)=A (24H oder 26H je nach Vorzweig 1A6E) — vermutlich ein zweites "
            "PIO-Steuerwort [?], danach CALL sub_1A89 (Wartefunktion, s. dort)",
    0x1A83: "L1A83: Fehlerpfad 'DISK READ/ONLY' — Fehlercode 11H nach (3599H), kein Zugriff "
            "auf die K5122",
    0x1A89: "sub_1A89: Wartet auf (359AH) Bit 1 (fertig?) oder Bit 7 (Timeout-Flag, per "
            "DE-Abwaertszaehler FFFFH gesetzt, wenn nichts passiert) — Timeout-Polling ohne "
            "Interrupt; im Timeout-Fall (L1AA7) wird trotzdem ein Byte an 14H nachgeschoben "
            "(IN(16H) gefolgt von OUT(14H) — Daten-PIO-Latch leeren/vorbelegen, kein "
            "Pruef-Lesen im Sinn eines CRC-Vergleichs)",
    0x1AB8: "sub_1AB8: baut Zeiger (352FH Ende, 3523H Byte-Zahl, 3527H->3511H "
            "Anfangszeiger) fuer den GESAMTEN zu schreibenden Bereich einer Operation "
            "(HL+=BC*n je nach Feldgroesse); OUT(14H)=00H + IN(16H) + OUT(10H) als "
            "Vorbereitung, dann OUT(11H)=22H (PIO scharf) und CALL sub_1A89 (auf Bereitschaft "
            "warten) — der eigentliche Bytestrom folgt erst in sub bei 1AEDH (s. u.), von "
            "hier per Sprungtabelle/berechnetem Aufruf erreicht (daher als eigene 'range'-"
            "Region disassembliert, nicht vom rekursiven Abstieg gefunden)",
    0x1AED: "[SCHREIBSTROM-KERN, /WAIT-Betrieb — das Herz fuer AP-E4f] PUSH AF; OUT(10H)=00H "
            "(alle Steuerausgaenge low: /WE, MK, /FR, /STR, MK1, MR/SD, /HL, /ST = 0 — "
            "Ruhezustand vor dem Strom); OUT(14H)=00H (Schreibdatenport vorbelegen/leeren)",
    0x1AF9: "HL=(3523H) = Byte-Zahl des Bereichs (von sub_1AB8/sub_1A2F vorbereitet), "
            "DE=(3527H) = Zeiger auf eine RAM-Tabelle aus BYTE-PAAREN (Datenbyte, Steuerwort) "
            "— je ein Paar pro auszugebender Spurposition. C=14H fest fuer die folgenden "
            "OUTI/OUT(C) (Schreibdatenport)",
    0x1B03: "dreifaches OUT(C),B (=OUT(14H),00H) als Priming/Anlauf, dazwischen OUT(11H)=20H "
            "(PIO-Steuerwort, vermutlich Freigabe [?]) und CALL sub_1A18 (EI; RETI — "
            "sub_1A18 wird sowohl als echtes ISR-Ende ALS AUCH hier als gewoehnliches "
            "'EI + Ruecksprung' benutzt, RETI wirkt bei CALL wie RET); (3580H)=00H, "
            "(3581H) inkrementiert (Versuchs-/Durchlaufzaehler)",
    0x1B1C: "HAUPTSCHLEIFE: A=(DE)=Datenbyte -> B, DE++; A=(DE)=Steuerwort, DE++; OUTI "
            "(schreibt (HL)->Port(C=14H): DAS DATENBYTE aus (HL), HL++, B--) — HL zeigt dabei "
            "NICHT auf die (DE)-Tabelle, sondern auf denselben laufenden Speicherbereich wie "
            "der Datenstrom [Bytezaehlung ueber B/OUTI, das Tabellen-Byte-Paar liefert nur "
            "das STEUERWORT fuer OUT(10H) direkt danach]; OUT(10H)=A (das zuvor aus (DE) "
            "gelesene STEUERWORT) — je Byte wird also SOFORT NACH dem Datenbyte ein "
            "Steuerwort ausgegeben (MK-Bit fuer Sync-/Adressmarken-Bytes wie A1/FE/FB, "
            "sonst 0 fuer normale Gap-/Daten-Bytes) — die Spur ist als (Byte,Steuerwort)-"
            "Paartabelle im RAM vorgebaut und wird im Bus-Takt hinausgeschrieben, ANALOG zur "
            "Lesestrom-Seite des /BUSRQ-Wegs (buildFaithfulReadTrack, Design-Doc §7.6/§7.7 in "
            "07_k5122_afs.md); doppeltes OUTI je Schleifendurchlauf (1B21H/1B28H), danach ein "
            "drittes bedingt ueber den (3580H)-Rest-Zaehler (1B2CH ff.) fuer das letzte Byte "
            "einer nicht durch 2 teilbaren Menge",
    0x1B38: "Abschluss: EX DE,HL (Bytezaehler HL<->DE); OUT(10H)=(3567H) (Steuerwort "
            "zuruecksetzen); IN A,(16H) (Lesedatenport LEEREN/entladen — kein Vergleich mit "
            "dem Geschriebenen, also KEIN Pruef-Lesen an dieser Stelle; passt zur Doku-Lage "
            "'Treiber schreibt ohne Pruef-Lesen', Design-Doc 07_k5122_afs.md §7.7); Bit 1 in "
            "(359AH) setzen (Fertig-Flag fuer sub_1A89-Wartende); Restlaenge HL=HL-DE+5 nach "
            "(352BH) ablegen (Buchfuehrung fuer den Aufrufer)",
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
        "; Design-Doc 07_k5122_afs.md §3) -- der Schreibstrom-Kern ab 1AEDH baut eine",
        "; (Datenbyte,Steuerwort)-Paartabelle im RAM und schreibt sie per OUTI/OUT(10H)",
        "; im Bus-Takt hinaus (kein Pruef-Lesen). DISGEN.COM dagegen bleibt beim normalen",
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
