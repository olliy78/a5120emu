# Z80-DMA / UA858 — Datenblatt-Transkription und Abdeckung (AP P9c)

Stand 2026-10-07. Zweck: alles, was `core/primitives/z80_dma.{h,cpp}` wissen muss, an einer
Stelle und mit Beleg, dazu die **Abdeckungsliste** (Datenblatt-Punkt → Umsetzung → Test bzw.
Annahme). Entwurf: `doc/design/25_p8000.md` §10.11a (Vollständigkeitsgrundsatz).

## 0. Quellen und Sicherheitsgrade

| Kürzel | Quelle | Bemerkung |
|---|---|---|
| **[U]** | *Schaltkreis für direkten Speicherzugriff DMA U 858 D — Technische Beschreibung*, VEB Mikroelektronik „Karl Marx“ Erfurt. Im Repo als Anwendertranskription `doc/trascripted/DMA_U959D.md` (Dateiname irreführend, Inhalt = U 858 D); Scan `~/projects/K1520_rebuild/U858D-DMA.pdf` (47 S., reiner Bildscan, = tiffe.de `Bausteinuebersicht/U858D-DMA.pdf`). § = Kapitel der Beschreibung. | **Primärquelle** für den UA858. Die Transkription trägt „KI generierte Interpretation“ der Bilder; die **Bit-Tabellen von WR3/WR4/WR5 sind dort verschoben** (Tabelle und Fliesstext widersprechen sich, s. §2) — maßgeblich ist dort [Z]. |
| **[Z]** | *Z8410/Z84C10 NMOS/CMOS Z80 DMA Product Specification*, Zilog PS017901-0602 (25 S.), `http://www.zilog.com/docs/z80/ps0179.pdf`, SHA-256 `f190450023e2b9777c03451abe18af4f7adcb9124596955236b7c2980429e750`, reiner Bildscan (nicht im Repo). Gelesen: S. 41–54 (Funktion, Pins, Register Bild 8a/8b, Beispielprogramm Bild 9, Zeitverhalten Bild 10–19). | Gegenprobe und **maßgeblich für die Bitlagen** (Bild 8b ist eine Zeichnung, kein KI-Text). Elektrische/AC-Daten (S. 55 ff.) nicht gelesen. |
| **[M]** | MAME `src/devices/machine/z80dma.{cpp,h}` (master, 2026-10-07; SHA-256 `2b87bb5f…53325` / `579545cc…ce0a`), mit Ableitung `ua858d_device`. | **Nur Gegenprobe.** Selbst als „minimum implementation“ gekennzeichnet; B7 dort `fatalerror`, Suche ohne Stopp, Fixed-Adress-LOAD fehlt. |
| **[G]** | Gastprogramme: PC 1715W (`doc/pc1715/pc1715w_hardware.md` §4.5, `doc/EPROMS/PC1715W/s550.prn`), P8000 MON8 3.1 (Hardwaretest 26 ab 087CH, Floppytreiber 1872H). | Was die Software tatsächlich programmiert und ausliest. |

Sicherheitsgrad je Aussage: **gelesen** (steht so in der Quelle), **bestätigt** (≥ 2 Quellen),
**abgeleitet**, **Annahme** (Datenblattlücke, im Header `z80_dma.h` benannt).

## 1. Aufbau

- Ein Kanal, zwei Ports A/B, jeder Speicher oder E/A, Adresse fest/+1/−1, 16 Bit. [Z S. 43–44, U §4] — bestätigt.
- **21 Schreibregister** in 7 Basisgruppen WR0–WR6 (7 Basis- + 14 Folgeregister), **7 Leseregister**
  RR0–RR6. Startadressen A/B sind gepuffert (Arbeits-Adresszähler getrennt). [Z S. 46–47, U §9] — bestätigt.
- Steuerport: ein E/A-Port (/CE). Lesen = Lesesequenz (§4), Schreiben = Basisbyte, gefolgt von den
  durch Zeigerbits angeforderten Folgebytes in fester Reihenfolge. [Z S. 48, U §10.1] — bestätigt.
