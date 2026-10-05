# 24 — K8915-Varianten: „Generation 2“ (Gerät des Anwenders) und „Generation 1“ (Planung)

Stand: 2026-10-05, Zweig `K8915Varianten` (von `main` @ 5190c9c). **Planungsdokument** —
noch kein Kerncode. **Abzüge der Gen-2-ZRE, der K7024 und der 2708-Karte liegen vor**
(`doc/EPROMS/K8915G2/`, §10 am Ende); Auswertung = AP-V3. Der Emulator bildet bisher nur die **V3** ab (`doc/design/16_k8915.md`,
ZRE 045-8762 mit 128 KB, `core/machines/k8915/`).

Legende wie in Entwurf 16: **[ROM]** aus einem Abzug, **[SLP]** Stromlaufplan, **[Web]**
Sekundärquelle, **[Gerät]** am Original des Anwenders, **[?]** Vermutung/offen.

---

## 1. Ziel und Abgrenzung

Zwei weitere K8915-Ausführungen als Varianten derselben Maschine (wie PRG 710 / 710-1):

- **„Generation 2“** — liegt als Gerät beim Anwender vor. Bestückung laut Anwender:
  ZRE **wie im PRG 710 (K2521-Familie), aber mit 3 EPROMs**, eine **zusätzliche RAM-Karte**
  und eine **Karte mit 16 × 2708 (je 1 KB)**. Die 2708-Karte hält der Anwender nicht für
  Standardbestückung. EPROM-Abzüge liefert der Anwender nach.
- **„Generation 1“** — kein Gerät, **kein Abzug**; nur über Dokumentation und Fremdquellen.

**Nicht Ziel:** Magnetkassetten-Version (KOKOS/KOBRA, „ausgestorben“ [Web]), Harddisk-Version.

---

## 2. Begriffsklärung — offene Zuordnung (Frage F1, blockiert die Namensgebung)

Entwurf 16 §2 und robotrontechnik.de kennen **sechs** Ausführungen, keine „Generationen“:

| robotrontechnik.de [Web] | ZRE / RAM | Tastatur | OS |
|---|---|---|---|
| Magnetkassette | — | — | KOKOS, KOBRA |
| 8″ | K2521, K3528 (64 KB), K5121 | K7634 (PIO) | SCPX 0/2, BIOS G3.2 |
| **5¼″ V1** | K2521, K3528 (64 KB), K5122 | K7634 (PIO) | SCPX 0/2, BIOS 4.x |
| **5¼″ V2** | K2521, K3528, 045-8778 (ABS 2K), K5122, 045-8732 | K7672 (flach) | SCPX 8915 5.x/8.x/9.x, CP/Z 2.2 |
| **5¼″ V3** (im Emulator) | 045-8762 (ZRE + 120/128 KB), 045-8732 | K7672 | SCPX 8915 |
| Harddisk | 045-8767, K5122, 045-8762, 045-8732, 045-8778 | K7672 | SCPX 8915 V0/2, BIOS 8.3 |

Die Bestückung des Anwendergeräts (K2521-artige ZRE + eigene RAM-Karte) passt zu **V1 oder
V2** der Tabelle, nicht zu V3. **[?] Zu klären:** Ist „Generation 2“ = „5¼″ V2“ und
„Generation 1“ = „5¼″ V1“ (oder die 8″-Version)? Dann gilt: Gen 1 = K7634/PIO-Tastatur,
Gen 2 = K7672. Der Anwender liest am Gerät ab: Tastaturtyp, Kartenbezeichnungen
(Platinen-Nr. 012-/045-…), Stecker-Belegung der Steckplätze, Laufwerke.

---

## 3. Quellenlage

### 3.1 Gefunden (alle bereits ausgewertet)

