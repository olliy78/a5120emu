# PC 1715 — Floppy-Ansteuerung: die Marken-ROMs 068 (Lesen) und 069 (Schreiben)

Stand 2026-10-03, AP-0d. Belege: **[SH]** Servicehandbuch §1.5.3.3–1.5.3.6 (Tabelle des Lese-ROMs
A2:2), **[ROM]** die Abzüge `doc/EPROMS/PC1715/pc1715_068_fdc_lese.bin` /
`…069_fdc_schreib.bin` (je 1024 Byte, bytegleich zu `eprom/068.bin`/`069.bin`; die Abzüge der
Steckeinheit-Fassung A301 `098`/`099` sind **leer, 0 Byte**), **[S502]** wie der Urlader die
Steuerbits benutzt (`doc/EPROMS/PC1715/s502.prn`), **[LAUF]** am Z80-Kern geprüft.

## 1. Lese-ROM 068 (A2:2) — Markenerkennung

Adressierung [SH 1.5.3.3]: Die Schiebekette (20 Bit) hält Takt- und Datenbits verschachtelt;
**an das ROM ist nur jeder zweite Ausgang geführt**, sodass in einem C1-Takt das **Taktbyte**,
im nächsten das **Datenbyte** der Marke anliegt (Taktinformation einen C1-Takt früher). Die
Adresse ist

| Bit | Bedeutung |
|---|---|
| A0–A7 | das gerade anliegende Byte der Schiebekette (Takt- bzw. Datenbyte) |
| **A8** | **/MK** — Steuersignal der Steuer-PIO (s. Abschnitt 3: gegenüber dem Portbit **invertiert**) |
| A9 | Rückkopplung vom Flipflop A7:3/05: 0 = „Taktteil gesucht“, 1 = „Taktteil war eine Marke, jetzt Datenteil prüfen“ |

Ausgang: **D7 = 80H** → „Taktteil gefunden“ (setzt das Rückkoppel-FF); **D6 = 40H** → „Datenteil
gefunden“, damit (verknüpft mit dem Zwischentakt ZT) **Marken-FF gesetzt = MKE = 1** (Steuer-PIO B
Bit 1). Alle anderen Adressen enthalten **00H**. Der ROM-Inhalt (alle 10 von 0 verschiedenen
Stellen [ROM]):

| A9 | A8 (/MK) | A7–A0 | Inhalt | Bedeutung |
|---|---|---|---|---|
| 0 | 0 | **14H** | 80H | Taktteil Synchronbyte **C2** (MFM; Takt mit fehlenden Impulsen) |
| 0 | 0 | **C7H** | 80H | Taktteil FM-ID-/Daten-/gelöschte-Datenmarke (Takt C7H) |
| 0 | 0 | **D7H** | 80H | Taktteil FM-**Indexmarke** (Takt D7H; im Handbuch als „07“ abgetippt — `D7` ist richtig und passt zum FM-Standard, die Abzugsbytes belegen es) |
| 0 | 1 | **0AH** | 80H | Taktteil Synchronbyte **A1** (MFM) |
| 1 | 0 | **C2H** | 40H | Datenteil Synchronbyte C2 (MFM) |
| 1 | 0 | **F8H** | 40H | Datenteil gelöschte Datenmarke |
| 1 | 0 | **FBH** | 40H | Datenteil Datenmarke |
| 1 | 0 | **FCH** | 40H | Datenteil Indexmarke |
| 1 | 0 | **FEH** | 40H | Datenteil ID-Marke |
| 1 | 1 | **A1H** | 40H | Datenteil Synchronbyte A1 (MFM) |

Eine **Marke = Taktbyte aus der A9 = 0-Zeile unmittelbar gefolgt von einem Datenbyte aus der
A9 = 1-Zeile mit gleichem /MK**. Daraus:

* **/MK = 0** (A8 = 0): MKE bei
  Takt **C7H** + Daten **FEH / FBH / F8H** (FM: ID-, Daten-, gelöschte Datenmarke),
  Takt **D7H** + Daten **FCH** (FM: Indexmarke) und
  Takt **14H** + Daten **C2H** (MFM-Indexsynchronbyte).
* **/MK = 1** (A8 = 1): MKE bei Takt **0AH** + Daten **A1H** (MFM-Synchronbyte vor ID-/Datenfeld).
* Nach dem Setzen kann **keine weitere Marke** erkannt werden, solange MKE = 1 (Rückführung
  /MKE → Rückkoppel-FF); zurückgesetzt wird über **/MR** bzw. **/STR** (Bit 5/Bit 3 des Steuerwortes
  der Steuer-PIO A, Abschnitt 3).
