# Z8-Kern (UB 8840 M / Z8) — Abdeckungsliste (AP P19c)

Stand 2026-10-08.  Primitive `core/primitives/z8.{h,cpp}` (Bibliothek `k1520_z8`), Befehlstabelle
`core/primitives/z8/z8_table.h` (eine Quelle für Kern, Disassembler, Assembler), Disassembler/
Assembler `tools/z8/z8_disasm.h`, `tools/z8/z8_asm.h`, Debuggerkontext `tools/dbg_z8.h`
(`k1520dbg --z8 <abzug>[@org] [--z8-fassung ub8840|ub8820|z8681]`).  Grundsatz §10.11a des Plans:
**vollständig** nach Datenblatt, nicht nur soweit das Terminal Typ 2 (`terminal_typ2.md`) oder die
K7673.09 (`tastatur_k7673.md`) es braucht.

**Quelle:** Zilog UM0016 „Z8 CPU User Manual" (UM001604-0108, zilog.com; nicht im Repo, Seiten
zitiert).  Ein Robotron-Datenblatt des UB 8840 M liegt nicht vor; UB 88xx gilt als befehls- und
registergleich zum Z8601/Z8611/Z8612 (Firmware-Kopf „U 882", Hardwarehandbuch „UB 8840 M").
Aus UM0016 bewusst **nicht** übernommen, weil Z86-Erweiterungen ohne Gegenstück am klassischen Z8:
Expanded Register File (RP.3–0 werden gespeichert, wirken aber nicht), Watchdog (WDT/WDH 5F/4F),
SMR/Stop-Mode-Recovery, Analogkomparatoren (P3M.1), Flankenwahl in IRQ.6/7, SPI.  HALT/STOP (7F/6F)
gibt es nur hinter `Z8Config::haltStop` (Vorgabe aus = unbelegt).

**Gegenprobe:** MAME `src/devices/cpu/z8/z8ops.hxx` + Opcodetabelle aus `z8.cpp` (Stand 741d827a,
Prüfsummen in `tests/oracle/CMakeLists.txt`; nur Vergleich, nichts übernommen) —
`tools/dev.sh test-oracle -R z8_mame`.

Tests: Binary `k1520_test_z8` (Suiten `Z8Tabelle`, `Z8Alu`, `Z8Einop`, `Z8Laden`, `Z8Stapel`,
`Z8Sprung`, `Z8Steuer`, `Z8Register`, `Z8Reset`, `Z8Irq`, `Z8Zaehler`, `Z8Uart`, `Z8Port`, `Z8Bus`,
`Z8Stand`, `Z8Sicht`, `Z8Firmware`), `k1520_test_z8_asm`, `k1520_test_dbg_z8`, Orakel
`k1520_test_z8_mame`.

## 1. Fassungen, Speicher, Bus

| Punkt (UM0016) | Umsetzung | Test | Annahme |
|---|---|---|---|
| Programmbus der Entwicklungsfassung (UB 8840 M ≙ Z8612: 0000–0FFF eigene Pins; 8820 ≙ Z8602: 0000–07FF; ROM-los Z8681) | `Z8Config::programmbus` → `programmLesen` | `Z8Reset.FassungenProgrammbusgrenze` | [Z8-M1] |
| Programm ≥ Grenze, LDE/LDEI, externer Stapel: Zyklus an P0/P1 mit /AS /DS R/W /DM (S. 131–136) | `busLesen`/`busSchreiben`, `Z8BusZyklus` (Pin-Adresse, Art, /DM, hochohmig, erweitert, Zeit) | `Z8Laden.LdcLdeLdciLdeiMitBuszyklen`, `Z8Bus.*` | — |
| 8/12/16-Bit-Adressierung: A8–A15 nur in Adressbetrieb (P01M), sonst Pegel von P0 (S. 132) | `pinAdresse()` | `Z8Laden.AdressnibblesVonPort0OhneAdressbetrieb` | — |
| LDC-Schreiben in den Programmbus | ohne Wirkung, gezählt | `Z8Laden.LdcLde…` | [Z8-M2] |
| /DM (P34) nur bei P3M.4–3 = 01/10; aktiv bei LDE/LDEI und externem Stapel (S. 134) | `Z8BusZyklus::dm` / `datenspeicher` | `Z8Laden.LdcLde…`, `Z8Stapel.ExternPushPopCallRetMitDatenspeicher` | — |
| P01M.4–3 = 11: AD, A8–A15, /AS, /DS, R/W hochohmig (Terminal-DMA) | kein Zyklus, Lesen FFH, sichtbar in `letzterZyklus()` | `Z8Bus.HochohmigerBusUnterdruecktZyklen` | [Z8-M3] |
| Erweitertes Timing P01M.5 (Tx, S. 137) | +1 interner Takt je externem Zyklus | `Z8Bus.HolenExternErweitertesTimingUndWartetakte` | [Z8-M4] |
| Wartetakte | `addWaitCycles()` aus dem Busrückruf | dto. | — |
| Scheinholen vor der Interruptannahme (Figure 102) | Zyklus `Scheinholen` am PC, nur extern | `Z8Irq.ScheinholenUndVektorAmExternenBus` | `Config::scheinholenBeiInterrupt` |
| Registerdatei 04–7F (124 Allzweckregister), 80–EF fehlen | Lesen FFH, Schreiben ohne Wirkung | `Z8Laden.NichtVorhandeneRegister…` | [Z8-M5] |
| Nur-Schreib-Register PRE0/1, P01M, P2M, P3M, IPR lesen FFH (S. 81) | `regLesen`; `regSicht` zeigt den Inhalt (Debugger) | `Z8Laden.NurSchreibbareRegisterLesenFF`, `Z8Sicht.*` | — |

