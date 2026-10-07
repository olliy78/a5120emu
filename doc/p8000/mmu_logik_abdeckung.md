# P8000 16-Bit-Karte — MMU-Steuerlogik: Abdeckung (AP P9b)

Stand 2026-10-07. Umsetzung `core/cards/p8000/mmu_logik16.{h,cpp}` (`P8000MmuLogik16`),
Wächter `tests/unit/cards/test_p8000_mmu_logik.cpp` (`P8000MmuLogik*`, 43 Fälle). Quelle der
Sollwerte: `schaltplan_16bit.md` (Rang 1), `hw_16bit.md` (Rang 2), Gegenprobe WEGA `mch.s`/`mmu.h`
und MON16 `p.test.s` (github.com/OlliL/P8000). Die Tests formulieren die Erwartung als **Tabelle**
(§1.2 des Plans), der Code bildet die **Gatter** von Bl. 5 nach — so prüft der eine den anderen.
[Ln] = benannte Annahme im Kopf von `mmu_logik16.h`. Gegenprobe der Tests per Mutation
(Gleichheit, Trap-Flanke, 4D16): jede Änderung macht mindestens einen Wächter rot.

## Schaltplanaussage → Test / Annahme

| Aussage (Fundstelle) | umgesetzt | belegt durch |
|---|---|---|
| /CS = LAD1/2/3 (Code/Data/Stack), low-aktiv, mehrere zugleich; Befehl in AD8–15 (§1.1) | ja | `CsAusLad123` (alle 256 Low-Bytes schreibend und lesend), `SpezialEaTraegtDenBefehlImHohenAdressbyte` |
| N/S-Eingang = Auswahlsignal, gewählte MMU sieht L (§1.1, §1.4) | ja | `Tabelle.*`, `OnBoardSperrtDieMmuAuswahl` |
| SN6 nur an der Code-MMU, Data/Stack fest L (§1.1) | ja | `Zeile3_…`, `MehrereTreiberVerdrahtetesUnd`, `MmuEinOhneTreiberSchwebtAufEins` |
| Auswahltabelle, 6 Zeilen (§1.2) | ja, Gatter wörtlich | `Zeile1_…` bis `Zeile6_…`; `GatterGebenHoechstensEineMmuUndFolgenDerTabelle` (16 Status × 2⁵); `AlleZyklusartenMalZustaende` (62 208 Zyklen je Gleichheitsregel: 16 Status × N/S × 8 SCR × 9 Segmente × 9 Offsets × 3 NBR × R/W, mit Ziel und Busadresse) |
| Status 1001 wird nicht ausgewertet (§1.2) | ja | `Status1001WirdNichtAusgewertet`, Matrix |
| NBR-Gleichheit → Stack (§1.2, Kaskadenrichtung abgeleitet) | ja, umschaltbar [L1] | `NbrGleichheitGehtAnStack`, `NbrGleichheitNachWegaGehtAnData`, Matrix mit beiden Regeln |
| NBR: DS8282, Bit n ↔ A(n+8), lesbar, kein Reset (§1.3) | ja; Startwert `Config::nbrStart` | `NbrLesenSchreibenStartwertUndUeberlebtReset` |
| 4D16: /OE des NBR bei SN1–5 = 0 hochohmig (§1.3, Deutung unsicher) | ja [L2] | `NbrLatchHochohmigInSegment0_1_64_65` |
| FFC9 SELSYSBRKREG (Index 4 „frei") | ohne Speicher [L3] | `SbrFfc9SpeichertNichts`, `Mon16ErkennungIndexAbEins` |
| On-Board-Fenster: SCR.0 = 0 ∧ SN = 0 ∧ Offset < 8000, unabhängig von MMU ON/Modus (§2) | ja | `OnBoardNurSegment0Unter8000` (alle 16 SCR × Segmente × Offsets × N/S × Status), Matrix |
| EPROM 0000–3FFF, SRAM 4000–5FFF mit 4 Spiegeln, 6000–7FFF leer (§2) | ja (Ziel + Bausteinadresse) | `OnBoardNurSegment0Unter8000` (Grenzen einzeln) |
| IMEML sperrt MMU-Auswahl, TRAD-Freigabe und SYSDS (§2, §3, §4.4) | ja | `OnBoardSperrtDieMmuAuswahl`, `SuppressVerhindertSchreibenNurAmHauptspeicher` |
| On-Board-Zugriff = 1 Wartetakt (§7) | ja (`Zugriff::wartetakte`) | `OnBoardNurSegment0Unter8000` |
| MMU aus: Busadresse = SN·64K + Offset, A23 = 0 (§3) | ja | `MmuAusPhysIstSegmentMal64K` (128 Segmente), Matrix |
| MMU ein: A0–A7 vom Offset, A8–A23 aus TRAD (§3) | ja; offen = 1, mehrere = UND [L7] | `Tabelle.*`, `MmuEinOhneTreiberSchwebtAufEins`, `MehrereTreiberVerdrahtetesUnd` |
| „MMU ON wirkt nicht auf die MMU-Schaltkreise" (Handbuch §2.6) | ja (MMUs sehen jeden Zyklus) | `SupWirktAuchBeiMmuAusUndBeimLesen`, `MmuAusPhysIstSegmentMal64K` |
| SUP sperrt SYSDS nur zum Speicherbus (§4.4) | ja; auch Lesen, auch MMU aus [L8] | `SuppressVerhindertSchreibenNurAmHauptspeicher`, `SupWirktAuchBeiMmuAusUndBeimLesen` |
| SCR: 4 Bit r/w, Reset 0 (Bl. 9 D18) | ja; Bit 4–7 lesen 1 [L4] | `ScrVierBitLesbarObereBitsEinsResetNull` |
| TRPL = LAD0–7, Takt SEGT-Flanke (§4.3) | ja [L6] | `TrplHaeltOffsetLowDesVerletzendenZyklus`, `ZweiteMmuBeiGezogenerLeitungTaktetNicht` |
| IF1L zweistufig: 3D20 bei jedem Holen, 1D20 mit SEGT (§4.3) | ja; Status 1101 [L6] | `If1lHaeltVorgaengerWennDasHolenVerletzt`, `If1lStufe1NurBeiStatus1101` |
| /SEGT gemeinsam (Pull-up), an die CPU (§1.1, §4.3) | ja (`onSegt`) | `ZweiteMmu…`, `MresetSetztMsen…`, `SaveStateRundreiseIstBitgleich` |
| Trap-Quittung: AD(8+ID) aller MSEN-MMUs (z8010_mmu.md §6.5) | ja; Rest offen, gleiche ID UND [L7] | `SegtQuittungKennwort` (8 MSEN-Masken × 3 Auslöser + gleiche ID) |
| E/A-Dekoder: A15–A6 = 1, A0 = 1, LAD3–5 wählen, A1/A2 nicht dekodiert (§4.5) | ja; Spiegel [L9] | `NurEigenePorts` (alle 65 536), `RegisterSpiegelnAnA1A2` |
| RETI = Schreiben ED 4D an FFE1 (§4.5) | ja [L12] | `RetiFolgeEdDann4D` |
| Index 4: LEDEIN FFD9, LEDAUS FFB9, RUN-LED durch MRESET− an (§5.1 Befund 3) | ja | `Index4RunLedEinAusUndReset` |
| Index 1: FFD9 = SNVR (Bl. 1 fehlt), kein LEDAUS (§9) | ja, ohne Wirkung [L10] | `Index1Ffd9IstSnvrOhneWirkungUndKeinLedaus` |
| SOFTRESET FFE9 (Index 1 und 4 laut Plan, §9 B11) | ja (`onSoftreset`) | `SoftresetMeldetSichBeiBeidenIndizes` |
| MRESET−: SCR = 0, MMU-Reset, „Master-Enable nicht auf Null" (Handbuch §2.2) | ja [L5] | `MresetSetztMsenUndLaesstDeskriptoren` |
| Nicht-Speicherzyklen (0–7, E, F) wirken nicht | ja | `NichtSpeicherzyklenOhneZielUndOhneWirkung` |
| Debugger-Sicht: `probe()` ohne Nebenwirkung, `sicht()`, `mmu()`, `onSegtEreignis` | ja | `ProbeOhneNebenwirkungUndGleichDemZyklus`, `TrplHaelt…` |
| Save-State v1 (Register, Latches, LED, RETI-Folge, /SEGT, 3 × Z8010) | ja | `SaveStateRundreiseIstBitgleich` (bitgleich, gleiches Weiterlaufen, Defekt abgelehnt ohne Zustandsänderung) |

## Gegenprobe Software

| Quelle | Aussage | Test |
|---|---|---|
| MON16 `p.test.s` vor Schritt 84 | Erkennung Index ≥ 1: `ld r12,@<01>0100` im System-Mode, Code-MMU verletzt ⇒ unterdrückt ⇒ offener Bus ≠ 0 ⇒ SEG-USR-Weg, **SBR-Test 84 entfällt** | `Mon16ErkennungIndexAbEins` — stützt [L3] und [L8] zugleich: ohne [L8] läse r12 = 0, MON16 nähme Index 0 an und prüfte FFC9 |
| MON16 `SEGMENT_TRAP_TEST` | Testschritte 85–90 (Limit/Read-Only, Data/Stack, je 62 Segmente, Durchreich-MMUs 80/81/82 parallel) | `Mon16Testschritte85bis90` |
| WEGA `mmu.h`, `mch.s` | Konstanten F6/FA/FC/F0, nichtsegmentierter Anwender in 3F, Kern nur über die Code-MMU | `CsAusLad123`, `WegaNichtsegmentierterAnwender` |
| WEGA `mch.s` `nsseg`/`getmem`/`putmem` | `cpb rh7,NBREAK; jr ugt` ⇒ **Gleichheit = Data** — Widerspruch zu §1.2 | [L1], `NbrGleichheitNachWegaGehtAnData` |

## Offen (Messfragen)

1. **NBR-Gleichheit** [L1]: Plan (abgeleitet) sagt Stack, WEGA rechnet Data. Am Gerät: NBR = 80H,
   nichtsegmentierter Anwender in 3F, Zugriff auf 80xx — welche MMU (VTR nach Verletzung)?
2. **4D16** [L2] (Frage §11.3 des Plans) — nur relevant, wenn ein nichtsegmentierter Anwender in
   Segment 0/1 läuft.
3. **/CS bei RESET** [L5]: MR der MMUs direkt nach Netz-Ein lesen (`sinb rh0, %00FC` vor `ENTRY_`
   nicht möglich — nur per Logikanalysator oder eigener Prüfsoftware).
4. **IF1L-Stufe 1** [L6]: Y4 (1100) oder Y5 (1101)?
5. **Lesewert unterdrückter Hauptspeicherlesezyklen** [L8] und von 6000–7FFF (Frage §11.2).
6. Nicht hier: CTC0-K3 zählt Stackzyklen, NMI-Identifier, Paritäts-FF (SCR.3) — Karte16 (P10).
