# P8000 — WDC-Firmware und WDC-Hardware: Befund für die Emulation (AP P13a)

Stand 2026-10-06, AP P13a (read-only). Ergänzt `hw_wdc.md` (Handbuch) um das, was dort fehlt:
E/A-Karte, Takt, Hostprotokoll, Kommandos, PAR/BTT, Spurformat.

**Sicherheitsgrade:** **[gelesen]** = steht so in der Quelle; **[abgeleitet]** = aus mehreren
Stellen gefolgert/nachgerechnet; **[unsicher]** = plausibel, nicht belegt.

## 0. Quellen (alle außerhalb des Repos, SHA-256)

| Kürzel | Datei | Herkunft | SHA-256 |
|---|---|---|---|
| **FW** | `~/projects/robotron/P8000/src_github/wdc.firm.s` (4098 Z.) | github.com/OlliL/P8000 `firmware/WDC/` | — (Git, master) |
| README | `…/src_github/README` (Firmware-Bau, CRC-Ablage) | dto. | — |
| **MON16** | `…/src_github/p.disk.s` (MON16 3.1, Host-Seite) | `firmware/MON16/` | — |
| **KERN** | `…/src_github/disk.s`, `md.c` (WEGA-Kern 3.2, Treiber) | `WEGA/src/uts/dev/` | — |
| **SAF** | `…/src_github/sa.format.c` | `WEGA/src/cmd/standalone/` | — |
| **AVR** | `…/src_github/avr/wdc_if_p8000.{c,h}`, `wdc_config.h` | github.com/OlliL/P8000_WDC_Emulator | — |
| WEGA31 | `~/projects/robotron/P8000/WEGA_3.1.20160725.zip` | pofo.de `/P8000/misc/` | `e56abf077f0baa3a26022ab466db0ba926ce355f2b9b5c233beda8acdad8a157` |
| | darin `WEGA_3.1.20160725.dd` (127 180 800 B) | entpackt nach `…/P8000/wega31/` | `a76ef89aa525f1c2fcad8419c2df22159ce5889e870a5832d559c70a06373d30` |
| **SP1/n** | `…/P8000/wdc_plaene/Index_1/Stromlaufplan_0n.tiff` (WDC Index 1, Bl. 1–9) | pofo.de `/P8000/notes/plaene/Winchester-Disk-Controller/Index_1/` | Bl.1 `4c219c81…`, 2 `ed103953…`, 3 `b09b02d9…`, 4 `63ecb9a2…`, 5 `6871a6a3…`, 6 `cab890e5…`, 7 `f40c0744…`, 8 `afc47f9d…`, 9 `9af83431…` |
| SL1 | `…/Index_1/Stueckliste_WDC_P8000.ods`, `Stueckliste.pdf` | dto. | `b59aae55…`, `536a192f…` |
| **SP3/n** | `…/P8000/wdc_plaene/Index_3/S0n.jpg` (WDC Index 3, Bl. 1, 2, 4, 7, 8, 9 — 3, 5, 6 fehlen auf pofo.de) | `…/Index_3/` | 1 `28df72c6…`, 2 `b13aeb7f…`, 4 `7622aa12…`, 7 `6faa1fac…`, 8 `387e7b77…`, 9 `87110842…` |
| DRAM | `…/P8000/wdc_plaene/DRAM/Stromlaufplan.tiff`, `Stueckliste.pdf` | pofo.de `/P8000/notes/plaene/Dynamischer-RAM/` | `adf14043…`, `11fdfe70…` (nur geladen, für P13 nicht gesichtet) |
| EPROM | `doc/p8000/eproms/WDC/WDC_{1,2}_4.2`, `eprom_diffs.txt` | Repo | `doc/p8000/eproms/SHA256SUMS.txt` |

**Gegenprobe Quelle ↔ Abzug 4.2 [gelesen]:** die vier CRC-Wörter am Ende von `WDC_2_4.2`
(`2C43 D3BC 2C03 D3FC`) stimmen mit README §3.5 überein; die Texte `PARMTR`/`DEFEKT` und die
Versatztabelle `01 0A 02 0B 03 0C …` stehen in `WDC_2_4.2` (Offs. 1973/1979/2007), die Ports
`OUT (18h)`/`IN (88h)`/`OUT (70h)` kommen im Abzug vor. **Abweichung:** die Kennung heißt im
Abzug `"WDC_4.2 "` (`18 08 …`), in der Quelle `'WDC_4.21'` — die Quelle ist also ein
leicht anderer Stand als der Abzug [gelesen]; für P13c gilt der **Abzug**, die Quelle dient zum
Verstehen. Ports und Protokoll gelten für alle Stände (3.x-Abzüge nutzen dieselben `OUT (18h)`,
`IN (88h)`, `OUT (70h)` [gelesen, Bytesuche]).

**Wichtiger Unterschied der Stände [gelesen]:** bis **4.0** sind die Laufwerksparameter im EPROM
eingebrannt (`eprom_diffs.txt`: Köpfe, Zylinder, Vorkompensation je `_04`/`_05`); **4.2 ist
laufwerksunabhängig** und liest die Parameter (**PAR**) zusammen mit der BTT aus Sektor
Z0/K0/S1 der Platte (FW Z. 563–716; `PARMTR` fehlt in den 3.x-Abzügen). Deshalb heißt der
Abzug nur `WDC_x_4.2` ohne Laufwerkskennung.

## 1. Takt [Index 3: gelesen; Index 1: abgeleitet]

- **Index 3 (SP3/8 „Taktgenerierung und -verteilung"):** Quarz **40 MHz** (C1, Oszillator mit
  KT326), Teilerkette 74S112 → C(25 ns)=40 MHz, C(50)=20 MHz, C(100)=10 MHz, C(250)=**4 MHz**;
  Am8127 (d20) als Z80-Taktgenerator/Reset/Wait. ⇒ **WDC-Z80 und CTC: 4,0 MHz.** Die
  Datenrate wird über S2–S0 gewählt (Tabelle auf SP3/8: `S2=0` Winchester; `S2=1` St. MFM,
  St. FM/Mini-MFM, Mini-FM) — die Karte könnte auch Disketten-Datenraten; die Firmware 4.2
  benutzt nur Winchester [gelesen: Tabelle; FW setzt Bit 6 von DSKC2 nie].
- **Index 1 (SP1/2, SL1):** Quarz **16 MHz** an Am8127 (d20) → ZCK; Stückliste dazu 13,824 MHz
  (c2, Zweck nicht gesichtet). 16 MHz/4 = 4 MHz [abgeleitet; Am8127 teilt durch 4].