## 2. Befehlssatz (Table 39, Figure 134)

| Punkt | Umsetzung | Test | Annahme |
|---|---|---|---|
| Tabelle: 231 belegte Opcodes, Längen, Takte nach der Opcode-Karte | `z8_table.h` (`decode()`) | `Z8Tabelle.*`, `Z8Steuer.TakteAllerOpcodesWieOpcodeKarte` (Karte unabhängig abgeschrieben, alle Opcodes ausgeführt) | — |
| ADD ADC SUB SBC OR AND TCM TM CP XOR × r,r / r,Ir / R,R / R,IR / R,IM / IR,IM mit C Z S V D H | `alu()` | `Z8Alu.MatrixAlleOperationenAlleArtenMitFlagsUndTakten` (10×6×14×14×5 Fälle gegen eine Referenz nach Datenblattdefinition: V als Bereichsüberschreitung, H als Übertrag/Borgen Bit 3) | — |
| Arbeitsregister (r, rr), Ex-Fluchtadresse in R-Feldern | `R()`, `arbeitsreg()` | `Z8Alu.ArbeitsregisterUeberEscapeEx` | — |
| DEC RLC INC COM RL CLR RRC SRA RR SWAP × R/IR, INC r | `einop()` | `Z8Einop.MatrixAlleWerteBeideArtenMitFlagsUndTakten` (alle 256 Werte), `Z8Einop.IncArbeitsregisterKurzform` | SWAP: C, V undefiniert ⇒ bleiben [Z8-F2] |
| INCW/DECW RR/IR (Z S V, 16 Bit) | | `Z8Einop.IncwDecwAlleArtenUndGrenzen` | — |
| DA (Tabelle S. 166) | | `Z8Einop.DaNachBcdAdditionUndSubtraktionAllePaare` (alle BCD-Paare × ADC/SBC × Übertrag), `Z8Einop.DaHandbuchBeispiel15plus27` | ausserhalb der Tabelle wie die Rechnung; V bleibt [Z8-F1] |
| LD: 13 Formen (rC, r8, r9, E3, F3, E4–E7, F5, C7, D7) | | `Z8Laden.AlleLdFormenWieHandbuchBeispiele` (Beispiele 1–12) | Indexbasis = Registeradresse, keine Ex-Abbildung [Z8-A3] |
| LDC/LDCI/LDE/LDEI beide Richtungen | | `Z8Laden.LdcLdeLdciLdeiMitBuszyklen` | — |
| CLR/LD ohne Flags | | `Z8Laden.ClrUndLdBeruehrenKeineFlags` | — |
| PUSH/POP R/IR, Stapel intern (SPL, SPH frei) und extern (SP 16 Bit, /DM) | `pushB/popB` | `Z8Stapel.InternPushPopAufSplSphUnberuehrt`, `Z8Stapel.ExternPushPopCallRetMitDatenspeicher` | PUSH extern +2 Takte (Karte 10/12, 12/14) |
| CALL DA/@RR, RET, IRET (FLAGS, PC, IMR.7) | | `Z8Stapel.ExternPushPopCallRet…` (Handbuchbeispiel CALL 3521H), `Z8Stapel.CallIndirektUndIret` | — |
| JP cc,DA / JP @RR / JR cc / DJNZ, 16 Bedingungen (Table 36), Takte 12/10 | `bedingung()` | `Z8Sprung.JrJpAlleBedingungenAlleFlagkombinationen` (16×16), `Z8Sprung.JrRueckwaertsUndDjnz` | — |
| SRP, DI, EI, RCF, SCF, CCF, NOP | | `Z8Steuer.FlagbefehleSrpDiEi` | RP.3–0 ohne Wirkung |
| Unbelegte Opcodes | wie NOP, 6 Takte, `onIllegal` | `Z8Steuer.UnbelegteOpcodesWieNopMitMeldung` | [Z8-B1] |
| HALT/STOP (nur Z86) | hinter `haltStop`; HALT endet mit Interrupt | `Z8Steuer.HaltStopNurMitZ86Option` | — |
| Ziel FLAGS bei flagsetzendem Befehl | geschriebenes Ergebnis gewinnt | `Z8Register.FlagsAlsZielDasErgebnisGewinnt` | [Z8-F3] |
| Ungerade Registerpaaradresse | n und n+1 | `Z8Register.RegisterpaarUngeradeNimmtNundNplus1` | [Z8-A2] |
| Inhalt eines Zeigerregisters ist volle Registeradresse (Ex dort kein Arbeitsregister) | | `Z8Register.IndirekterInhaltExIstKeinArbeitsregister` | [Z8-A1] |