* **Was das Betriebssystem liest**: der gleiche Impuls, der MKE setzt, lädt den Bitzähler A12 auf
  12 (Byte-Takt); nach 4 weiteren C1-Takten steht **das Markenbyte** (bei /MK = 1 also das **erste
  A1**, bei /MK = 0 das **Kennbyte FE/FB/F8/FC bzw. C2**) in der Daten-PIO B und /BSTB übernimmt es
  → der **erste `IN` von Port 02H nach MKE liefert die Marke selbst**; jeder weitere `IN` das
  nächste Byte. [SH 1.5.3.6] [S502: `ReadField` liest bei MFM A1, A1, A1, dann FEH.]
* Die Marke wird **nur im bitgenauen Strom** erkannt: Das MFM-Synchronbyte A1 trägt einen
  **fehlenden Takt**, FM-Marken tragen **andere Taktbytes** als FF/00-Daten — ein Nachbau auf
  Byteebene (wie der `/WAIT`-Zweig der K5122) muss dafür **die Lage der Marke im Spurstrom** kennen
  (Bezug: `TrackCodec`/`BitCodec`, dort stehen Sync-Gruppe und Kennbyte).

## 2. Schreib-ROM 069 (A2:1) — nur für das Schreiben

Adressierung: **A0–A7 = zu schreibendes Datenbyte**, **A8 = letztes Bit des vorigen Bytes**,
**A9 = 0 → Marken/Synchronbytes, 1 → Daten** (nur bei MFM; bei FM wird der ROM nicht selektiert,
das Taktbyte ist dann FFH). Ausgang = **Taktbyte** (1 = Taktimpuls zwischen den Datenzellen):

* **A9 = 1 (Daten)**: der ROM ist **bitgenau die MFM-Taktregel** — Taktbit vor Datenbit *i* =
  1 genau dann, wenn Vorgängerbit und Datenbit *i* beide 0 sind (MSB zuerst, A8 liefert das
  Vorgängerbit des ersten Bits). Alle 512 Stellen nachgerechnet [LAUF].
* **A9 = 0 (Marken)**: C2H → **14H**, A1H → **0AH**, FBH/FEH/F8H → **C7H**, FCH → **D7H**
  (A8 ohne Einfluss); alle anderen Stellen 00H. Es sind dieselben Taktbytes wie im Lese-ROM, hier
  in der Schreibrichtung.
* Einzelnes Streubyte **069[0013H] = 04H** (A9 = A8 = 0, Daten 13H): keine Marke, ohne
  Bedeutung (Brennfehler/Altlast, **[?]**).

Für den Emulator ist 069 **ohne eigene Wirkung**: der K5122-Weg schreibt auf Byteebene
(`TrackCodec`), die Taktbytes entstehen beim Kodieren (`BitCodec`). Wichtig ist nur: FM-Marken
(Takt C7H/D7H), MFM-Sync C2 (Takt 14H) und A1 (Takt 0AH) müssen beim Schreiben **mit diesen
Takten** auf die Spur gehen, sonst findet das Lese-ROM sie nicht wieder.

## 3. Steuerbits der Steuer-PIO A (Port 04H) — Polarität (wichtigste Abweichung zum Plan)

Das Handbuch benennt die Signale **/MK, /MK1, /SD, …** (Tabelle [SH 1.5.2.2]); **die Portbits
wirken bei drei Leitungen invertiert**. Der Beleg ist der Urlader (läuft bei MFM-Disketten mit
dem Wort `85H`, bei FM mit `87H`) und stimmt mit dem Lese-ROM überein:

| Bit | Name | Portbit = 1 | Portbit = 0 | Beleg |
|---|---|---|---|---|
| 0 | /WE | Schreiben gesperrt | Schreibsteuerung frei | [SH] |
| **1** | **MK** | **/MK = 0: FM-Marken + C2** (S502 FM-Wort `87H`; `BBH`) | **/MK = 1: A1-Synchronbyte** (S502 MFM-Wort `85H`) | [S502], [ROM] |
| 2 | /FR | **Seite 0** | **Seite 1** (mit fallender /STR-Flanke übernommen) | [S502] `ReadTrack` |
| 3 | /STR | interne Steuerung gesperrt, Marken-FF zurückgesetzt | **frei** (Lesen läuft) | [SH], [S502] |
| **4** | **MK1** | **/MK1 = 0: ständig 1 in das Schieberegister** (Spülen) | **Lesedaten ins Schieberegister** | [SH], [S502] (`DetectMark`: `BBH`→`85H`/`87H`) |
| **5** | SD-MR | Schritt nach **höherer** Spur **und** Marken-FF gesperrt (/MR aktiv) | Richtung niedriger, Marken-FF scharf | [S502] (`BFH`/`3FH`/`BFH` = nach innen, `9FH`/`1FH`/`9FH` = nach außen, `85H`/`87H` = scharf) |
| 6 | /HL | Kopf abgehoben | **Kopf angelegt** | [SH], [S502] |
| 7 | /ST | **Schritt-Impuls bei 0** | — | [S502] |

