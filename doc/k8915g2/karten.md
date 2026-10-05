# K8915 „Generation 2“ — Karten: K2521 (ZRE), K3528 (RAM), K3820 (PFS), Speichersteckeinheiten

Stand 2026-10-05, Ergebnis von **AP-V1a** (`doc/design/24_k8915_varianten.md`). Nur Lesen von
Unterlagen, kein Kerncode. Alle Unterlagen stammen von tiffe.de und liegen **nicht** im Repo
(Scratchpad der Sitzung; bei Bedarf neu laden, URLs unten).

Legende: **[SLP]** aus einem Stromlaufplan, **[Text]** aus gedrucktem Beschreibungstext,
**[hand]** handschriftlicher Eintrag auf einem Scan (Urheber unbekannt), **[ROM]** aus den
Abzügen `doc/EPROMS/K8915G2/`, **[?]** Vermutung/nicht eindeutig lesbar.
Fundstellen: *Datei, Seite/Blatt, Bildbereich*. „PDF-S.“ = Seitenzahl in der PDF-Datei;
„gedr. S.“ = aufgedruckte Seitenzahl.

## 0. Quellen und Lesbarkeit (ehrlich)

| Quelle | Zustand |
|---|---|
| `K2521/K2521_Beschreibung.pdf` (12 S.) | Textebene vorhanden (`pdftotext`), Abbildungen als Bild. Seiten wie ein Auszug aus „Betriebsdokumentation K 1520, Heft 1“, gedr. VII-4 … VII-20 (`K2521_sch/scan00…scan10.jpg` sind dieselben Seiten als Foto). |
| `K2521/K2521_sch/Schaltplan_1…3.jpg` (3201–3337 px) | gut lesbar; Blatt 2 = Adressdekoder/E/A-Teil. Die Dateien `K2521_Schaltplan_1…3.jpg` (kleiner) **nicht** verglichen. |
| `K2521/BSPlan_CPU.jpg` | Belegungsplan, **auf dem Kopf stehend** (180°), lesbar. Identisch mit `K5221_Bestueckung.jpg` (gleiche Dateigröße). |
| `K3528/OPS_K3528-1.pdf`, `-2.pdf` | je **1 Seite**, ein Rasterbild 9897×7042 px (300 dpi), Blatt 1/2 und 2/2 eines Stromlaufplans, mit **handschriftlichen Eintragungen in Bleistift/Blau/Rot**. Mit 150 dpi gerendert, Ausschnitte gelesen. Eine Beschreibung/ein Text zur K3528 liegt **nicht** vor — alles unten ist aus dem Plan gelesen. |
| `K3820/Speichersteckeinheiten_K3520_3521_3525_K3820_Betriebsdokumentation.pdf` (31 S.) | reine Scans (CCITT), keine Textebene; gelesen als Bild. **gedr. S. = PDF-S. − 2.** |
| `K3820/…Serviceschaltplaene.pdf` (20 S.) | **nicht gelesen** (für die Fragen dieses AP nicht nötig). |
| `K3820/K3520_K3820_Heft2.pdf` | 218 Byte, **keine PDF** (HTML-Fehlerseite); Download unbrauchbar. |

URLs: `https://www.tiffe.de/Robotron/K1520/{K2521,K3528,K3820}/…`.

---

## 1. K2521 — ZRE (Gen-2-Zentraleinheit)

### 1.1 Größe, Organisation (alles [Text])

