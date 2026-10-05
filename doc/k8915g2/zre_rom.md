# K8915 „Generation 2“ — Analyse der ZRE-ROMs 175/176/177 und der Kartenchips (AP-V3b)

Stand 2026-10-05, Ergebnis von **AP-V3b** (`doc/design/24_k8915_varianten.md`). **Statische
Analyse** der Abzüge `doc/EPROMS/K8915G2/` (MD5 geprüft) auf Grundlage der Listings
`k8915g2_zre.prn` / `k8915g2_pfs3820.prn` (AP-V3a), dazu ein Byte-Vergleich mit dem V3-Boot-ROM
`doc/EPROMS/K8915/k8915_boot_2732.bin` und ein Probelauf von `tools/k8915_sysload.py` (bildet den
ROM-Lader nach, ohne Maschine). Es gibt noch keine Gen-2-Maschine, also keinen Lauf.

Legende: **[ROM]** folgt aus dem Code, **[ROM-V3]** aus dem Byte-Vergleich mit dem V3-ROM und
dessen belegtem Verhalten im Emulator, **[SLP]** Stromlaufplan (über `karten.md`), **[?]**
Vermutung/offen, **[gerechnet]** aus Befehlstakten abgeschätzt.
Fundstellen: „ZRE xxxx“ = Lade-Adresse im 3-KB-Abzug `k8915g2_zre_0000-0BFF.bin` (bei 175 ab
0021H läuft der Code als Kopie bei Lade + FC00H, dort steht die Lauf-Adresse in Klammern);
„3C00+xxxx“ / „3000+xxxx“ = Lage im jeweiligen Kartenchip.

---

## 0. Kernbefund vorweg: der Lader ist der V3-Lader

| Bereich | Vergleich Gen 2 ↔ V3-ROM (`k8915_boot_2732.bin`, gleiche Adressen) |
|---|---|
| 0000–03FF (175) | 1007 von 1024 Byte verschieden — **eigener Urlader/Selbsttest** (Aufbau verwandt, §4) |
| **0400–07CB (176)** | **byteidentisch bis auf 2 Byte**: 041BH (Quelle des Stubs 098FH statt 0987H) und 042AH (`JP NZ,03FAH` statt `0FFAH`) |
| **0800–0906 (177)** | **byteidentisch** (Abschluss, Folgezylinder, Fehlerzähler, Software-CRC 08D7H/08E0H) |
| 0907–0976 (177) | Meldungsroutine gleich gebaut; Tabellen um 8 Byte verschoben (0977H/0983H statt 096FH/097BH), **Tastenauswertung erweitert** (`E` und `ESC c` ⇒ `JP 03F3H`) |
| 098F–09A5 (177) | Stub für FFE0H: Werte **8FH/0EH** statt 87H/06H |

[ROM-V3] Damit gilt alles, was am V3 über **Lader, Ladekopf, K5122-Ansteuerung und
Warmstart-Einsprung 0406H** belegt ist (Entwurf 16 §4.3/§4.4, Wächter `K8915Boot.*`,
`K8915Scpx.*`), unverändert auch für die Gen 2. Die README-Aussage „1007 von 1024 Bytes
verschieden“ betrifft nur den Baustein 175.

---

## 1. Speicherkarte

### 1.1 Was das ROM vom Speicher verlangt [ROM]

| A8H | 0000–0BFF | 0C00–0FFF | 1000–17FF | 1800–3FFF | 4000–BFFF | C000–FFFF | Beleg |
|---|---|---|---|---|---|---|---|
| 00H (nach /RESET, bis ZRE 0005) | ROM 175–177 | [?] | [?] | [?] | [?] | [?] | ZRE 0000–0005 läuft ohne Speicherzugriff |
| **06H**, **0EH** | **ROM** | K2521-RAM, vom ROM nicht benutzt | **K7024-Bild** | unbenutzt | RAM, wenn der Ladekopf dorthin lädt [?] | **RAM** | s. u. |
| **87H**, **8FH** | **RAM** | **RAM** | **RAM** (Bild ausgeblendet) | **RAM** | **RAM** | **RAM** | RAM-Test, Stub |

Belege für die Zeilen:
- **06H/0EH, Seite 3 = RAM:** ZRE 0008 schreibt FFF9H gleich nach `OUT (A8H),06H` (ZRE 0005);
  der Kopierer ZRE 0011–001E legt bei 0EH das ROM 0000–03FF nach FC00–FFFF und springt hin
  (also ROM bei 0000 **und** RAM bei FC00 zugleich); Lader-Zellen F700–F78BH, Stapel F7E0H
  (ZRE 042C); Ladeziel des Systems C000–EFEFH (Ladekopf, §5).
- **06H/0EH, Bild bei 1000H:** Bild löschen 1000–177FH (ZRE 02E2 = FEE2), Statuszeile 1734H/
  1740H/1770H/1776H (Tabelle ZRE 0388 = FF88), Meldungsroutine rollt 1050H→1000H (ZRE 0916).