- **Jedes Steuerbyte sperrt die DMA** (Disable), bis sie wieder freigegeben wird; Ausnahmen nur
  Enable DMA (87H) und WR3 mit D6 = 1. „Writing the Read Status Byte command or the Initiate Read
  Sequence command disables the DMA“. [Z S. 44, U §10 + §10.1 WR6] — bestätigt. Lesen sperrt nicht.

## 2. Schreibregister (Bitlagen nach [Z] Bild 8b)

Erkennung des Basisbytes:

| Register | Muster | Folgebytes (Reihenfolge) |
|---|---|---|
| WR0 | `0 D6 D5 D4 D3 D2 D1 D0`, D1D0 ≠ 00 | D3 A-Start L, D4 A-Start H, D5 Blocklänge L, D6 Blocklänge H |
| WR1 | `0 D6 D5 D4 D3 1 0 0` | D6 Zeitsteuerbyte Port A |
| WR2 | `0 D6 D5 D4 D3 0 0 0` | D6 Zeitsteuerbyte Port B |
| WR3 | `1 D6 D5 D4 D3 D2 0 0` | D3 Maskenbyte, D4 Vergleichsbyte |
| WR4 | `1 D6 D5 D4 D3 D2 0 1` | D2 B-Start L, D3 B-Start H, D4 Interrupt-Steuerbyte → (dessen D3) Impulssteuerbyte → (dessen D4) Vektor |
| WR5 | `1 x D5 D4 D3 0 1 0` | — |
| WR6 | `1 D6 D5 D4 D3 D2 1 1` | nur BBH: Lesemaske |

Ein WR0 mit D1D0 = 00 gibt es nicht — das Muster ist WR1/WR2. Basisbytes `1xxxx110` (D2 = 1 bei
D1D0 = 10) sind undefiniert [M: „0x8e newtype on Sharp X1, unknown purpose“].

**WR0** — D1D0: 01 Transfer, 10 Search, 11 Search/Transfer. D2: 1 = A→B (A Quelle), 0 = B→A.
Blocklänge = Anzahl − 1 („Es ist erforderlich, daß die Blocklänge stets um 1 niedriger ist“);
**Blocklänge 0 ⇒ 2¹⁶ + 1 Byte**. [U §10.1, Z S. 44/50] — bestätigt.

**WR1/WR2** — D3: 0 Speicher / 1 E/A. D5D4: 00 −1, 01 +1, 10/11 fest. D6: Zeitsteuerbyte folgt.
(Die Transkription [U] nennt D0–D2 — KI-Fehllesung, Kennbits sind D2–D0.) [Z Bild 8b] — gelesen.
Zeitsteuerbyte: D1D0 Zykluslänge 00 = 4, 01 = 3, 10 = 2, 11 = nicht benutzen; D2 = 0 /IORQ endet
½ Takt früher, D3 = 0 /MREQ, D6 = 0 /RD, D7 = 0 /WR (Flanken; „keine Steigerung der
Übertragungsgeschwindigkeit“). [Z Bild 8b + 14, U §10.1] — bestätigt.

**WR3** — D2 Stopp bei Treffer, D3 Maskenbyte folgt, D4 Vergleichsbyte folgt, D5 Interruptfreigabe
(≙ ABH), D6 DMA-Freigabe (≙ 87H). Maske: **0 = Bit wird verglichen**; Maske FFH ⇒ jedes Byte
trifft. [Z Bild 8b, U §10.1 Fliesstext] — bestätigt ([U]-Tabelle verschoben). [M] `ua858d_device`
ignoriert D6 — ohne Beleg, hier nicht übernommen.

