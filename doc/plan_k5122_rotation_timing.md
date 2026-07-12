# Umbauplan: K5122 auf rotationsgekoppelten realen Byte-Takt

**Ziel:** Der emulierte K5122 soll sich gegenüber *jedem* Programm/OS wie echte Hardware verhalten —
**ohne programm- oder OS-spezifische Sonderfälle**. Konkret: ein Programm, das die Laufwerksdrehzahl
misst (wie `INIT.COM` von SCPX, s. `doc/analyse_scpx_init_format.md` §7a), muss die reale Drehzahl
sehen: **K5601 = quarzgenaue 300 rpm, CPU = exakt 2,45 MHz** — unabhängig von CP/A oder SCPX.

Status: **Phase 1 umgesetzt + Boot taktrobust (Hauptrisiko gelöst).** Branch `scpx_boot`.
597/597 ctest + 58/58 Legacy-Harness grün.

> **★ Umgesetzt 2026-07-12 (Phase 1 + Boot-Robustheit):**
> - **Byte-Takt encoding-abhängig, rotationsgekoppelt:** `DriveProfile::bytePeriodCycles(enc, cpu_hz)`
>   aus der Datenrate (MFM 250 kbit/s → 78 Takte/Byte, FM 125 kbit/s → 156 @ 2,45 MHz), rpm-unabhängig.
>   Die boot-getunte Konstante `kBytePeriodCycles = 150` ist ersetzt (`K5122::currentBytePeriod()`).
>   Verhältnis Drehzahl↔Takt konstant, Datenrate FM vs. MFM unterschiedlich.
> - **Boot-Hänger @OS.COM gelöst — taktrobust, ohne Boot-/OS-Sonderfall.** Root Cause (per
>   boot-disasm-Analyse verifiziert): **keine** Datenkorruption (20/20 Software-CRC matchten), sondern
>   eine ZWEITE, nicht mitskalierte Konstante `kStrEndSampleCycles = 320` ("~2 Byteperioden" = 2×150),
>   die bei Periode 78 fälschlich ~4,1 Perioden bedeutete → /STR=1-Transfer-Ende zu spät → `head_pos_`
>   an der Sektorgrenze falsch übergeben → ZVE2 findet in der positionsbasierten MK1-Resync-Suche
>   (`0x1FA9–0x1FBF`) die FE-Marke nie → ZVE1 gibt nach 20 Retries auf. **Fix:** `strEndSampleCycles()
>   = 2 × currentBytePeriod()` (mitskaliert). Prinzip: JEDE „N-Byteperioden"-Konstante muss mit dem
>   Byte-Takt skalieren, nicht an die alte 150 gekoppelt sein.
> - **Golden-Snapshot-Tests nachgezogen** (taktabhängig, kein Aufweichen): `cli_bt_until`
>   PC 0x0168→0x0169, `cli_bt_savestate` 0x0169→0x016A.
> - **INIT-Messung vermessen (2026-07-12, k1520dbg live durch den SCPX-INIT-Dialog):**
>   INITs Drehzahl-Zählschleife (`OUT(14H)`, 2×/Iteration → `[12A6]`) läuft **auf ZVE2**
>   (dem DMA-Prozessor!), nicht ZVE1 — bestätigt per `b2 0x1005`. ZVE1 wartet derweil bei
>   `0x0F05`. Damit ist die Messung **bereits rotationsgepaced durch die bestehende Byte-Drossel**
>   (write_mode_ aktiv, ZVE2 pro Byteperiode ein OUT): Ist-Wert `[12A6] ≈ 4900` (≈200 Maschinen-
>   Takte/Iteration; ZVE2-Eigenzyklen nur 53/Iter, Rest = Drossel-Wartezeit). Ziel-Fenster 5219–5323
>   (Mitte 5271 ≙ ~187 Takte/Iter). **Gap ≈ 6 %** (200 vs 187) — reiner Modell-Overhead der Drossel.
>   ⇒ Ein **separates ZVE1-Datenport-Pacing (Phase 2) hilft INIT NICHT** (Messung ist ZVE2/bereits
>   gepaced) und wurde nach Test wieder entfernt.
> - Der `kWriteEndSampleCycles`/`kPostWriteStrEndCycles`-Schreibpfad blieb absolut (FORMAT-Tests grün).

