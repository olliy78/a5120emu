# Lade-ROM-Fassungen der K2526 (A5120) im Vergleich — AP A0

Stand 2026-10-13, Zweig `kassette_mc80`.  Grundlage: byteweiser Vergleich der vier Abzüge und
Disassemblat der Unterschiede (`tools/z80_disasm2.py --org 0 --entry 0 --linear`, Hilfsskript
mit Befehlsvergleich ohne Adressoperanden).  **Statisch hergeleitet — im Emulator nicht
gefahren** (der Kern hat keine Möglichkeit, ein anderes Lade-ROM als das eingebettete
`core/cards/k2526/rom_data.h` = `zre.rom` zu laden; siehe §6).

## 1. Die Abzüge

Alle vier sind 1024 B (0000H–03FFH), die **letzten zwei Bytes (03FEH/03FFH) sind eine
CRC-16/CCITT-FALSE** (Poly 1021H, Init FFFFH, big-endian) über 0000H–03FDH.  Kein Code liest
sie; sie dient der EPROM-Prüfung.

| Kurzname | Datei | MD5 | CRC-Feld | CRC gerechnet | Bemerkung |
|---|---|---|---|---|---|
| **V0.6** | `doc/EPROMS/K2526/laderom_v06.bin` | 07245123… | 07F1 | 07F1 ✓ | Anwender-Abzug; `laderom_v06.mac` ist nur ein Kopf (Zeile „V0.6, 02.11.2019“), keine Analyse |
| **V0.9_1** | `doc/EPROMS/K2526/laderom_v09_1.bin` | 0195729d… | 3FAE | 3FAE ✓ | lt. Plan = MAME `a5120_v2` (nicht nachgeprüft) |
| **V0.9_2** | `doc/EPROMS/zre.rom` | 99280684… | F76B | F76B ✓ | lt. Plan = MAME `a5120_v1`; **die im Emulator eingebettete Fassung**; `laderom_orirom.mac` (Anwender 2005, „LOAD.V09“) ist ihr kommentiertes Listing (Bytes stimmen: `ld b,l` 0109, `ld (hl),10h` 0197) |
| **V0.9a (K8924)** | `doc/EPROMS/K2526/k8924_laderom_v0.9a.bin` | 7309c039… | F76B | **2900** ✗ | = V0.9_2 mit gepatchter Suchschleife, **CRC-Feld nicht nachgeführt** (trägt die CRC von V0.9_2) → von Hand geändert |

Zur Einordnung der übrigen Abzüge in `doc/EPROMS/`: `Prozessoreinheit_ZRE_K2526_A26.bin`,
`A5120_ZRE_rom.bin`, `k2526rom.bin` unterscheiden sich von V0.9_2 an 1003/1002/1006 Stellen — das
sind die bekannten **fehlerhaften Abzüge** (siehe `doc/merkposten`/Speicher „A5120_ZRE_rom.bin
ist ein KORRUPTER Dump“), keine weitere Fassung; hier nicht ausgewertet.

Anzahl abweichender Bytes je Paar:

| | V0.9_1 | V0.9_2 | V0.9a |
|---|---|---|---|
| **V0.6** | 760 | 761 | 761 |
| **V0.9_1** | | 28 | 33 |
| **V0.9_2** | | | 20 |

V0.6 ↔ V0.9 ist **ein anderes Programm** (gleicher Anfang 0000–00B9, gleiche Speicherprüfung,
gleiche Kassettenfolge, aber anderer Plattenzugriff); die V0.9-Fassungen sind **Feinänderungen
einer Codebasis**.

## 2. V0.9_1 ↔ V0.9_2 ↔ V0.9a — vollständig

Beide Zeilen in Byteadressen, links V0.9_1.  Alles andere ist byteidentisch.

### V0.9_1 → V0.9_2 (`zre.rom`), 28 Bytes in vier Gruppen

