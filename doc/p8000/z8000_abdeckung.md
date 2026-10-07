# U8001/Z8001-Kern — Abdeckungsliste (AP P8)

Stand 2026-10-07, Branch `P8000`. Kern `core/primitives/z8000.{h,cpp}`, Tabelle
`core/primitives/z8000/z8k_table.h`, Befunde/Festlegungen `core/primitives/z8000/README.md`.
Massstab: Zilog *Z8000 CPU User's Reference Manual* (OCR
`~/projects/A5120.16/docs/508523565-Zilog-Z8000-Reference-Manual.txt`, Kapitelangaben §).
Zweck (Plan 25 §10.11a): jede Handbuchaussage → wo umgesetzt → welcher Test → welche Annahme.

Testorte (Kurzform):

| Kürzel | Datei | Lauf |
|---|---|---|
| **U** | `tests/unit/primitives/test_z8000.cpp` (98 Fälle, Prüfstand `z8k_rig.h`) | `tools/dev.sh test` |
| **O** | `tests/oracle/test_z8000_mame.cpp` gegen MAME z8000 (Stand 741d827a) | `tools/dev.sh test-oracle` |
| **T** | `tests/debugtools/test_z8k_{table,asm,disasm}.cpp` | `tools/dev.sh test` |
| **D** | `tests/debugtools/test_dbg_u8000.cpp`, `tests/cli/cases/dbg_u8000_*.cli` | `tools/dev.sh test` |
| **E** | A5120.16-Bestand: `test_em16*`, `em16_maschine`, `cli_em256_*` (unverändert grün) | `tools/dev.sh test` |

## 1. Befehlssatz (Kap. 6)

| Handbuch | Umsetzung | Test |
|---|---|---|
| Alle Kodierungen der Opcode-Map, Z8001 seg. (SA kurz/LA lang) und nichtseg., Z8002 | eine Tabelle, 438 Zeilen / 195 Mnemoniks | T `TableIsUnambiguous`, `EveryFirstWordDecodesOrIsUnknown`, `OpcodeMapSamples` |
| Semantik jeder Zeile (Register, Speicher, E/A, Flags, PC) | `Z8000::execute` | O `JederBefehlWieMame` (309 264 Zufallsfälle, 4 Modi), U je Gruppe |
| **Matrix Befehl × Adressierungsart × Flagzustand** | — | O `JedeZeileJedeFlagbelegung`: jede Zeile × alle 64 Belegungen C Z S V D H × {Z8001 seg Sys/Norm, Z8002 Sys/Norm} = 98 993 Fälle |
| Flag-Wirkungen „undefiniert" | bleiben unverändert (README) | O blendet sie aus (`masksFor`) |
| Arithmetik/Logik, DAB (Tabelle) | `add/sub`, DAB nach Handbuchtabelle | U `Z8000Arith.*` (`DabTabelleVollstaendig`), O (DAB: bekannte MAME-Abweichung) |
| MULT/MULTL/DIV/DIVL inkl. aller vier DIV-Fälle, /0, Überlauf, Takte | `Op::Mult/Div` | U `Mult`, `MultlUndTakte`, `DivAlleVierFaelle`, `Divl`; O |
| Schieben/Rotieren statisch/dynamisch, RLDB/RRDB | `shiftOp` | U `Z8000Shift.*`; O |
| Laden/Stapel/LDM/EX/LDA/LDAR/LDR/LDPS/LDCTL/LDCTLB | | U `Z8000Load.*`; O (LDCTL REFRESH nur U) |
| Sprünge/CALL/CALR/RET/DJNZ/DBJNZ/SC, Takte genommen/nicht | | U `Z8000Jump.*`, `Z8000Cycles.Stichproben`; O |
| Block-/Stringbefehle (LD/CP/CPS/TR/TRT/IN/OUT/SIN/SOUT, I/D, R) | `blockStep`, ein Durchlauf je Schritt | U `Z8000Block.*`; O (ein Durchlauf) |
| Unterbrechbarkeit der Wiederholungsbefehle (§7.6.2 †) | PC bleibt am Befehl, +7 Takte | U `LdirIstUnterbrechbarUndLaeuftNachIretWeiter`, `Z8000Segt.UnterbrichtWiederholungsbefehlZwischenDurchlaeufen` |
| Privilegierte Befehle im Normalmodus → Trap | Erkennung am ersten Wort | U `PrivilegierterBefehlImNormalmodus`, T `PrivilegedExactlyTheSystemInstructions` |
| MSET/MRES/MBIT/MREQ (µI/µ0) | `Op::Mset…Mreq` (Ablauf §6 MREQ) | U `MultiMicroMsetMresMbit`, `MreqGewaehrtUndAbgelehnt` (O: Pins, nicht modelliert) |
| HALT, DI/EI, SETFLG/RESFLG/COMFLG, TCC, NOP | | U, O |
| EPA-Schablonen (0E/0F/4E/4F/8E/8F), EPA = 0 → Trap | Extended-Instruction-Trap, Kennung = 1. Wort, gekellert = 2. Wort | U `EpaBefehlOhneEpaBitTrapt` |
| EPA = 1 ohne EPU | NOP der vollen Länge | U `EpaBefehlMitEpaBitIstNopDerRichtigenLaenge` — **Annahme A1** |
| Unbelegte Kodierungen | NOP der gelesenen Länge, `onIllegal` | U `UnbelegteKodierungMeldetUndLaeuftWeiter` |