## 3. Interrupts (S. 95–109)

| Punkt | Umsetzung | Test | Annahme |
|---|---|---|---|
| IRQ-Register nach Reset 00 und gesperrt (Lesen 0, Schreiben/Anforderungen wirkungslos) bis zum ersten EI; IMR.7 allein genügt nicht | `irqEin_` | `Z8Irq.IrqRegisterBisZumErstenEiGesperrt` | — |
| Quellen: P32→IRQ0, P33→IRQ1, P31→IRQ2, P30→IRQ3 (nicht im UART-Betrieb), fallende Flanke | `p3Flanke()` | `Z8Irq.FlankenP30bisP33NurFallend`, `Z8Uart.SeriellerBetriebUnterdruecktT0Irq4UndP30Irq3` | klassischer Z8: nur fallend (IRQ.6/7-Flankenwahl = Z86) |
| Software-Interrupt durch Schreiben auf IRQ | | `Z8Irq.AnnahmeStapelrahmenVektorTakte` | — |
| Annahme: IMR.7 := 0, Anforderung löschen, PCL, PCH, FLAGS kellern, Vektor 2n/2n+1, 24 Takte | `interruptAnnehmen()` | `Z8Irq.AnnahmeStapelrahmenVektorTakte` | Synchronisierung 2 Takte nicht nachgebildet [Z8-I1] |
| Maske, globales Freigabebit, abgefragter Betrieb | | `Z8Irq.MaskeUndGlobalesFreigabebit` | — |
| Rangfolge IPR (Gruppen A/B/C, 6 Reihenfolgen + 3 Gruppenbits) | `hoechsteAnforderung()` | `Z8Irq.RangfolgeAlleIprWerteAlleAnforderungsmengen` (64 IPR × 63 Mengen gegen Table 19/20) | 000/111 „reserviert": C>A>B bzw. B>A>C wie MAME [Z8-I2] |

## 4. Zähler T0/T1 (S. 80–94)