| Merkmal | Wert | Fundstelle |
|---|---|---|
| CPU | Q300 = U880, Takt 2,4576 MHz (Quarz 9,8304 MHz / 4, bei K2521/K2523) | Beschreibung S.1 „Takterzeugung“; scan00 (VII-4) |
| ROM | **3 × U555 (= 2708, Q260), 0000–03FF / 0400–07FF / 0800–0BFF**, steckbar, „abrüstbar in Stufen zu 1 KB“ | Beschreibung S.3 „Struktur“; scan00 (VII-5) „Speicher“ |
| RAM | **1 KB** statisch, 8 × U202 (1 Bit breit), **0C00–0FFF** | Beschreibung S.3; Schaltplan_1 (RAM A18–A25) |
| Gesamt | 4 KB, „fest“ adressiert (keine Brücke verlegt ROM/RAM) | scan00 (VII-5) „Adressierung: fest“ |
| CTC Q302 (U857) | **E/A 80H–83H** (Kanal = AB1:0) | Beschreibung S.6; scan00 VII-5 (dort „0080H … 0083H“) |
| PIO Q301 (U855) | **E/A 84H–87H**: 84H = Daten A, 85H = Daten B, 86H = Steuerwort A, 87H = Steuerwort B (AB0 = B/A-SEL, AB1 = C/D-SEL) | Beschreibung S.10; scan01 (VII-6) „Ø084H … Ø087H“ |
| Interruptkette | CTC höchste Priorität, PIO dahinter (IEO CTC → IEI PIO) | Beschreibung S.6 und S.10 |
| Speichersperre | Standard **/MEMDI**; wahlweise **/MEMDI1** oder **/MEMDI2** (müssen vom Anwender bereitgestellt werden) | Beschreibung S.3 |
| /RDY, /WAIT | RDY wird bei jedem Speicher- und E/A-Zugriff der Karte gezogen (offener Kollektor) | Beschreibung S.3, S.6, S.10 |

Speicheraktivierung: /MREQ = low, **AB12–AB15 = low**, /RFSH und /MEMDI (bzw. -1/-2) = high
(Beschreibung S.3 „Funktion“). Die ZRE belegt damit **nur 0000–0FFF** und sperrt sich beim
Anlegen des gewählten /MEMDI-Signals.

### 1.2 Wickelbrücken (Tabelle der Stellungen)

Lage der Felder: `BSPlan_CPU.jpg` (= Beschreibung S.4, Abb. VII-13, auf dem Kopf stehend);
Funktion: Beschreibung S.1, S.3, S.6, S.10–S.11. Die **Vorgabestellung** steht nur dort, wo
der Text einen „Normalfall“ nennt.

| Feld | Stellung | Wirkung | Fundstelle |
|---|---|---|---|
| X6–X7 | geschlossen (Vorgabe, [?]) / offen | Takt TAKTO auf den Koppelbus; offen, wenn der Takt von außen kommt | Beschreibung S.1 |
| **X8:1–X9:1** | | ROM/RAM der ZRE gesperrt durch **/MEMDI** (X1:B09) | Beschreibung S.3 |
| **X8:2–X9:2** | | … durch **/MEMDI1** (X2:A21) | ebd. |
| **X8:3–X9:3** | | … durch **/MEMDI2** (X2:B21) | ebd. |
| X10:3–X11:3 / :2 / :1 | | CTC-Kaskade ZC/TO0→CLK/TRG1, TO1→TRG2, TO2→TRG3 | Beschreibung S.6 |
| X12/X13 (:1–:5) | | Mehrrechnerkopplung Master/Slave (PIO-Port B als Kopplung) | Beschreibung S.11; Abb. S.10 [Tabelle 1 dort nicht ausgewertet] |
| **X14:1–X15:1** | Normalfall | /IODI der ZRE (ZRE hat höchste Priorität) | Beschreibung S.11 |
| X14:2–X15:2 | | ZRE wird mit /IEI in die Prioritätskette eingereiht | ebd. |
| X14:3–X15:3 | | UM (Kopplung mit Entwicklungssystem MRES 20 über BVE K4120) | ebd. |
| X17 | im Plan eingezeichnet, im Text **nicht** beschrieben | liegt im Pfad Dekoder-Ausgang 00 → CTC-Auswahl, siehe 1.3 | Schaltplan_2, Dekoder A34 [?] |

Kein Brückenfeld ändert die E/A-Adressen von CTC/PIO (siehe 1.3).

### 1.3 Antwort: PIO 84H–87H oder 08H–0FH? — **84H–87H** [Text + SLP]

* Der **Text** nennt zweimal ausdrücklich 84H–87H (Beschreibung S.10, scan01 VII-6) und CTC
  80H–83H (S.6, scan00 VII-5). Eine zweite Adressangabe kennt die Beschreibung nicht.