**WR4** — D6D5 Betriebsart: 00 Byte, 01 Continuous, 10 Burst, 11 nicht programmieren. D2/D3
B-Startadresse L/H folgt, D4 Interrupt-Steuerbyte folgt. [Z Bild 8b] — gelesen ([U]-Tabelle verschoben).
Interrupt-Steuerbyte: D0 Interrupt bei Treffer, D1 bei Blockende, D2 Impulse erzeugen, D3
Impulssteuerbyte folgt, D4 Vektor folgt, D5 Status beeinflusst Vektor, D6 Interrupt bei RDY (vor
der Busanforderung). [Z, U] — bestätigt.
Vektorbeeinflussung (nur wenn D5): **V2V1 = 00 RDY, 01 Treffer, 10 Blockende, 11 Treffer und
Blockende**. [Z Bild 8b, M] — bestätigt.
Impulssteuerbyte: Impuls auf /INT/PULSE, wenn die DMA Busmaster ist und die unteren 8 Bit des
Bytezählers gleich dem Impulssteuerbyte sind (alle 256 Byte, Versatz 1–255). [U §7, Z S. 45] — bestätigt.

**WR5** — D3: 0 RDY aktiv low / 1 aktiv high; D4: 0 /CE allein / 1 /CE/WAIT gemultiplext;
D5: 0 Stopp bei Blockende / 1 Auto-Restart. [Z Bild 8b] — gelesen ([U]-Tabelle verschoben;
[U] RR0-Text nennt richtig „WR5/D3“).

**WR6 — Befehle** [Z Bild 8b, U §10.1]:

| Code | Name | Wirkung (Datenblatt) |
|---|---|---|
| C3 | Reset | Interrupts sperren, IP/IUS zurück; Busanforderung sperren; Auto-Restart und WAIT (WR5 D5/D4) aus; Zeitverhalten A/B = Standard; Force Ready aus. **Lesesequenz bleibt** (A7H nötig). 6 × C3 nach Abbruch, weil WR4 fünf Folgeregister hat. |
| C7 / CB | Reset Port-A-/B-Timing | Standard-Zeitverhalten (Speicher 3, E/A 4 Takte) |
| CF | Load | Startadressen in die Adresszähler, Bytezähler 0, Force Ready aus; **eine feste Adresse wird nur in den QUELL-Port geladen** („only loads a fixed address to a port selected as the source“ — daher das Muster LOAD, WR0 gedreht, LOAD). Status D0 zurück. |
| D3 | Continue | Bytezähler 0, Adresszähler laufen weiter; Status D5 zurück |
| AF | Disable Interrupts | anstehende Interrupts gesperrt; IUS bleibt |
| AB | Enable Interrupts | ≙ WR3 D5 = 1 |
| A3 | Reset and Disable Interrupts | IUS, IP zurück, Force Ready aus, Interrupts gesperrt |
| B7 | Enable after RETI | keine Busanforderung bis RETI (für „Interrupt bei RDY“: ISR gibt B7, 87, RETI) |
| BF | Read Status Byte | nächstes Lesen = Statusbyte |
| 8B | Reinitialize Status Byte | Status D4 und D5 zurück (= 1); D3 nur über Quittung/A3, D0 nur über LOAD |
| A7 | Initiate Read Sequence | nächstes Lesen = erstes per Maske freigegebenes Register |
| B3 | Force Ready | RDY intern aktiv; zurück durch C3, CF, A3, Blockende, Treffer und **jede Busfreigabe** (im Byte-Betrieb also nach jedem Byte) |
| 87 | Enable DMA | gibt die Busanforderungslogik frei, setzt nichts zurück |
| 83 | Disable DMA | sperrt die Busanforderung (Interrupts nicht) |
| BB | Read Mask Follows | nächstes Schreibbyte = Lesemaske, Lesesequenz danach initialisiert |

## 3. Leseregister

RR0 Status, RR1/RR2 Bytezähler L/H, RR3/RR4 Adresszähler A L/H, RR5/RR6 Adresszähler B L/H.
Lesemaske D0–D6 = RR0–RR6 (1 = lesen), D7 ohne Wirkung; gelesen wird stets aufsteigend, danach
wieder von vorn. [Z Bild 8a, U §10.2] — bestätigt.

RR0: D0 = 1 Transfer seit dem letzten LOAD erfolgt; **D1 = 0 RDY aktiv** (Pin, Polarität WR5 D3);
D3 = 0 Interrupt anstehend; D4 = 0 Treffer (seit Reset/8B); D5 = 0 Blockende (seit Reset, LOAD,
Continue, 8B); D2, D6, D7 undefiniert. [Z Bild 8a, U §10.2] — bestätigt.