| Punkt | Umsetzung | Test | Annahme |
|---|---|---|---|
| Zählertakt = interner Takt/4, Vorteiler 1–64 (00 = 64), Zähler 1–256 (00 = 256), Zeit = 4·p·t | `zaehlerTakt()`, `syncTo()` | `Z8Zaehler.T0EinzeldurchlaufZeitIstVierMalPmalT` (p ∈ {1,3,7,64} × t ∈ {1,2,5,256}) | — |
| Erste Zählung 4 Takte nach dem Freigeben | `laufPruefen()` | dto. | — |
| Einzeldurchlauf: steht bei 00, Freigabebit gelöscht; Dauerbetrieb: Anfangswert neu | `endwert()` | `Z8Zaehler.T0Einzeldurchlauf…`, `Z8Zaehler.DauerbetriebPeriodeUndZaehlerstandLesen` | — |
| Zählerstand lesbar ohne Wirkung | | `Z8Zaehler.Dauerbetrieb…` | — |
| Anhalten/Weiterzählen ab Stand; Laden = Software-Retrigger; Ladebits lesen 0 | | `Z8Zaehler.AnhaltenUndWeiterzaehlenNeuLadenWaehrendDesLaufs` | — |
| T1: interner Takt (PRE1.1 = 1), TIN extern (Flanke = Vorteilertakt), Tor, Trigger, Retrigger; IRQ2 je TIN-Flanke | `t1Intern()`, `t1Flanke()` | `Z8Zaehler.T1InternUndExternerTaktUeberTin`, `Z8Zaehler.T1TorTriggerRetrigger` | — |
| TOUT an P36: T0/T1-Endwert kippt, Laden setzt 1, P3-Schreiben wirkt nicht | `p3Ausgang()` | `Z8Zaehler.ToutAnP36` | Betrieb „interner Takt" (11): Pin = 1 [Z8-T5] |
| Registerzugriffe zeitlich | Peripherie bis „Befehlsende" nachgezogen | alle Zählertests | [Z8-T1] |

## 5. UART (S. 115–122)

| Punkt | Umsetzung | Test | Annahme |
|---|---|---|---|
| P3M.6: P30 = SIN, P37 = SOUT, T0-Endwert ist ×16-Bittakt, kein IRQ4 von T0 | `uartTakt()` | `Z8Uart.SeriellerBetriebUnterdruecktT0Irq4UndP30Irq3` | — |
| Bitrate = XTAL / (2·4·p·t·16): 7,3728 MHz, p = 3, t = 2 ⇒ 9600 Bd (Table 24) | | `Z8Uart.*` (Bitzeit 384 interne Takte) | — |
| Senden: Start, 8 Daten LSB zuerst, 2 Stopp; ÷16 beim Schreiben neu; IRQ4 nach dem 2. Stoppbit | | `Z8Uart.SendenRahmenStartAchtDatenZweiStoppUndIrq4` | — |
| Ungerade Parität ersetzt Bit 7 (Senden), Bit 7 = Paritätsfehler (Empfang) | | `Z8Uart.SendenMitUngeraderParitaet`, `Z8Uart.EmpfangParitaetsfehlerInBit7` | — |
| Empfang: Startflanke, Prüfung in der Bitmitte, falsches Startbit verworfen, IRQ3 in der Stoppbitmitte | | `Z8Uart.EmpfangenZeitpunktPufferUndUeberschreiben`, `Z8Uart.FalschesStartbitWirdVerworfen` | keine Rahmenprüfung (Datenblatt) |
| Puffer: Schieberegister + Empfangspuffer (Doppelpuffer), Überschreiben ohne Merker | | `Z8Uart.EmpfangenZeitpunktPufferUndUeberschreiben` | — |
| Zeitgenaue Leitung von außen | `p30Quelle(takt)` (Abtastung zur Zeit des T0-Endwerts) | dto. | — |

## 6. Ports (S. 44–68)