## 7a. Doku-Recherche + empirisches Feinjustieren (2026-07-12) — INIT-Format nicht per Tuning lösbar

**K5122-Handbuch `doc/trascripted/Floppy Anschlußsteuerung K 5122.md` ausgewertet** — bestätigt die
physikalische Byteperiode:
- Schreib-/Lesetakt quarzgesteuert **1 MHz bzw. 500 kHz** (§4.4, Z.402/429), Grundquarz 10 MHz.
- **HF=1 = niedrige Frequenz (500 kHz)** für FM-8″ **und MFM-5″ (MFS/K5601)** — unser Laufwerk (Z.259/260).
- **Alle 16 Takte = 1 Byte** (Bitzähler A13, Z.427). ⇒ MFM-5″: 500 kHz / 16 = 31250 Byte/s = 32 µs/Byte
  = **78 CPU-Takte @ 2,45 MHz** — deckt sich exakt mit unserem `bytePeriodCycles(MFM)=78`.
- Betriebsart: Das Handbuch kennt ZWEI per Lötbrücke wählbare Synchronisationen (§5.6.3): **§5.6.1
  „DMA-Betrieb/Simultanarbeit"** (ZRE K2526 mit 2 CPUs, ZVE2 = programmgesteuerter DMA-Kanal; /BUSRQ
  aus dem Daten-PIO-RDY, durch /STR unterdrückt) und **§5.6.2 „mit /WAIT — *ohne* Simultanarbeit"**
  (eine CPU, zeitkritische Geräte). **Der A5120 nutzt §5.6.1 (/BUSRQ, Dual-CPU)** — genau unser Modell.
  Ein „/WAIT-Modell" wäre die *falsche* Betriebsart.

**K5601-Datenblatt (vom Nutzer beschafft):** Diskettendrehzahl **300 min⁻¹ ±2 %**, Übertragungsrate
**125 / 250 kbit/s** (FM/MFM), Kapazität unformatiert 1 MByte (MFM), Motorstart ≤500 ms, Kopfzustellzeit
0 ms, Kopfberuhigung 15 ms, Schrittzeit Spur/Spur 3 ms. ⇒ **300 rpm und MFM-Byteperiode 78 sind
datenblattbelegt korrekt** — die frühere „~360 rpm"-Hypothese ist damit **widerlegt**. (Werte in
`drive_profile.h` übernommen.)

**★INITs Fenster live nachgemessen (k1520dbg, `b 0x0E2E`):** Für unser Laufwerk lädt INIT
**`BC=1868H` (6248), `DE=7D` (125) → Fenster 6248–6373**, weil `(IX+0)=0xE5` → **Bit 7 = 1** den Default
`BC=1463H` (5219) bei `0x0E28` überschreibt. **Der frühere Analystenbefund „5219–5323" war falsch** (er
las nur den Default vor dem Bit7-Override). Ist-Wert `[12A6]≈4900` liegt weit unter 6248 → BAD DRIVE SPEED.

**Das erklärt alles — es ist Spacing vs. additiv, NICHT rpm oder Byteperiode:**
- Fenster **6248–6373 ≈ 490000/78 = 6282** = die **Spacing-Zählung** (ein `OUT` je Byte-Slot, Schleifen-
  Overhead *absorbiert*) für unser Laufwerk bei den korrekten Werten 300 rpm / 78 Takte.
- Unser Per-Byte-`/BUSRQ`-Drossel ist **additiv** (friert ZVE2 im Byte-Wait ein und *addiert* danach den
  Schleifen-Overhead) → `[12A6]≈4900 = 490000/(78+~22)`. Ein Byteperioden-Sweep bestätigt: der Wert ist
  **quantisiert** (Plateaus 4902/4904 → Sprung 5470 bei Periode 74) und trifft das Fenster nie.