## 2. Modi und Register (Kap. 2, 4)

| Handbuch | Test |
|---|---|
| Tabelle 4.1 (R14/R15-Bänke Sys/Norm × seg/nichtseg) | U `Tabelle41StapelzeigerBaenke` |
| Z8001 nichtsegmentiert: Segment = PC-Segment (§4.3.2), Zeiger/LDA 16 Bit, CALL kellert ein Wort | U `UnsegmentiertBleibtImPcSegment`, `Z8000Unseg.*` (MAME: „TODO", daher nur U) |
| Segmentierter PC-Offset läuft im Segment über (§5.5) | U `PcUeberlaeuftImSegment` (MAME trägt — Festlegung README) |
| Ungerade Registerpaare | U `UngeradesRegisterpaarAlsZeigerIstDasGeradePaar` (Annahme README §8.2) |

## 3. Ausnahmen (Kap. 7)

| Handbuch | Umsetzung | Test |
|---|---|---|
| Reset §7.4: FCW 0002, PC-Seg 0004, Offset 0006 (Z8002: 0004), Systemprogrammraum, Segment 0; RESET-Leitung hält | `doReset` | U `Z8000Reset.*` |
| Extended Instruction / Privileged / System Call Trap (§7.3.1–3), Kennung = 1. Wort, PC laut Tabelle §7.6.2 | `takeException` | U `Z8000Trap.*`; O (SC/PRIV über `JederBefehlWieMame`) |
| **Segmenttrap §7.3.4** (nur Z8001, unabhängig vom SEG-Bit) | `setSEGT`, `checkInterrupts` | U `Z8000Segt.RahmenQuittung0100PsaUndIret`, `AusNichtsegmentiertemNormalmodusUndZurueck`, `Z8002HatKeinenSegtEingang`; O `AusnahmenMitQuittungWieMame` |
| SEGT am Befehlsende abgetastet (§9.6.1), Befehl läuft zu Ende, gekellert = nächster Befehl | | U `Z8000Segt.WirdErstAmBefehlsendeAngenommen` |
| SEGT während Wiederholungsbefehl: gekellert = der Befehl; im letzten Durchlauf: der nächste | | U `UnterbrichtWiederholungsbefehlZwischenDurchlaeufen`, `ImLetztenDurchlaufIstDerGesicherteDerNaechsteBefehl` |
| SEGT weckt aus HALT | | U `Z8000Segt.WecktAusHalt` |
| NMI flankengetriggert; VI/NVI Pegel, maskiert über VIE/NVIE (§7.5) | | U `Z8000Irq.*` |
| Vektor-Interrupt: PSA + %3C + 2·Kennung (Z8001), PSA + %1E + 2·Kennung (Z8002) | | U `VektorInterruptRahmenQuittungUndIret`, `Z8002VektortabelleUndRahmen`; O |
| Quittungszyklus §7.6.1/§9.4.5: erst Scheinholen (alter Modus), dann System/seg., Status 0100/0101/0110/0111, Wort lesen | `takeException(external)` | U `RahmenQuittung0100PsaUndIret` (Busfolge), `AusNichtsegmentiertemNormalmodusUndZurueck` (N/S je Zyklus) |
| Rahmen Bild 7-1 (PC-Offset, PC-Segment, FCW, Kennung) auf dem Systemstapel, PSA aus dem Programmspeicher | | U, O `AusnahmenMitQuittungWieMame` |
| **Rangfolge §7.7**: Reset > interner Trap > NMI > SEGT > VI > NVI; je Ausnahme neuer Status, die nächste kellert den Status der vorigen Behandlung | `checkInterrupts` | U `Z8000Rangfolge.NmiVorSegtVorViVorNvi`, `InternerTrapVorNmi`, `PrivilegTrapUndSegtAusDemselbenBefehl` |
| IRET §7.6.5 (Kennung verwerfen, FCW, PC) | | U, O |

## 4. Bus, Status, Pins (Kap. 2.3, 8, 9)

| Handbuch | Umsetzung | Test |
|---|---|---|
| Status ST3..0 je Zyklus (Tabelle 2.1): 0001 Refresh, 0010/0011 E/A, 0100–0111 Quittungen, 1000/1001 Daten/Stapel, 1100 Programmraum (weitere Worte, LDR, PSA, Resetvektor), 1101 erstes Wort | `bus()` | U `JederStatuscodeDenDerKernAusgibt`, `StatusJeZugriffSegmentiert` |
| SN0..6 je Zyklus (seg.: Adresssegment; nichtseg.: PC-Segment; E/A: 0), N/S, B/W, R/W | `Z8kBusCycle` | U `SegmentnummerUndNsJeZyklus`, `ByteHaelftenSpeicherUndEa`, `WortzugriffeSindGerade` |
| Befehlsgrenze für die MMU = Zyklus mit Status 1101 (auch Scheinholen) | Statusfolge | U Busfolge; MMU-Seite `doc/p8000/z8010_mmu.md` [A1]/[A2] |
| Refresh periodisch (RATE) und im Stop | `finish`, `refreshCycle` | U `RefreshZyklen`, `StopNachDemErstenWortUndRefresh` |
| STOP, BUSREQ/BUSACK, WAIT (`addWaitCycles`) | | U `Z8000Pins.*`; E |
| Byte-E/A Hälften, `ioByteOnBothHalves` | | U `EaByteLageNachA0`, `IoByteNurEineHaelfteKonfigurierbar` |
| Takte (Anhang C) | Tabelle | T `CyclesFromManual`, U `Z8000Cycles.Stichproben` |
| Ablaufzustand für Save-State | `runState` | U `AblaufzustandMittenImLdirUebertragbar` |

## 5. Debugger und Werkzeuge

| Anforderung | Umsetzung | Test |
|---|---|---|
| Disassembler/Assembler über den ganzen Befehlssatz | `tools/z8000/` | T `Z8kRoundTrip.EveryRowEncodeDisasmAssemble` (jede Zeile) und **`JedesErsteWortMitZufallsfolgeworten`** (alle 65 536 ersten Worte × 3, beide Modi, je 169 710 Befehle: Zeile, Operanden, Text gleich) |
| Pins/Merker, letzter Buszyklus, letzte Ausnahme in `r`/`rj` | `dbg16::pinsText/cycleText/exceptionText` | D `DbgU8000P8.*`, `cli_dbg_u8000_p8` |
| Ausnahmeprotokoll `trap`, Halt `btrap`, Statuszählung `status` | `dbg16::ExcLog16`, `statusCountLines` | D `cli_dbg_u8000_p8`, `cli_dbg_u8000_em16abl` (SC + VI der echten Firmware) |

## 6. Annahmen und offene Punkte

- **A1 EPU-Transfers nicht nachgebildet.** Die Bitbilder der sieben EPA-Schablonen (§6.8) fehlen
  im OCR (MAME dekodiert sie ebenfalls nicht). Mit EPA = 1 läuft ein EPA-Befehl als NOP der
  vollen Länge, ohne Zyklen mit Status 1010/1011/1110. Der P8000 hat keine EPU; nachzuholen,
  sobald eine lesbare Fassung der Schablonen vorliegt (dann additiv hinter einer Config).
- **A2 Interne Operation (Status 0000) wird nicht als Buszyklus ausgegeben.** Das Handbuch
  (§9.4.6) nennt sie nur „to maintain a minimum transaction rate" ohne Lage je Befehl; keine
  Karte wertet sie aus (MMU: ohne Wirkung).
- **A3 STOP nach dem zweiten Wort eines EPA-Befehls** (§2.3.5) nicht nachgebildet (nur nach dem
  ersten Wort) — folgt aus A1.
- **A4 Takte des Interrupt-/Trapeintritts** geschätzt (README); MAME vergleicht keine Takte.
- **A5 Vektortabelle über %FFFF**: Offset läuft im Segment über (wie der PC); MAME trägt ins
  Segment — im Orakel ausgenommen.
- **A6 SEGT-Pegel nicht in `Z8kRunState`** (Aufbau trägt den A5120-Save-State v7): die
  P8000-Karte (P10) stellt ihn aus dem MMU-Zustand wieder her.
- „SUP/Suppress" ist keine CPU-Funktion (der Z8001 hat keinen SUP-Eingang); Unterdrücken von
  Schreibzugriffen macht die Karte (P9b), Befehlsabbruch kann erst der Z8003.
