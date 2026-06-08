# Analyse: Bootloader (geladenes Boot-Record, ab 0x0400)

Diese Analyse beschreibt den **zweiten** Abschnitt des Bootvorgangs: den Code, den
das ZRE-Boot-ROM per DMA von der Diskette nach `0x0400` lädt und ab `0x0437`
startet. Die erste Stufe (Boot-ROM + ZVE2-DMA) ist in
[`analyse_zre_rom_boot.md`](analyse_zre_rom_boot.md) dokumentiert.

> **Stand (2026-06-08):** Der Bootloader läuft an, gibt seinen Banner aus
> (`Bootloader, Version 24.02.87`), und lädt per loader-eigener ZVE2-Routine (`0x062E`)
> ~52 Sektoren nach `0x0800–0x2200` (CRC-verifiziert). **Der Sekundär-Loader ist
> vollständig** — er springt nach `0x0880` (`JP 0x1800`) in die **dritte Stufe**
> (CP/A-Bootsystem), die ihren Banner ausgibt (`CP/A-Bootsystem, Version 05.04.88 laedt
> @OS.COM …`) und `@OS.COM` aus dem 1024-B-Datenbereich lädt (siehe §7).
> **Aktueller Stand der dritten Stufe:** Datenbereich-Read funktioniert (cyl 2/3/4, 1024 B,
> CRC OK), `@OS.COM` lädt nach `0x0100`; **offen** ein Timeout `'U'` bei cyl 2 head 1.
> Lesemodelle: [`K1520_architecture.md` §14.5/§14.5b](K1520_architecture.md). Das
> Interrupt-System funktioniert (IM2, Daisy-Chain, RETI, Index-Interrupt).

## Werkzeuge / Reproduktion

```sh
# Bootloader laufen lassen, RAM 0x0400–0x0800 dumpen, Trace + Screen ausgeben
./build/boot_trace -L /tmp/emu.log -p 9000000 -d 0x0400:0x0800 /tmp/ram_loaded.bin disks/cpadisk01.img

# Den gedumpten Live-RAM disassemblieren (relocateter/ggf. selbstmodifizierter Code –
# das Disk-Image ist KEINE verlässliche Quelle, daher Live-Dump verwenden):
python3 tools/z80_disasm2.py /tmp/ram_loaded.bin --org 0x0400 --entry 0x0437 \
    --entry 0x052A --entry 0x060E --entry 0x0624 --no-strings
```

Das `-d start:end [datei]`-Flag von `boot_trace` schreibt den Live-RAM-Bereich nach
dem Lauf in eine Binärdatei (Default `/tmp/ram_dump.bin`). Der `boot_trace`-Summary
(`-p`) enthält zusätzlich ein I/O-Port-Histogramm, VRAM-Schreibzähler, ein
Loaded-Code-PC-Histogramm und einen 80-Spalten-Textdump des VRAM.

## 1. Speicherlayout des geladenen Records

Das Boot-ROM lädt die ersten 4 Sektoren (Zyl 0, 512 Byte) nach `0x0400–0x05FF` und
springt nach `0x0437`. Der Loader belegt anschließend:

| Bereich            | Inhalt                                                              |
|--------------------|--------------------------------------------------------------------|
| `0x0400–0x0402`    | Signatur `53 59 4C` = `"SYL"` (vom ROM bei `0x01B6` geprüft)        |
| `0x0407–0x042D`    | `sub_0407` – CRC-16-Routine (siehe §2)                             |
| `0x0437…`          | Loader-Einsprung / Initialisierung (§3)                            |
| `0x052A…`          | Interruptgesteuerte Haupt-/Warteschleife (§4, §5)                  |
| `0x0605`           | Fehler-Handler (Halt-Schleife `JR $`)                              |
| `0x060E`           | ISR **Timer** (Vektor `0x62`) – dekrementiert `[07EC]`            |
| `0x0624`           | ISR **Event** (Vektor `0x60`) – setzt Bit0 von `[07F7]`           |
| `0x062E…`          | weiterer ISR-/Hilfscode (`[0x0001]`-Vektor zeigt hierher)         |
| `0x06C1`           | Banner: Attribut `0x06` + `"Bootloader, Version 24.02.87"` (0x1D B) |
| `0x0760/0x0762`    | IM2-Vektortabelle: `0x0624` (Vek 0x60), `0x060E` (Vek 0x62)        |
| `0x0770…`          | Tabelle (`[07F4]`-Zeiger, von der Verifikation gelesen)           |
| `0x07E0–0x07FF`    | Handshake-/Statusvariablen (§4.1)                                 |