* Der **Schaltplan** (`K2521_sch/Schaltplan_2.jpg`, Blatt 2, unten Mitte, Dekoder **A34 = 8205**,
  „1 aus 8“) stützt das: Die drei Auswahleingänge A0/A1/A2 hängen an den Abgriffen 03/04/05
  des Adressbus-Bündels, die Freigaben E1/E2 an 06/07, E3 an der IORQ/IODI-Verknüpfung
  (A31, A40) mit Abgriff 08. Die Abgriffe 01–08 laufen der Reihe nach als AB0…AB7 [?:
  Abgriffsnummern **nicht** gegen die Steckerbelegung geprüft, nur gezählt]. Danach ist die
  Auswahl **AB7 = 1, AB6 = AB5 = 0, AB4:2 = Ausgangsnummer**: Ausgang 00 → 80H–83H,
  Ausgang 01 → 84H–87H. **Nur Ausgang 00 (über X17) und 01 sind belegt**, Ausgänge 02–07 enden
  offen (Schaltplan_2, Ausschnitt rechts vom A34). Eine E/A-Adresse **08H–0FH** (AB7 = 0)
  ist mit dieser Dekodierung **nicht erreichbar**.
* Was **nicht** nachverfolgt wurde: die Leiterbahn von Ausgang 00/01 bis zu den CS-Pins von
  CTC (A11) und PIO (A10). Die Zuordnung 00 → CTC, 01 → PIO folgt aus Text + Adresslage,
  nicht aus dem Verfolgen der Leitung [?].
* **Erklärung des Forum-Widerspruchs (Forum 5713, „PIO 08–0FH“):** unbekannt. Möglich: andere
  Karte/Variante, Verwechslung (die 0Axx-Ports der A5120-BS-PIO liegen bei 08H–0BH), oder
  hex/dezimal. Dort genannt: CTC 80–83H (stimmt mit K2521), ATS E0–FFH, AMF 10–2FH. Auf der
  K2521 selbst ist 08H–0FH **nicht** möglich [SLP]. → Frage F24.
* **Befund aus dem Abzug (Vorgriff auf V3b):** Die Gen-2-ZRE-ROMs enthalten **keinen**
  unmittelbaren Zugriff auf 84H–87H **und keinen** auf 08H–0FH (`IN/OUT (n)`, alle
  Fundstellen mit `tools/z80_disasm2.py --linear` ausgewertet) und keine `LD C,84H…87H`/
  `LD C,08H…0FH` zu `OTIR`/`INIR`. Genutzt werden u. a. 80H, 83H (CTC, [ROM] `OUT (80H)`,
  `OUT (83H)`), 10H–18H (Floppy), 44H–5CH, 61H, **A8H**. Der Boot braucht die PIO der ZRE
  offenbar nicht; die Frage ist für den Start folgenlos (Detail: V3b).
* `LD C,0AH` bei ROM 0300 gehört zu einem `LDIR` (Zähler), kein Port.

### 1.4 Weitere Randbefunde

* **Handschriftlicher Hinweis „für 08 → 16“** in `Schaltplan_1.jpg` (links unten): Pinänderung
  beim Ersatz der 2708 durch eine 2716: Pin 21 −5 V → +5 V, Pin 20 CS → /OE, Pin 19 −12 V →
  A10, Pin 18 PR → /CS [hand]. Heißt: ROM-Plätze sind 24-polig auf 2708 verdrahtet
  (+12 V/±5 V), eine 2716 braucht Umbau; relevant für den Anwender beim Auslesen (Entwurf 24 §5.3).
* ROM-Chips A26/A27/A28 (Plätze 0000/0400/0800), RAM A18–A25, Dekoder A3/A4 (8205) für die
  1-KB-Auswahl, Datenpuffer A1/A2 (8216) [SLP Schaltplan_1].

---

## 2. K3528 — OPS 64 K D-RAM (die „zusätzliche RAM-Karte“?) [F2 offen]

Quelle: `K3528/OPS_K3528-1.pdf` = **Blatt 1/2** (Logik), `-2.pdf` = **Blatt 2/2** (Speichermatrix).
Schriftfeld: „OPS K3528 64K D-RAM, .00 64K / .10 48K / .20 32K“, VEB Robotron-Elektronik
Zella-Mehlis, 1984/85. Zeichnungsnummern (Lesung unsicher): .00 `1.45.518530.6/04`,
.10 `1.45.518550.7`, .20 `1.45.518551.5`.

### 2.1 Größe und Organisation