| Adresse | V0.9_1 | V0.9_2 | Wirkung |
|---|---|---|---|
| 0109–0120 | `LD (03F0H),HL … JR NC,0128 … DEC D / JR Z … LD A,09H / OUT (10H),A / LD A,89H / OUT (10H),A / LD B,08H` | davor **`45` = `LD B,L`** eingeschoben (B=0, folgenlos: B wird gleich überschrieben), `JR NC` 14H→13H, `JR Z` 29H→28H, **`LD B,A`** (B=09H) statt `LD B,08H` | **Zeitschleife zwischen den Schrittimpulsen der Spur-0-Suche 8→9 Durchläufe der äußeren Schleife** (≈ +12 % je Schritt: 8·256·16 T gegen 9·256·16 T, ≈ 13 ms → 15 ms bei 2,45 MHz — Taktangabe aus dem Kopf von `zre_rom.asm`, ungeprüft) |
| 0127 | `18 E7` (`JR 010FH`) | `18 E8` (`JR 0110H`) | Folge der Verschiebung um 1 Byte |
| 0198 | `36 05` | `36 10` | **Zeitüberschreitung des Leseversuchs (Indexzähler `[03F7]`, vom Index-ISR 01C7 heruntergezählt): 5 → 16 Indexpulse** (≈ 1 s → ≈ 3,2 s bei 300/min) |
| 03FE–03FF | 3F AE | F7 6B | CRC |

Dazu der Code von 0109–0126 als Disassemblat:

```
V0.9_1                                  V0.9_2 (zre.rom)
0106 LD HL,0400H                        0106 LD HL,0400H
0109 LD (03F0H),HL                      0109 LD B,L
010C LD DE,5006H                        010A LD (03F0H),HL
010F IN A,(12H)  ; Bit7 = /TRACK00      010D LD DE,5006H
0111 RLCA                               0110 IN A,(12H)
0112 JR NC,0128H                        0112 RLCA
0114 DEC D                              0113 JR NC,0128H
0115 JR Z,0140H                         0115 DEC D
0117 LD A,09H                           0116 JR Z,0140H
0119 OUT (10H),A  ; Schritt nach außen  0118 LD A,09H
011B LD A,89H                           011A LD B,A
011D OUT (10H),A                        011B OUT (10H),A
011F LD B,08H                           011D LD A,89H
0121 DEC C / JR NZ / DJNZ               011F OUT (10H),A
0126 JR 010FH                           0121 DEC C / JR NZ / DJNZ   ; B = 9
                                        0126 JR 0110H
```

### V0.9a (K8924) gegen V0.9_2 — 20 Bytes, nur 0113–0127

Die Suchschleife ist **umgebaut** (Rest identisch inkl. 0197 = 10H und CRC-Feld):

```
V0.9_2                          V0.9a (K8924)
0110 IN A,(12H)                 0110 IN A,(12H)
0112 RLCA                       0112 RLCA              ; C = Bit7 = /TRACK00 (1 = nicht auf Spur 0)
0113 JR NC,0128H   ; fertig     0113 LD A,09H
0115 DEC D                      0115 LD B,A            ; Flags bleiben
0116 JR Z,0140H                 0116 JR NC,011EH       ; auf Spur 0: nicht schrittschalten
0118 LD A,09H                   0118 OUT (10H),A       ; Schritt nach außen
011A LD B,A                     011A LD A,89H
011B OUT (10H),A                011C OUT (10H),A
011D LD A,89H                   011E CALL 03B6H        ; Zeitschleife (B=9) — IMMER
011F OUT (10H),A                0121 DEC D
0121 DEC C/JR NZ/DJNZ           0122 JR NZ,0110H       ; IMMER alle 80 Durchläufe
0126 JR 0110H                   0124 RLCA              ; A = 09H (zuletzt NC) / 89H (zuletzt Schritt)
                                0125 JR C,0140H        ; zuletzt noch geschaltet → Fehler
                                0127 NOP
```

Wirkung: **kein vorzeitiger Ausstieg bei Spur 0** — es werden immer alle 80 Durchläufe
(80 × ≈ 15 ms ≈ 1,2 s) abgewartet, und erfolgreich ist nur, wenn der *letzte* Durchlauf Spur 0
gemeldet hat.  Das fängt ein Laufwerk ab, dessen Spur-0-Schalter prellt oder dessen Kopf zum
Ausrücken länger als ein Schritt braucht (Vermutung; Herkunft/Anlass des Eingriffs unbekannt).
Die Zeitschleife ist hier der gemeinsame Unterprogramm-Rumpf 03B6H (derselbe, den der
Kassettenteil benutzt).