- **87H, alles RAM:** RAM-Test ZRE 01B9–01DA (FDB9–FDDA): `OUT (A8H),87H`, legt `ED 45` (RETN)
  nach 0066H, prüft dann **0068H bis FDC7H** zerstörungsfrei mit 55H/AAH — also auch unter dem
  ROM, unter dem K2521-RAM und unter dem Bildspeicher. Gelingt das nicht ⇒ Fehler `A` bei „RAM“.
  Der laufende Code (FDC8H ff.) liegt selbst in Seite 3. ⇒ **64 KB RAM durchgehend.**
- **8FH:** Stub bei FFE0H (ZRE 098F–09A5) liest 0000H und 0005H aus dem RAM (Systemkennung
  `C3`), schaltet auf 0EH zurück.

### 1.2 Lage der Programmteile [ROM]

| Lauf | Herkunft | Inhalt |
|---|---|---|
| 0000–0020, 0066, 03F3, 03FA | ROM 175 | Rücksetzen, Kopierer 0011H, NMI = `RETN`, Vektoren `JP 0400H` / `JP 0011H` |
| FC21–FFFF | Kopie von 175 (Lade 0021–03FF) | Selbsttest, Unterprogramme, Tabellen, Merker FFF8H/FFF9H, IM-2-Wort FFF6H → FF3DH |
| 0400–0BFF | ROM 176/177, unverschoben | Lader, Meldungsroutine 0907H |
| FFE0–FFF6 | Kopie von 177 (Lade 098F–09A5) | Stub (überschreibt in der 175-Kopie den leeren Bereich FFE0–FFF2H und FFF3–FFF6H samt Vektorwort; harmlos, weil danach entweder der Kopierer 0011H neu kopiert oder der Lader ohne Interrupts läuft) |
| F700–F7E0 | RAM | Lader: F700–F70F Ladekopf, F710 kalt(0)/warm(1), F711 Diskettenwechsel, F712 Meldung gezeigt (20H), F713 CRC-Versuche (4), F714/F715 Markenversuche (256 × 4), F716 Kopf noch zu lesen, F717 Zylinder, F718 Sektor, F719 Länge in 128-B-Einheiten, F71A Zeiger Sektorzahl, F71C.. Daten-CRCs, F784 Zeiger CRC-Liste, F786 Prüfzeiger, F788 Kopf, F789/A Steuerworte (B585H/B181H), F78B Sektoren dieses Zylinders; SP = F7E0H |

**K2521-RAM 0C00–0FFF:** die ZRE-Fassung benutzt es nicht (kein Zugriff auf 0Cxx) [ROM]. Es muss
bei 87H/8FH vom RAM überdeckt sein (RAM-Test). Erst der Kartenchip „3000“ legt dort Zellen ab (§7).

---

## 2. Register A8H

### 2.1 Werte und Wirkung [ROM]

| Wert | Bits | Fundstelle | Zweck |
|---|---|---|---|
| **06H** | 1, 2 | ZRE 0003/0005 (Reset), 01DC/01DE und 01E2/01E4 (FDDC/FDE2, nach dem RAM-Test) | ROM + Bild + RAM oben |
| **0EH** | 1, 2, 3 | ZRE 099F/09A1 (Stub FFF0/FFF2) | wie 06H; danach Kopierer bzw. Kaltstart |
| **87H** | 0, 1, 2, 7 | ZRE 01B9/01BB (FDB9) | RAM-Test, alles RAM |
| **8FH** | 0–3, 7 | ZRE 098F/0991 (Stub FFE0/FFE2) | „System im RAM?“ |

Kartenchip „3C00“: 0EH (3C00+0003, +0210, +0216), 8FH (3C00+01ED); „3000“: 8FH/0EH im Stub
(3000+015B/+016B). Der V3 schreibt dagegen **8EH** (Reset), 06H, 87H, 44H–74H (Entwurf 16 §4.2).

### 2.2 Vergleich mit dem V3-Register (`doc/merkposten/k8915.md`, `core/cards/zre8762/`)

V3 (am Gerät belegt, §4.2a von Entwurf 16): Bit0 = Seite 0 RAM **und** ROM aus, Bit1 = Seite 1,
**Bit2 = Seiten 2 und 3**, Bit3 = `/MEMDI1` (aktiv bei 0), Bit5:4/Bit6 = Bank 2, Bit7 = `/MEMDI`
(aktiv bei 1). Mit dieser Belegung ergeben die vier Gen-2-Werte **genau** die Tabelle 1.1:
06H/0EH = ROM + Bild + Seiten 1–3 RAM, 87H/8FH = Seiten 0–3 RAM. Kein Gen-2-Wert widerspricht.