| Merkmal | Befund | Fundstelle |
|---|---|---|
| Typen | **K3528.00 = 64 KB, .10 = 48 KB, .20 = 32 KB** | Blatt 1 und 2, Schriftfeld |
| Speicher | **32 dynamische RAM-Chips „Q281“** (D14.01–D14.32, 4 Reihen × 8, je Reihe 16 KB, **16K × 1 Bit** [?: Chiptyp aus Anzahl und 7 Multiplex-Adressen PA0–PA6 geschlossen]) | Blatt 2/2 |
| Abrüstung | **.10: D14.25–D14.32 entfallen** (eine Bank), **.20: D14.17–D14.32 entfallen** (zwei Banken) | Blatt 2/2, Randvermerk rechts, gelesen |
| Versorgung | +5 V, +12 V, **−5 V** (Anschlüsse 5P, 12P, 5N) | Blatt 1, Siebkondensatoren C1/C3 unten rechts |
| Banken | vier 16-KB-Banken, Auswahl **AB14/AB15** über Dekoder **D2:03 (8205)**, Freigabe E3 = H3 (+5 V), E1/E2 = 0 V; Ausgänge 00–03 → /CAS1…/CAS4 (jede Bank ein CAS) | Blatt 1, Ausschnitt Mitte unten (Dekoder D2:03), oben /CAS1–4 |
| Bank-Brücken | Wickelfeld **X3:64–X3:71** (Ausgänge D2:03 ↔ Pull-ups R2:10–R2:12). Handeintrag **„64K alle Brücken“** | Blatt 1 [hand] |
| Gültige Adresse | NAND-Verknüpfung D10 (LS20) „gültige Adr.“ mit /MREQ und /RFSH | Blatt 1 Mitte |
| Adressmultiplex | vier 8-Bit-Register (Adressen AB0–AB15 getaktet), 7 Multiplexleitungen PA0–PA6 zu den Chips; Prüfpunkte PP1–PP6; /RAS, /CAS-Steuerung per D-FF D4:01/D4:02 und Gatter, Takt = TAKT | Blatt 1 links, Blatt 2 unten |
| Refresh | **selbständig**: die Karte erzeugt RAS/CAS für /RFSH-Zyklen; /RFSH und AB0–AB6 vom Bus | Blatt 1 „RFSH-Adresse“, „RAS-Steuerung“ |
| WAIT | eigene Wait-Bildung mit D-FF-Kette (T1/T2/TW) und Brücken X3:60–X3:63 (WAIT/RDY); Ausgänge /WAIT (X1) und Datenbusbeteiligung /RDY | Blatt 1 links oben [genaue Brückenstellung **nicht** gelesen] |
| /RDY | OC-Ausgang X1:B25 | Blatt 1 rechts oben |

### 2.2 Das Register (die eigentliche Neuigkeit)

Die K3528 enthält ein **8-Bit-Ausgaberegister D8 (Bauteilkennung „SE12“ = U212/8212,
„8 Bit par. Ein-/Ausgaberegister“, belegt durch die Bauelementetabelle K3820-Doku gedr. S.6)** mit
Datenbus-Eingängen DI1–DI8, Ausgängen DO1–DO8, CS1/CS2, MD, STB, CLR. Handeintrag daneben:
**„software Ausblendung Speicher“ / „OUT-Befehl“** [hand]. Es tut zweierlei:

1. **Treiber der Bus-Sperrsignale /MEMDI, /MEMDI1, /MEMDI2, /MEMDI3**
   (Plan-Anschlüsse X1:B09, X2:A21, X2:B21, X2:B13, Ausgänge nach oben; Blatt 1 rechts oben).
   Die Verbindung Registerausgang → Sperrleitung läuft über das **Wickelfeld X3:23–X3:45**;
   Treiber sind offene-Kollektor-Gatter D9 (LS03). **Handeintragung:** eine Drahtbrücke
   X3:37 → X3:38, wobei X3:37 = Registerbit DO1 **invertiert** (D5:01) und X3:38 =
   /MEMDI (X1:B09) — also *Bit 0 = 1 ⇒ /MEMDI low ⇒ Speicher gesperrt*; dazu zwei
   sich kreuzende Handdrähte X3:35/36 ↔ X3:44/45 und ein „?“ am Feld X3:37
   (Blatt 1, Ausschnitt oben rechts) [hand/?]. **Welches Bit welche der vier Leitungen treibt,
   ist aus dem Plan nicht eindeutig lesbar [?]**.