## 4. Betriebsarten und Busprotokoll

- **Byte**: je Byte /BUSRQ, ein Byte, Bus zurück — „die Freigabe … erlaubt der CPU, mindestens
  einen M1-Zyklus abzuarbeiten“ — **unabhängig vom RDY-Pegel**. [U §3.2.1, §11.2.3; Z S. 52] — bestätigt.
- **Burst**: Bytes, solange RDY aktiv; RDY inaktiv ⇒ laufendes Byte zu Ende, Bus zurück, bei RDY
  wieder anfordern. [U §3.2.2, Z S. 53] — bestätigt.
- **Continuous**: Bus bis Blockende (oder Treffer-Stopp); RDY inaktiv ⇒ DMA pausiert **mit**
  gehaltenem Bus. Ein RDY-Impuls genügt zur Busübernahme. [U §3.2.3, §11.2.2; Z S. 44/53] — bestätigt.
- Busanforderung nur bei RDY aktiv (Pegel, keine Flanke); Datenoperationen nach zwei Takten
  /BAI = low. Blockende in Burst/Continuous: /BUSRQ mit derselben Flanke inaktiv; das letzte Byte
  wird auch bei vorher fallendem RDY noch übertragen. [U §11.2.2/3, Z S. 52–53] — bestätigt.
- **IUS sperrt weitere Busanforderungen** dieser DMA bis RETI (bzw. A3). [U §8.2, §8.5] — gelesen.
- Mehrere DMAs: /BAI–/BAO-Kette, /BUSRQ open drain. [U §2.3, Z S. 45] — gelesen; im Kern eine DMA je Maschine.

## 5. Zähler und Adressen (Pipelining)

[U §4] Tabelle 1 (Stopp durch Blockende, Blocklänge N): übertragen N + 1, **Bytezähler N**, Quell-
adresse As ± (N+1), Zieladresse As ± N. Tabelle 2 (Stopp durch Treffer im Byte M): Transfer/Search
alle Betriebsarten und Search-Byte: M Bytes, Zähler M − 1; **Search Burst/Continuous: M + 1 Bytes**
(gepuffertes Lesen — der Treffer wird erst beim Lesen des nächsten Bytes erkannt). [Z S. 46]: „In
searches, data byte comparisons with the match byte are made during the read cycle of the next byte.“

## 6. Interrupts

Bedingungen RDY (vor der Busanforderung), Treffer, Blockende; jede setzt IP und kann den Vektor
ändern (§2 WR4). IP → /INT; Quittung (M1 + IORQ) ⇒ IP zurück, IUS gesetzt, Vektor auf den Bus.
IUS sperrt eigene Interrupts, niederpriore Bausteine (IEO = low) und eigene Busanforderungen.
RETI (ED 4D, von der DMA mitgelesen, solange IEI = high) setzt IUS zurück. IEO = IEI ∧ ¬IUS
(∧ kein anstehender Interrupt während der Quittungsauflösung). Während die DMA Busmaster ist,
nimmt die CPU keine Interrupts an; Interrupt bei Blockende/Treffer kommt nach der Busfreigabe.
[U §8, Z S. 45/53] — bestätigt.

## 7. Zeitverhalten im Modell

Der Kern ist transaktionsgenau, nicht taktgenau: je `step()` ein Byte, Rückgabe = Takte des Lese- plus
Schreibzyklus. Zykluslänge je Port: Zeitsteuerbyte (4/3/2), sonst Standard — **Vorgabe 4 Takte**
(bisheriges Verhalten, PC 1715W/P8000 laufen so), mit `setStandardZyklen(true)` Speicher 3 / E/A 4
nach Datenblatt. Search ohne Transfer kostet nur den Lesezyklus. /WAIT (WR5 D4) über den Rückruf
`warteTakte`. Flanken-Bits des Zeitsteuerbytes werden gespeichert und angezeigt, wirken nicht
(laut Datenblatt keine Geschwindigkeitsänderung). Busübernahme-Latenz (/BUSRQ → /BAI, zwei Takte)
ist nicht nachgebildet; die Maschine prüft `busRequest()` vor jedem CPU-Befehl (= Ende des
M1-Zyklus), und `cpuZyklus()` meldet ihr den Befehl zurück (Byte-Betrieb).

