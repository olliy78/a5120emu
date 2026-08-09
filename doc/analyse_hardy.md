# HARDY-Hardwaretest — Reverse-Engineering & Emulator-Handoff

Vollständige Erkenntnisse aus der Analyse von **`boot_disk/hardy.com`** (HARDY V.3/1, Humboldt-
Universität zu Berlin, 31.01.87), damit weitere Testabschnitte in einer neuen Session
genauso ans Laufen gebracht werden können wie der bereits gelöste **Rechner-Test**.

- Bootdiskette: `disks/scpx17_5x1024_k5601_hardy.hfe` (SCPX 1526 V1.7, Geometrie 5×1024) → bootet
  vollautomatisch bis `A>`.
- Der MEMDI-Freeze-Fix des Rechner-Tests ist separat in `doc/analyse_hardy_memdi.md`
  beschrieben; dieses Dokument ist die **Gesamtreferenz** (Struktur, I/O-Landkarte,
  Vorgehen) für alle Testabschnitte.
- Guard-Test: `tests/system/test_hardy.cpp` (`Hardy.RechnerTestRunsCleanWithoutFreezing`).

---

## 1. Programm-Grundgerüst

- **CP/M-.COM**, lädt ab `0x0100`, 16128 Byte → belegt `0x0100–0x3FFF`.
- `0x0100  JP L045D` (Einsprung). Code bis ~`0x24xx`, danach **Datenblock/Strings**
  (`0x24xx–0x27xx`) + Interrupt-Stubs (`0x09ED–0x0A36`).
- Disassemblieren: `python3 tools/z80_disasm2.py --org 0x100 boot_disk/hardy.com > hardy.asm`.
- Läuft unter **SCPX** (CP/M-kompatibel): BDOS-Aufrufe `CALL 0005H` (C=2 Zeichen, C=9
  `$`-String), Konsole/Tastatur im SCPX-BIOS ab `0xD000` (CONOUT `D148`/`D190`, CONIN
  `E079`, Print-Schleife `D1D3`).