## 2. `sub_0407` – CRC-16

```asm
L0407: LD A,(IY+0)   ; Datenbyte
       XOR B
       LD B,A
       RRCA … (4×)   ; Nibble-Swap + Tabellenlose CRC-Berechnung
       AND 0FH / XOR B …
       INC IY
       DEC E          ; E = Byteanzahl
       JR NZ,L0407
       RET            ; Ergebnis in BC
```

Klassische byteweise CRC-16 über `E` Bytes ab `IY`, Ergebnis in `BC`. Der Loader
nutzt sie an mehreren Stellen, um geladene Sektoren bzw. Header zu verifizieren
(`CP B` / `CP C` gegen abgelegte Soll-CRC). Das CRC-Polynom-Startwort wird je nach
Pfad-Byte `[03FD]` Bit1 gewählt (`BF84H` vs `E295H` / `CDB4H`).

## 3. Initialisierung ab `0x0437`

```asm
0437  OUT (EEH),0          ; HW-Strobe (Port 0xEE – im Emulator nicht belegt)
043B  ── 3× CALL sub_0407  ; Verzögerung / „warm-up"
0445  CP (IY+0) / CP (IY+1); CRC des geladenen Records gegen Soll prüfen
      JP NZ,0605           ;   → Fehler-Halt bei Mismatch
0453  LD HL,0 / LD BC,0800 ; High-RAM löschen (VRAM-Bereich)
0459  IN A,(0AH) / BIT 6   ;   ZRE-Port 0x0A: Konfig (BC=0x0400 oder 0x0800)
046A  LD HL,06C1 / LDIR    ; Banner „Bootloader, Version 24.02.87" → VRAM
0471  LD (0760),0624 / LD (0762),060E   ; IM2-Vektortabelle füllen
047D  LD A,07 / LD I,A     ; IM2-Page = 0x07
0481  OUT (11H),62H        ; ctrl-PIO Port A: Interruptvektor = 0x62  (→ ISR 060E)
0485  OUT (13H),60H        ; ctrl-PIO Port B: Interruptvektor = 0x60  (→ ISR 0624)
0489  LD (0001),062E       ; ISR-Hilfsvektor
048F  LD (07FC),0034       ; 0x34 = 52 Sektoren zu laden
0495  LD (07FA),0800       ; Ladezeiger = 0x0800
049B  [07E9]=0 / [07EA]=1
04A6  ── Spurpositionierung (Step-Schleife, OUT(10) Step / IN(12) Status)
04DC  ── Lese-Auftrag aufsetzen ([07F0..3], [07ED]=B5, [07E0..3])
0500  ── ctrl/data-PIO scharfschalten: OUT(13),97/AF, OUT(10/14/17/04)
052A  ── Eintritt in die Warteschleife
```

Wichtige Erkenntnis: Der Loader arbeitet **vollständig interruptgesteuert** über die
beiden Z80-PIOs der K5122 (`ctrl_pio_`, Ports `0x10–0x13`):

* `OUT(11H),0x62` setzt den **Interruptvektor** von ctrl-PIO **Port A** auf `0x62`
  (Bit0=0 ⇒ Vektorwort). Vektor `0x62` ⇒ Tabelle `[0x0762]=0x060E` = **Timer-ISR**.
* `OUT(13H),0x60` setzt den Interruptvektor von ctrl-PIO **Port B** auf `0x60`.
  Vektor `0x60` ⇒ `[0x0760]=0x0624` = **Event-ISR**.
* Die späteren `OUT(13H),0x97` / `0xAF` sind PIO-**Interrupt-Control-Words** (Bit0=1,
  Bit7=EI) – sie schalten die PIO-Interrupts scharf.

`EI`/`IM 2` selbst stehen **nicht** im geladenen Code – `IFF1=1` und `IM 2` werden
vom Boot-ROM geerbt (der ROM-Pfad `CALL [0x0175]` macht kein `DI`).

## 4. Die Haupt-/Warteschleife `0x052A`

```asm
L052A: LD A,(07F7H)   ; Event-Flags (von ISRs gesetzt)
       LD B,A
       RRC B
       JR NZ,L059B     ; Flag gesetzt → Ereignis verarbeiten (059B)
       LD L,EAH
       LD A,(07E3H)
       CP (HL)         ; [07E3] (empfangen) vs [07EA] (verarbeitet)
       JR Z,L052A      ; gleich → weiter warten
       … (053A) Sektor verifizieren: sub_0407, Pointer fortschalten, [07FC]--
L0576: LD HL,0880H / PUSH HL / … / RET   ; [07FC]==0 → Sprung nach 0x0880 (nächste Stufe)
```