| Quelle | Inhalt | Nutzen |
|---|---|---|
| [tiffe.de K1520/K2521/](https://www.tiffe.de/Robotron/K1520/K2521/) `K2521_Beschreibung.pdf` | Kartenbeschreibung: **3 × U555 (2708) bei 0000–0BFFH, 1 KB RAM 0C00–0FFFH**, CTC 80H–83H, PIO 84H–87H, /MEMDI-Brücken X8–X9, Takt 9,8304 MHz : 4 | Hardware der Gen-2-ZRE; bereits im Kern (`core/cards/k2521/`, PRG 710) |
| [tiffe.de K1520/K3528/](https://www.tiffe.de/Robotron/K1520/K3528/) `OPS_K3528-1/-2.pdf` | **Scans 168/154 MB** der 64-KB-RAM-Karte | Adressierung/Brücken der RAM-Karte — **noch nicht gelesen** (AP-V1) |
| [tiffe.de K1520/K7634_36/](https://www.tiffe.de/Robotron/K1520/K7634_36/) `K7634-36.pdf` | Parallele Tastatur K7634/36 (5,1 MB) | Tastatur Gen 1 [?] — **noch nicht gelesen** (AP-V1) |
| [tiffe.de CPM-KRZ/](https://www.tiffe.de/Robotron/CPM-KRZ/) | CP/M-3-Quellen (BIOS2710, FL, KM, LFLO…), Lader, `ccp.bin` | **Nur für K2521 + K5122 + K3528/K3526 + K7634**, also Gen-1-nahe Hardware; Quelle für Speicherumschaltung und Boot [?]; Teile (KMB/Kassette) irrelevant |
| Forum [CP/M auf K2521/K5122](https://www.robotrontechnik.de/html/forum/thwb/showtopic.php?threadid=12101) | System aus K2521, K3526 64K, K3525 16K, K7028, K5122, K7024; Speicherumschaltung über **PIO Port B → /MEMDI** | Bestätigt, dass die K2521 die Bankumschaltung per PIO macht |
| Forum [SCP auf K2521 + AMF im Wait-Betrieb](https://www.robotrontechnik.de/html/forum/thwb/showtopic.php?threadid=5713) | ROM „V.09“ (1 KB) lädt Bootspuren nach 0400H; /MEMDI1+2 aus PIO in 8 Bitmustern; genannte Ports: PIO **08–0FH**, CTC 80–83H, ATS E0–FFH, AMF 10–2FH | **Widerspruch zur Kartenbeschreibung (PIO 84H–87H)** — vor Verwendung klären (AP-V1/V3) |

### 3.2 Nicht gefunden

- **Kein EPROM-Abzug einer Gen-1-/V1-/V2-ZRE.** Auf tiffe.de liegt unter `K8915/Eprominhalte/`
  nur `k8915.bin` = `k8915-ZVE_2732.bin` = **V3-Boot-ROM** (beide MD5 `19e301bd…`, identisch mit
  `doc/EPROMS/K8915/k8915_boot_2732.bin`) und `c10_char.bin` (Zeichensatz, 8 KB, MD5
  `723eaf3f…`), dazu `EPROMs_ABS/`. Keine Treffer in Suchmaschinen, Foren, classic-computing.
- **Keine Systemdiskette SCPX 0/2 BIOS 4.x oder G3.2.** Forum erwähnt nur private Angebote
  („Rolly2 bietet SCP-Bootdisketten an“; „Rüdiger bot SCP1526/710 mit Boot-ROM an“) — **nicht
  verifiziert, nicht heruntergeladen**.
- **Folgerung:** Gen 1 ist ohne Fremdhilfe **nicht bootfähig nachbildbar**. Das PRG-710-ROM
  (`doc/EPROMS/PRG710/`, 1 KB-Lader, ebenfalls K2521) ist ein Nachbar, aber **kein** K8915-ROM.
  Nächster Schritt dafür: Anfrage an die Foren (robotrontechnik.de, VzEkC) und an das
  Rechenwerk Halle, wo laut Web eine V1 läuft [Web].

---

## 4. Was der Kern schon hat (Wiederverwendung)

| Baustein | Stand | Gen 2 / Gen 1 |
|---|---|---|
| `K2521` (`core/cards/k2521/`) | U880, ROM bis 3 × 1 KB, 1 KB RAM, CTC 80H, PIO 84H, Brücken X10/X14; Speicher über **externen Speicherweg** (kein `registerMem`) | **Kernstück beider Varianten**; Speicherverwaltung fehlt (PRG: `prg710_speicher`, E8H–EBH) |
| `K7024`, `K5122` (`/WAIT`-Zweig), `Laufwerke` | wie V3 | unverändert; K5122 ist im PRG 710 mit K2521 schon im Betrieb |
| `K7028` (ATS, 2×SIO+2×CTC) | V3 = 045-8732 | V2 hat sie auch (045-8732); V1 nennt „K7028“ (K7028.10/.20?) [?] |
| `k7672` (Tastatur) | V3 | V2 ja; **V1: K7634 (PIO)** — neu |
| ZRE 045-8762 (`zre8762`) | V3 | **nicht** Teil von Gen 1/2 |
| `prg710_speicher` | Seitenregister E8H–EBH | Vorlage für die RAM-Karte, **nicht** übertragbar (andere Platine) [?] |

Maschinenform: wie PRG 710 eine Klasse mit `Variante`-Wahl (`Prg710Machine`) **oder** Erweiterung
von `K8915Machine` um `Config::generation` — Entscheidung in AP-V2 (Empfehlung: **eine Klasse
`K8915Machine`, `Config::generation {V3, Gen2, Gen1}`**, damit Oberfläche, Debugger, Paket und
Starter `k8915emu` unverändert bleiben und `Config::variante` das Modell wählt, wie beim A5120
die `modellwahl`).

---

## 5. Gen 2 — Bestand und Unbekannte

### 5.1 Gesichert (K2521-Beschreibung)

ROM 3 × U555 0000–0BFFH (steckbar), RAM 0C00–0FFFH, CTC 80–83H, PIO 84–87H, Interruptkette
CTC vor PIO, Koppelbus-Brücken X10/X11, IEI-Wahl X14/X15. Das ist die schon modellierte Karte.

### 5.2 Unbekannt (alles [?], bis Abzüge/Gerätebefund da sind)

1. **Inhalt der drei EPROMs** — liegt vor (§10): Urlader mit Selbsttest und Floppy-Boot, **kein**
   CCP. Offen bleibt die genaue Hardware-Zuordnung der Ports.
2. **Die RAM-Karte:** K3528 (64 KB) oder K3526 + K3525? Adressierung, /MEMDI-Nutzung,
   Bankumschaltung. Der Anwender nennt eine einzelne „extra RAM-Karte“; bei 64 KB ergäben
   ZRE-RAM (1 KB) + Karte kein vollständiges 64-KB-System ohne Umschaltung der ZRE-ROM.
3. **Die 2708-Karte (16 × 1 KB = 16 KB EPROM):** ist die **PFS K3820** (Platine 012-7040/-7041,
   Handbuch `Speichersteckeinheiten_K3520…K3820_Betriebsdokumentation.pdf` §6, Chips Abb. 7,
   Startadresse über X8/X9 in 4-KB-Schritten, MEMDI über X6/X7). Inhalt: **nur 2 von 16 Chips
   programmiert** (§10) — eine abweichende Fassung des Urladers. Der Anwender hält sie nicht
   für Standard — deshalb **optional** modellieren (wie RAF/K6022: steckbare Karte, Vorgabe aus),
   und nur, wenn AP-V3 zeigt, dass der Gast sie benutzt.
4. **Speicherumschaltung** der ZRE per PIO-Port B (→ /MEMDI, /MEMDI1, /MEMDI2) und welche
   Bitmuster welche Karte freigeben (Forum nennt 8 Muster [Web]); die Wickelbrücken-Stellung
   X8–X9 am Gerät ablesen.
5. **Tastatur und Bildschirmkarte** der Gen 2 (K7634 oder K7672; K7024 oder 045-8778 ABS 2K).
6. **Laufwerke/Controller:** K5122 im `/WAIT`-Betrieb wie V3 [?]; Drive-Typen.

### 5.3 Auslesen der 2708 — Hinweise für den Anwender

U555/2708 brauchen **+12 V, +5 V, −5 V** — viele Programmer (TL866) lesen sie **nicht**
(der eigene Dump der V3 war ein 2732). Der Weg läuft deshalb über den **EPROMmer des PRG 710**
(`doc/prg710/eprommer.md`) oder einen Adapter mit −5 V/+12 V. Je Chip: zweimal lesen, MD5
vergleichen, Beschriftung/Bestückungsplatz (D-Nummer) festhalten. **Vorschlag für die
Ablage:** `doc/EPROMS/K8915G2/<karte>_<platz>_<aufschrift>.bin` + `README.md` (Muster:
`doc/EPROMS/PC1715/README.md`), und die ZRE-Bausteine **einzeln** nach Platz 0000/0400/0800.

---

## 6. Gen 1 — Bestand und Unbekannte

Nur belegt über [Web]: K2521 + K3528 (64 KB) + K7024 + K7028 + K5122, Tastatur **K7634 (PIO)**,
SCPX 0/2 BIOS 4.x, zwei 5¼″-Laufwerke extern. Alles Weitere ist offen:

1. Boot-ROM (Inhalt, Selbsttest, Lader) — **kein Abzug** (§3.2).
2. K3528: Adressierung/Umschaltung — Datenblatt im Netz (OPS_K3528, Scans), **nicht gelesen**.
3. K7634: Tastenmatrix, PIO-Anbindung an die K7028 (ATS) — K7634-36.pdf, **nicht gelesen**.
4. Ob die Gen-1-ATS eine K7028.10/.20 ist (andere Bestückung als 045-8732) [?].
5. BIOS/System: SCPX 0/2 4.x. Aus dem CPM-KRZ-Archiv (CP/M 3, BIOS2710.MAC) ließe sich ein
   **lauffähiges Testsystem** bauen [?], aber das ist **nicht** das Original und beweist die
   Nachbildung nur bedingt.

**Empfehlung:** Gen 1 erst bauen, wenn ein Abzug oder eine Diskette beschafft ist. Bis dahin
nur Vorarbeiten: K7634-Tastatur, K3528-Karte, Gen-1-Bootpfad **auf dem Papier**. Ein ROM, das
niemand je gesehen hat, nachzuerfinden wäre kein Nachbau.

---

## 7. Arbeitspakete

Reihenfolge nach Abhängigkeit. **Gen 2 zuerst** (Hardware am Gerät, Abzüge kommen), Gen 1
danach (blockiert auf Beschaffung).

| AP | Inhalt | Braucht | Fertig, wenn |
|---|---|---|---|
| **V0** | Dieses Dokument; Fragen F1–F6 an den Anwender | — | Antworten da |
| **V1** | Unterlagen lesen: `OPS_K3528`, `K7634-36.pdf`, K2521-Schaltplan (Blätter 1–3); Ergebnis als Merkposten-Entwurf | Netz | RAM-Karte, Tastatur, Brücken beschrieben, Widerspruch PIO 84H/08H geklärt |
| **V2** | Entwurf der Maschinenform (`generation`, `Config`), Steckplatzplan, Port-/Speicherkarte Gen 2; Entscheidung eine Klasse vs. zwei | V1, F1 | Plan im Dokument, ohne Code |
| **V3** | **EPROM-Abzüge Gen 2** auswerten: Disassembler `tools/`, `.prn` mit Kommentar (Muster `doc/EPROMS/PRG710/*.prn`); ROM-Zweck, Port-/Speicherzugriffe, Boot-Ablauf | Abzüge | Boot-Ablauf dokumentiert; Port-Liste mit [ROM]-Belegen |
| **V4** | Speicherverwaltung Gen 2 (RAM-Karte + ZRE-ROM-Abblendung) als eigene Karte unter `core/cards/`; Unit-Tests | V2, V3 | Tests grün; `/MEMDI`-Muster belegt |
| **V5** | 2708-Karte (16 KB) als **optionale** Karte, nur wenn V3 ihre Rolle klärt | V3 | Rolle belegt; Wächter |
| **V6** | `K8915Machine`-Zweig Gen 2: Verdrahtung, Boot bis Prompt von einer Gen-2-Diskette; `boot_trace`/`k1520dbg --machine k8915 --generation 2` | V4 | Boot bis Prompt, Integrationstest |
| **V7** | C-ABI/Python (`K1520Emulator(machine="k8915", generation=…)`), Programmprofil `k8915emu` (Modellwahl wie `general.model`), Konfiguration, Handbuch; `test_c_api.py` gleicht ab | V6 | `py_k8915emu_gui` + `py_c_api` grün |
| **V8** | DiskTool: Disketten der Gen 2 (Format, Profil) — falls vom V3 abweichend | Disketten | `ls`/`check` ohne Befund |
| **V9** | Gen 1: Tastatur K7634 (`core/peripherals/`, wie K7672), K3528-Speicher | V1, Abzug/Diskette | Boot bis Prompt **oder** begründet zurückgestellt |
| **V10** | Abschluss: Merkposten `doc/merkposten/k8915_varianten.md`, CLAUDE.md, Verweis in 16 | alle | Doku-Wächter grün |

Jedes AP: eigener Commit auf dem Zweig (nicht pushen), `tools/dev.sh test` vor dem Commit, vor
dem Merge alle vier Läufe (`test`, `test-format`, `test-matrix`, `win`).

---

## 8. Risiken

- **Hardware-Nachbau ohne ROM:** Gen 1 ist ohne Abzug nicht zu verifizieren (§3.2). Kein
  Nachbau aus der Phantasie.
- **K2521 ≠ PRG-710-Speicherverwaltung:** die K2521 selbst kennt keine Seitenregister (E8H–EBH
  sind **PRG-spezifisch**, Karte `prg710_speicher`); Gen 2 braucht eine eigene Lösung.
- **Zwei Quellen, zwei PIO-Adressen:** K2521-Beschreibung 84H–87H, Forum 08H–0FH (§3.1) —
  die ROM-Abzüge entscheiden.
- **2708-Auslesen:** −5 V/+12 V nötig; Fehlablesungen sehen wie Emulatorfehler aus. Doppelte
  Abzüge mit MD5 sind Pflicht.
- **Namen:** „Generation“ ist kein Begriff der Quellen (F1). Ein falsch gewählter Name landet
  in Oberfläche, Konfiguration und Handbuch.

---

## 9. Fragen an den Anwender

| # | Frage |
|---|---|
| **F1** | Entspricht „Generation 2“ der „5¼″ V2“ (K7672, K2521, K3528, 045-8778?) und „Generation 1“ der „5¼″ V1“ (K7634)? Oder sind es andere Bezeichnungen (z. B. Aufschrift am Gerät)? |
| **F2** | Kartenliste des Gen-2-Geräts mit **Platinennummern** (012-/045-…), Steckplätzen und Tastaturtyp (K7634 PIO oder K7672 flach); Fotos beider Seiten der RAM- und der 2708-Karte. |
| **F3** | Wickelbrücken-Stand der ZRE (X6–X15) und der RAM-Karte; Aufschriften der drei ZRE-EPROMs und ihre Plätze. |
| **F4** | Laufwerke und Controller (K5122 oder anderer); eine Systemdiskette (Image) falls vorhanden. |
| **F5** | Darf Gen 1 bis zur Beschaffung eines Abzugs warten (Empfehlung: ja)? Soll ich in den Foren (robotrontechnik.de, VzEkC) und beim Rechenwerk Halle anfragen? **Das ginge nur mit Ihrer Freigabe — ich schreibe nichts ohne Rückfrage in ein Forum.** |
| **F6** | Der Name der Variante in der Oberfläche (z. B. „K8915 (Gen 2)“) und ob `k8915emu` die Modellwahl wie der A5120 erhält. |

---

## 10. Abzüge der Gen-2-Hardware (2026-10-05, AP-V3 vorbereitet)

Ablage, Prüfsummen und Einzelbefunde: **`doc/EPROMS/K8915G2/README.md`**. Kurzfassung:

- **ZRE: drei Bausteine 175/176/177 (0000/0400/0800, Reihenfolge aus dem Inhalt [?])** =
  Urlader: Einstieg DI/IM 2, `OUT (A8H)`, **kopiert sich nach FC00** und läuft dort; Selbsttest
  „MROM RAM SIO KEY CTC“; Floppy-Boot über Ports 10H–19H; Meldungen „Coldstart * Disk on A: ready“,
  „No system disk“. **Nicht** das V3-Boot-ROM (1007 von 1024 Bytes verschieden) und **nicht** die
  PRG-710-Lader — aber im Aufbau verwandt (A8H, 61H, Meldungen). Folgerung: die Gen-2-ZRE ist
  keine K2521 pur, sondern hat mindestens Speicherumschaltung über **A8H** wie die V3-ZRE
  [?, Port-/Bankbelegung in AP-V3 klären].
- **K7024: 171/172 sind byteidentisch mit `v171`/`v172`** (A5120-Zeichensatz) — nichts Neues,
  der vorhandene Zeichengenerator genügt.
- **2708-Karte PFS K3820: 14 von 16 Chips leer (FFH)**, zwei programmiert: „3C00“ = andere
  Fassung des Urladers (Selbsttest „I/O“, Code bei FC00), „3000“ = Lader-Meldungen wie ZRE 177,
  aber Arbeitszellen bei 0Cxx. Vermutung: eine **ältere/andere Urladerfassung** auf der Karte;
  Lage vermutlich ab C000 (Chip „3C00“ = FC00–FFFF) — **vom Brückenstand X8/X9 abhängig, am Gerät
  abzulesen** (Frage F3).
- Folge für den Plan: **V5 (2708-Karte)** schrumpft auf „Abzug als Beigabe, keine eigene
  Karte“, solange kein Gast sie anspricht; V3 bekommt als erste Aufgabe, die drei ZRE-Bausteine
  und die zwei Kartenchips zu vergleichen (Disassembly mit `tools/`, Muster
  `doc/EPROMS/PRG710/*.prn`).
