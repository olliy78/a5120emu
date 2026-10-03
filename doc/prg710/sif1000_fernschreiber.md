# Lochband (ADA K6022, SIF1000) und Fernschreiber (ASS 590069) am PRG 710 / 710-1

Analyse zu AP-P8a (`doc/design/20_prg710.md` §10). Quellen: die Treiber und Prozeduren der
Gerätedisketten (`~/Documents/K1520emu/Disketten/PRG/`, u. a. `PRG710_UDOS_MRS_Boot/Side0`,
`UDOS.PRG710-1_V4.3_1_89/dateien/Side0` — in beiden Varianten bytegleich), `DRUCK.DOK`
(Seite 1), `UDOS4.1.DOK` (`PRG710-1_UDOS43_Boot`), die SCPX-BIOS-Module `B17172FS.SYS` /
`B17272FS.SYS` (`PRG710-1_SCPX_Boot/dateien`), robotrontechnik.de (SIF1000, §1 des Plans).
Disassembliert mit `tools/z80_disasm2.py`; Adressen sind Ladeadressen.

Kennzeichnung wie im Plan: **[Disk]** aus Programmcode belegt, **[Web]** aus der Webseite,
**[?]** Annahme/ohne Kartenbefund.

---

## 1. Bestand auf den Disketten

| Datei | Typ | Inhalt |
|-------|-----|--------|
| `PTAPE.6022` | P1, F000H–F1FFH (483 B) | **Treiber** Lochband für die ADA K6022 (Leser + Stanzer) |
| `PTAPE.S1` | P1, F000H–F715H (nur 710-1-Disketten) | Treiber für ein anderes Gerät (nicht untersucht) |
| `TREAD.1210` / `TWRITE.1215` | A (Prozedur) | `[ACTIVATE $PTAPE.6022;DEFINE 20 $PTAPE.6022;TAPE.READ #1 [(#2 …)];DEFINE 20 $NULL;DEACTIVATE $PTAPE.6022]` bzw. dasselbe mit `TAPE.WRITE` |
| `TREAD.CON337` / `TWRITE.DT105` | A | dieselben Prozeduren mit `$PTAPE.S1` |
| `TAPE.READ` / `TAPE.WRITE` | P, 4000H | Zilog-Dienstprogramme („TWRITE 780809 … COPYRIGHT ZILOG“), schreiben/lesen über **logische Einheit 20** |
| `SD1156` | P1, ED00H | Treiber Drucker SD1156 am **Stanzeranschluss** der K6022 (`DRUCK.DOK` §4) |

Aufruf unter UDOS: **`DO TWRITE.1215 <datei> F=A`** / **`DO TREAD.1210 <datei> F=A`** (die
Optionen gehen über `#2…` in die Klammer). `TAPE.WRITE` kennt `F=A` (ASCII), `F=I`, `F=R`
(Vorgabe) sowie `C=`/`L=` (Optionszerlegung 4068H–40E8H) [Disk].

## 2. ADA K6022 und `PTAPE.6022`

### 2.1 Ports und PIO-Betriebsarten [Disk]

`DRUCK.DOK` §4: E0H Daten A, E1H Daten B, E2H Steuer A, E3H Steuer B (Stanzeranschluss);
`PTAPE.6022` benutzt zusätzlich E4H–E7H (Leser). Die PIOs liegen also mit **A0 = B/A,
A1 = C/D** am Bus. Initialisierung (Anfrage 00H, F115H–F158H):

| Port | Werte | Bedeutung |
|------|-------|-----------|
| E7H | CFH, F0H | Leser Tor B: Bitbetrieb, D4–D7 Eingänge (STA), D0–D3 Ausgänge (KOM) |
| E6H | ECH, 4FH, 83H | Leser Tor A: Vektor ECH, **Betriebsart 1 (Eingabe)**, Interrupt ein |
| E4H | `IN` | erstes Lesen = RUF an den Leser |
| E3H | FFH, F0H | Stanzer Tor B: Bitbetrieb, D4–D7 Eingänge, D0–D3 Ausgänge |
| E2H | EEH, 3FH, 83H | Stanzer Tor A: Vektor EEH, **Betriebsart 0 (Ausgabe)**, Interrupt ein |
| E1H / E5H | 01H / 03H | KOM-Ausgänge |