**Entscheidung: gleiches Register (bitkompatibel für Bit 0, 1, 2, 7), anderer Träger.**
- [ROM] Die Gen-2-Werte setzen Bit0 und Bit7 **immer gleich** (06/0E: beide 0; 87/8F: beide 1).
  Der V3-Wert 8EH (Bit7 = 1, Bit0 = 0) kommt im Gen-2-ROM **nicht** vor; das Gen-2-ROM ersetzt
  ihn beim Rücksetzen durch 06H. [?] Naheliegende Deutung: in der Gen 2 sperrt `/MEMDI` die
  **steckbare** K2521 (Brücke X8:1–X9:1, `karten.md` §1.2) — 8EH würde dort das eigene ROM
  abschalten. Ob `/MEMDI` an Bit7 (wie V3) oder an Bit0 (Handdraht X3:37 → X3:38 im K3528-Plan,
  `karten.md` §2.2) hängt, kann das ROM **nicht** unterscheiden; für alle bekannten Werte (ROM
  **und** SCPX-BIOS 87H/06H) ist das Ergebnis dasselbe.
- [SLP/?] Träger ist nach `karten.md` §2.3/§5 das 8212-Register der **K3528** (Adresse per
  Wickelfeld, A8H = D2:02-Ausgang 05; AB0/AB1 nicht dekodiert ⇒ A8H–ABH). Wie die K3528 die
  16-KB-Banken je Registerbit freigibt, ist im Plan nicht gelesen (F23) — das ROM verlangt nur
  das **Verhalten** aus 1.1.
- **Bit3** (06H ↔ 0EH): ohne beobachtbare Wirkung im ROM — Lader und Selbsttest laufen mit beiden
  Werten. Wie beim V3 `/MEMDI1` mitführen, ohne Verbraucher [?].
- **Bits 4–6:** vom Gen-2-ROM nie gesetzt, **keine Bank 2** (RAM-Test kennt kein „RAZ“, §4).
- **Nach /RESET 00H** [SLP K3528, CLR an /RESET]: das ROM greift vor `OUT (A8H),06H` auf keinen
  Speicher außer dem ROM selbst zu (ZRE 0000–0005).

---

## 3. Portkarte

### 3.1 ZRE-ROMs 175–177 (Gen 2) — vollständig [ROM]