**Der Plan (§3.5) schreibt „MK = 0: FM-Marken + C2; MK = 1: A1“ — das gilt für das Signal /MK
(= ROM-Adresse A8), nicht für das Portbit.** Am Port 04H Bit 1 gilt: **Bit 1 = 1 (`87H`) → FM-
Marken und C2; Bit 1 = 0 (`85H`) → A1.** Damit stimmt auch die Festlegung im K5122-Entwurf
(`doc/design/07_k5122_afs.md`, „MK = 0: A1, MK = 1: C2/FM-Marken, Handbuch /MK, am Port
invertiert“) und das Verhalten des A5120-Bootroms (`0x85` = MFM, `0x87` = FM). Die K5122-
Konfiguration „1715“ kann den Wahlschalter **unverändert** übernehmen.

Port 06H (Steuer-PIO B, Mode 3, E/A-Maske `E3H`; 1 = Eingang): B0 /RDYL, **B1 MKE**, B5 /WP,
B6 /FW, B7 /T0 sind Eingänge; **B2 /MFM (1 = MFM)**, B3 PRE, **B4 F0 (1 = 8″)** Ausgänge — die
Ausgabewerte des Urladers je Format: FM 8″ `10H`, MFM 8″ `14H`, FM 5,25″ `00H`, MFM 5,25″ `04H`.

## 4. Ablauf einer Markensuche beim Urlader (für das K5122-Modell)

`DetectMark` (`03E9H`): `OUT 04H,A5H` · `OUT 04H,BBH` · `IN A,(02H)` — danach das Steuerwort
`85H`/`87H` (+ Seite in Bit 2), und `IN A,(06H)` wird **gepollt, bis Bit 1 (MKE) = 1** (Zeitgrenze:
ID-Suche 8 × 255 Abfragen zu 41 Takten = 34 ms; **Datenmarke nur 79 Abfragen = 1,3 ms**).
Anschließend `IN A,(02H)` mit `C = 02H` (`IN A,(C)` bzw. `INI`): erstes Byte = Marke.
Das `IN A,(02H)` in `DetectMark` ist ein **Leerlesen**, das bei fehlendem Byte per `/WAIT` die CPU
anhält ([SH 1.5.3.6]: IN/OUT „obwohl seit dem letzten IN kein Byte übernommen“). Den Index
(`/ASTB` der Steuer-PIO A, Mode 0) braucht `WaitRDY` als Interrupt (IM 1, RST 38H): mindestens
**drei** Impulse in 2,31 Mio Takten (0,94 s), sonst gilt das Laufwerk als nicht bereit —
**jeder Impuls muss einen Interrupt auslösen, ohne dass dazwischen Port 04H geschrieben wird**
(Prüfpunkt für `Z80PIO` im Mode 0).

**Lücke 2 und Leselage** (folgt aus `ReadField`, gemessen am Code): nach dem ID-Feld liest der Urlader
**`(IX+3)` Lückenbytes** (5,25″ MFM 27, 8″ MFM 16, FM 10) ungenutzt weg und **armiert erst danach**
neu (`DetectMark` setzt MKE zurück). Das Datenfeld-Synchronbyte (A1) muss also **nach** diesen
`(IX+3) + 1` Bytes (das 7. gelesene ID-Byte ist schon das erste Lückenbyte) beginnen, aber
**innerhalb von 1,3 ms danach** liegen (≈ 41 Byte bei MFM 250 kbit/s, ≈ 20 Byte FM 8″, ≈ 81 Byte
MFM 500 kbit/s). Die Normlücken (22 × 4E + 12 × 00 = 34 Byte bei 5,25″ MFM) erfüllen das mit 6 Byte
Luft; **knappere Lücken als `(IX+3) + 1` lassen den Urlader am Datenfeld scheitern** (dieselbe
Eigenschaft wie beim A5120-Bootsystem, `BootIntegrationLuecke2.*`). Der Urlader **prüft die CRC**
von ID- und Datenfeld (CCITT, MFM mit A1A1A1-Präambel [LAUF]) — ein falscher CRC im Datenfeld
führt nach 96 Fehlversuchen zu `Shutdown` (Dauer-Halt).