| Punkt | Umsetzung | Test | Annahme |
|---|---|---|---|
| P0 nibbleweise Ein/Aus/Adresse, P1 Byte Ein/Aus/AD/hochohmig; Lesen = Pins | `portLesen()`, `portPegel()`, `portTreibt()` | `Z8Port.Port0NibbleArtenPort1Byte` | Adressnibble lesen = letzte Busadresse [Z8-P1] |
| P2 bitweise (P2M), offener Drain (P3M.0 = 0) bzw. Gegentakt; Lesen = Pin | | `Z8Port.Port2BitweiseOffenerDrainUndGegentakt` | — |
| P3: P30–P33 Eingänge, P34–P37 Ausgänge; Lesen = Eingänge + Ausgangsregister; Sonderfunktionen nicht überschreibbar | `p3Ausgang()` | `Z8Port.Port3LesenUndSchreiben`, `Z8Zaehler.ToutAnP36` | — |
| Handshake Port 0 (P32/P35), Port 1 (P33/P34), Port 2 (P31/P36), Eingang (DAV latcht, Schutz bis Lesen, RDY) und Ausgang (DAV nur bei RDY = H) | `hsRichtung()`, `p3Flanke()` | `Z8Port.HandshakeEingangPort0UndAusgangPort2`, `Z8Port.HandshakePort1UeberP33P34` | Ausgangsdaten sofort am Port (S. 65: „not output until State 5" nicht nachgebildet) [Z8-P2] |
| Ausgangsänderungen nach außen | `portAusgang(port, pegel, treibt)` | `Z8Port.Port0…`, Firmware-Tastatur | — |

## 7. Reset, Save-State, Debugger

| Punkt | Umsetzung | Test | Annahme |
|---|---|---|---|
| Resetwerte Table 12/13 (P01M 4D, P2M FF, P3M 00, TMR 00, IRQ 00, IMR.7 0, RP 00, PRE-Modusbits, P34–P37 = 1); GPR bleiben; Start 000CH | `tuReset()` | `Z8Reset.SteuerregisterWieTable12AllzweckregisterBleiben` | Start 5 Takte nach dem Loslassen [Z8-R1]; „undefinierte" Register behalten ihren Wert |
| /RESET gehalten: nichts läuft | `setResetLine()` | dto. | — |
| Save-State (alle Register, Ports, Handshake, Zähler samt Phase, UART-Schieber) | `serialize()/deserialize()` (Version 1, `ZAr`) | `Z8Stand.RundreiseMittenImSendenUndZaehlenIstBitgleich` (bitgleiche Fortsetzung, kaputte Daten abgewiesen) | — |
| Beobachtung | `sicht()`, `regSicht()`, `letzterZyklus()`, `onInterrupt`, `onIllegal` | `Z8Sicht.*`, `DbgZ8.*` | — |
| Debugger `cpu z8`: r, s/n/fin/g, b/bl/bd, u, a, d/de/dr, e/ee/er/set, t, uart, irq (mit Rangfolge), ports, bus, sicht, pin/port, irqlog, reset | `tools/dbg_z8.h`, `k1520dbg --z8` | `DbgZ8.*` | — |
| Disassembler/Assembler | `tools/z8/` aus der gemeinsamen Tabelle | `Z8Asm.RundlaufAlleOpcodesAlleOperandenbytes`, `Z8Asm.HandbuchKodierungen`, `Z8Disasm.*` | — |

**Entscheidung Debugger:** C++-Gegenstück statt Anbindung von `tools/z8_disasm.py`: k1520dbg ist
C++, braucht Disassembler und Assembler im Prozess (Schritt-Anzeige, `a`), und beides wird aus
derselben Tabelle erzeugt wie der Kern — damit können Kern, Disassembler und Assembler nicht
auseinanderlaufen (Wächter: Rundlauf über alle Opcodes).  Das Python-Werkzeug bleibt für ganze
Abzüge mit rekursivem Abstieg (Befund-Arbeit wie P19a).

## 8. Praxisprobe (Zusatz, `Z8Firmware.*`)

| Probe | Ergebnis |
|---|---|
| Terminal P8T_1_5.0 auf Prüfstand nach `terminal_typ2.md` §3 (2 KB RAM, 8275-Protokoll, LDC-Strobes, Bildende P32, Zeilenanforderung P31, Tastaturbyte P2/P33, Host P30) | Init bis zur Tastaturwarte (0268H): P01M 96, P3M 51, 8275 `00 / 4F 97 CC 5A / E0 E0`, Start 20H, Strobes 4000H/C000H, Einschaltmeldung „ADM31/9600 baud/Video Attr. on (c)zft/keaw" im BWS, Zeilentabelle 1780H, IRQ0/2/5 mit 24 verschiedenen Zeilen je Bild, Scheinbyte 00H über den UART |
| Terminal nach AA | Hauptschleife, Tastatur-Rücksetzen, Hostzeichen „Hi" im BWS (IRQ3), BEL ⇒ Strobe 8000H; nach FC „Error Tastatur" |
| Tastatur K7673.09 (2716 am Programmbus, Matrix an P0/P1, P30 = 0, P31 = 0) | sendet nach dem Einschalten AA über P36/P37 (Startbit, 8 Daten invertiert, LSB zuerst, Abtastung an steigender Taktflanke); Taste Zeile 0/P0.0 ⇒ 02/82, Zeile 0/P1.0 ⇒ E0 4D / E0 CD; T1-Takt der Wiederholung läuft |

Damit ist das Bitprotokoll der Tastatur aus dem Lauf bestätigt (`tastatur_k7673.md` §3): Daten
gelten an der steigenden Flanke von P36; vor jedem Rahmen gibt es eine zusätzliche steigende Flanke
mit P37 = 1 (Übergang P3 = 80H → C0H), die ein Empfänger als Ruhezustand übergeht.

## 9. Annahmen

| Kennung | Annahme | Begründung / Prüfung |
|---|---|---|
| [Z8-T1] | Registerzugriffe eines Befehls wirken zur Zeit „Befehlsende"; Zähler/UART werden davor nachgezogen | Datenblatt nennt die Lage nur für TMR (M2T2); Fehler ≤ 1 Befehl |
| [Z8-T5] | TOUT-Betrieb „interner Takt" zeigt am Pin 1 | Takt nicht als Pegel darstellbar |
| [Z8-M1] | Programmbusgrenze der Entwicklungsfassung = ROM-Größe der Maskenfassung (8840: 4 KB, 8820: 2 KB) | Terminal: 2732 an 0000–0FFF (`terminal_typ2.md` §2) |
| [Z8-M2] | LDC-Schreiben unter der Grenze ohne Wirkung | ROM; am 64-Poler hat der Programmbus kein /WR |
| [Z8-M3] | Lesen bei hochohmigem Bus = FFH, kein Zyklus | Terminal liest im Tristate nie |
| [Z8-M4] | Tx = 2 XTAL-Perioden = 1 interner Takt | UM0016 S. 137 |
| [Z8-M5] | 80–EF: Lesen FFH, Schreiben ohne Wirkung | wie MAME; Zilog: „not implemented" |
| [Z8-A1] | Inhalt eines Zeigerregisters ist eine volle Registeradresse | Table 37 (IR = Reg 00–FF); MAME gleich |
| [Z8-A2] | Ungerade Paaradresse ⇒ n, n+1 | Datenblatt verlangt gerade; MAME gleich |
| [Z8-A3] | Indexbasis X ohne Ex-Abbildung | Table 37 „Reg (Rn)", Beispiel 12; **MAME bildet E0–EF ab** (im Orakel ausgeblendet) |
| [Z8-F1] | DA ausserhalb der Tabelle wie die Rechnung (±06/±60, C = C ∨ Über-/Unterlauf), V bleibt | Datenblatt: „undefined"; MAME gleich |
| [Z8-F2] | SWAP: C und V bleiben | „undefined"; MAME gleich |
| [Z8-F3] | Ziel FLAGS eines flagsetzenden Befehls: Ergebnis gewinnt | UM0016 rät davon ab; MAME je Befehl verschieden (im Orakel ausgeblendet) |
| [Z8-B1] | Unbelegte Opcodes wie NOP (6 Takte) | unbekannt; MAME 0 Takte ohne Wirkung |
| [Z8-I1] | Externe Anforderungen ohne die 2 Synchronisiertakte | Wirkung nur auf Latenz |
| [Z8-I2] | IPR 000 ⇒ C>A>B, 111 ⇒ B>A>C | „reserved"; wie MAME |
| [Z8-P1] | P0-Adressnibble liest die letzte Busadresse | undefiniert; MAME liefert 1 |
| [Z8-P2] | Handshake-Ausgang: Daten sofort am Port | vereinfachend |
| [Z8-R1] | Erster Befehl 5 interne Takte nach dem Loslassen von /RESET | „5–10 TpC" |

## 10. Orakel gegen MAME

`tests/oracle/test_z8_mame.cpp`: je belegtem Opcode 3000 Zufallszustände (Stapel intern/extern im
Wechsel), Vergleich PC, FLAGS, RP, SP, IMR, IRQ-Freigabe, Register 04–7F, Speicherschreibzugriffe,
Takte.  Ausgeblendet: Fälle mit Port-/Steuerregistern als Operand (Peripheriewirkung nur im Kern),
LD mit Indexbasis E0–EF [Z8-A3].  **Bekannte Abweichungen MAME ↔ Datenblatt (Datenblatt gilt):**
Takte von 02/03 (MAME 10, Karte 6) und PUSH mit externem Stapel (MAME ohne Zuschlag).
Ergebnis des letzten Laufs: §10a.

### 10a. Ergebnis

(wird nach dem Lauf eingetragen)