### Init (`L045D`, 0x045D)
1. `LD A,I` → speichert das vom OS geerbte I-Register nach **`[0421H]`** (Anzeige
   „CPU-Interruptvektor", unter CP/A = `0xF7`).
2. Kopiert 256 Byte der OS-Vektortabelle von `I_old*256` nach **`0x0300`** (LDIR).
3. `LD I, 03H` → HARDY läuft ab jetzt mit **eigener IM2-Vektorseite `0x0300`** (Kopie der
   OS-Vektoren + eigene Einträge). Kein `IM`-Opcode im Binary → IM2 wird vom OS geerbt.
   Die eigenen ISR-Zeiger liegen ab **`0x03D0`** (16-Byte-Block, kopiert bei `L06C1`
   aus `0x09ED`).
4. Tastatur-Erkennung `0x052D`: wartet per Direkt-Poll auf eine **Leertaste** (0x20).

### Feste Konfigurationsbytes (HARDY-eigener Datenblock)
| Adresse | Wert | Bedeutung |
|---------|------|-----------|
| `[041FH]` | `0xD0` | HARDYs private Vektor-Basis (Zielseite `0x03D0`) |
| `[0420H]` | `0x03` | HARDYs eigenes I-Register |
| `[0421H]` | *runtime* | vom OS geerbtes I (Anzeige, `0xF7`) |
| `[0422H]` | `0x0C` | **CTC-Basisport** (System-CTC Kanal 0) |
| `[0457H]` | `0x5C` | Tastatur-**Status**port (= `[041AH]`, erkannt) |
| `[0458H]` | `0x5C` | Tastatur-**Daten**port |
| `[0459H]` | — | HARDYs 1-Byte-Tastenpuffer |
| `[0456H]` | — | zuletzt gelesenes BS-PIO Port B (Konfig-Brücken) |
| `[0A32H]` | — | ISR-Sammelflags Bits 0–3 (CTC-/OSS-Test) |
| `[0A33H]` | — | „armed"-Flag für PIO-/RDY-Interrupt (ISR `0x0A24` löscht es) |

---

## 2. I/O-Landkarte (aus HARDYs Sicht + Emulator-Karten)

| Port | Karte / Chip | Nutzung im Test |
|------|--------------|-----------------|
| `08H` | K2526 **BS-PIO Q301** Port A Daten | MEMDI (Bit7, Ausgang) setzen/lesen; Interruptquelle |
| `09H` | BS-PIO Port A **Control** | Vektor + Interrupt-Control-Word (ICW) + Maske |
| `0AH` | BS-PIO Port B Daten | `/LD-ROM` (Bit0), Konfig-Brücken lesen (Bits 5–7) |
| `0BH` | BS-PIO Port B Control | — |
| `0CH–0FH` | K2526 **System-CTC Q302** Kanal 0–3 | System-CTC-Test |
| `5CH` | K8025 **SIO A32 ch A** Daten | Tastatur (K7637) lesen |
| `5DH` | SIO A32 ch A **Control** | RR0-Status (Bit0 = RX ready) |
| `5EH/5FH` | SIO A32 ch B (Drucker) | (Drucker-Test) |
| `50H–53H` | K8025 SIO A33 (DFÜ) | (Datenübertragungs-Test) |
| `58H–5BH` | K8025 CTC A34 (Baud) | Baudraten |
| `EEH` | OSS-Bank-Umschaltport | OSS-Erkennung (`3CH`=ein, `00H`=aus) |

### BS-PIO Port A Bit-Belegung (K2526, siehe `core/cards/k2526/k2526.h`)
`A0 /M1` · `A1 /SUE` · `A2 /NMI` · `A3 SPS-Ind` · `A4 /EBF` · `A5 /WR` · `A6 /RDY` ·
`A7 = MEMDI1/2 (Ausgang)`. **Alle A0–A6 sind Eingänge**; im Test dienen sie als
Interruptquellen (Mode-3-Bitüberwachung).

---

## 3. Menü- & Ablaufstruktur

```
HARDY⏎  → Copyright-Banner            (wartet auf LEERTASTE, sub_064C/Direkt-Poll)
        → "Achtung / Dokumentation"   (wartet auf LEERTASTE)
        → Testauswahl-Menü            (1-Rechner … 7-Floppy, X-Austritt)
             1 → Rechner-Test  ─┐
             2 → RAM            │  jeweils Auto-Lauf + evtl. eigenes Untermenü,
             3 → Tastatur       │  danach LEERTASTE zurück ins Testmenü
             …                  │
             7 → Floppy        ─┘
             X → Austritt (zurück zu CP/M)
```

**Tastatur-I/O (wichtig!):** HARDYs Dialoge/Untermenüs lesen die Tastatur per **DIREKT-
Poll** der SIO (`sub_0E56` Status Port `5DH` RR0-Bit0, `sub_0E6D` Daten Port `5CH`),
NICHT über den BDOS-CONIN-Puffer. Folge: eine Taste greift nur, wenn HARDY im Moment des
Anschlags gerade pollt (sonst geht sie verloren). Dialoge erwarten **SPACE** (0x20),
Menüpunkte eine **Ziffer/X**. Routinen: `sub_0E40` (Poll-Dispatch), `sub_0E56`
(Status+Read), `sub_0E6D` (blockierend lesen), `sub_064C` (auf SPACE warten), `sub_0654`
(Zeichen ausgeben via BDOS func 2 **und** Tastatur in `[0459]` mitpuffern).

---

## 4. Rechner-Test (Menü 1) — Untertest-Aufschlüsselung

Auto-Lauf, gibt Zeile für Zeile aus. Adressen = ZVE1-PC.

| Ausgabe | Code | Mechanik | Emulator-Anforderung |
|---------|------|----------|----------------------|
| `CPU - Interruptvektor: F7` | `0x06EF` | `[0421H]` (geerbtes I) als Hex | — (Info) |
| `Geraetekonfiguration: 000` | `sub_09BB`+`sub_0977` @`0x070E/11/14` | `IN 0AH` (BS-PIO Port B), Bits 7/6/5 als `0/1` | Brücken = 0 |
| `Lade-EPROM umschaltbar` / `ok checksum:F76B` | `sub_09C3/09CD` (Port B Bit0 = /LD-ROM), Checksumme | Boot-ROM ein/aus + summieren | ROM-Remap ✓ |
| `System - CTC ok` | `0x0783` | CTC Kanäle `0C–0F` mit Vektoren `10/20/30/40` armen, Timer-Ablauf setzt je 1 Bit in `[0A32]`; alle 4 → ok | CTC-Interrupts ✓ |
| `System - PIO ok` | `0x07F0` | BS-PIO Port A Interrupt auf **A0=/M1** (Maske `0xFE`, aktiv-low) armen; ISR `0x0A24` löscht `[0A33]` | **/M1 aktiv** (Fix) |
| `keine OSS` | `0x0828` | Port `EEH` (`3CH`/`00H`) schaltet OSS-Bank bei `0x4000`; ohne Zusatzkarte kein Unterschied | Port EE inert = keine OSS ✓ |
| `MEMDI1/2 aktiv: RDY bei: 00-FF` | `sub_0915`/`sub_09D7` @`0x0867/0x0876` | Seiten-Sweep 0x00–0x07; BS-PIO Port A Interrupt auf **A5=/WR & A6=/RDY** (Maske `0x9F`, AND, aktiv-low); Grenzen als Hex | **/WR+/RDY aktiv** (Fix) |
| `MEMDI [0][0][0][0]` | `L0887` `0x0881–0x0907` | pro 16K-Gruppe: schreibt mit/ohne MEMDI, prüft ob geblockt → `[-]/[0]/[1]/[2]/[1/2]` | /MEMDI wirkungslos → `[0]` (Fix) |

### Schlüsselroutinen (Rechner-Test)
| Adr | Routine |
|-----|---------|
| `0x069F` | `/MEMDI` löschen: `DI; LD C,08; IN A,(C); RES 7,A; OUT (C),A` |
| `0x06A9` | `/MEMDI` setzen: `LD C,08; IN A,(C); SET 7,A; OUT (C),A; EI` |
| `0x06B3`/`0x06BA` | OSS-Bank ein/aus: `OUT (EEH),3CH` / `OUT (EEH),00H` |
| `0x090A` | Speicherzelle testen (Muster `D=0x55`, `E=0xAA`, restauriert Original) |
| `0x0915` | RDY-Sweep (C=1 → MEMDI1, C=2 → MEMDI2) |
| `0x09BB` | `IN 0AH` (BS-PIO Port B) → `[0456]` |
| `0x09D7` | PIO-Interrupt armen: `OUT 09: D7H, 9FH` (ICW+Maske) |
| `0x09E6` | PIO-Interrupt entwaffnen: `OUT 09: 07H` |
| `0x0977` | ein Bit als ASCII `0`/`1` drucken (`RL C`) |
| `L0591` (`0x0591`) | Rechner-Untermenü-Dispatch (`x`/`X`→`L05D1`, Ziffer→`L06C1`) |
| `L06C1` (`0x06C1`) | Test-1-Setup: ISR-Tabelle nach `0x03D0` kopieren, PIO konfigurieren |

### Interrupt-Stubs (ab `0x09ED`, kopiert nach `0x03D0`)
- `0x09FD`/`0x0A06`/`0x0A0F`/`0x0A18` → setzen je Bit 0/1/2/3 in `[0A32]` (CTC-/OSS-Kanäle),
  gemeinsamer Tail `0x0A1F`: `POP HL; POP AF; EI; RETI`.
- `0x0A24` → `CALL 09E6` (PIO entwaffnen); `XOR A; LD (0A33H),A` (Flag löschen); `RETI`.
  → das ist der „Interrupt kam an"-Beleg für System-PIO- und RDY-Test.

---

## 5. Emulator-Lücken, die der Rechner-Test aufdeckte (Muster für weitere Tests!)

1. **/MEMDI sperrte den Speicher** → RST-38-Freeze. Fix: globales `/MEMDI` gatet
   `memRead`/`memWrite` NICHT (Standard-A5120: keine OPS-Gruppe darauf gejumpert). Details
   `doc/analyse_hardy_memdi.md`.
2. **Bus-Strobes fehlten an BS-PIO Port A** → „kein INTERRUPT" / leere RDY-Zeile. Fix:
   `port_a_inputs_ = 0xFF & ~0x61` (A0=/M1, A5=/WR, A6=/RDY aktiv-low) in `k2526.cpp`.
3. **Z80PIO wertete Mode-3-Interrupt nur bei Pegeländerung aus** → Selbsttest löste nie
   aus. Fix: Neubewertung beim Scharfmachen in `z80_pio.cpp writeCtrl`.

**Verallgemeinerung:** HARDY testet Hardware über **Port-I/O + Mode-3-PIO-Interrupts +
CTC-Interrupts**. Fehlende/statische Signalmodellierung → „kein INTERRUPT"/„nicht
bereit". Vorgehen bei jedem neuen Testabschnitt: den ISR-/Poll-Mechanismus disassemblieren,
die überwachten Port-Bits/Interruptquellen identifizieren und im passenden Kartenmodell
als aktiven Strobe/Interrupt nachbilden.

---

## 6. Erwartete weitere Testabschnitte (Hypothesen, noch NICHT verifiziert)

| Menü | Vermutlich getestet | Vermutete Emulator-Karte / offene Punkte |
|------|---------------------|------------------------------------------|
| 2 RAM | TPA/Speicher-Muster-Test (Sweep über OPS) | K3526 (Schreiben/Lesen funktioniert bereits) — sollte weitgehend laufen |
| 3 Tastatur | Tastatur-SIO/K7637 direkt (Scancodes, LEDs) | K8025 SIO A32 ch A + K7637; Direkt-Poll-Timing beachten |
| 4 Monitor | K7024-Bildschirm (VRAM/Zeichensatz/6845) | K7024; evtl. Retrace-/Status-Bits nötig |
| 5 Drucker | Drucker-SIO (K8025 SIO A32 ch B) | ch B ohne Peripherie → evtl. „nicht angeschlossen" (Soll?) |
| 6 Datenübertrg | DFÜ-SIO (K8025 SIO A33) | A33 ohne Gegenstelle → evtl. Loopback/Handshake nötig |
| 7 Floppy | K5122-Controller (Drehzahl, /RDY, Seek, R/W) | K5122; viele Statusleitungen (/RDYL, /TO, /WP, /FW) |

Strings im Binary geben Hinweise (`strings -n4 boot_disk/hardy.com`), u.a. schon gesehen:
`Kopfandruck ein ==> / RDY = 0/1`, `Laufwerk bereit / nicht bereit` (Floppy-Test).

---

## 7. Arbeits-Rezept (reproduzierbar, für die nächste Session)

```sh
# 1) Disassemblieren
python3 tools/z80_disasm2.py --org 0x100 boot_disk/hardy.com > /tmp/hardy.asm

# 2) Strings + Referenzen finden
strings -n4 boot_disk/hardy.com                 # Testabschnitt-Meldungen
python3 -c 'd=open("boot_disk/hardy.com","rb").read(); print(hex(d.find(b"...")+0x100))'

# 3) Im Debugger reproduzieren (COW-Mount ist Default → Fixture sicher)
tools/dev.sh tool k1520dbg -s tools/scpx1526.sym disks/scpx17_5x1024_k5601_hardy.hfe -x script.dbg
```
`script.dbg` (Muster):
```
g 90000000            # bis SCPX-Prompt A>
keys HARDY\r
g 30000000
keys \                # LEERTASTE (Backslash-Space-Escape!) — Dialog 1
g 8000000
keys \                # LEERTASTE — Dialog 2
g 8000000
keys 2                # Menüpunkt wählen (hier: RAM)
g 40000000
screen                # 80×24-VRAM ansehen
where                 # beide CPUs; PC=0038 (RST 38H) = MEMDI-artiger Speicher-Disable-Crash
hist 2000000          # Hot-Loop lokalisieren (Soft-Hang)
bt                    # Callstack (CALL/RST/RET-Tracker)
```

**Gotchas, die Zeit gekostet haben:**
- **Leertaste** = `keys \ ` (Backslash + Space). `keys ` mit nur Whitespace → „unknown
  command".
- HARDY reagiert nur, wenn es **gerade pollt** — Taste ggf. mehrfach senden, bis der
  Folge-Screen erscheint (im Guard-Test: Helper `pressKeyUntil`).
- **`PC=0x0038` in `where`** = die CPU holt `0xFF`(=RST 38H) als Opcode → typischerweise
  ein Speicher-Disable-/Read-Gate-Problem (wie /MEMDI).
- Nach dem Fetch-Crash folgt oft ein **Soft-Hang** in BIOS (`D1D3`-Print-Schleife u.ä.);
  `bt` zeigt, dass der Aufrufer HARDYs Print-`CALL 0005` ist → das eigentliche Problem
  liegt im HARDY-Code davor (z.B. MEMDI blieb gesetzt).
- Boot bis `A>` kostet ~90–130 M Zyklen **pro Lauf** — das war der größte Zeitfresser
  (siehe Feature-Request `doc/feature_requests/interaktive_programme.md`).

Delegation: breite Disasm-Analyse eines Testabschnitts → `boot-disasm-analyst`-Subagent
(rein statisch gegen `/tmp/hardy.asm`), Log-/Trace-Auswertung → `log-trace-analyzer`.