Die Schleife verlässt den Wartezustand nur, wenn

1. eine ISR ein Bit in `[07F7]` setzt (Event/Timeout), **oder**
2. `[07E3] != [07EA]` wird, d.h. ein **neuer Sektor** per Interrupt eingetroffen ist.

Pro eingetroffenem Sektor wird die CRC geprüft (`sub_0407`), der Ladezeiger `[07FA]`
und der Sektorzähler `[07FC]` fortgeschaltet. Erreicht `[07FC]` Null, legt der Loader
`0x0880` auf den Stack und springt per `RET` dorthin — **die nächste Loader-Stufe**
(ab `0x0800/0x0880`), die letztlich `@os.com` aus dem Dateisystem laden und starten
soll. `0x0880` liegt außerhalb des aktuell geladenen Records und ist erst nach den
52 Folgesektoren gültig.

### 4.1 Handshake-/Statusvariablen (Page 0x07)

| Adresse  | Bedeutung (abgeleitet)                                              |
|----------|--------------------------------------------------------------------|
| `[07E0]` | Sync/IDAM-Sollwert (`FE`)                                          |
| `[07E1]` | Zylinder (Soll)                                                   |
| `[07E2]` | Kopf                                                              |
| `[07E3]` | **empfangener** Sektorzähler (von der Lese-ISR fortgeschaltet)     |
| `[07E9]` | aktueller Zylinder-Index                                          |
| `[07EA]` | **verarbeiteter** Sektorindex (in der Hauptschleife `INC`)        |
| `[07EC]` | Timer-Countdown (Start `0x05`), von ISR `060E` dekrementiert       |
| `[07F4]` | Zeiger in die Verifikationstabelle (`0x0770…`)                    |
| `[07F7]` | Event-Flags: Bit0 (Event-ISR 0624), Bit1 (Timer-Timeout 060E)     |
| `[07FA]` | Ladezeiger (Start `0x0800`)                                       |
| `[07FC]` | verbleibende Sektoren (Start `0x0034` = 52)                       |
| `[03FD]` | Pfad-Byte aus Stufe 1 (`0x87`), Bit1 wählt CRC-Startwort           |

### 4.2 Die ISRs

```asm
; Vektor 0x62 → Timer-Tick (ctrl-PIO Port A)
060E  EI / PUSH AF/HL
      LD HL,07ECH / DEC (HL)      ; Countdown
      JR NZ,0620
      LD HL,07F7H / SET 1,(HL)    ; Timeout → Bit1 in Event-Flags
      LD A,ADH / OUT (10H),A
0620  POP HL/AF / RETI

; Vektor 0x60 → Event (ctrl-PIO Port B)
0624  EI / PUSH AF/HL
      LD HL,07F7H / SET 0,(HL)    ; Bit0 in Event-Flags
      JR 061C (… OUT(10),AD / RETI)
```

## 5. Sektor-Load + Track-Übergang — **funktioniert (gelöst)**

Der Loader lädt per loader-eigener ZVE2-Routine (`0x062E`) die Folgesektoren in die
Hauptschleife `0x052A` ein: pro empfangenem, **CRC-verifiziertem** Sektor wird der
Ladezeiger `[07FA]` und der Sektorzähler `[07FC]` fortgeschaltet, `[07EA]` (verarbeitet)
nachgezogen. So werden ~52 Sektoren echter Daten nach `0x0800–0x2200` geladen.

Das zugrunde liegende **Diskettenlese-Modell** (Adressmarken-Felder IDAM/DATEN,
`MK`-Strobe-getriebener Feldwechsel, reale CRC-16) ist in
[`K1520_architecture.md` §14.5](K1520_architecture.md) spezifiziert. Der Track-Übergang
(ZVE2-Track-Ende `L0696`, `/BUSRQ`-Freigabe bei `OUT(13H),03H`, Seek, ZVE2-Reset via
`OUT(04)`) ist gelöst — `[07FC]` zählt sauber auf 0, der Loader springt nach `0x0880`
(`JP 0x1800`) in die dritte Stufe.

## 7. Dritte Stufe — CP/A-Bootsystem, `@OS.COM`-Load aus dem Datenbereich

Die dritte Stufe (`0x1800` = `JP 0x08AF`-Bereich; Banner `CP/A-Bootsystem, Version
05.04.88 laedt @OS.COM …`) lädt `@OS.COM` aus dem **1024-B-Datenbereich** der Diskette.