ISR (IM 2, I = 0FH): `(0FEEH)` = F1C0H (Stanzer, setzt Bit 0 von F1E4H), `(0FECH)` = F1C8H
(Leser, setzt Bit 1). Abmelden (Anfrage 44H): `OUT (E4H)`, dann 03H an E2H/E6H/E3H/E7H
(Interrupts aus). Die Vektoren kollidieren nicht mit dem Residenten (CTC-K3 = E6H,
`resident.md` §5).

### 2.2 SIF1000 an der PIO

robotrontechnik.de [Web]: unidirektional, getrennte Ein- und Ausgabe; Signale 8 × DAT,
3 × KOM, 3 × STA, RUF, END. Ausgabezyklus: der Rechner prüft END, legt DAT (und KOM) an und
schaltet RUF ein; das Gerät führt aus und meldet mit einem END-Impuls. Eingabe sinngemäß
umgekehrt. Auf der PIO [Disk + ?]:

| SIF1000 | Stanzer (E0H–E3H) | Leser (E4H–E7H) |
|---------|-------------------|-----------------|
| DAT | Tor A (Ausgabe) | Tor A (Eingabe) |
| RUF | **ARDY** — Schreiben nach E0H | **ARDY** — Lesen von E4H |
| END | **/ASTB** → Interrupt EEH | **/ASTB** → Byte übernommen, Interrupt ECH |
| KOM | Tor B D0–D3 (`PTAPE`: 01H; `SD1156`: 00H/03H/05H) | Tor B D0–D3 (03H) |
| STA | Tor B D4–D7; **D5 \| D6 ≠ 0 = Fehler** (F181H) | Tor B D4–D7; **D6 = Bandende / kein Band** (F191H) |

Die genaue Bedeutung der KOM-Bits und der übrigen STA-Bits ist ohne Kartenbefund offen
**[?]** (§8 des Plans).

### 2.3 Ablauf im Treiber [Disk]

**Stanzen** (Anfrage 0EH „Zeile schreiben“, F033H–F058H, Unterprogramm F166H): Zeile bis
vor ein FFH (CPIR); je Byte: LF wird übergangen, **CR → 1EH (NL)**, Bit 7 gelöscht und als
**gerade Parität** gesetzt (`AND A; JP PE`), `OUT (E0H)`, dann bis zu 255 × 255 Runden
(≈ 2,3 Mio. Takte ≈ **0,93 s**) auf das END-Bit warten; danach `IN (E1H) AND 60H` — ≠ 0 oder
Fristablauf ⇒ Status **C2**. Abbruchtaste (`CALL 1006H`) ⇒ Status 49H.

**Lesen** (Anfrage 0AH „Zeile lesen“, F09FH–F0E4H, Unterprogramm F191H): erst ein
`IN (E4H)` (RUF; das dabei gelesene Byte wird verworfen), dann je Zeichen: STA D6 gesetzt ⇒
vor Datenbeginn **C2**, danach wie ein Nullbyte; sonst Bit 1 löschen, `IN (E4H)` (Byte
abholen = nächster RUF), bis zu 65 536 Runden (≈ 0,93 s) auf END warten — so wird jedes Byte
erst zurückgegeben, wenn das **nächste** angekommen ist. Verworfen werden **FFH**
(Lochung „Irrung“), **LF**, **00H vor dem ersten Datenzeichen**; NL → CR, Bit 7 gelöscht
(Parität ungeprüft). Nach dem ersten Datenzeichen zählt F1E3H von 100 abwärts je Nullbyte
und wird bei jedem anderen Zeichen neu gesetzt: **100 Nullbytes in Folge = Bandende**,
Status **C9**. Gelesen wird stur die angeforderte Byteanzahl (CR beendet nichts).
Anfrage 04H (Öffnen) setzt F1E3H = 0, 06H (Schließen) tut nichts.

`UDOS4.1.DOK` §3 bestätigt: „Als Satzendekennzeichnung bei Lochstreifen sind die Zeichen
CRLF (0D0AH) oder NL (1EH) zulässig. Der Lochbandtreiber des PRG 710 gibt als
Satzendekennzeichen ein NL aus.“