## 8. Verhaltensänderungen durch P9c (gegenüber AP-W2a/P5f)

Jede Zeile: Datenblatt-Beleg, Wirkung auf die Gastsysteme, Wächter. Die volle Regression
(`tools/dev.sh test`, `test-format`) ist danach grün; PC 1715W bootet SCP 3.0 und der
P8000 UDOS wie vorher.

| # | Vorher | Jetzt (Datenblatt) | Gastsysteme |
|---|---|---|---|
| V1 | Schreiben liess die Freigabe stehen | **jedes Basisbyte sperrt**, ausser 87H/WR3 D6 [Z S. 44, U §10] | alle Folgen enden mit 87H; MON8 `AB 87 BF`: BF kommt erst nach dem Block (OTIR gibt den Bus je Iteration frei). Test `RdyPolaritaet`/`ForceReadyOhneRdy` umgestellt (87 bzw. Richtung vor 87). |
| V2 | Byte-Betrieb bewegte bei dauerhaft aktivem RDY Byte um Byte | Bus nach jedem Byte zurück, erst nach `cpuZyklus()` wieder [U §3.2.1] | PC 1715W/P8000: der U8272 nimmt DRQ mit dem DACK zurück — gleiche Abfolge. Maschinen melden `cpuZyklus()` (`pc1715.cpp` runW, `karte8` über `setzeBusmaster`). |
| V3 | Force Ready hielt bis C3 | fällt auch bei CF, A3, Blockende, Treffer, **jeder Busfreigabe** [U §10.1 B3] | S550-„Blindlauf“ (Byte-Betrieb, B3) schreibt nun **1 statt 2** Byte auf 40H — dort wirkungslos (`pc1715.cpp`), die nächste Folge beginnt mit C3. |
| V4 | LOAD lud nur den Quellport | auch den Zielport, wenn er nicht fest ist; D0 → 0, D5 → 1, Force Ready aus [Z S. 48, U §10.1] | Doppel-LOAD-Folgen (PC 1715W, MON8) ergeben dieselben Adressen. |
| V5 | C3 = Hardware-Reset (Lesemaske 7FH, WR5 = 0, Betriebsart Byte) | C3 lässt Lesesequenz, RDY-Polarität und Betriebsart; nur WR5 D4/D5 [U §10.1 C3]; `reset()` (Systemreset) wie vorher | alle Folgen schreiben WR4/WR5 und BB nach C3. |
| V6 | D3 (Continue) gab frei | gibt nicht frei (V1), setzt D5 = 1 [U §10.2] | `D3 8B AB 87` |
| V7 | 8B löschte D0 | 8B setzt nur D4/D5; D0 löscht LOAD [U §10.1/§10.2] | nur D1/D5 (S550) bzw. `AND 3BH` nach einem Transfer (MON8) ausgewertet |
| V8 | B7 + RETI gab frei | B7 hält die Busanforderung bis RETI zurück, freigeben muss 87H [Z S. 53] | B7 benutzt kein Gastsystem |
| V9 | IUS sperrte nur die Kette | IUS sperrt auch die eigene Busanforderung; RETI wirkt nur bei IEI = high [U §8.2/§8.5] | alle ISRs geben A3; die DMA ist vorderstes Kettenglied (IEI = 1) |
| V10 | Blocklänge 0 = 1 Byte | 65537 Byte [U §10.1] | nicht benutzt |
| V11 | AF liess /INT aktiv | AF blendet aus [U §10.1 AF] | nicht benutzt |
| V12 | WR3 D5 ohne Wirkung | Interruptfreigabe ≙ AB [Z, U] | nicht benutzt |