- **Firmware bestätigt 4 MHz** [abgeleitet, Nachrechnung]: `time`/`time1`/`time2` „4MHz-CPU,
  Wait-Zyklen" (FW Z. 3901–3942); Zeitüberwachung CTC3 Zeitgeber/256 mit `20h` = „ca. 2,0 ms"
  (32·256/4 MHz = 2,05 ms, FW Z. 1934–1937); `dis_1` CTC1 256×230 = 14,7 ms = „fast eine
  Umdrehung" (FW Z. 2311–2320). Die PAR-Felder **`ztk_40`/`ztk_41`, `zmn/zmx_40/41`**
  unterscheiden zwei Bestückungen **40 MHz und 41,4 MHz** (CPU 4,0 bzw. 4,14 MHz): die Firmware
  misst beim ersten Formatieren eine Umdrehung mit CTC1 (Zeitgeber 256×256, 8 Messungen,
  Mittelwert, FW Z. 3021–3101) und wählt danach die Zeitkonstante. Nachrechnung 3600 U/min:
  16,67 ms·4 MHz = 66 667 Takte ⇒ Rest nach 65 536 = 1 131 Takte ≈ 4,4 Ticks ⇒ Zählerstand ≈ **251**;
  bei 4,14 MHz ≈ **242**. SAF Z. 40: K5504.50 `zmin40/zmax40 = 251/253`, `zmin41/zmax41 = 241/243` ✓.
- **Wait-Zyklen:** die Kommentare nennen Wait-Zyklen; SP1/2 zeigt einen „Waitgenerator"
  (Am8127 WAIT). Wie viele und bei welchem Zugriff **[unsicher]** — für die Emulation unkritisch,
  solange die Zeitschleifen nur Wartezeiten sind (Bereitschaft, Schrittzeit).
- **Datenrate Platte:** ST506 5 Mbit/s MFM; Bitzellentakt 10 MHz = C(100) [abgeleitet]. Pro
  Umdrehung ≈ 10 416 Byte, **1 Byte ≈ 1,6 µs ≈ 6,4 CPU-Takte** [abgeleitet].