2. **Programmierbare 1-KB-Ausblendadresse** („Ausblendadresse für Bildschirm“, handgeschrieben):
   sechs Exklusiv-ODER-Gatter (D11:01/02, PS86) vergleichen **AB10…AB15** mit einem über
   Brückenfeld **X3:46–X3:57** („Sperren 1K-Blöcke“, Wertigkeiten 400H, 800H, 1000H, 2000H,
   4000H, 8000H je Adressbit) bzw. dem Register eingestellten Wert; D13 (LS30, 8-fach-NAND)
   liefert „Treffer“; der Block wird ausgeblendet (als Platz für einen Bildwiederholspeicher
   nach der Handschrift). Polarität der Brücken (Pull-up R2:01–R2:08 = +5 V „A“ gegen 0 V „B“)
   und Beteiligung der Registerbits **nicht eindeutig** [?].
3. **/RESET löscht das Register** (CLR über Inverter D5:01 an /RESET; zugehöriger Handtext
   teilweise unleserlich: „CLEAR wenn alle 6 K …“) [SLP/?]. Nach Reset also alle DO = 0.

### 2.3 E/A-Adresse des Registers

Zwei **1-aus-8-Dekoder (8205)** D2:01 und D2:02, handbeschriftet „Auswahl der E/A-Adresse —
Decodierung OUT-Befehl **88**“ [hand]:

* **D2:01:** A0–A2 = **AB2–AB4**, Ausgänge 00–07 an Wickelstiften **X3:02–X3:09**, gemeinsamer
  Sammelstift **X3:10**.
* **D2:02:** A0–A2 = **AB5–AB7**, E-Eingänge = **/IORQ** und **/IODI**, Ausgänge 00–07 an
  **X3:11–X3:18**, gemeinsamer Stift **X3:19**.
* Je ein **blauer Handdraht**: D2:01 Ausgang **02** → X3:10; D2:02 Ausgang **04** → X3:19.
  Damit ist (AB7:5 = 100, AB4:2 = 010) = **88H–8BH** [hand, Ausgangsnummern aus der
  Pixellage der Drähte gelesen, Genauigkeit „ein Stift“ [?]]. Das stimmt mit der Handnotiz
  „88“ überein.
* AB0/AB1 gehen **nicht** in die Dekodierung ein ⇒ vier aufeinanderfolgende Adressen sprechen
  dasselbe Register an [SLP].

**Vergleich mit dem Gen-2-ROM:** Die ROMs schreiben **`OUT (A8H)`** [ROM, 6 Fundstellen in der
ZRE, 4 in der Karte „3C00“]. A8H = 1010 1000 = AB7:5 = **101**, AB4:2 = **010**. Das ist
**derselbe Dekoder, nur der D2:02-Draht ein Ausgang weiter (04 → 05)** [Folgerung aus SLP; ob
die Karte des Anwenders so gebrückt ist: F20]. Der handeingetragene Stand „88“ beschreibt eine
**andere** Brückung derselben Karte, nicht einen Widerspruch.

Geschriebene Werte laut ROM (Vorgriff auf V3b, `LD A,n / OUT (A8H),A`, [ROM]): ZRE `06H`
(0003, 01DC, 01E2), `87H` (01B9), `0EH` (099F); eine weitere Stelle 098E–0991 rechnet den Wert
(`LD A,3EH / ADC A,A`, bei linearer Disassemblierung vermutlich verschoben) und ist **nicht**
ausgewertet; Karte „3C00“: `0EH` (F003, F210, F216), `8FH` (F1ED). Auffällig: **Bit 0 und Bit 7 sind nur in den Werten 87H/8FH gesetzt**
(vermutlich: *DO1 → /MEMDI* sperrt die ZRE-ROM/RAM und gibt das K3528-RAM auf 0000–0FFF
frei; *DO8* schaltet die D9-Treiber). **Reine Vermutung [?]**, V3b entscheidet.

### 2.4 Adressbelegung der K3528

* Die vier Banken decken **0000–FFFF** vollständig (AB14/15-Dekoder, 16 KB je Bank), Jumper-
  Feld X3:64–X3:71 öffnet/schließt die Banken (**„64K alle Brücken“** [hand]); .10/.20 lassen
  die oberen Banken weg (Chips entfallen, Brücken nehmen heraus — Zuordnung der
  entfallenen Chips zu *welchen* Adressbereichen nicht gelesen [?]).
* Die Karte hat **keine eigene Startadress-Wahl in 4-KB-Schritten** (anders als K3520/K3525/K3820);
  sie ist **fest an 16-KB-Grenzen** nach AB14/15 gelegt.