Neu (additiv): Search und Search/Transfer mit Maske/Vergleich, Stopp bei Treffer samt Tabelle 2,
Interrupt bei Treffer und bei RDY, Vektorcode für alle vier Gründe, Impulsausgang, variable
Zyklen je Port, /WAIT, `sicht()` für den Debugger, Save-State-Erweiterung „DM2“ (Stände ohne
den Block laden weiter, mit Vorgaben).

Bewusst **nicht** umgestellt (Vorgabe ≠ Datenblatt, Gastsysteme laufen auf der Vorgabe):
Bytezähler nach Blockende N + 1 (Tabelle 1: N; `setZaehlerLiestBlocklaenge`, am P8000 an),
Standardzyklus 4 + 4 (`setStandardZyklen`), Zieladresse nach Blockende As ± (N+1) statt
As ± N laut Tabelle 1 — **offen**: S550 0559H setzt nach einem vorzeitig beendeten Lesen mit der
gelesenen Port-A-Adresse fort, was nur mit „nächste freie Adresse“ aufgeht; die KI-Transkription
der Tabelle ist an dieser Stelle nicht gegengeprüft.

## 9. Abdeckungsliste

Tests: `tests/unit/primitives/test_z80_dma_voll.cpp` (`Z80DmaVoll.*`, 31 Fälle) und
`test_z80_dma.cpp` (`Z80Dma.*`, Gastfolgen PC 1715W). „A n“ = Annahme im Header `z80_dma.h`.