- Reale /BUSRQ-HW (§5.6.1): der Byte-Slot ist ein freilaufendes Quarz-Raster; ZVE2s Nicht-Port-
  Instruktionen (INC/CP/JR) laufen innerhalb des Slots → Overhead **absorbiert** → **Spacing** → 6282.

**Fazit (korrigiert):** INIT erwartet für unser Laufwerk die **Spacing-Rate 6282**. Byteperiode (78),
Drehzahl (300) und Betriebsart (/BUSRQ §5.6.1) sind **korrekt** — die einzige Abweichung ist, dass unsere
Drossel den ZVE2-Schleifen-Overhead **addiert** statt ihn (wie die reale freilaufend-getaktete /BUSRQ-HW)
im Byte-Slot zu **absorbieren**. Der belastbare Fix ist daher **kein** /WAIT-Umbau und **keine** rpm-/
Byteperioden-Änderung, sondern die **Drossel auf ein freilaufendes Byte-Raster mit Overhead-Absorption
(Spacing)** umzustellen — innerhalb der bereits korrekten /BUSRQ-Betriebsart. Das berührt den geteilten
Byte-Takt (Boot-Neuvalidierung nötig), ist aber vermutlich boot-arm: der Boot-DMA nutzt `INIR` mit ~0
Per-Byte-Overhead, wo additiv ≈ spacing. Offen zu bestätigen: ob (IX+0)-Bit7=1 datenblattkonform für die
K5601 ist (Deskriptor-Quelle noch nicht abschließend lokalisiert) — falls ja, ist Spacing der ganze Fix.

---

## 1. Root Cause (verifiziert)

`INIT.COM` misst vor dem Formatieren die Drehzahl (Details + Adressen in
`doc/analyse_scpx_init_format.md` §7a):

1. Es zählt in einer Schleife (`0x0FF2`–`0x1012`), **wie viele `OUT(14H)`-Datenport-Schreibzugriffe
   in eine Index→Index-Periode passen**, und legt den Zählwert in `[12A6]` ab.
2. Es verlangt (Prüfung `0x0E19`–`0x0E36`) **`5219 ≤ [12A6] ≤ 5323`** für MFM (FM: `6248…6373`).
   Außerhalb → `BAD DRIVE SPEED`.

**Was stimmt schon:** Die Index-Periode ist exakt real —
`DriveProfile::indexPeriodCycles = cpu_hz·60/rpm = 2450000·60/300 = 490000 Takte`. Der Index-Puls
wird als `/ASTB`-PIO-Interrupt zugestellt, die Mess-Schleife **terminiert korrekt** (die Index-ISR
setzt `[12A8]`). Index-Timing und -Zustellung sind also **nicht** das Problem.

**Der einzige Defekt:** Datenport-Zugriffe (`OUT 14H` schreiben, `IN 16H` lesen) werden **nicht an
die reale Byte-Rate der drehenden Scheibe getaktet**.
- Auf echter HW hält der Controller die CPU bei jedem Datenport-Zugriff per **`/WAIT`** (bzw. über
  den `/BUSRQ`-DMA-Kanal) an, bis die Scheibe ein Byte-Slot weitergedreht hat (~78–93 Takte/Byte
  bei MFM). Die CPU kann also gar nicht schneller als die Rotation Bytes ein-/ausgeben.