- **Empfehlung `Config::taktwdc_hz = 4'000'000`** (Entwurf §10.2 „Annahme 4 MHz" ist damit belegt).

## 2. Speicherkarte der WDC-Z80 [gelesen FW Z. 31–240, abgeleitet aus b_daz]

| Bereich | Inhalt |
|---|---|
| 0000–0FFF | EPROM 1 (2732) |
| 1000–1FFF | EPROM 2 (2732); 1FF8/1FFA = CRC EPROM 1 und Komplement, 1FFC/1FFE = CRC EPROM 2 (über 1000–1FF7) und Komplement |
| **2000–2FFF** | RAM 4 KB = **Sektorpuffer**, „rund laufend" (Adressbit 12 per `RES 4,H` gelöscht) |
| **3000–37FF** | RAM 2 KB = Arbeitszellen, Stack (`SP = 3081h`), ISR-Tabelle 30A0–30A7, Kommandobereich 30B7–30BF, Fehlerbyte 30C7, PAR 30D0–30F9, BTT je LW 3204/3281/32FE (je 2 + 120 + 3 B), Kennfeldbereich 3600–378F |
| 3800–3FFF (und Spiegel mit A15/A14/A9/A8 beliebig) | **keine Speicherzelle: Schreiben lädt einen Adresszähler** (s. u.) |

- RAM gesamt 6 KB = 12 × U214 (1K×4) [gelesen SL1, hw_wdc §3.1]. Nach Reset füllt die Firmware
  2000–37BF mit **81h** (FW Z. 307–311), rettet vorher Zugriffs-/Fehlerzähler nach 37C0–37FB
  (überleben also einen Host-Reset, nicht das Einschalten).
- **Adresszähler-Laden durch Speicherschreiben** (`b_daz`, FW Z. 3886–3899; Host-Zähler in
  `p_h_1`, Z. 3739–3756): eine Pufferadresse a (13 Bit, 0000–17FF relativ zu 2000h) wird so in eine
  Schreibadresse umgesetzt: A7–A0 = a7–a0, A9–A8 = a9–a8, **A15–A14 = a11–a10**, A13–A10 = Kennung
  **`1111` = Disk-Adresszähler**, **`1110` = Host-Adresszähler**; a12 kommt nicht aus der Adresse,
  sondern aus dem Steuerport (DA12 bzw. HA12, §3). `LD (HL),A` mit dieser Adresse lädt den Zähler;
  das Datenbyte ist bedeutungslos. SP1/3 bestätigt: Host- und Disk-Adresszähler (10 × 74LS193),
  Adressmultiplexer (74LS257), Adresskomparator, RAM-Steuerung [gelesen: Blattlegende].
- **Interruptvektoren** [gelesen FW Z. 314–335]: `I = 30h`, IM 2, CTC-Vektor `A0h` ⇒ Kanal 0 → (30A0)
  `isr_mk` (Marke), 1 → (30A2) `isr_dk` (DEND/Zeitgeber), 2 → (30A4) `isr_hs` (Host), 3 → (30A6)
  `isr_ix` (Index/Zeitüberwachung). Die Firmware arbeitet fast nur mit `HALT` + Interrupt und
  **verbiegt Rücksprungadressen in ISRs** (`isr_m9`, `isr_zt`) — die CPU-Emulation muss
  `HALT`/IM 2/`RETI` exakt können.

## 3. E/A-Karte der WDC-Z80 [gelesen FW Z. 16–26; Bits abgeleitet aus der Benutzung]

Dekodiert über A6–A4 (Basis x8h), CTC über 70h–73h.

| Port | Baustein | Richtung | Funktion |
|---|---|---|---|
| **08h** `DSKEA` | 8282 | OUT | **Disk-Endadresse**: Vergleichswert für das Low-Byte des Disk-Adresszählers ⇒ Impuls **/DEND**; die unteren 3 Bit nennen zugleich die **Markenanzahl** (FW Z. 1915–1921, 3457) |
| **18h** `CNTST` | 8212 (CLR an Reset) | OUT | Steuer-/Statusport, s. u. |
| **28h** `BM_T` | — | OUT | Taktbitmuster der Marke (Lesen: `0Ah`) |
| **38h** `BM_D` | — | OUT | Datenbitmuster der Marke (Lesen: `A1h`); beim **Schreiben** Steuerwort `04h` = „MFM, PRAEOFF, eine Marke", `0Ch` = „MFM, PRAEON" (FW Z. 2704, 2720, 3377–3392) |
| **48h** `DSKC1` | 8282 | OUT | Laufwerkssteuerung 1, s. u. |
| **58h** `DSKC2` | 8212 | OUT | Laufwerkssteuerung 2 / Disk-Schnittstelle, s. u. |
| **68h** `IMPAUS` | — | OUT (Strobe) | Disk-Schnittstelle aus (Abschaltimpuls) |
| **78h** `IMPATV` | — | OUT (Strobe) | Impuls /ATV „Anzeige Takt vorwärts" (Beisteller-Anzeige; in 4.2 unbenutzt) |
| **88h** `ST_PRT` | DL541 | IN | Statusport; **Lesen löscht ein CRC-Fehlerflag** (FW Z. 1893) |
| **70h–73h** | UA857 CTC | E/A | Kanal 0–3 |

**CNTST (18h)** [Bits abgeleitet aus FW Z. 1896–1900, 2723–2726, 3696–3738, 3831; Signalnamen SP1/1]:
Bit 2–0 = **Hoststatus ST2–ST0** (§4); Bit 3 = **HEN** (Hostschnittstelle frei); Bit 4 = **HA12**;
Bit 5 = **HR/W** Richtung Host (1 = WDC-RAM → Host); Bit 6 = **DA12**; Bit 7 = **DR/W** Richtung
Platte (1 = RAM → Platte, Schreiben). Nach Reset 00h (8212-CLR).

**DSKC1 (48h)** [FW Z. 386–389, 511–514, 1564–1574, 1592–1598, 2368–2377]: Bit 0 = **STEP**
(Impuls durch zwei OUTs), Bit 3 = **Richtung** (1 = nach innen), Bit 7–4 = **Kopfauswahl
HS3–HS0** (16 Köpfe adressierbar); Bit 1/2 „Motor"/„HL" laut Kommentar, in 4.2 nie gesetzt [unsicher].

**DSKC2 (58h)** [FW Z. 376–432; Bit 6 aus SP3/8-Tabelle]: Bit 0 = **DEN** (Datenübertragung
Platte↔RAM aktiv), Bit 1 = **/MEN** (Markenerkennung bzw. -einblendung, low-aktiv), Bit 2 = **CRCEN**
(CRC-Prüfung/Einblendung), Bit 3 = **WG** (Write Gate), Bit 5–4 = **Laufwerk** `01`/`10`/`11` =
LW 0/1/2 (`00` = keins), Bit 6 = **S2** (0 = Winchester-Datenrate), Bit 7 = **FR** Fault Reset.
Die Firmware bildet daraus feste Steuerbytes (`cl_crc`=x7, `st_ina`=x2 alles aus, `st_akt`=x5
Markensuche, `st_min`=x7 CRC-Test, `st_dak`=x3 langer Transfer, `st_vb`=x4, `st_wa`=xD,
`st_wm`=xB, `st_wc`=xF; x = Laufwerksbits).

**ST_PRT (88h)** [FW Z. 468–470, 778–790, 1972–1974, 2041–2043, 2797–2799]: Bit 2 = **/READY**
(0 = bereit), Bit 3 = **/SEEK COMPLETE** (0 = fertig), Bit 5 = **/WRITE FAULT** (0 = Fehler),
Bit 6 = **/TRACK 0** (0 = auf Spur 0), Bit 7 = **CRC gut** (0 = CRC-Fehler); Bit 0, 1, 4 in 4.2
unbenutzt [unsicher: vermutlich Index, Drive Selected].

**CTC-Kanäle** [FW, Trigger-Eingänge SP1/2 gelesen für Kanal 1 = /DEND, 3 = IX]:
- **K0** Zähler auf **MAERK** (Marke erkannt), ISR `isr_m1/m2/m7/m9`.
- **K1** Zähler auf **/DEND** (Endadresse erreicht; Zeitkonstante 1 oder 2) bzw. Zeitgeber (dis_1, Umdrehungsmessung).
- **K2** Zähler am **Host-Adresszähler Bit 0 (HA0)** — zählt je zwei Hostbytes eine Flanke; Flanke je nach Parität der Endadresse (FW Z. 3757–3804), > 256 Zählimpulse über Zwischeninterrupts `isr_h1`.
- **K3** Zähler auf **IX (Index)** bzw. Zeitgeber für Zeitüberwachung (≈2–3 ms) und für den Abstand der Kennfelder beim Formatieren (VT16 × `ztk`).


## 4. Hostprotokoll

### 4.1 Signale und Polaritäten

| Signal | WDC-Seite | Host-Seite (16-Bit-PIO2, `schaltplan_16bit.md` §6) |
|---|---|---|
| D0–D7 | Hostdatenpuffer 2 × 8282 je Richtung (SP1/1) | Port A, über DS8286, Richtung = B6 |
| ST0–ST2 | CNTST Bit 0–2 | B0–B2, **invertiert** (Host liest `NOT status`, KERN `comb rl1; andb #7`) [gelesen] |
| TE− | Eingang | B6: 1 = Lesen (WDC → Host), 0 = Schreiben; zugleich Treiberrichtung [gelesen KERN/MON16] |
| TR− | Ausgang („Transfer Request") | B7 Eingang [gelesen] |
| WDARDY− / ASTB− | Byte-Handshake an PIO-A-ARDY/ASTB | Port A Modus 0 (`0Fh`, Ausgabe) bzw. Modus 1 (`4Fh`, Eingabe) [gelesen KERN] |
| RST | Index 0/1 low, Index 3 high | B5 = 1 ⇒ WDC im Reset; Pull-up ⇒ nach Host-Reset bleibt WDC im Reset [gelesen] |

**Statuswerte** (WDC-intern = was der Host nach `NOT` sieht) [gelesen FW `p_h_wc`/`p_wc_h`-Aufrufe; AVR `wdc_if_p8000.h`; MON16]:

| Wert | Bedeutung | gesetzt in FW |
|---|---|---|
| 0 | besetzt / in Arbeit (auch nach Ende eines Transfers, `isr_h0`) | Z. 3831 |
| 1 | **bereit zum Kommandoempfang** (9 Byte) | `begin`, Z. 1309–1312 |
| 2 | **bereit zum Datenempfang** (Host → WDC) | Z. 1396, 3598, 3612, 3622 |
| 3 | **bereit zum Datensenden** (WDC → Host) | Z. 1721, 1829, 3281, 3587, 3605, 3665, 3674 |
| 6 | **EPROM-Prüfsummenfehler**, danach `HALT` (kein Fehlerbyte) | Z. 283–286 |
| 7 | **Fehler**: genau 1 Byte Fehlercode folgt | `error`, Z. 3871–3879 |

### 4.2 Ablauf und Hardwarebeteiligung [gelesen FW Z. 3678–3841; abgeleitet für die Byte-Ebene]

Der WDC überträgt **nicht per CPU-Schleife**: die Firmware lädt den **Host-Adresszähler**
(Speicherschreiben, §2), setzt in CNTST HA12, Richtung (Bit 5), **HEN** und den Status, und
programmiert **CTC K2** auf (Länge+1)/2 Flanken von HA0. Danach läuft jedes Byte in **Hardware**
zwischen Port A der Host-PIO und dem WDC-RAM (Adresszähler +1 je Byte). Ende = CTC-K2-Interrupt
`isr_h0`: HEN aus, Richtung aus, **Status 0**, „Treiber frei" (`hst_by`). Längen > 512 Flanken
über Zwischeninterrupts `isr_h1`.
- **WDC → Host überträgt die Hardware ein Byte mehr** („wird vom Host nicht abgeholt, nur zur
  Ready-Abschaltung PIO", FW Z. 3686). Der Host liest seinerseits zuerst ein **Scheinbyte**
  (`inb … !Scheineingabe!`, KERN `_mdint2`, MON16 `rww03`), das die PIO-Eingabe (Modus 1) anstößt;
  danach folgen die n Nutzbytes. Bei Fehlerstatus 7: Scheinbyte, dann das Fehlerbyte.
  Die AVR-Nachbildung macht es genauso: n Bytes + ein weiteres ASTB, TR während der Übertragung
  aktiv, Beginn erst nach TE aktiv (AVR `wdc_write_data_to_p8k`) [gelesen].
- **Host → WDC:** Status 1 bzw. 2, der Host schreibt mit `outb`/`otirb` auf PIO2-A (Modus 0);
  ARDY meldet „Byte da", der WDC holt es per ASTB ab (AVR `wdc_read_data_from_p8k`: wartet auf TE
  **inaktiv**, dann je Byte ARDY↑ → ASTB → ARDY↓) [gelesen AVR; die Index-3-Hardware macht dasselbe
  per Gatter, SP1/1].
- **Reihenfolge eines Lesekommandos** (KERN `_mdcmd`/`_mdint2`, MON16 `DISK`) [gelesen]:
  Host wartet Status 1 → 9 Kommandobytes → (MON16: wartet Status 0, dann ≠ 0) → Status 3 ⇒
  Port A auf Eingabe, B6 = 1, Scheinbyte, `inirb` Länge Bytes → Status 1 (fertig). Der Kern
  wartet dafür auf den PIO-Interrupt (Steuerwort `97h FEh`: Interrupt, wenn B0 low = Statusbit 0
  gesetzt ⇒ Status 1, 3 oder 7).
- **Schreibkommando:** 9 Bytes → warten auf Status 2 (B1 low) → `otirb` Länge Bytes → Status 0
  (Arbeit) → Status 1 bei Erfolg bzw. 7 + Fehlerbyte.
- **Fehler während der Initialisierung** werden erst **nach dem ersten Kommando** gemeldet, das
  dann **nicht** ausgeführt wird (FW Z. 1324–1360) ✓ Handbuch.
- **Zeitbedingung:** AVR wartet 200 µs vor Status 1 („für sa.format bei 28h/58h nötig") und
  70 ms vor einem Fehlerstatus (sonst „friert" sa.diags bei unformatierter Platte ein) [gelesen
  AVR Z. 151–153, 182–187]. Die echte Firmware ist ohnehin langsamer; der Emulator braucht keine
  Sonderwartezeit, wenn die Firmware echt läuft.

### 4.3 Kommandoblock (9 Byte, Kommandobereich 30B7–30BF) [gelesen FW Z. 1309–1528, KERN `_mdcmd`, SAF `rg_um`]

| Byte | Block-Kommandos (21/22/A1/A2) | Sektor-Kommandos (01/02/11/12/81/82/92/04/14/24/44/84) | RAM-Kommandos (x8) |
|---|---|---|---|
| 0 | Kommandocode | Kommandocode | Kommandocode |
| 1 | Laufwerk 0–2 | Laufwerk | WDC-RAM-Adresse low |
| 2–5 | **Blocknummer 32 Bit, LSB zuerst** | Zylinder low, Zylinder high, Kopf, Sektor (1–18) | 2 = RAM-Adresse high |
| 6–7 | **Länge in Byte, LSB zuerst** (Vielfaches von 512; ≤ 4 KB Puffer) | Länge | Länge |
| 8 | unbenutzt („Prüfsumme, nicht benutzt") | unbenutzt | unbenutzt |

**Block → Sektoradresse** (FW Z. 1461–1506) [gelesen]: **Block 0 = Zylinder 1, Kopf 0, Sektor 1** —
**Zylinder 0 ist für PAR/BTT reserviert und liegt außerhalb des Blockraums**. Zylinder =
Block / (Köpfe·Sektoren) + 1, Kopf = Rest / Sektoren, Sektor = Rest mod Sektoren + 1. Höchste
Blocknummer = (Zyl − 1)·Köpfe·Sektoren − 1 (K5504.50: 92 069), sonst Fehler 05. Danach
**Defektspur-Verschiebung** `sc_def`: jede BTT-Spur ≤ Zielspur schiebt die Zielspur um eine Spur
weiter (Spurschlupf); Mehrblockzugriffe springen über folgende Defektspuren (`l_inc`).
Mehrsektor-Transfers laufen über Spur- und Zylindergrenzen (Schritt nach innen, `step1`).

**Kommandocodes** (Tabelle `com_tb`, FW Z. 3986–4013; Bitbedeutung aus Z. 1382–1528) [gelesen]:
Bit 0 Lesen, Bit 1 Schreiben, Bit 2 Formatieren, Bit 3 WDC-RAM, Bit 4 mit BTT, Bit 5 Blocknummer,
Bit 6 PAR/BTT-Sektor, Bit 7 Rücklesen/Test.

| Code | Funktion |
|---|---|
| 00 | Ready-Test (Laufwerksprüfung, sonst nichts; `mv_ok`). Nicht in `com_tb` — die Suchschleife `com_ts` vergleicht aber `cp 0ffh` mit **A** statt mit der Tabelle und läuft daher über das Tabellenende hinaus, bis irgendein ROM-Byte passt; abgewiesen (Fehler 01) wird dort nur FFh, alle anderen unbekannten Codes erst in `mv_ok` [gelesen FW Z. 1375–1381; ob der Abzug dieselbe Schleife hat: **unsicher**] |
| **21** | Lesen ab Blocknummer (WEGA-Kern, MON16) |
| **22** | Schreiben ab Blocknummer |
| 01 / 11 | Lesen Sektoradresse ohne / mit BTT |
| 81 | Testlesen Sektor (255 Versuche, Statistik 8 Byte an Host) |
| 02 / 12 | Schreiben Sektoradresse ohne / mit BTT |
| 82 / 92 | dto. mit Rücklesen |
| A1 | Kopieren Block von LW n auf LW 0 |
| A2 | Schreiben Block mit Rücklesen |
| **C2** | PAR + BTT aus WDC-RAM nach Z0/K0/S1 schreiben, danach **Neustart der Firmware** (`jp entry`) |
| 04 | Spur formatieren |
| **14** | Spur formatieren + Rücklesen aller Sektoren |
| **24** | Spur formatieren mit Defektfeststellung: bis 2 Formatierversuche, je 1 Rücklesen; bei Misserfolg Eintrag in die BTT und **Status 3 mit 3 Byte (Zyl low, Zyl high, Kopf)** an den Host |
| 44 | Spur rücklesen (Verify) |
| 84 | Spur löschen |
| 08 / 18 | WDC-RAM lesen / schreiben (Adresse aus Byte 1–2) |
| **28** | WDC-Parameterblock lesen (ab 30D0: Kennung 8 B, PAR 34 B, Blockzahl 4 B, `d_rdy`, `s_rdy`, `err_in`, Laufwerksanzahl) |
| 38 | Fehlerstatistik lesen (ab 31AA) |
| 48 | BTT im WDC-RAM löschen |
| 58 / 68 | BTT WDC-RAM → Host / Host → WDC-RAM (max. 125 B) |
| **78** | Parameterblock schreiben (ab 30D8, PAR) |

`sa.format` benutzt 28, 78, 14, 48, 58, 68, 24, C2 und 08 (Statistik ab 37CA) [gelesen SAF
Z. 61–432]; es verlangt Firmware ≥ 4.1 (`db[4] < '4' && db[6] < '1'` ⇒ „Firmwareversion not ok",
SAF Z. 75) [gelesen] — **mit 3.x läuft `sa.format` der WEGA 3.x nicht**.

## 5. Fehlercodes und Statusbytes (Firmware 4.2) [gelesen FW Z. 1324–1360, 2163–2192, 3846–3867]

Die Handbuchtabelle 6.5-3 (`hw_wdc.md` §5.5) gehört zu einem **älteren Stand**; 4.2 vergibt:

| Code | Bedeutung 4.2 |
|---|---|
| 01 | unerlaubter Kommandocode |
| 02 | Kopfnummer zu groß |
| 03 | Laufwerk ≥ 3 oder nicht bereit |
| 04 | Zylinder zu groß (auch beim Weiterzählen im Mehrsektortransfer) |
| 05 | Blocknummer zu groß |
| 06 | Kopfposition unbekannt (Laufwerk nie auf Spur 0 gefahren) |
| 07 | Spur 0 angefordert, /TRACK0 nicht aktiv |
| 0A | richtiges Kennfeld nicht gefunden (Sektor nicht gefunden) |
| 0B | keine Marke in der Zeitüberwachung |
| 0C | Datenfeld fehlerhaft (kein FB oder CRC-Fehler im Datenfeld) |
| 10 | Write Fault |
| 11 | Fehler beim Rücklesen (Read after Write) — im Abzug ist die Zählschleife hinter `ret` toter Code (FW Z. 2581–2595) |
| 12 | falsche Sektornummer vor dem Schreiben |
| 15 | Umdrehungsmessung passt zu keiner Bestückung (Formatieren) |
| 16 | BTT voll (40 Einträge) |
| 17 | PAR im WDC-RAM ungültig (C2 abgewiesen) |
| 18 | BTT im WDC-RAM ungültig (58) |
| 27 | Init: kein Laufwerk bereit |
| 28+Bits | Init: Spur 0 nicht erreicht, Bit 0–2 = LW 0–2 |
| 30+Bits | Init: Z0/K0/S1 nicht lesbar, Bit 0–2 = LW |
| 38+Bits | Init: PAR oder BTT fehlerhaft, Bit 0–2 = LW |

**„Fehler 20" ist kein WDC-Fehlercode**, sondern die Meldung von **MON16** für **WDC-Status 6**
(EPROM-Prüfsummenfehler, Firmware hält an): MON16 `wrea1` „Status 6 → `ld r2,#%20 !Fehler 20!`"
[gelesen]. MON16-eigen ist ferner **C1** = Zeitüberschreitung beim Warten auf den WDC [gelesen].
Das passt zum Handbuch („20H = EPROM-Prüfsumme"). Der Fehler „CRC" im Sinne der Platte ist 0C
(Datenfeld) bzw. 0A nach erfolglosen Kennfeld-Versuchen.

Die Prüfsumme (FW `crc_br`, Z. 3947–3981) ist CRC-CCITT (0x1021) mit Startwert FFFF über
0000–0FFF bzw. 1000–1FF7, gespeichert **byteweise vertauscht**; Komplement-Test vorab [gelesen;
Polynom abgeleitet aus der Nibble-Tabellenform].

**`d_rdy`/`s_rdy`/`err_in`** (im Parameterblock, SAF Z. 78–95) [gelesen]: `d_rdy` Bit 0–2 LW bereit,
3–5 Spur 0 nicht erreicht, 6 Init-Fehler, 7 „erste Abfrage nach Init"; `s_rdy` Bit 0–2 PAR gültig,
3–5 BTT gültig; `err_in` Bit 0–2 Z0/K0/S1 unlesbar, 3–5 PAR/BTT fehlerhaft.

## 6. Initialisierung nach Reset [gelesen FW Z. 245–769]

1. SP setzen, **EPROM-CRC prüfen** (Fehler ⇒ Status 6, HALT).
2. Zähler retten, RAM 2000–37BF mit 81h füllen, IM 2, CTC zurücksetzen, ISR-Tabelle.
3. CNTST = 0 (Status 0 = besetzt), DSKC2 = 92h (FR + LW0) dann 12h, DSKC1 = 0.
4. Vorläufige PAR: 100 Zyl., 2 Köpfe, 18 Sektoren, Ramp 1.
5. **Bereitschaft:** LW 0 bis **32 s** warten (`ld b,30; inc b; inc b`, je 1 s), LW 1 und 2 je 2 s;
   Kriterium ST_PRT Bit 2 = 0.
6. **Spur 0:** je bereitem LW Schritte nach außen (Einzelschritt, je bis 400 ms auf SEEK COMPLETE),
   bis /TRACK0 = 0, höchstens 3000 Schritte.
7. **Z0/K0/S1 lesen** je LW, PAR prüfen und übernehmen (LW 1/2 müssen zu LW 0 passen), BTT prüfen und
   übernehmen.
8. Blöcke je Zylinder und je Laufwerk rechnen, dann `begin`: **Status 1**.

**Für den Emulator:** ohne Laufwerk ⇒ Fehler 27 beim ersten Kommando; mit Laufwerk ist der Weg bis
Status 1 kurz, wenn READY und SEEK COMPLETE sofort anliegen (die Bereitschaftsschleife bricht beim
ersten Treffer ab). Startzeit des echten Laufwerks (max. 15 s, `hw_wdc.md` §2) kann als
Konfiguration nachgebildet werden, muss aber nicht.

## 7. PAR/BTT-Sektor Z0/K0/S1 [gelesen FW `t_par`, `t_btt`, `v_par`, `par_v`, `wr_df`; SAF]

Lage: **Zylinder 0, Kopf 0, Sektor 1**, Datenfeld 512 B. Die Firmware liest ihn mit festem Kennfeld
`FE 00 00 00 01` (`id_df`, FW Z. 4042–4046), also **ohne** BTT-Umrechnung. Offsets im Datenfeld:

| Offset | Länge | Inhalt |
|---|---|---|
| 0 | 6 | `"DEFEKT"` |
| 6 | 2 | **Anzahl der BTT-Bytes** (LSB zuerst), Vielfaches von 3, ≤ 120 |
| 8 | 3·n | Einträge **Zylinder high, Zylinder low, Kopf** (Zylinder **big endian**!), aufsteigend nach (Zyl, Kopf), Zyl < `cyls`, Kopf < `hds` |
| 8+3n | 3 | Endekennung `FF FF FF` |
| … bis 130 | | Rest des 125-Byte-BTT-Bereichs (6 + 2 + 120 + 3 = ab Offset 6 werden 125 B übernommen) |
| **256** | 6 | `"PARMTR"` |
| 262 | 12 (+1) | Laufwerksbezeichnung ASCII 20h–7Ah, z. B. `"ROB K5504.50"`; 13. Byte beliebig |
| 275 | 2 | frei (beim Übernehmen 00) |
| 277 | 2 | Zylinderzahl (LSB zuerst), 100–2999 |
| 279 | 1 | Köpfe 2–16 |
| 280 | 1 | Sektoren je Spur 17 oder 18 |
| 281 | 2 | Zylinder der Vorkompensation (≤ Zylinderzahl) |
| 283 | 1 | `rp_mod` 1–21: Schrittzahl ≤ `rp_mod` ⇒ Einzelschritt mit Pause, darüber gepuffert („Ramp"); 1 = ab 2 Schritten gepuffert (FW Z. 1581–1589) |
| 284 | 6 | frei |
| 290 | 1 | `ztk_40` Zeitkonstante Kennfeldabstand bei 40 MHz, ≥ B0h |
| 291 | 1 | `ztk_41` bei 41,4 MHz, > `ztk_40`, ≤ F0h |
| 292–295 | 4 | `zmn_40`, `zmx_40`, `zmn_41`, `zmx_41` Grenzen der Umdrehungsmessung; `zmn_41 < zmn_40` |

Eine **Prüfsumme gibt es nicht** — die Gültigkeit ergibt sich allein aus den Kenntexten, der
Wertebereichsprüfung und der BTT-Ordnung [gelesen]. Fehlt/verletzt ⇒ Init-Fehler 30+/38+ beim
ersten Kommando; Lesen/Schreiben mit Blocknummer geht dann mit den **vorläufigen** Parametern
(100 Zyl./2 Köpfe) weiter — `sa.format` behebt das mit 78 + C2.

**Werte für K5504.50** (SAF Z. 40) [gelesen]: `"ROB K5504.50"`, 1024 Zyl., 5 Köpfe, 18 Sektoren,
Vorkompensation 1024 (= nie), Ramp 1, `ztk_40/41 = 203/209`, Grenzen `251/253`, `241/243`.
Weitere Typen: NEC D5126 (615/4/18, Vork. 128, Ramp 12, 203/209, 248/250, 239/241), NEC D5146
(615/8/…), ROBOTRON VS (820/6/18, 820, 1, wie K5504.50).

**BTT-Kapazität:** 40 Spuren je Laufwerk; `24h` trägt neue Defektspuren sortiert ein.

## 8. Spur- und Sektorformat, wie die Firmware es erzeugt [gelesen FW `ft_trk` Z. 3297–3530, `wrt_dt` Z. 2601–2925, `prae`/`post` Z. 4014–4040; Bytewerte gelesen, Reihenfolge auf der Platte abgeleitet]

**Formatieren einer Spur** (04/14/24):
1. **Löschen:** RAM 2000–2FFF = 00, WG ein, DMA RAM → Platte **über zwei Indeximpulse** (Spur voller 00).
2. **Kennfelder:** am Index beginnend schreibt die Firmware 18-mal (je CTC-K3-Zeitgeber `16·ztk`
   Takte, K5504.50 @4 MHz: 16·203/4 MHz = **0,812 ms ≈ 508 Byte** Abstand, 18 × = 14,6 ms von
   16,67 ms) ein Kennfeld aus dem Puffer 3600h:
   `FF ×18 · A1 A1 A1 (Marken) · FE · Zyl low · Zyl high · Kopf · Sektor · CRC CRC · FF ×10`
   (Marken ab DSKEA 12h, CRC-Einblendung ab DSKEA 1Ah). Zwischen den Kennfeldern bleibt das 00
   des Löschens stehen.
3. **Datenfelder:** RAM = E5h, dann wie ein gewöhnlicher 18-Sektor-Schreibauftrag ab Sektor 1
   (`wrt_dt`).

**Kennfeld (ID):** `A1* A1* A1* FE CL CH HD SC CRC1 CRC2` — **ohne Längenbyte N**, Zylinder 16 Bit
**LSB zuerst**, Sektor **1–18**. CRC-Generator wird vor den Marken auf FFFF gesetzt (`cl_crc`
„CRC-Gen./Checker auf ff") ⇒ CRC-CCITT über `A1 A1 A1 FE CL CH HD SC` [Polynom abgeleitet: Kette
aus 4 × Schieberegister + XOR auf SP3/7 „CRC-Generator/Checker"; Startwert gelesen]. Beim Lesen
vergleicht die Firmware **alle 5 Byte FE…SC** mit dem Soll (`tst_id`).

**Datenfeld** (beim Schreiben eines Sektors, nach Erkennen des eigenen Kennfelds in `isr_m2`):
`FF ×11 · A1* · FB · 512 Daten · CRC1 CRC2 · FF ×12` — **nur EINE A1-Marke** vor FB. Lücke 2
ergibt sich aus der Latenz `isr_m2` (Interrupt nach dem Kennfeld + 8 NOPs). Beim Lesen erwartet
die Firmware eine Marke, prüft `FB` (sonst 0C) und die CRC (Statusport Bit 7).

**Füllbytes:** Lücken FFh, Löschbyte 00h, Datenfüllung nach Formatieren **E5h** [gelesen].

**Sektorreihenfolge (Interleave 2:1)** `sc_tab` (FW Z. 4075–4095, im Abzug bei EPROM 2 Offs. 2007):
`1 10 2 11 3 12 4 13 5 14 6 15 7 16 8 17 9 18` [gelesen].
**Kopfversatz (Skew):** für Kopf h beginnt die Spur mit dem Eintrag `sc_tab[secs−h]`, d. h. die
Folge wird je Kopf um eine Position rotiert (FW Z. 3406–3426) [gelesen]. Beispiel Kopf 1:
`18 1 10 2 11 …`. Beim Schreiben sucht die Firmware das Kennfeld des **physischen Vorgängers**
(`sc_tab` rückwärts) und schreibt hinter dem nächsten passenden Kennfeld [gelesen Z. 2615–2631].

**Vorkompensation** ab Zylinder `pre_cy` (BM_D-Steuerwort 0Ch statt 04h) [gelesen]; für die
Emulation bedeutungslos.

## 9. Was die Hardware tut und was die Firmware — Ebene der Plattenemulation

**In Hardware** (SP1/3, SP1/6, SP3/7, SP3/8; Steuerung aus der Firmware abgeleitet):
- Seriell↔parallel, **MFM-Kodierung/-Dekodierung**, Datentrennung, Vorkompensation (SP3/7
  „FM/MFM-Encoder, Präkompensation", SP3/8 Takt).
- **Markenerkennung** mit programmierbarem Takt-/Datenmuster (BM_T/BM_D = 0Ah/A1h) und
  **Markeneinblendung** beim Schreiben (fehlender Takt); Markenanzahl aus DSKEA Bit 2–0.
- **CRC-Generator/-Prüfer** (diskret, SP3/7), Ergebnis als ST_PRT Bit 7; CRC-Einblendung beim Schreiben.
- **DMA Platte ↔ RAM** über den Disk-Adresszähler, **Endadressvergleich** (DSKEA → /DEND → CTC K1).
- **DMA Host ↔ RAM** über den Host-Adresszähler mit Byte-Handshake (HEN, CTC K2).
- Laufwerksauswahl, Kopfauswahl, STEP/DIR, WG, Fault Reset über Latches.

**In Firmware:** alles Logische — Kommandoauswertung, Blockumrechnung + BTT-Schlupf, Suchen des
Kennfelds (Vergleich im RAM), Wiederholungen/Fehlerzähler, Zeitpunkte des Ein-/Umschaltens der
Disk-Schnittstelle **per Interrupt und abgezählten Befehlen** (`isr_m1`: nach dem Kennfeld wird
die Schnittstelle sofort auf das Datenfeld umgeschaltet; `isr_m2`: 8 NOPs bis „MEN aus"),
Formatieren mit zeitgesteuertem Kennfeldabstand, Schrittsteuerung, Bereitschafts-/Spur-0-Prüfung.

**Folgerung für die Emulation (Empfehlung):**
1. **Nicht auf Bitstrom-/Fluss-Ebene** — die Firmware sieht nie Bits; MFM-Takt, PLL und
   Vorkompensation sind für sie unsichtbar.
2. **Nicht rein sektorweise „Kommando-HLE"** — der Projektgrundsatz verlangt die echte Firmware, und
   diese treibt die Disk-Schnittstelle Byte für Byte mit DMA-Zähler, Endadressvergleich, Marken-
   und CRC-Steuerung, verteilt über ISRs, deren Timing gegen die Spurdrehung läuft.
3. **Empfohlen: Byte-Strom-Ebene mit Markenkennzeichnung** (wie `TrackCodec`/K5122, aber einfacher):
   je Spur ein umlaufender Bytestrom (≈ 10 416 Byte/Umdrehung, 1 Byte je 6,4 WDC-Takte bei 4 MHz,
   Index am Spuranfang), jedes Byte mit Flag „Marke" (A1 mit fehlendem Takt). Die Karte modelliert
   nur die Hardware aus der Liste oben: bei DEN schreibt sie empfangene Bytes über den Disk-Adresszähler
   ins RAM bzw. liest sie von dort, MEN sucht die Marke und zählt MAERK, DSKEA erzeugt /DEND, CRCEN
   rechnet CRC-CCITT und setzt ST_PRT Bit 7, WG ersetzt Bytes in der Spur.
4. **Speicher sektorweise, Spur synthetisiert:** die Spur wird bei Bedarf aus dem Abbild **erzeugt**
   (Kennfeld aus Lage + Datenfeld aus der Datei, Lücken nach §8, Reihenfolge nach `sc_tab` + Kopfversatz)
   und nach dem Schreiben wieder **zerlegt** (Kennfelder/Datenfelder erkennen, Daten in die Datei).
   Eine Spur, deren Kennfelder nicht dem Standard folgen, braucht eine Ausnahme — `sa.format`
   schreibt aber nur Standard-Kennfelder (Kommando 04/14/24 mit Zyl/Kopf = Kopfposition), daher
   **keine `.idmap`** nötig [abgeleitet aus FW `ft_trk` + SAF Z. 408–418]; einziger Sonderfall
   „unformatiert" (vor `sa.format`) ⇒ ein Spurzustand je Spur genügt, nicht je Sektor. Diese
   Entscheidung (Entwurf §10.8) ist damit gefallen.
5. **Abbildlage:** Sektor (Z, K, S) mit **S = Sektornummer 1–18 aus dem Kennfeld**, nicht physische
   Lage: Offset `((Z·Köpfe + K)·18 + (S−1))·512`. Der Versatz (`sc_tab`, Skew) gehört in die
   Spursynthese, nicht in die Datei. **Entwurf §10.8 („S = 0…17 = physische Lage") ist so zu
   berichtigen.** Z0/K0/S1 = PAR/BTT liegt dann bei Offset 0.
6. **Zeit:** der Strom muss in WDC-Takten laufen (Index alle 66 667 Takte bei 3600 U/min), weil
   Zeitüberwachung (CTC K3 ≈ 2–3 ms), Umdrehungsmessung (Formatieren, Fehler 15!) und Kennfeldabstand
   daran hängen. `zmn/zmx` verlangen eine Umdrehung von 16,5–16,8 ms @4 MHz.

## 10. Zeitverhalten [gelesen/abgeleitet wie angegeben]

- **Schritt:** gepuffert („Ramp"), wenn Schrittzahl > `rp_mod` (K5504.50: ab 2 Schritten), Schrittimpulse
  direkt hintereinander, danach Warten auf **SEEK COMPLETE** (ST_PRT Bit 3) ohne Zeitgrenze
  (FW Z. 1607–1622); Einzelschritt mit 320 × `time` (~3,2 ms) Pause (FW Z. 1592–1605). Die
  Positionstabelle `cy_tab` führt die Firmware selbst — das Laufwerk meldet keine Position.
  ⇒ Emulator: SEEK COMPLETE nach einer Spurwechselzeit aktiv (z. B. 3 ms + n·Schrittzeit) oder sofort.
- **Spur 0:** /TRACK0 nur bei Zylinder 0 (Init und Zugriff auf Zylinder 0 prüfen das, Fehler 07).
- **Bereitschaft:** /READY beim Einschalten nach Startzeit; Init wartet bis 32 s (LW 0).
- **Index:** 1 Impuls je Umdrehung an CTC K3; Formatieren wartet auf den 1./2. Index.
- **Zeitüberwachung Markensuche:** CTC K3 Zeitgeber 256·30h ≈ 3,1 ms je Versuch; Sektorsuche bis
  40 Kennfelder (`sc_cnt`), Wiederholzähler `fe_n` = 4/8/4/4/4/4/4.
- **Kennfeld → Datenfeld:** die Umschaltung in `isr_m1` läuft in wenigen µs; die Spursynthese muss
  Lücke 2 so lang machen, dass die ISR (≈ 60–80 Takte ≈ 10–13 Byte) fertig ist, bevor A1/FB des
  Datenfelds kommen — mit der Firmware-eigenen Lücke (FF ×10 nach dem Kennfeld + FF ×11 vor dem
  Datenfeld) ist das gegeben [abgeleitet].
- **Host:** keine Wartezeiten nötig (AVR-Wartezeiten ersetzen nur die Langsamkeit der echten Firmware).

## 11. Das WEGA-3.1-Abbild von pofo.de [gelesen: Inspektion der Datei]

`WEGA_3.1.20160725.dd` (127 180 800 B = **248 400 Sektoren**) ist ein Abbild für den **AVR-WDC-Emulator**,
nicht für die echte Karte:
- Geometrie **1380 Zyl. × 10 Köpfe × 18 Sektoren** (= 248 400 ✓), lineare Lage `(Z·10+K)·18+(S−1)`;
  Daten beginnen bei Sektor 180 = **Block 0 = Zylinder 1** ✓ §4.3.
- **Sektor 0 ist KEIN PAR/BTT-Sektor**, sondern das Bild der Antwort auf Kommando 28 (Parameterblock):
  `"WDC_4.2\0"`, `"WDC-Emulator"`, Zyl. 0564h = 1380, Köpfe 0Ah, Sektoren 12h, Vork. 1380, Ramp 1,
  `ztk`/Grenzen **00**, Blockzahl 03C99Bh (= 248 219 = höchster Block ✓), `d_rdy` 01, `s_rdy` 09,
  `err_in` 00, 1 Laufwerk; dazu bei Offs. 45h/5Bh/D2h weitere AVR-eigene Bytes [Bedeutung unsicher].
- ⇒ Mit der echten Firmware 4.2 meldet dieses Abbild **Fehler 38+1** (kein `DEFEKT`/`PARMTR`) und
  arbeitet mit vorläufigen 100 Zyl./2 Köpfen. **Umsetzung für P13b:** beim Einlesen Sektor 0 durch
  einen gültigen PAR/BTT-Sektor ersetzen (`ROB…`-Text frei, 1380/10/18, Vork. 1380, Ramp 1,
  `ztk_40/41 = 203/209`, Grenzen `251/253/241/243`, leere BTT) — die übrigen Zylinder-0-Sektoren
  bleiben ungenutzt. Ob WEGA 3.1 die Geometrie 1380/10 auch selbst erwartet (Plattenaufteilung im
  Kern, `md_sizes`/`wpar.c`), ist für P13d zu prüfen.
- Ein echtes K5504.50-Abbild (1024/5/18) hat dasselbe Lageschema mit 47 185 920 B.

## 12. Offene Fragen an das Gerät

1. **Welche Firmware steckt?** Mitschnitt nennt `WDC_V.3.4.05` (EPROM-Parameter, kein PAR-Sektor) und
   später 4.2. Für `sa.format` der WEGA-Disketten ist ≥ 4.1 nötig (§4.3). Bitte EPROM-Aufkleber/CRC melden.
2. **WDC-Index am Gerät** (Typenschild „V: abcd", c) und **Quarz** (40 oder 41,4 MHz bei Index 3; 16 MHz
   bei Index 1) — beeinflusst nur `ztk`-Wahl.
3. **Z0/K0/S1 der echten Platte** (512 B) per WDC-Kommando 01 (Lesen Sektoradresse) oder `sa.format`
   „t"-Abfrage auslesen — bestätigt §7 und liefert die echte BTT.
4. **Wait-Zyklen** der WDC-Z80 (Am8127 WAIT): bei jedem M1? (nur für exakte Zeitschleifen).
5. **Spurlänge real:** Abstand Kennfeld–Kennfeld und Lücke 4 an einer Spur (Logikanalysator an
   READ DATA) — bestätigt die Spursynthese §8; nicht zwingend.
6. **ST_PRT Bit 0, 1, 4** und **DSKC1 Bit 1/2** (Motor/HL) — Belegung aus SP3/6 (Blatt fehlt auf pofo.de)
   oder Messung.
7. **Index-1-Zusatzleiterkarte:** liegt dort der MFM-Codec/Datenseparator (Plan fehlt)? Für die
   Emulation gleichwertig, nur zur Dokumentation.
8. **Ist der Kommando-Suchfehler** (`com_ts`, §4.3 Code 00) im Abzug 4.2 vorhanden? Klärt P13c am Abzug.