**Folgerungen:**
- Der Treiber trägt nur **7-Bit-Text**. Das Vorgabeformat von `TAPE.WRITE` (`F=R`) beginnt
  mit einem Kopfsatz, der Nullbytes enthält (Typ, Länge 7DH, Datum) — beim Lesen fallen sie
  weg, der Rest verschiebt sich, `TAPE.READ` endet mit **ERROR C9** (im Emulator so
  beobachtet). Für Lochband also immer **`F=A`**.
- `TAPE.WRITE` stanzt selbst **150 Nullbytes Vorlauf** und **150 Nachlauf**; `TAPE.READ`
  legt die Datei auf **Seite 1** an (`CAT`: „DRIVE 4“) und füllt den letzten 128-Byte-Satz
  mit 00H auf.
- Zeitverhalten: der Treiber verlangt nur, dass END innerhalb ≈ 0,93 s kommt — jede
  Geräteschnelligkeit über ~2 Zeichen/s geht. Er braucht aber einen **Abstand > einige
  10 µs** zwischen RUF und END, sonst verwirft das `IN (E4H)` am Anfang jeder Anfrage ein
  echtes Zeichen (am Gerät: 1 ms bei 1000 Z/s).

### 2.4 Modell im Emulator (`core/cards/k6022/`)

- Zwei `Z80PIO` hinter einer Portumsetzung (E0/E1/E2/E3 → A-Daten, B-Daten, A-Steuer,
  B-Steuer). RUF erkennt die Karte am Buszugriff (Betriebsart 0: Schreiben von Tor A;
  Betriebsart 1: Lesen von Tor A) — `Z80PIO` bleibt unverändert.
- **Leser** daro 1210 [?] 1000 Z/s: Band = Datei (Bytes wie gestanzt). Vor dem Inhalt
  16, dahinter 128 Nullbytes (wie ein gestanztes Band; ohne Vorlauf ginge das erste Zeichen
  verloren, ohne Nachlauf das letzte, s. §2.3). STA D6 = kein Band bzw. ganz durchgelaufen.
- **Stanzer** daro 1215 [?] 150 Z/s: jedes Byte ins Stanzband (Speicher; Speichern in eine
  Datei auf Anforderung), END, STA = 0. Ausgeschaltet: kein END ⇒ der Treiber meldet C2.
- `SD1156` am Stanzeranschluss landet damit ebenfalls im Stanzband (DARO-1000-Code, KOM =
  Druckersteuerung) — nicht gesondert nachgebildet.
- Band im Leser und Stanzband überleben `/RESET` (sie gehören den Geräten).

## 3. ASS 590069 und der Fernschreiber (`B17172FS` / `B17272FS`)

### 3.1 Programmierung [Disk]

LIST (E2DAH): beim ersten Aufruf Initialisierung (E2F4H, Merker E2F3H):
`LD HL,E3A6H; LD BC,0ACDH; OUTI; OUTI` → **07H, 18H an CDH**; `LD C,C7H; OTIR` → **04H F8H,
01H 00H, 03H 01H, 05H 88H an C7H**.

- CDH = CTC-Kanal 1: 07H = Zeitgeber, Vorteiler 16, Zeitkonstante folgt; 18H = 24 ⇒
  ZC/TO1 = φ / 384 = 6400 Hz bei φ = 2,4576 MHz. Die CTC liegt damit vermutlich auf
  **CCH–CFH** [?].
- C7H = Steuerregister SIO-Kanal **B**: WR4 = F8H (**×64**, **1½ Stoppbits**, ohne Parität),
  WR1 = 00H (keine Interrupts), WR3 = 01H (Empfänger ein, 5 Bit), WR5 = 88H (DTR, Sender
  ein, **5 Bit**). Bei TO1 als Sende-/Empfangstakt [?]: 6400 / 64 = **100 Bd**.