## 3. V0.6 ↔ V0.9 (V0.9_1) — Blocktabelle

Byteweise Abweichungen: 760.  Unverändert sind 0000–00B9 (Löschen von 2 KB, PIO-Init,
Speicherprüfung `0032–0079`, ISR 007A, Tabellenkopie) und der Block 00BB–00D6.  Hinter 00EF
verschiebt sich der Code (V0.6 ist an den Stellen bis 0267 kürzer/länger), weshalb die
Bytevergleiche dort „alles anders“ zeigen; nach **Befehlen** verglichen (Adressoperanden
maskiert) bleiben die folgenden Blöcke.  Adressen: V0.6 / V0.9_1.

| # | V0.6 | V0.9 | Unterschied |
|---|---|---|---|
| 1 | 00BA = `DD` | 00BA = `C7` | **Vektor 0BAH** (Index-Interrupt der K5122): V0.6 → ISR 01DD, V0.9 → ISR 01C7 (nur Folge der Adressverschiebung) |
| 2 | 00D7 `LD A,F7H; OUT (13H)` | `LD A,F3H; OUT (13H)` | Richtungsmaske Steuer-PIO Tor B: V0.6 treibt nur Bit 3 (PRE), **V0.9 zusätzlich Bit 2 (/HF)**; entsprechend schreibt V0.9 danach `7FH` (/HF=1 = FM/5¼″, PRE=1) nach 12H, V0.6 schreibt `F7H` (Reihenfolge der OUTs 12H/11H vertauscht, `7FH` nach 11H wie bei V0.9) |
| 3 | 00E1 `LD A,D9H; OUT (10H)` | `A9H` | Ruhewert Tor A: V0.6 **Kopf entladen** (Bit 6 = /HL = 1), V0.9 **Kopf geladen** (Bit 6 = 0) und Richtung „innen“ (Bit 5 = 1) |
| 4 | 00EF–00FD: `(03FA) := (0462)`; `(0000) := JP 01F3H` | `(07F2) := 04`; `(0000) := JP 01DDH` | V0.6 rettet das Ende des RAM-Walks nach `[03FA]` (Rückgabe bei 01A1); V0.9 legt die **Sektorzahl `[07F2]` = 4** ab.  ZVE2-Einsprung V0.6 01F3 / V0.9 01DD |
| 5 | 0107 `LD D,50H` (kein Ladezeiger) | 0106–010C `LD HL,0400H; LD (03F0H),HL; LD DE,5006H` | V0.9 merkt die Ladeadresse `[03F0]`=0400H und den Wiederholzähler **E=6** (FM/MFM-Wechsel) |
| 6 | 0111/0115: Schrittbytes `49H/C9H` | `09H/89H` | Spur-0-Suche: **Bit 6 (/HL)** gesetzt (Kopf entladen) bzw. gelöscht (geladen); Richtung (Bit 5) beide „außen“ |
| 7 | **0122–012E: `LD D,05H` und ein weiterer Schritt `A9H–29H–A9H`** (Bit 5 = 1 = innen, ein Impuls) | **fehlt** | **V0.6 fährt nach Spur 0 einen Schritt nach innen → Spur 1**; V0.9 bleibt auf Spur 0 |
| 8 | 013F–0161: wartet auf Indexzähler (`[03F7]`=6): erst `==5`, dann `==1` (= 5 Indexpulse, ohne Zeitgrenze im zweiten Teil), danach **`IN A,(12H); RRA` → Bit 0: CY → `87H` (FM) sonst `85H` (MFM)** nach `[03FD]`; `LD BC,0001H` | V0.9: `[03F7]`=4, wartet bis 0 (4 Pulse), **setzt `[03FD]`=87H (FM) vor**, bei Fehlversuch **abwechselnd `XOR 02H`** (FM/MFM-Probieren, E=6 Versuche) | **FM/MFM-Wahl: V0.6 liest ein Statusbit, V0.9 probiert** (siehe §5 zur Bedeutung von Bit 0) |
| 9 | 0164–0191: Parametertafel `[03F3]=0001H` (**Zylinder 1**, Kopf 0), `[03F5]=0001H` (Sektor 1, Größencode 0); Fertigmeldung in **`[03F6]`** (0 = läuft, 1 = Zeitüberschreitung, BDH = fertig); `[03F7]`=5 | 0156–0165: `[03F3]=0` (**Zylinder 0**), `[03F5]=0001H`; Fertigmeldung in **`[03F8]`** (0/1/**3**); Unterprogramm 0194 (`[03F7]`=5) | **V0.6 liest Zylinder 1, V0.9 Zylinder 0.** Meldezelle und Fertigwert verschieden |
| 10 | 01BE–01C8: Wiederholung mit `D` (5×); bei `[03F8]≠FF` neuer Leseversuch ohne neues Suchen | 014B–014E: `DEC E` (6×), `XOR 02H` auf `[03FD]` | Wiederholungsstrategie |
| 11 | 01CB: Signaturprüfung als Einzelvergleich `S`,`Y`,`L` an 0400–0402 | 01B6: `LD HL,(0400H)`/`SBC HL,5953H` + `CP 4CH` | **gleiche Prüfung** (SYL), V0.9 kürzer |
| 12 | 01DD ISR: bei Zähler 0 `LD A,FDH; OUT (10H),A` (**Kopf entladen**), Meldung `[03F6]`=1 | 01C7 ISR: `LD A,BDH` (**Kopf bleibt geladen**), Meldung `[03F8]`=1 | wie #3 |
| 13 | **01F3–026E ZVE2-Leser (V0.6)**: Index/Sync per **Software**: `IN (16H)`, A1 überspringen, **muss `FEH` sein** (sonst Neustart), dann Vergleich Zylinder/Kopf/Sektor (`CP (HL)`, 3 Bytes, **Größenbyte nicht geprüft**), 12 Bytes (FM) bzw. 23 Bytes (MFM) Lücke, Datenmarke `FBH` nach A1-Überspringen, **`INIR` 128 B**, Sektorzähler `[03F5]`; **liest die Sektoren 1–6** (`CP 07H`) = **768 B** nach 0400H–06FFH | **01DD–027B ZVE2-Leser (V0.9)**: hängt am **Markenerkenner der K5122** (`OUT (10H),B5H` + `[03FD]`-Bit 1 = MK-Steuerung), vergleicht feste Folgeposition `FE, Zyl, Kopf, Sektor, Größe` (Register BC/DE/HL/EXX), **Größencode ≠ 0 ⇒ Umschaltung auf 256-B-Sektoren, 2 Sektoren** (`[07F1]`=0 = 256 B, `[07F2]`=2), liest sonst **Sektor 1–`[07F2]`=4 mit je 128 B** = 512 B (3×`INI` + `INIR` + 2×`INI` CRC) | der **eigentliche Funktionsunterschied**: V0.9 lädt 512 B (4×128 oder 2×256) von Zylinder 0, V0.6 768 B (6×128) von Zylinder 1 |
| 14 | Einsprung des geladenen Codes: Platte `CALL 042EH` (019E), Kassette `CALL 0434H` (036C) | Platte **`CALL 0437H`**, Kassette `CALL 0434H` | **Platteneinsprung 042EH (V0.6) ↔ 0437H (V0.9)** — das geladene Stück muss zum ROM passen |
| 15 | 0267–0398 Kassettenteil | 027E–03B4 | funktional gleich (Adressen +17H…+21H) mit drei Abweichungen: (a) Wartezeit nach Laufwerks-/Kassettenwahl `LD B,97H; CALL 0395` (V0.6) gegen **2 × `LD B,00H; CALL 03B6`** (V0.9, ≈ 3,4-mal länger); (b) im Zweig „Bit 7 gesetzt“ löscht V0.9 vor `OR 30H` zusätzlich Bit 5 und wartet (`AND DFH; OUT (34H),A; CALL 03B6` bei 0317–031D) — V0.6 nicht; (c) V0.9 rückt den Fehlersprung `JR 1DH`→`JR 24H` entsprechend nach |
| 16 | Interrupt-Routinen 039B/03A6/03B5 (`[03F0]`/`[03F4]`/`[03F2]` auf 0394 gerichtet) | 03BC/03C7/03D4 | gleiche Wirkung; V0.6 hat die Epilog-Befehle doppelt, V0.9 einen gemeinsamen Ausgang; Rücksprungziel bei Zeitüberschreitung `0372H`↔`0393H` |
| 17 | 03D9–03FD = 00H (ungenutzt), 03F0–03FD V0.6 nicht belegt | `03F1`=04H, `03F2`=FEH, `03FB`=08H, sonst 0 | die drei Bytes in V0.9 sind **keine Reste**: das Lade-ROM wird mit `LDIR 00BA…03FF` ins RAM kopiert, danach stehen sie als **Vorbelegung** in `[03F0]`=0400H (Ladeadresse) und `[03FA]`=0800H.  **`[03FA]`=0800H wird auf dem Plattenweg beim Rücksprung bei 0177 gelesen (`LD HL,(03FAH)`; `LD (0462H),HL`; auf dem Plattenweg schreibt V0.9 `[03FA]` sonst nirgends)** — V0.6 setzt `[03FA]` dort selbst (Punkt 4).  Daß 0800H der Zweck ist, ist Schluss aus dem Wert (= Start des RAM-Walks), nicht belegt |
| 18 | 03FE–03FF = 07 F1 | 3F AE | CRC (§1) |

### Zusammenhang mit der SYL-Diskette (Beleg für #7, #9, #14)

`SYL` an 0400H–0402H ist bei **beiden** Fassungen die Gültigkeitsprüfung.  Das geladene Stück
(`tools/bootsec/src/bootsec_syl.mac`, CP/A) liegt bei V0.9 in **Spur 0, Sektor 1** mit den Einstiegen

```
0037H (= 0437H geladen)   FDC_INIT: LD A,29H; OUT (10H),A  ; selbst einen Schritt nach innen
002EH                     JP 0140H   (Kaltstart)
```

Die BCU-„SYL auf Spur 1“-Disketten tragen die Kennung dagegen auf **Zylinder 1** (Abbild
`satz1_SYL_auf_Spur1/disk.img`, Offset 0D00H = Zylinder 1, Sektor 1; Zylinder 0 ist dort mit
`40H`/`00H` gefüllt) und haben bei Offset +2EH `FD 21 07 04` (`LD IY,0407H`) — den
V0.6-Einsprung **042EH**; 0434H = `LD BC,FFFFH`, 0437H = `LD A,65H` liegen dahinter.

## 4. Gleichbleibendes

Gleich in allen vier: Löschen von 2 KB, BS-PIO-Init, Speicherprüfung (62 × 1 KB), ROM→RAM-Kopie
(0346H Bytes), Laufwerkswahl-Rotation `EEH→DDH→BBH→77H` über `[03FC]` (`RLCA`), Wechsel zum
Kassettenweg bei `77H` (K5020, Ports 30H–37H), `SYL`-Prüfung.  Der Kassettenteil ist in allen
vier Fassungen vorhanden und bis auf §3 #15 gleich.

## 5. Offene Punkte und Unsicherheiten

1. **Bedeutung von Port 12H Bit 0 bei V0.6.**  V0.6 wählt FM (`87H`) bei Bit 0 = 1, sonst MFM
   (`85H`).  Nach Entwurf 07 §5 ist Bit 0 `/RDYL` (0 = bereit); unser `K5122::updateStatusPortB`
   liefert 0, sobald eine Diskette auf Drehzahl ist → V0.6 würde im Emulator **MFM** wählen.
   Ob das am Gerät die gemeinte Bedeutung ist (z. B. Laufwerkstyp-Brücke) ist **nicht belegt**.
2. *(AP A7, `bcu.md` §5: „EPROM BCU 880 VERSION 2.2“ ist kein Start-EPROM, sondern die Kennung von
   `EPROM.COM`; V0.6 startet die „SYL auf Spur 1“-Disketten im Emulator tatsächlich — §5.3 ist damit
   gefahren.)*  **BCU „ohne SYL“** (`satz2_OhneSYL.1–4`, teils „Bootfähig: Ja“): ihr erster Sektor beginnt
   `40 20 'BCU 880 …'`, enthält **keine** `SYL`-Kennung.  **Keines der vier ROMs** nimmt sie an
   (V0.9 liest Zylinder 0 und prüft `SYL`, V0.6 liest Zylinder 1 und prüft `SYL`).  Sie müssen
   von einem anderen Start-EPROM geladen werden — Entwurf 30 §3.2 nennt „EPROM BCU 880 VERSION
   2.2“ aus dem Archiv; ein Abzug davon liegt nicht vor.
3. **Nur statisch geprüft:** V0.6 war im Emulator nie in Betrieb.  Die Zuordnung „SYL auf
   Spur 1 ⇔ V0.6“ ist durch die Bytes belegt (Zylinder 1, 6 Sektoren, 042EH, `SYL` an der
   erwarteten Stelle) — gefahren ist es nicht.
4. Die Zuordnung der Abzüge zu **MAME `a5120_v1`/`a5120_v2`** und die Versionsnamen V0.9_1/_2 stammen aus
   der AP-Karte; MAME-Quellen lagen hier nicht vor.
5. **Wirkung auf das V0.9-Ladeverfahren des Emulators:** unverändert; der Kern nutzt `zre.rom`.
   Die 16-Impuls-Zeitgrenze (V0.9_2) gilt im Emulator seit je (Boot-Invarianten 1–8 in
   `doc/merkposten/boot_debugging.md` beziehen sich auf sie).

## 6. Empfehlung je Modell

| Modell | Lade-ROM | Begründung |
|---|---|---|
| **A5120 (CP/A, UDOS, SCPX; Vorgabe)** | **V0.9_2 (`zre.rom`)** — unverändert | läuft; getestet (alle Boot-Wächter) |
| frühes A5120 „5¼″ alt“ mit CP/A-Diskette (K7023/K7636/K5600.10, A6) | **V0.9_2** | CP/A bootet von Zylinder 0 (SYL-Spur 0); die Laufwerksunterschiede liegen im BIOS, nicht im ROM |
| **BCU 880, „SYL auf Spur 1“** (`satz1_`, `satz2_SYL_auf_Spur1`) | **V0.6** (`laderom_v06.bin`) | einziges ROM mit Zylinder 1 und Einsprung 042EH |
| BCU 880 „ohne SYL“ | **keines der vier** | siehe §5.2 — offen, erst klären (A7) |
| K8924 (andere Maschine, wäre `K8924`) | V0.9a nur, wenn ein langsamer Spur-0-Schalter nachzubilden ist | handgepatcht, CRC nicht nachgeführt; **nicht** als Vorgabe empfohlen |
| Kassettenboot (K5020, A8) | V0.9_2 | Kassettenteil gleich in allen Fassungen; Einsprung 0434H |

Folgerung für AP A5/A7: das Lade-ROM wird eine **Konfigurationsgröße der Maschine** (Modellwahl
„frühe A5120 / BCU“ → V0.6, sonst V0.9_2); der Kern braucht dafür eine Möglichkeit, ein anderes
1-KB-Abbild zu laden (`tools/eprom_to_h.py` erzeugt `*_data.h`; vorhandene `zre.rom`-Einbettung
bleibt Vorgabe).  V0.9_1 hat keine Aufgabe, die V0.9_2 nicht erfüllt — sie unterscheidet sich nur
im Zeitverhalten (kürzere Zeitgrenze, schnellere Schritte).

## 7. Nachvollziehen

```
python3 -I tools/z80_disasm2.py doc/EPROMS/K2526/laderom_v06.bin --org 0 --entry 0 --linear --no-strings
python3 -I tools/z80_disasm2.py doc/EPROMS/zre.rom                --org 0 --entry 0 --linear --no-strings
```

Byteweiser Vergleich: `[i for i in range(1024) if a[i] != b[i]]` über die vier Dateien; CRC:
CRC-16/CCITT-FALSE über `data[:0x3FE]`, big-endian gegen `data[0x3FE:]`.
Wichtige Adressen V0.6: Seeklauf 0109, Einzelschritt 0122–012E, Leseversuch 0164, Signatur 01CB,
Index-ISR 01DD, ZVE2-Leser 01F3, Kassette 0267, Zeitschleife 0395; V0.9: 0110, —, 0156/0194,
01B6, 01C7, 01DD, 027E, 03B6.