- Unser Modell: der Z80/Run-Loop **honoriert `/WAIT` nicht** (`core/machines/a5120/a5120.cpp`
  `run()` steppt die CPU frei; `bus_.isWAIT()` wird nicht abgefragt). Direkte ZVE1-Portzugriffe
  laufen mit voller CPU-Geschwindigkeit → `[12A6]` viel zu groß → BAD DRIVE SPEED. Der einzige
  vorhandene „Pacing"-Mechanismus ist die **boot-getunte Per-Byte-/BUSRQ-Drossel**
  (`K5122::kBytePeriodCycles = 150`), die aber nur die ZVE1↔ZVE2-Arbitrierung im DMA-Pfad betrifft,
  nicht direkte CPU-Portzugriffe — und deren Konstante ist kein physikalischer Byte-Takt, sondern
  ein für den CP/A-Boot-Handshake abgestimmter Wert.

## 2. Warum das ein Architekturthema ist

Der heutige K5122 ist ein **transaktions-/sektornahes Modell**: er bildet die konkreten
Port-Nutzungsmuster des **CP/A-BIOS** ab (Pfadbytes, `/STR`-Strobes für den DMA, Per-Byte-/BUSRQ-
Drossel, Vollspur-FORMAT über `write_mode_`). Es fehlt die **physikalische Zeitbasis** der drehenden
Scheibe. Solange nur das CP/A-BIOS bedient wird, fällt das nicht auf. `INIT.COM` (und potenziell
andere Fremd-OS/-Programme) verlässt sich dagegen auf die **rohe HW-Eigenschaft** „Datenport ist
rotationsgetaktet". Ein programmspezifischer Patch dafür wäre falsch (Nutzer-Prinzip); die Lösung ist,
die fehlende HW-Eigenschaft **generisch** nachzurüsten.

## 3. Zielmodell — ein rotationsgekoppelter Byte-Takt + CPU-Pacing

**Kernidee:** Ein **einziger Byte-Takt** pro selektiertem Laufwerk, hart an die Rotation gekoppelt:

```
byte_period_cycles = indexPeriodCycles / bytes_per_revolution(enc)
```

wobei `bytes_per_revolution` aus dem realen Datentakt des Verfahrens folgt (MFM 250 kbit/s → ~6250
Rohbytes/Umdrehung @ 300 rpm; FM halbe Rate). Aus diesem Takt ergibt sich **automatisch** die von
`INIT` gemessene Byte-Zahl je Umdrehung; die Konstante `kBytePeriodCycles = 150` entfällt.

**Alle Datenport-Transfers werden an diesen Takt gebunden** — egal welche CPU (ZVE1 direkt **oder**
ZVE2 im DMA) und egal welches Programm:

- **Lesen (`IN 16H`)**: ein Byte steht erst bereit, wenn der Byte-Takt es freigibt; ein früherer
  Zugriff stallt die CPU (bzw. liefert „nicht bereit" + `/WAIT`).
- **Schreiben (`OUT 14H`)**: der Controller nimmt erst nach Ablauf des Byte-Takts das nächste Byte
  an; ein früherer Zugriff stallt die CPU.

Das ist exakt das reale `/WAIT`-Verhalten und ersetzt sowohl die ZVE2-Drossel als auch die fehlende
ZVE1-Pacing durch **eine** physikalische Regel.

### 3.1 Zwei Realisierungsvarianten des Stalls

- **(A) Echtes `/WAIT` im Z80/Run-Loop (bevorzugt, am treuesten).** Der K5122 assertiert `/WAIT`
  (`bus_.assertWAIT()`), solange der Byte-Takt den Zugriff noch nicht freigibt. Der Run-Loop prüft
  vor einem Datenport-Zugriff `bus_.isWAIT()` und **wiederholt die Instruktion nicht**, sondern lässt
  die Zeit (Floppy-Takt) voranschreiten, bis `/WAIT` fällt. Erfordert, dass der Z80-Portzugriff
  WAIT-States einlegen kann (heute nicht modelliert) — kleine, klar umrissene Kern-Erweiterung.
- **(B) Zyklus-Aufschlag im Port-Handler (pragmatisch, approximiert).** Beim Datenport-Zugriff meldet
  der K5122 dem Run-Loop, wie viele Warte-T-States bis zum nächsten Byte-Slot fehlen; der Run-Loop
  schlägt sie auf `used` auf (die CPU „verbraucht" die Wartezeit). Kein Z80-Kernumbau, aber die
  Instruktions-Granularität ist gröber (ganze Instruktion statt echter T-State-Stall).

> Empfehlung: mit **(B)** als Machbarkeits-/Kalibrier-Vehikel starten (schnell, risikoarm,
> reversibel), dann bei Bedarf auf **(A)** heben. Beide nutzen denselben Byte-Takt aus §3.

## 4. Phasenplan (jede Phase hält den CP/A-Boot grün)

Regressions-Leitplanke: nach **jeder** Phase `tools/dev.sh test` (inkl. `test_boot_integration`,
`test_k5122`, `test_k2526`) grün + `boot_trace --until PC==0xE079` (SCPX) und der CP/A-Boot müssen
weiter bis zum Prompt laufen. Der Boot-Handshake ist an das Byte-Timing getunt
([[project_per_byte_busrq_model]], „gehaltener Bus" [[project_scpx_com_load_bug]]) — **das ist die
eigentliche Schwierigkeit**, nicht der Byte-Takt selbst.

- **Phase 0 — Messgerüst.** Reproduzierbarer k1520dbg-Ablauf, der `[12A6]` beim ersten Erreichen von
  `0x0E19` ausliest (Logpoint), plus ein Mikrobenchmark, das den heutigen `[12A6]`-Wert dokumentiert
  (Ist: außerhalb `5219…5323`). Referenz für alle folgenden Phasen. Dazu ein GoogleTest, der eine
  reine Datenport-Schreib-Schleife über eine Index-Periode zählt (ohne vollen Boot).

- **Phase 1 — Byte-Takt aus dem DriveProfile ableiten.** `bytes_per_revolution(enc)` bzw.
  `bytePeriodCycles(enc, cpu_hz)` in `DriveProfile`/`FloppyDriveV2` einführen (MFM/FM getrennt),
  `kBytePeriodCycles` durch diesen Wert ersetzen. **Kalibrieren**, bis `INIT`s `[12A6]` in `5219…5323`
  (MFM) fällt — und **gleichzeitig** der CP/A-Boot grün bleibt. Erwartungswert ~78–93 Takte/Byte;
  der genaue Wert folgt aus INITs Schleifen-Overhead (empirisch aus Phase 0).

- **Phase 2 — Datenport-Pacing für ZVE1 (Variante B).** `IN 16H`/`OUT 14H` im K5122 an den Byte-Takt
  binden: Zugriff vor dem Slot → Warte-T-States zurückmelden, Run-Loop schlägt sie auf. Damit sieht
  INITs ZVE1-Mess-Schleife die reale Rate. **Nur** der Datenport wird gepaced (Status-/Steuerports
  bleiben ungetaktet). Ziel: INIT-Format läuft bis `FORMATTING COMPLETE`.

- **Phase 3 — ZVE2-DMA auf denselben Takt vereinheitlichen.** Die bestehende Per-Byte-/BUSRQ-Drossel
  durch den Byte-Takt aus Phase 1 ersetzen, sodass Boot-DMA **und** INIT-Format denselben
  physikalischen Takt nutzen. Delikat: der Boot-Handshake ([0x03F8]=3-Watcher, /STR=1-Abtastung) ist
  an das alte Tempo gewöhnt → hier ggf. iteratives Nachjustieren, sonst Phase 3 vertagen (Phasen 1–2
  liefern INIT-Format bereits, Phase 3 ist Konsolidierung).

- **Phase 4 (optional) — echtes `/WAIT` (Variante A).** Z80-Portzugriff WAIT-fähig machen, K5122
  assertiert `/WAIT` statt Zyklus-Aufschlag. Höchste Treue, entkoppelt das Timing von der
  Instruktions-Granularität. Nur, wenn (B) sich als zu grob erweist.

- **Phase 5 — INIT-Format end-to-end + Regressionstest.** INIT formatiert eine leere B: (alle 5
  M5-Formate), Verifikation (`disk verify`, DIR/STAT/PIP). GoogleTest `ScpxIntegration.InitFormatsDriveB`
  analog `BootThenDirStatPipLoadComFiles`.

## 5. Risiken & offene Punkte

- **Boot-Timing-Kopplung (Hauptrisiko).** `kBytePeriodCycles=150` ist boot-getunt; `80` brach schon
  einmal den CP/A-DMA-Snapshot ([[project_scpx_boot]]). Ein physikalisch korrekter, kleinerer
  Byte-Takt kann den Boot-Handshake stören → Phase 3 evtl. mit Kompatibilitäts-Schicht (Boot nutzt
  weiter das getunte Tempo, nur der Datenport-Pacing-Pfad den physikalischen — sauber getrennt,
  nicht programmabhängig).
- **Genauer MFM-Byte-Takt vs. INIT-Schleifen-Overhead.** `[12A6]` ist keine reine Rohbyte-Zahl,
  sondern INITs `OUT(14H)`-Zählung inkl. Schleifen-Overhead. Der Zielwert (`5219…5323`) legt den
  effektiven Takt fest; Phase 0/1 kalibrieren ihn empirisch. FM-Zweig (`6248…6373`) separat prüfen.
- **`IN 16H`-Lesen ebenfalls pacen** (nicht nur Schreiben), sonst brechen andere Fremdprogramme, die
  die Leserate messen. Der Byte-Takt muss für beide Richtungen gelten.
- **Nur der Datenport** darf gepaced werden. Status-/Steuer-/Select-Ports (`10H/11H/12H/18H`) bleiben
  ungetaktet, sonst bricht die Laufwerkserkennung/Seek-Logik.
- **Kein `/WAIT` bei stehendem Motor / Spin-up:** der Byte-Takt gilt nur bei `motorAtSpeed(selected)`
  (wie der Index) — sonst kein Slot, kein Pacing.

## 6. Betroffene Dateien (Erstschätzung)

- `core/peripherals/floppy_drive/drive_profile.h` — `bytePeriodCycles(enc,cpu_hz)` / Datenrate.
- `core/cards/k5122/k5122.{h,cpp}` — Byte-Takt-Zustand, Datenport-Pacing (`ioRead 0x16`/`ioWrite 0x14`),
  Ablösung von `kBytePeriodCycles`.
- `core/machines/a5120/a5120.cpp` `run()` — Warte-T-State-Aufschlag (Variante B) bzw. `/WAIT`-Abfrage
  (Variante A).
- `core/primitives/z80.cpp` + `core/bus/*` — nur bei Variante A (WAIT-fähiger Portzugriff).
- `tests/cpp/test_k5122.cpp`, `tests/cpp/test_boot_integration.cpp` — Byte-Takt- + INIT-Format-Tests.

## 7. Definition of Done

- `INIT B: 0` formatiert eine leere DD-DS-16×256-Diskette bis `FORMATTING COMPLETE` / `BAD TRACKS:
  - NO -`; danach DIR/STAT/PIP auf B: ok (Analyse §7a-Tabelle, jetzt via INIT statt `mk_blank`).
- `INIT`s `[12A6]` liegt in `5219…5323` (MFM) — **weil** der Byte-Takt real ist, nicht durch einen
  INIT-spezifischen Zweig.
- CP/A- **und** SCPX-Boot weiter grün; volle Testsuite grün.
- Keine programm-/OS-abhängige Verzweigung im K5122-Datenpfad.

Verweise: `doc/analyse_scpx_init_format.md` (INIT-Dialog + Root Cause §7a),
`doc/design/07_k5122_afs.md` (Portmodell/Statusbits), `doc/K1520_architecture.md` §8.5/§14.5.
[[project_scpx_init_format]] [[project_per_byte_busrq_model]] [[project_scpx_com_load_bug]]
[[project_fm_mfm_faithful_readpath]]