* **/MEMDI-Eingang:** im Plan hat die K3528 **nur** /MEMDI*-Ausgänge (Pfeile nach außen); ein
  Eingang, der die Karte selbst sperrt, ist nicht eingezeichnet [SLP, Aussage „fehlt“ ist
  Negativbefund aus dem Bild, [?]].

---

## 3. K3820 — PFS (16 × 1 KB EPROM), „2708-Karte“

Quelle: `Speichersteckeinheiten_K3520_3521_3525_K3820_Betriebsdokumentation.pdf`, **gedr.
S. 22–27 (PDF-S. 24–29)**. Platine 012-7041 (Doku) bzw. 012-7040 (Anwender), „Erzeugnisprogramm
Dezentrale Datentechnik“.

| Merkmal | Befund | Fundstelle |
|---|---|---|
| Kapazität/Chips | **16 KB aus 16 × Q260 (1 K × 8, U555/2708)**, 24-polige Sockel | gedr. S.22 (6.1, 6.2) |
| Zugriffszeit | ≤ 350 ns | S.22 |
| Versorgung | 5 P ≤ 0,9 A, 5 N ≤ 0,5 A, 12 P ≤ 0,9 A; **−5 V nicht später als 10 ms nach 5 P/12 P** | S.22 |
| Startadresse | **X8:1…4 / X9:1…4 binär**, Vielfache von **4 KB** (Wertigkeit 4/8/16/32 KB für :1/:2/:3/:4, z. B. alle vier Brücken = F000H); die **interne Adresse = AB12…AB15 minus Startadresse** (Subtrahierer T183 = 74LS83, gedr. S.22/Funktionsbeschreibung 6.4.2.2) | S.23 (6.3.2) |
| Chip-Auswahl | AB10/AB11 wählen einen der 16 KB-Blöcke…; **AB14K/AB15K = 0** wählt die Karte, AB12K/AB13K = Block | S.23 |
| Chip-Raster | Abb. 7: Reihe 0: 0000, 0400, 0800, 0C00 … Reihe 3: 3000, 3400, 3800, 3C00 (je 1 KB, **relativ** zur Startadresse; die vierte Spalte ist in der Abbildung mit 3000–3FFF/2000–2FFF/1000–1FFF/0C00–0FFF **verdruckt**, gemeint 3C00/2C00/1C00/0C00) | S.24, Abb. 7 |
| **/MEMDI** | X6:1–X7:1 = /MEMDI (X1:B09), X6:2–X7:2 = /MEMDI1 (X2:A21), X6:3–X7:3 = /MEMDI2 (X2/B21); die Karte **wird** durch das gewählte Signal gesperrt (Ausgänge hochohmig, CE gesperrt). Normalkonfiguration ≤ 64 KB: /MEMDI geschlossen, MEMDI1/2 offen | S.5 (1.2), S.24 (6.3.4) |
| **WAIT** | X10–X11 **offen** = WAIT im M1-Zyklus, **geschlossen** = keine WAIT-Bildung | S.24 (6.3.5) |
| RDY | OC-Ausgang, wird beim gültigen Zugriff gezogen | gedr. S.5 |

Das **Dokument ist eine reine Beschreibung der Steckeinheiten**; die Karte *erzeugt* keine
Sperrsignale, sie *empfängt* eines.
**Anwender-Befund** (Entwurf 24 §10): nur Chips „3C00“ und „3000“ programmiert. Lage am
Gerät folgt aus X8/X9. Hypothese „Start = C000“ (Chip 3C00 = FC00) ist mit der Tabelle
vereinbar: C000 = X8:4 + X8:3 gebrückt, X9 entsprechend (Tabelle S.23, letzte Zeile).

---

## 4. Die übrigen Speichersteckeinheiten (aus derselben Doku)