- Ausgabe (E35BH): `IN A,(C7H); BIT 2,A` (RR0 D2 Sendepuffer leer) bis frei, `OUT (C5H),A`.
  C5H = Daten B, C7H = Steuer B ⇒ SIO auf **C4H–C7H** mit **A0 = B/A, A1 = C/D** (anders
  als die K8025, deren SIO Daten A, Steuer A, Daten B, Steuer B belegt).
- READER (E3B5H) liefert fest 1AH; PUNCH (E3B1H) tut nichts. Die Empfangsrichtung benutzt
  kein bekanntes Programm.

### 3.2 Zeichensatz [Disk]

**ITA2 (CCITT Nr. 2)**, 5 Bit. Umsetzung (E308H): Kleinbuchstaben → groß, 02H → „Bu“
(1FH), 1AH → „Zi“ (1BH), sonst Suche rückwärts in der Tabelle **E366H–E3A5H** (64 Byte,
Index = Lage · 20H + Code; Buchstabenlage ab E366H, Ziffernlage ab E386H); Index-Bit 5
entscheidet, ob vorher eine Umschaltung zu senden ist (Lage in E3B0H Bit 7). Zeichen ohne
Eintrag (z. B. `>` `*` `#`) gehen als **Zwischenraum (04H)** hinaus. CR = 08H, LF = 02H
(beide Lagen). Die Tabelle ist in `B17172FS` und `B17272FS` gleich (Unterschied der
Module: Bildschirm 1 K/2 K).

### 3.3 Modell im Emulator (`core/cards/ass590069/`) und Entscheidung „Hub, Text“

- `Z80SIO` an C4H–C7H (Portumsetzung wie oben), `Z80CTC` an CCH–CFH [?], Kanal B →
  **ein Anschluss „Fernschreiber“ im `SerialHub`**, hinter den K8025-Anschlüssen (Index
  3 am 710, 2 am 710-1 — die bisherigen Indizes bleiben). Kanal A geht nicht nach außen [?].
- **Begründung Hub statt eigener Datei:** der Hub hat alles schon — Datei (Fernschreib-
  protokoll), Telnet (ein Terminal sieht mit), Statuszeile, Reiter, C-ABI, Konfiguration
  je Name. Eine eigene Dateischnittstelle der Karte wäre ein zweiter Weg für dasselbe.
- **Begründung Text statt ITA2:** draußen hängt kein Fernschreiber, sondern eine Datei oder
  ein Terminal; 5-Bit-Codes mit Umschaltzeichen wären dort unlesbar. Die Karte bildet
  deshalb den **Fernschreiber selbst** nach: sie führt die Umschaltung, „Bu“/„Zi“ und 00H
  verbrauchen ihre Zeichenzeit (die Karte nimmt sie selbst ab), erscheinen aber nicht; jedes
  andere Zeichen geht als ASCII hinaus (CR und LF wie gesendet). Gemeldet wird das Tempo
  der Gastleitung (100 Bd, 75 ms je Zeichen) mit **8 Bit** — es geht Text hinaus; RFC 2217
  meldet also 100 8N1. Ein echter Fernschreiber an einem Wirts-COM-Port ginge so nicht —
  das ist bewusst nicht Ziel.
- Empfang: ein ASCII-Zeichen vom Hub kommt als ITA2-Code (ohne Umschaltung) im Empfänger an.

## 4. Was offen bleibt (Fragen an den Anwender, §8 des Plans)

1. **K6022:** Bedeutung der KOM-Leitungen (D0–D2 an Tor B) und der STA-Leitungen außer
   Leser-D6 und Stanzer-D5/D6; Bandvorrat-/Bandrissmeldung des Stanzers.
2. **Geschwindigkeit** daro 1210 / daro 1215 am Gerät (Modell: 1000 / 150 Zeichen je
   Sekunde) und die Stellung der K6022 in der Interruptkette.
3. **590069:** Bausteine (SIO C4H–C7H? CTC CCH–CFH? was liegt an SIO-Kanal A, an den übrigen
   CTC-Kanälen?), welcher CTC-Ausgang taktet SIO-B (⇒ 100 Bd?), welcher Fernschreiber (F1100
   oder F1200) mit welcher Schrittgeschwindigkeit (50 Bd wären WR4 ×64 an 3200 Hz), und
   Linienstrom/Stecker.