| Port | R/W | Baustein | Belegung / Werte | Fundstellen (ZRE) | V3 |
|---|---|---|---|---|---|
| 10H | W | K5122 PIO1-A Daten (Steuerport) | Schrittimpuls Bit7, Richtung Bit5 (DFH aus/zu Spur 0, 9FH, BFH ein), FBH Ruhe, BBH, Lese-Steuerworte B5H/85H (Kopf 0) bzw. B1H/81H (Kopf 1) per `OUT (C),D/E` | 0467, 046B, 046F, 047D, 04CC, 052E, 0558, 055A, 0612, 0614, 0679, 067B, 06FA, 06FC | gleich |
| 11H | W | K5122 PIO1-A Steuer | 3FH (Mode 0) | 04C8 | gleich |
| 12H | R | K5122 PIO1-B Status | Bit0 Index/bereit (053C), Bit1 MKE (04A9 Ruhepegel; 055E/0618/067F/0704 mit B' verknüpft), Bit7 Spur 0 (04DB) [ROM-V3] | 04A9, 04DB, 053C, 055E, 0618, 067F, 0704 | gleich |
| 12H | W | K5122 PIO1-B Daten | 04H bzw. C' (00H/04H nach MKE-Ruhepegel) | 04B9, 0538 | gleich |
| 13H | W | K5122 PIO1-B Steuer | FFH/F7H, bei MKE-Ruhe 0: CFH/F3H | 049E, 04A2, 04B1, 04B5 | gleich |
| 14H | W | K5122 Daten schreiben | 00H vor dem Scharfmachen | 0550 | gleich |
| 15H | W | K5122 Daten-PIO Steuer | 3FH | 04C6 | gleich |
| 16H | R | K5122 Lesedaten (**`/WAIT`**) | ausgerollt, ohne Statusabfrage, `INI` | 054D, 0593, 05A4…05DD, 05E6…0610, 062B, 0641…0656, 065C, 0666, 066C, 0677, 0692, 06A4, 06E9…06F8, 0717, 072C, 0732, 073C, 0744, 074C | gleich |
| 17H | W | K5122 Daten-PIO Steuer | 7FH | 04C2 | gleich |
| 18H | W | K5122 Laufwerksauswahl (8212) | FFH aus, EEH = Laufwerk 0 + Motor | 0481, 049C, 04D0 | gleich |
| 44H / 46H | R/W | ATS SIO1-A / SIO1-B Daten (Spiegel 40H/42H) | Prüfmuster AAH/55H | 017A/0183, 017C/018A (FD7A–FD8A) | gleich |
| 45H / 47H | W | ATS SIO1-A / -B Steuer (Spiegel 41H/43H) | `OTIR` 8 Byte FFCCH: WR0 18H, WR3 C1H, WR4 45H, WR5 68H | 015C–016A (FD5C–FD6A) | gleich |
| 48H | W | ATS CTC1 K0 | Vektor F0H | 00FB (FCFB) | gleich |
| 4BH | W | ATS CTC1 K3 | A7H, TK 18H, Halt 03H | 0103, 010C, 0123 (FD03–FD23) | gleich |
| 4CH–4EH | W | ATS CTC1 K0–K2 (Spiegel 48H–4AH) | 17H, TK 01H (9600 Bd) | 014A–0158 (FD4A–FD58) | gleich |
| 52H | R/W | ATS SIO2-B Daten = **K7672** | aus: 13H DC3, 1BH 63H `ESC c`, `ESC [2;1y`, 11H DC1, 07H BEL; ein: 11H, 7FH, 0DH, 23H, 7CH, 1CH, 45H, 1BH/63H | 002B, 00B8, 00C8, 00D2, 00E5, 01F4, 0200, 02B5, 0331, 033A (FCxx/FExx/FFxx); 0948, 0965 | gleich |
| 53H | R/W | ATS SIO2-B Steuer | `OTIR` WR0 18H, WR4 44H, WR3 C1H, WR5 EAH, WR1 00H; RR0 Bit0 | 032C (FF2C), 0914; 02AF (FEAF), 0942, 095F | gleich |
| 54H / 55H | R/W / W | ATS SIO2-A Daten / Steuer (Spiegel 50H/51H) | wie SIO1 | 017E/0191, 016C–0172 | gleich |
| 58H | W | ATS CTC2 K0 | Vektor F0H | 00FD (FCFD) | gleich |
| 5AH | W | ATS CTC2 K2 (Tastaturtakt) | 07H, TK 01H | 0326 (FF26), 090F | gleich |
| 5BH | W | ATS CTC2 K3 | A7H, TK 24H, Halt 03H | 0105, 0110, 012A | gleich |
| 5CH | W | ATS CTC2 K0 (Spiegel 58H) | 17H, TK 01H | 0150, 015A | gleich |
| 61H | W | Anzeigefeld (Latch, aktiv low) | FFH aus, 7FH Fehler, 00H Diagnose, E0H Lesen, B0H bereit, 60H Fehler + Lesen | 000C, 01ED, 02DA, 02DF, 0498, 0827, 0907 (A = B0H/60H von den Aufrufern) | gleich |
| 80H | W | **K2521-CTC K0** | Vektor F0H | 00F9 (FCF9) | gleich (ZRE-CTC) |
| 83H | W | K2521-CTC K3 | A7H, TK 0CH, Halt 03H | 0101, 0109, 011C | gleich |
| A8H | W | Speicherregister (§2) | 06H, 87H, 06H, 8FH, 0EH | 0005, 01BB, 01DE, 01E4, 0991 (FFE2), 09A1 (FFF2) | gleich, andere Werte |

**Nicht angesprochen** [ROM]: K2521-PIO 84H–87H (und 08H–0FH), 40H–43H/48H–4AH/50H–53H/58H–5BH
außer über die genannten Spiegel, 42H/43H/4AH (die nutzt erst das BIOS), 60H, 62H–67H, A9H–ABH,
E0H–FFH, B0H–B3H. **Abgleich V3:** die Portmenge ist **dieselbe** wie im V3-ROM (gezählt aus
`k8915_zre.prn`); die Gen 2 trägt also dieselbe ATS 045-8732 (= K7028.30, `core/cards/k7028/`)
und dieselbe K5122 im `/WAIT`-Betrieb — passt zu robotrontechnik.de „5¼″ V2: … K5122, 045-8732, K7672“.

### 3.2 Kartenchips „3C00“ (Fassung von 175) und „3000“ (Fassung von 177) [ROM]

| Port | R/W | Werte | Fundstellen | Deutung |
|---|---|---|---|---|
| A8H | W | 0EH, 8FH, 0EH, 0EH; Stub 8FH/0EH | 3C00+0005, +01EF, +0212, +0218; 3000+015D, +016D | wie Gen 2, aber **kein 06H** |
| E0H | R | Tastencode; erwartet `A0H` (TYP), `1FH`, `10H`, `9DH` | 3C00+0010, +00D1, +00D9, +00DE, +022B, +0238, +02E4, +030F, +0318; 3000+013B | **K7634 Daten** (`k7634.md` §4) |
| E1H | R | Bit3 = 0 ⇒ Code gültig | 3C00+02DE, 3000+0135 | K7634 Status |
| E2H | W | 00H, 20H (Fehler), E0H (Diagnose), 00H | 3C00+0008, +0225, +030D, +0313 | K7634 Kommando [?] |
| E3H | R | Bit2 = 1 ⇒ RAM-Test über 0068–FDFBH mit 8FH, sonst nur 8000–FDFBH ohne Umschalten | 3C00+01E2 | Ausbaukennung („64 K“?) **[?] → F10** |
| E4H | W | FFH, 7FH, 00H, FFH; B0H; A der Meldungsroutine | 3C00+000E, +0229, +0309, +0316; 3000+0027, +0107 | Anzeigefeld wie 61H **[?]** |
| B3H / B1H | W | 0FH (Mode 0) / 40H | 3C00+0014 / +0018 | PIO B0H–B3H unbekannter Karte **[?] → F10** |
| 80H / 83H | W | F0H / A7H, 0CH, 43H | 3C00+0101, +0107, +010D, +011C | K2521-CTC |
| F8H / FBH | W | F0H / A7H, 18H | 3C00+0103, +0109, +0110, +0124 | CTC der K7028.10 |
| FCH–FEH | W | 17H, 01H | 3C00+0149–0155 | dieselbe CTC K0–K2 (Spiegel) [?] |
| F5H / F7H | W | `OTIR` 8 Byte FFD0H | 3C00+0157–0165 | SIO A/B Steuer |
| F4H / F6H | R/W | AAH/55H | 3C00+016D–017B | SIO A/B Daten (Rückschleife) |

---

## 4. Selbsttest

Reihenfolge der Gen 2 (175): **ROM → KEY → CTC → SIO → RAM** (V3: ROM → RAM → RAZ → KEY → CTC →
SIO). Anzeige wie V3: „DIAGNOSTIC“ bei 1740H, Testname bei 1770H, Kennbuchstabe bei 1776H
(Tabelle ZRE 0388 = FF88). Vorab `DC3` an die Tastatur (ZRE 0029 = FC29).

| Test | Was | Braucht | Fehlerkennung | Fundstelle |
|---|---|---|---|---|
| **ROM** | 24-Bit-Summe je Baustein (0000/0400/0800, je 3FDH Byte) gegen die letzten 3 Byte | 175–177 lückenlos ab 0000H | `A`/`B`/`C` = Baustein 1/2/3, `Z` = mehr als einer | ZRE 003A–0096 |
| **KEY** | `ESC c` ⇒ `DC1` binnen ≈ 0,86 Mio. Takten; `ESC [2;1y` (6 Byte, je ≈ 0,21 Mio. Takte Pause) ⇒ `DC1`; danach `DC1` senden. Gelesen wird 52H **ohne** Statusabfrage | K7672 an SIO2-B, CTC2 K2 als Takt | `A` (nach `ESC c`), `B` (nach `ESC [2;1y`); `7FH` (DEL) ⇒ **Diagnosebild** | ZRE 00A8–00CA, 00CC–00E2 |
| **CTC** | I = FFH, Vektor F0H an 80H/48H/58H, K3 aller drei als Zeitgeber mit Interrupt (A7H; TK 0CH/18H/24H ⇒ 3072/6144/9216 Takte), ISR FF3DH zählt A; Fenster je ≈ 3330 Takte | K2521-CTC **und** ATS-CTC1/CTC2, IM 2, Interruptkette mit allen drei, taktgenau | erwartet A = 4, sonst `(A+3) OR 40H` bzw. `@` (A = 0) | ZRE 00F3–013C |
| **SIO** | SIO1-A, SIO1-B, SIO2-A mit 9600 Bd 8 ungerade, **drei** Runden (AAH, 55H, 55H; V3: zwei), je ≈ 1,6 Mio. Takte Wartezeit | **Rückschleife auf allen drei Kanälen** (wie V3: `Config::pruefstecker`) | Bitmaske der Kanäle OR 40H (`A`…`G`) | ZRE 0148–01AA |
| **RAM** | A8H = 87H, `RETN` nach 0066H, 0068–FDC7H mit 55H/AAH, Inhalt bleibt | 64 KB RAM, ROM/K2521-RAM/Bild abschaltbar | `A` | ZRE 01B9–01E9 |

**Abbruchverhalten** [ROM]: der **erste** Fehler beendet den Test (keine weiteren Tests),
`OUT (61H),7FH` (ERROR), **16 × BEL** an die Tastatur (BC = 1040H, ZRE 01EB–01FD), dann
Tastenschleife ZRE 02AF (FEAF): `#` ⇒ Selbsttest zyklisch (FFF9H Bit2) von vorn, **`CR` ⇒
Kaltstartmeldung** (`JP 0403H`), sonst weiter warten. Ein Fehler **sperrt den Start also nicht**.
Nach bestandenem Test: `IN A,(52H)` ohne Status, `7FH` ⇒ Diagnosebild, sonst (nicht zyklisch)
`JP 0403H` (ZRE 0200–020B). Der V3 kennt 64 × BEL beim ROM-/RAM-Fehler; die Gen 2 immer 16 ×.

**Diagnosebild** (ZRE 0211–02AD): Lampen an (61H = 00H), Rahmen aus 48H, Zeichentabelle 20H–7FH,
Text „ENTER: LADER / "#": ZYKL.“ bei 1734H; dann Tastenschleife wie oben.

**Folge des Abzugs 177 (F9):** mit dem Abzug, wie er vorliegt, endet der Selbsttest im Emulator
bei **„ROM“ mit Kennbuchstabe `C`**, ohne KEY/CTC/SIO/RAM zu prüfen; erst `CR` führt zur
Kaltstartmeldung. Ob das echte Gerät das ebenso zeigt, klärt F9.

**Dauer** [gerechnet]: ROM ≈ 0,12 Mio., KEY ≈ 1,5 Mio. + Antwortzeit der K7672, CTC ≈ 0,01 Mio.,
SIO ≈ 4,9 Mio., RAM ≈ 10 Mio. Takte ⇒ **≈ 17 Mio. Takte ≈ 7 s** bis zur Kaltstartmeldung (V3:
≈ 29 Mio., gemessen). Nicht gemessen.

**NMI** [ROM]: 0066H = `RETN` (ZRE 0066) — der NMI-Taster wirkt im ROM nicht (V3: Selbsttest von
vorn). Unter einem System bei 87H liegt dort das vom RAM-Test gelegte `ED 45`, bis ein Programm es
überschreibt.

---

## 5. Bootablauf

1. **Reset** (ZRE 0000): `DI`, `IM 2`, A8H = 06H, FFF9H = 0, 61H = FFH, `JP 0400H` — **ohne**
   Stapel, ohne Kopie.
2. **0400 → 041A:** Stub 098F–09A5H nach FFE0H, dort A8H = 8FH, prüft **0000H = C3H und 0005H =
   C3H**, A8H = 0EH, `JP 0428H`.
3. **Kein System im RAM** (NZ): `JP 03FAH` → `JP 0011H` → Kopierer (SP = FC00H, 175 nach FC00H)
   → Selbsttest (§4) → `CR` bzw. Erfolg ⇒ `JP 0403H` → 042BH.
   **System im RAM** (Z, Warmstart-Reset): sofort 042BH, **ohne** Selbsttest (wie V3,
   `K8915Boot.SystemImRamFuehrtZumLader`).
4. **042B Kaltstart:** `DI`, SP = F7E0H, Bild löschen, 61H = B0H, „**\* Coldstart \*  Disk on A:
   ready ?** --> <ENTER>“. Warten auf `CR`, `|` (7CH) oder 1CH ⇒ weiter; **`E` (45H) oder `ESC c`
   ⇒ `JP 03F3H` = Neubeginn bei 0400H** (neu gegenüber V3, ZRE 0956–096B).
5. **Lader** (0462: `XOR A`, `CALL 0484H` = Kaltladen, F710H = 0): Laufwerk 0 (18H = EEH), MKE-
   Ruhepegel aus 12H Bit1 bestimmen (Polarität wie das BIOS), höchstens 80 Schritte nach Spur 0
   („**Drive A: not ready, check**“, dann nach `CR` von vorn), Index abwarten (sonst dieselbe
   Meldung), Sektor 1 Zylinder 0 Kopf 0 suchen (ID-Vergleich Zylinder/Kopf/Sektor, Länge aus N).
   Erster Durchgang liest nur die **16 Byte Ladekopf** nach F700H und prüft sie mit CRC-CCITT
   (FFFFH ⇒ 0), sonst „**No system disk, change disk**“. Dann Sektor 1 nochmals (ab Byte 16) und
   die folgenden Sektoren ab der Ladeadresse; Kopf 1 ab der Sektorzahl in Byte 11–13; Daten-CRC
   je Sektor **in Software** (08D7H, Startwert E295H) nach Ende des Zylinders, 4 Versuche, sonst
   „**Disk-error, change disk**“ (auch nach 256 × 4 vergeblichen Markensuchen). Nach einem
   Diskettenwechsel (F711H = 1) und am Ende „**Loading complete, replace disk by previous disk**“;
   bei Ladekopf-Byte 10 ≠ 0 „**Loading complete, replace system disk**“. Höchstens Zylinder 0–2.
6. **Übergabe:** 61H = B0H, Laufwerk aus (10H = FBH, 18H = FFH), **`JP (HL)` mit HL = (F702H)**
   (Einsprung aus dem Ladekopf), A = (F712H) (20H, wenn eine Meldung kam, sonst 0), SP = F7E0H,
   `DI`, IM 2, I = FFH nach Selbsttest bzw. unverändert nach Warmstart-Reset, **A8H = 06H nach dem
   Selbsttest, 0EH auf dem Weg über den Stub**. Der V3 übergibt immer mit 06H — das BIOS schaltet
   selbst auf 87H.
7. **Warmstart des Systems** (`CALL 0406H` bei A8H = 06H, so ruft es das SCPX-8915-BIOS): Bit7 im
   Bild löschen, F710H = 1 ⇒ Sektorzahlen aus **Ladekopf-Byte 7–9** (3/0/0 ⇒ 3056 B ab C000H),
   Rückkehr mit `RET` (HL = Einsprung, A = Meldungsmerker). *Berichtigung zu Entwurf 16 §4.4
   („Byte 7–9 … vom ROM nie benutzt“): sie werden von 0406H benutzt, also beim BIOS-Warmstart —
   auch am V3, der Code ist byteidentisch.*

**Passung zu `disks/k8915scpx_boot1.hfe`** [ROM-V3]: `tools/k8915_sysload.py` (bildet diesen Lader
nach) liest den Ladekopf `00 C0 00 D6 0A 02 00 03 00 00 00 05 05 05 E1 28` und lädt **12 272 B nach
C000–EFEFH, Einsprung D600H**. Weil 0400–0906 mit dem V3-Lader byteidentisch sind (außer
Stub-Quelle und Vektorseite), liest die Gen 2 diese Diskette **genau wie der V3**: Format
5 × 1024 MFM ab Zylinder 0 (`cpa800`), K5122 im **`/WAIT`-Betrieb** (ausgerollte `IN (16H)`/`INI`
ohne Statusabfrage, keine ZVE2 — der A5120-`/BUSRQ`-Weg kommt nicht in Frage). Das BIOS SCPX 8915
V5.3 verlangt nur 87H/06H, `CALL 0406H`, I = FFH, SIO2-B, K2521-CTC K3 und die K5122-PIO-
Interrupts — alles in der Gen 2 vorhanden. **[?]** Ob das Gen-2-Gerät dieses System tatsächlich
fährt, zeigt erst eine eigene Diskette (F4); `RADE` (Autostart auf 901) braucht Bank 2 und erkennt
die Maschine an 0C00H: bei 0EH liegt dort in der Gen 2 das **K2521-RAM** statt ROM ⇒ RADE wählt
den „A0H“-Weg (Entwurf 16 §4.4) — für den Bootnachweis unerheblich, Ergebnis offen (F13).

---

## 6. Interrupts, CTC, SIO, Takt [ROM]

- **IM 2** ab Reset (ZRE 0001), Interrupts erst im CTC-Test (`EI` ZRE 0115, `DI` ZRE 0128).
  **I = FFH** (ZRE 00F5), alle drei CTCs Vektor **F0H**, Kanal 3 ⇒ F6H ⇒ Wort FFF6H = **FF3DH**
  (`INC A / EI / RETI`). SIO-Interrupts: keine (WR1 = 00H). K5122-PIO: im ROM gepollt.
- **Baudraten:** CTC-Steuerwort 07H/17H (Zeitgeber, Vorteiler 16), TK 01H ⇒ Φ/16 = 153,6 kHz; SIO
  ×16 ⇒ **9600 Bd** bei **Φ = 2,4576 MHz** (K2521: 9,8304 MHz / 4, `karten.md` §1.1).
  Tastatur 8N1 (WR4 44H, WR3 C1H, WR5 EAH), SIO-Prüfkanäle 8 Bit ungerade (WR4 45H, WR5 68H).
- **CTC-Test braucht dieselbe Taktbasis** für K2521-CTC und ATS-CTCs; die Kaskade TO2 → CLK/TRG3
  der K2521 (X10/X11) ist ohne Belang (K3 im Zeitgebermodus mit automatischem Start).

---

## 7. Die Karte (3C00/3000) gegenüber der ZRE

- **Ersatz, keine Ergänzung.** „3C00“ ist eine **175-Fassung** für den Platz **0000H** (Kopierer
  3C00+003F kopiert 0000–03FF nach FC00, Vektoren bei 03F3H/03FAH wie 175, Einsprung `JP 0400H`),
  „3000“ eine **177-Fassung** für **0800H** (Lauf 0800–097F, Rufe in den 176-Bereich 0467H, 047BH,
  0496H, 0548H, 054DH, 056CH). Beide haben die richtige Summe.
- **Sie passen nicht zum vorhandenen 176:** 176 kopiert den Stub ab **098FH** (dort ist 3000 leer,
  sein Stub steht bei **095BH**) und führt seine Zellen bei **F7xx**, „3000“ liest dieselben Zellen
  bei **0Cxx** (18 Adressoperanden F7H → 0CH). Zum Kartensatz gehört also ein **176-Gegenstück mit
  0Cxx-Zellen, das fehlt** (Platz 0400 der Karte ist leer) — **F11**.
- **Hardware-Annahme 0Cxx:** Arbeitszellen im **1-KB-RAM der K2521 (0C00–0FFF)** statt in
  RAM-Seite 3; „3C00“ prüft dieses RAM eigens (55H/AAH über 0C00–0FFF, 3C00+01AA–01CA). Warum,
  ist offen [?].
- **Andere E/A:** Tastatur **K7634** an E0H/E1H/E2H, Anzeigefeld an E4H [?], SIO F4–F7H und CTC
  F8–FEH (**K7028.10**), Test „I/O“ statt „SIO“ (zwei Kanäle, zwei Runden), kein K7672-Protokoll,
  dazu B1H/B3H und E3H [?]. Erwartete Tastencodes: TYP `A0H`, `1FH` = RESET (Diagnose bzw.
  `JP 03F3H`), `10H` = OFF (zyklisch), `9DH` = ENTER. ⇒ **Das ist der Urlader der Gen 1**
  (`k7634.md`), nicht der Gen 2. Das ZRE-ROM 175–177 fragt die K7634 **nicht** ab.
- **Im Gen-2-Ablauf nie angesprochen** und auch nicht gleichzeitig mit dem RAM betreibbar: jede
  Lage der Karte (4-KB-Raster) läge über dem 64-KB-RAM, das der RAM-Test (87H) vollständig prüft
  und das der Kopierer (0EH) bei FC00H beschreibt und ausführt. Die Kartenchips können auch an
  keiner Startadresse an 0000H/0800H erscheinen (Chip „3C00“ läge bei Start 0000 auf 3C00H).
  [?] Deutung: die Karte ist ein **Träger/Archiv** für die Gen-1-Fassung. ⇒ **V5 = Beigabe**.
- Kleinigkeiten in „3C00“: prüft D001–D003H auf `F3 ED 5E` und schreibt sonst `76H` (HALT) nach
  D000H, wenn dort FFH/00H steht (3C00+001A–003A) [?, F10]. Im Listing `k8915g2_pfs3820.prn` sind
  die Kommentare der Datentabelle FF92H–FFA0H um ein Byte verrutscht (Code liest Muster FF92H,
  Länge FF9CH, Abstand FF9EH, Spalten FFA0H, Rahmentabelle ab FFA1H) — nur Kommentar.

---

## 8. Anforderungsliste für V4/V6 — „die Gen-2-Maschine braucht“

**Karten und Verdrahtung**
1. **K2521** (`core/cards/k2521/`, unverändert) mit ROM 175/176/177 = 3 KB bei 0000–0BFFH
   (`rom_len = 0x0C00`), 1 KB RAM 0C00–0FFFH, CTC 80H–83H (IM-2-Vektor), PIO 84H–87H vorhanden,
   aber unbenutzt; Φ = 2,4576 MHz. Abzug 177 **so, wie er ist** (keine Korrektur, F9).
2. **Speicher (V4): 64 KB RAM + Register A8H** (Spiegel A9H–ABH [?], nur schreibbar, /RESET ⇒
   00H, Inhalt des RAM bleibt bei Reset). Verhalten je Wert wie V3 für **Bit 0, 1, 2, 7**:
   Bit0 = Seite 0 RAM und K2521 (ROM+RAM) **und** K7024 ausgeblendet; Bit1 = Seite 1; Bit2 =
   Seiten 2 + 3; Bit7 = `/MEMDI` (blendet die K2521 aus — für alle bekannten Werte gleichwertig
   zu Bit0); Bit3 = `/MEMDI1` mitführen ohne Verbraucher; Bit 4–6 wirkungslos, **keine Bank 2**.
   Die Zuordnung Bit → Wirkung als **Konfigurationsstruktur** (wie `K8915Zre::Brueckenfeld`),
   Vorgabe „V3-kompatibel“, weil F20/F23 offen sind. Nicht gewählte Adressen gehen an den Bus.
   Wächter-Vorschlag: Speicherbild je Wert 00H/06H/0EH/87H/8FH gegen Tabelle 1.1.
3. **ATS 045-8732 = `K7028` (K7028.30) unverändert**, gleiche Ports und Spiegel, Latch 61H,
   **Rückschleife SIO1-A/SIO1-B/SIO2-A** über `Config::pruefstecker` (Vorgabe wie V3).
4. **K7672** an SIO2-B unverändert (SCP-Modus im ROM, `DC1` auf `ESC c`/`ESC [2;1y`).
5. **K7024** mit Bild bei 1000H (`forK8915()`, `read_protect = false`), aber Zeichengenerator
   **`v171`/`v172` = A5120-Satz** (`chargen_zg1/zg2`), nicht `y411`/`y412` [ROM: Abzüge 171/172].
6. **K5122 im `/WAIT`-Zweig** unverändert (Ports 10H–18H), Laufwerke wie V3.
7. **Interruptkette** mit K2521-CTC, ATS (CTC1, CTC2, SIO2) und K5122; Reihenfolge [?] wie V3.
8. **Keine** PFS K3820 im Gerätemodell (V5 = Beigabe).

**Ablauf (V6), Wächter-Kandidaten**
9. Netz-Ein ⇒ Selbsttest; **mit dem Abzug 177 Kennbuchstabe `C` bei „ROM“** + ERROR-Lampe
   (61H = 7FH) + 16 × BEL ⇒ `CR` ⇒ „\* Coldstart \*  Disk on A: ready ?“ — prüfbar **ohne
   Diskette**. Mit Diskette 901 nach `CR` bis `A>` (Ladekopf wie V3).
10. Den vollen Selbsttest (KEY/CTC/SIO/RAM) erreicht man nur mit fehlerfreiem ROM: Wächter dafür
   mit einer **im Test** geflickten Summe (Kopie, nicht im Repo-Abzug) oder erst nach F9.
11. Warmstart-Abkürzung wie V3: `JP` bei 0000H/0005H im RAM ⇒ Stub (8FH) ⇒ Kaltstartmeldung ohne
   Selbsttest.
12. `E`/`ESC c` an der Kaltstartmeldung ⇒ Neubeginn bei 0400H; NMI im ROM wirkungslos (`RETN`).
13. Übergabe ans System mit A8H = 0EH oder 06H (je Weg), nicht wie V3 stets 06H.
14. `boot_trace`/`k1520dbg`: Listing `k8915g2_zre.prn` in Teilbereichen annotieren
   (`@0xFC00:0021-03FF` für die Kopie, `…:098F-09A5` bei FFE0H).

**Gen 1 (V9)**
15. Das Gen-2-ROM fragt die K7634 nicht ab; die Kartenchips sind der Gen-1-Urlader, aber
   **ohne passendes 176** (0Cxx-Zellen, Stub 095BH) nicht startfähig — kein Nachbau aus dem
   ZRE-176 (F11). Ports der Gen 1 laut Chips: E0H–E4H, F4H–F7H, F8H–FEH, B1H/B3H, E3H.