**Disk-Geometrie (asymmetrisch, geklärt):** Die Seiten sind interleaved (cyl0/A, cyl0/B,
cyl1/A, cyl1/B, …). Der Systembereich sind **drei** 128-B-Seiten (cyl 0 beide Seiten +
cyl 1 Seite A); der **1024-B-Datenbereich beginnt bei cyl 1 Seite B** (`0x2700`, Füller),
das CP/M-**Verzeichnis** liegt sauber bei **cyl 2 Seite A** (`0x3B00`,
`00 40 4F 53 …"@OS     COM"` + `CPABCGEN`/`FORMAT`/…; `3×3328 + 5120 = 0x3B00`). `@OS.COM`
ist die erste Datei (Allocation-Blocks 02–09, Record-Count `0x76`). Die dritte Stufe liest
ihren Datenbereich hardkodiert bei physisch cyl 2 (`0x1F7D`: IDAM cyl=2, size_code=3). Frühere
Fehlannahmen: 3 Boot-Spuren (cyl 2 = 128 B → Size-Mismatch `RU;…=020001`) bzw. cyl 1 B = 128 B
(Datenbereich `0x700` zu früh, @OS.COM-Blöcke fehlausgerichtet).

**Eigene ZVE2-DMA-Routine `0x1F7D`** (1024-B-Read): liest IDAM (Verify cyl/head/sec/size)
und Daten **kontinuierlich** (`INIR`), re-syncht über `MK1` (Steuer-Port-A Bit 4), und
**CRC-verifiziert** jeden Sektor mit der ZVE1-Routine `sub_1E44` (= `loaderCrc16`, aber
Seed `0xCDB4` statt `0xBF84`, über `[Datenmarke]+1024 Daten`). Anders als der
Sekundär-Loader (diskrete `MK`/Bit-1-Felder). Vollständige Spezifikation des Emulator-
Modells: [`K1520_architecture.md` §14.5b](K1520_architecture.md).

**Read-Setup `0x1F36`** (byte-genau): `[0x0000]=JP 0x1F7D`, `OUT(04)=0x00` (ZVE2-Reset),
`/STR`-Flanke → `doReadSector`, `[0x0000]=JP 0x1803` wiederhergestellt, Warteschleife
`0x1F6C`. ZVE2 läuft `0x1F7D` aus PC=0 an, getriggert durch das `/BUSRQ` des `/STR`
(`K2526::zve2StartFromReset`).

**Read-/Verify-Pfad (ZVE1):** Retry-Schleife `0x1D3C…0x1DDE` (Read = `CALL 1E2A`,
Verify = `CALL 1E20`); CRC-Routine `sub_1E44`, CRC-Vergleich `sub_1E20` (`DE` vs
`(IX+0/1)`); Timeout-/Status-Poll `0x1C5B` (`[0x1E78]`). Fehleranzeige `sub_1BF0`:
`"RC;T,Si,Se=…"` — 2. Zeichen `'C'`=CRC (`0x1DE1`) / `'U'`=Timeout (`0x1E04`).

**Aktueller Stand:** Datenbereich-Read funktioniert über cyl 2/3/4 (beide Köpfe, 1024 B),
CRC besteht, `@OS.COM` lädt nach `0x0100` (≈125/128 echte Bytes in der ersten Spur).
**Offen:** verbleibender Timeout `'U'` bei `cyl 2, head 1` (`RU;T,Si,Se=020101`); siehe
[`K1520_architecture.md` §14.6b](K1520_architecture.md).

## 6. Neu genutzte ROM-/Hardware-Funktionen

* **`sub_0407` (CRC-16)** ist Teil des geladenen Records, nicht des ROMs – der
  Loader bringt seine eigene Verifikation mit.
* Der Loader **erbt** `IM 2` und `IFF1=1` vom Boot-ROM und stellt die IM2-Page auf
  `0x07` um (`LD I,A`). Damit ändert sich die Bedeutung **aller** Interruptvektoren
  gegenüber der ROM-Phase – relevant für den `0xBA`-Konflikt oben.
* Die K5122 besitzt zwei Z80-PIOs (`ctrl_pio_` Ports `0x10–0x13`, `data_pio_`
  `0x14–0x17`). Stufe 1 (ROM-DMA) nutzte primär den Datenstrom über Port `0x16`;
  Stufe 2 nutzt zusätzlich die **Interrupt-Fähigkeit der ctrl-PIO** (Vektoren,
  Interrupt-Control-Words, Port-A/-B-Interrupts). Dieser Pfad ist im Emulator noch
  nicht vollständig modelliert.

Ergänzungen zur ROM-Analyse siehe [`analyse_zre_rom_boot.md`](analyse_zre_rom_boot.md).