| Datenblatt-Punkt | Umsetzung | Test | Annahme |
|---|---|---|---|
| Basisbyte-Erkennung WR0–WR6, alle 256 Bytes | `schreibeWR` | `AlleBasisbytesGruppeFolgebytesUndSperre` | A4 (1xxxx110, unbekannte WR6) |
| Folgebytes WR0 (16 Teilmengen) | `nimmFolge` | `WR0AlleFolgebyteTeilmengen` | — |
| Folgebytes WR4 inkl. Int-Steuer → Impuls → Vektor | `nimmFolge` F_INTCTL | `WR4AlleFolgebyteTeilmengenSamtImpulsUndVektor`, `Z80Dma.FolgebytesNachWR4MitIntCtlPulseVektor` | — |
| WR3 Maske/Vergleich, WR1/WR2 Zeitsteuerbyte | `nimmFolge` | `WR3UndWR1WR2Folgebytes` | A2 |
| Schreiben sperrt, 87/WR3 D6 geben frei | `ioWrite` | `AlleBasisbytes…`, `SchreibenWaehrendDesTransfersSperrt` | — |
| Transfer/Search/beides × Byte/Cont/Burst/11 × Richtung × Speicher/E/A × −1/+1/fest | `step` | `TransferMatrixArtModusRichtungPorttypAdressmodus` (1536 Fälle) | A1 |
| Blocklänge + 1, Blocklänge 0 = 65537, FFFFH, Adressüberlauf | `blockEndeBei` | `BlocklaengeNullUndFFFFUndAdressueberlauf`, `Z80Dma.LaengePlusEinsByte` | — |
| Bytezähler (Vorgabe / Tabelle 1) | `zaehlerLesen` | `BytezaehlerVorgabeUndDatenblattTabelle1`, `Mon8Hardwaretest26` | §8 |
| Byte-Betrieb: Bus nach jedem Byte, CPU-M1 dazwischen | `busFreigabe`, `cpuZyklus` | `ByteBetriebGibtNachJedemByteAb…`, Matrix (CPU-Zyklen) | — |
| Burst: Abgabe bei RDY inaktiv, Wiederaufnahme | `busRequest` | `BurstGibtBusBeiRdyInaktivAb…` | — |
| Continuous: Bus gehalten ohne RDY, Beginn nur mit RDY | `busRequest`/`step` | `ContinuousHaeltDenBus…` | — |
| RDY-Polarität WR5 D3, Status D1 = Pin | `pinAktiv`, `status` | `RdyPolaritaetUndStatusD1`, `Z80Dma.RdyPolaritaet`, `Z80Dma.StatusD1Folgt…` | — |
| Force Ready und seine sechs Rücksetzbedingungen | `befehl`, `load`, `busFreigabe` | `ForceReadyRuecksetzbedingungen`, `Z80Dma.ForceReadyOhneRdy` | — |
| Search: Maske (0 = vergleichen), Vergleichsbyte, Status D4 | `step` | `SuchMaskeUndVergleichMatrix` (8 Masken × 5 Bytes × 256 Daten) | — |
| Stopp bei Treffer, Tabelle 2 (Search Burst/Cont M + 1) | `step` (`nachlauf`) | `StoppBeiTrefferTabelle2` | A8 |
| 8B + 87 = Weiterlaufen nach Treffer | `befehl` 8B | `StoppBeiTrefferTabelle2` | — |
| Search/Transfer ohne Stopp | `step` | `SuchenMitTransferOhneStopp…` | — |
| Interrupt bei Blockende / Treffer / beides, Vektor V2V1 | `interrupt`, `vektor` | `VektorOhneUndMitStatusbeeinflussungJeGrund`, `Z80Dma.InterruptUndVektorBeiBlockende` | A8 |
| Interrupt bei RDY, B7, RETI | `pruefeRdyInterrupt`, `onRETI` | `InterruptBeiRdyMitB7UndRETI`, `Z80Dma.EnableNachRETI` | A6 |
| IUS sperrt Kette und Busanforderung; RETI nur mit IEI | `getIEO`, `busRequest`, `onRETI` | `IusSperrtBusanforderungBisRETIUndIeiGatetRETI` | A11 |
| AB/WR3 D5/AF/A3, Bedingung bei Sperre | `befehl`, `interrupt` | `InterruptFreigabeSperreUndRuecksetzen`, `Z80Dma.ISRResetUndDisableInterrupts` | A5 |
| Impulse alle 256 Byte mit Versatz | `step` | `ImpulseAlle256ByteMitVersatz` | A7 |
| Zykluslänge 4/3/2 je Port, Standard, C7/CB | `zyklus` | `ZykluslaengenMatrixUndStandard` | A2, §7 |
| /CE/WAIT | `warte` | `WaitEingangVerlaengertZyklen` | A9 |
| C3 (was ja, was nein), sechs C3 | `befehlReset` | `ResetC3WasErTutUndWasNicht`, `Z80Dma.FloppyLesen…` (6 × C3) | — |
| CF: fester Zielport nicht geladen, Status | `load` | `LoadFesteZieladresseWirdNichtGeladen`, `Z80Dma.DoppelLoad…` | — |
| D3, 8B, 83, unbekannte Codes | `befehl` | `ContinueReinitStatusDisableUnbekannt`, `Z80Dma.PruefLesenContinueReinitStatus` | A4 |
| Lesemaske (alle 256), Umlauf, A7, BF, Lesen sperrt nicht | `ioRead` | `LesesequenzAlleMaskenUndA7BF`, `SchreibenWaehrend…` | A3 |
| Auto-Restart, neue Startadresse während des Blocks | `blockEndeBehandeln` | `AutoRestartLaedtNeuUndWartetAufRdy`, `Z80Dma.DekrementUndAutoRestart` | A10 |
| Save-State (Rundreise, Altformat, abgeschnitten) | `serialize` | `SaveStateRundreiseMitErweiterungUndAltformat`, `Z80Dma.SaveStateRundreise` | — |
| Beobachtung ohne Seiteneffekt | `sicht()` u. a. | `BeobachtungOhneSeiteneffekt` | — |
| Gastfolgen bitgleich | — | `Mon8Hardwaretest26`, `Z80Dma.*`, `P8000Floppy8_.*`, `P8000Boot.*`, PC-1715W-Boot | — |
| /BAI–/BAO-Kette, /BUSRQ als Eingang, Busübernahme-Takte | nicht nachgebildet (eine DMA je Maschine, §7) | — | — |
| CMOS-Reset über M1 ohne RD/IORQ | nicht nachgebildet (NMOS/UA858 hat das nicht) | — | — |