| Karte | Inhalt | Start/Brücken | MEMDI | WAIT | Fundstelle (gedr. S.) |
|---|---|---|---|---|---|
| **K3520** | **4 KB statisches RAM**, 32 Chips Q240 (1K×1), 5 V, Zugriff ≤ 530 ns | X8:1…4/X9:1…4, 4-KB-Raster (0000, 1000, …, F000); AB10/AB11 = 1-KB-Block | X6/X7 wie oben | Brücken X10/X11 (§3.3.4), **Stellung nicht gelesen** | S.7–S.9 (§3) |
| **K3521** | 4 KB CMOS-RAM mit Stützbatterie (3× KBL 0,225), Spannungsüberwachung, Signal **/SUE** (X2:B22) | wie K3520 [?] | [?] | [?] | S.15–S.16, Blockbild Abb. 3 |
| **K3525** | **16 KB dynamisch** (32 Chips Q250, 4K×1), Zugriff ≤ 350 ns, 12 P/5 N, **keine eigene Refresh-Steuerung**: Refresh über AB0–AB6 und /RFSH vom Bus (ZVE) | X8/X9 (4-KB-Raster, 16 KB zusammenhängend; Tabelle S.18) | X6/X7 (S.19) | **X10–X11 geschlossen = WAIT im M1, X11–X12 geschlossen = keine WAIT-Bildung** (S.19, 5.3.4) | S.17–S.21 (§5) |

Alle Karten: Adressen **AB0…AB15 für 64 KB**, „Speichererweiterung über 64 KB durch /MEMDI1 und
/MEMDI2 des Koppelbusses; Zusatzverdrahtung und Zusatzelektronik erforderlich“ (S.3, S.5). Die
Doku nennt die Quelle dieser Signale **nicht** — **die K3528 ist in diesem Satz die einzige
Karte, die sie tatsächlich erzeugt (Abschnitt 2.2).**

---

## 5. Antwort auf die Kernfrage

> Welche dieser Karten kann zusammen mit einer ZRE, die per **A8H** umschaltet, ein **64-KB-System**
> bilden?

1. **Die K2521 schaltet nicht selbst um.** Sie kennt weder Seitenregister noch ein
   Ausgaberegister (nur CTC 80H–83H und PIO 84H–87H, Abschnitt 1.3). Ihre ROM/RAM (0000–0FFF)
   werden **von außen** über /MEMDI (bzw. -1/-2, Brücken X8/X9) abgeschaltet.
2. **Nur die K3528 trägt ein per `OUT` beschreibbares Register, das /MEMDI…/MEMDI3 treibt**
   (Abschnitt 2.2/2.3). Mit **K2521 + K3528 (64 KB) + K3820** ergibt sich ein 64-KB-RAM-System:
   die K3528 liegt **unter** der ROM (0000–FFFF), die ZRE-ROM/RAM wird beim Boot gesperrt oder
   freigegeben, die Register-Adresse ist per Wickelfeld wählbar. Die vom ROM benutzte Adresse
   **A8H** ist mit den Dekodern der K3528 **einstellbar** (Abschnitt 2.3).
3. **K3520, K3525, K3521, K3820 haben kein Register**; sie lassen sich nur an 4-KB-Raster
   ohne Umschaltung legen oder über X6/X7 von **fremd erzeugten** MEMDI1/2 sperren. Allein
   (mit der ZRE) bilden sie **keine** Umschaltung; ein 64-KB-System würde ohne die K3528 die
   Bereiche 0000–0FFF **doppelt** belegen (ZRE-ROM/RAM *und* Karte).
4. **Offen [?]:** welche Karte der Anwender wirklich hat (F2/F20) und wie sein Register verdrahtet
   ist (welches Bit welches /MEMDI-Signal treibt; Brückenstand X8/X9 der ZRE).

---

## 6. Offene Fragen (auch in Entwurf 24 §9, F20–F24)

* **F20** Platinen-/Aufdruck der „zusätzlichen RAM-Karte“ (K3528? 012-/045-Nr., 32 Q281-Chips,
  8212 D8?), Stand des Wickelfeldes **X3** (Register-Adresse A8H: D2:02-Draht), Bank-Brücken X3:64–71.
* **F21** Ist die handschriftliche „88“ im Plan Stand eines **anderen** Gerätes oder gilt sie
  für das des Anwenders (ROM: A8H)?
* **F22** Brückenstand der ZRE (X6–X9, X14/X15) und der PFS K3820 (X6/X7: welches MEMDI,
  X8/X9: Startadresse, X10–X11: WAIT) am Gerät.
* **F23** Wie ist das Registerbit→Leitungsfeld X3:23–X3:45 am Gerät gebrückt (Handdrähte im
  Plan: 37→38, Kreuzung 35/36↔44/45)? Foto der Karte genügt.
* **F24** Woher stammt „PIO 08–0FH“ aus Forum 5713? Gibt es ein Gerät/eine Karte mit anderer
  Dekodierung?
