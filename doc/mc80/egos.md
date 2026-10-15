# MC80.3x — EGOS 30.1: Auswertung des Disassemblats und Fassungsvergleich (AP M1)

Stand: 2026-10-14.  Ergebnis von AP M1 (`doc/design/32_…` §6).  Das kommentierte Listing ist
**`doc/mc80/egos.prn`** (0000H–27FFH, 10 KB), erzeugt von `tools/gen_mc80_egos_prn.py`
(Wächter `cli_mc80_egos_prn_passt_zur_quelle`: MD5 der Abzüge, jedes Byte genau einmal,
alle Einsprünge der Softwareschnittstellen benannt, ROM-Kette, bytegleich).  Hardware:
`doc/mc80/hardware_3x.md` (M0).  Statische Analyse — es gibt noch keinen MC80-Emulator.

Quellenkürzel wie in `hardware_3x.md`; zusätzlich **[P xxxx]** = Adresse in `egos.prn`,
[EGOS2 S.n] = Teil 2 der Systembeschreibung (Magnetband, PDF-Seiten 24–27, gedruckte Seite n).
[?] = Deutung/Vermutung.

---

## 1. Befund in Kürze

1. **Abnahme M1:** alle Einsprünge aus [EGOS S.12–19] sind wiedergefunden — Sprungleiste
   **0BB5H–0BFCH** (RLL 0BBBH … LZOU 0BF7H, dazu vier nicht dokumentierte: 0BB5H Kommandoschleife,
   0BB8H Zeichen zeichnen, 0BE2H TST mit Codeabbildung, 0BE5H Fehleranzeige, 0BFAH IY-Grundzustand)
   und **2004H–2075H** (JHL … S2X, dazu „Befehl im Segment ausführen“ 201CH–2034H, 2073H).
   Einzige Abweichung: **VERA liegt nicht bei 2290H** [EGOS S.19] — dort stehen 00H; VERA beginnt
   bei **22A8H** (Hilfsroutine 2298H) [P 225E/22A8].
2. **SEGC = FBFBH**, nicht FBFDH: [EGOS S.17] schreibt FBFDH, [EGOS S.19] FBFBH; der Code
   spiegelt jedes `OUT (85H)` nach FBFBH (z. B. [P 0939], [P 207F]).  FBFCH/FBFDH sind zwei weitere
   Segmentzellen, die nur die „Befehl im Segment ausführen“-Einsprünge 202BH–2034H benutzen.
3. **Die ROM selbst ist eine RAM-Kette.**  Jede Suche (Kommando, Treiber, RESET-Routine, CAT) beginnt
   bei **0AFEH im Segment 1** [P 0924, 262F, 078E]: CON → INIT → (03) → KLOG → KINIT → READ →
   WRITE → (03) → (1BH, Segmentprogramme) → RELAD → DIR → LADT → DEFINE → ACT → DEACT → (03) →
   CAT → CATA → RKET → **2800H**.  Ein Name endet am ersten Byte ≥ 7FH (FFH, im ROM auch B0H)
   [P 0958].  FEH-Einträge wechseln Adresse und Segment [P 097D].
4. **Folge für M2 — Anschluss 2800H→4000H:** RAM-Programme (Spiele `TE`, `HASI`) werden nur
   gefunden, wenn die Kette von 2800H bis 4000H weiterläuft.  Mit dem MAME-Satz allein steht dort
   nichts: liest der Leerbereich **FFH, endet die Kette** (C7H „nicht gefunden“); liest er **00H**,
   läuft sie über 2048 leere Kennbyte-00-Objekte (je 3 Byte) genau auf 4000H [nachgerechnet:
   1800H = 3·2048].  Die ycdt-Erweiterung `3s` kettet mit einer RESET-Routine bei 3940H nach
   4000H.  Wie es am Gerät ohne Erweiterungs-ROM aussah, ist offen [?] → M2 entscheidet (Vorschlag:
   Leerbereich 2800H–3FFFH liest 00H, wie MAME; gegen die Spiele in M5 prüfen).
5. **SPE-2-Zustand nach Reset (M0 offener Punkt 3): frei.**  Kein Befehl in 0000H–27FFH greift auf
   B0H–B3H zu, das RESET führt aber sofort Code aus dem SPE-2-ROM (RESET-Routine 25BDH) aus und
   beschreibt das SPE-2-RAM (INIT 4000H ff.).  Also sind ROM und RAM der SPE 2 nach Reset
   freigegeben (Auslieferung „ohne Anfangsinitialisierung“).  Außerdem schaltet der Code aus dem
   SPE-2-ROM heraus das Segment um und läuft weiter [P 207F]: der **ROM-Block hängt nicht am
   Segmentregister** (nur der RAM-Block 4000H–BFFFH).
6. **Schreibschutz 8CH (M0 offener Punkt 1):** einziger Zugriff ist die RESET-Schleife
   `LD BC,008CH / OUT (C),B / DJNZ` (256 Ausgaben, B = 00H, FFH … 01H auf A8–A15 und Datenbus)
   [P 0780].  Danach schreibt EGOS ohne weitere Freigabe nach FFFCH/FFEEH (CON-Init), FC00H (ADT),
   FEE8H/FEFDH (INIT) und füllt C000H–FFFFH mit FFH (INIT) — und berührt 8CH/8DH nie wieder.
   **Nach dem RESET ist der Schutz also aus**, EGOS schaltet ihn nie ein.  Welche Wirkung die
   256 Ausgaben einzeln haben (Freigabe aller 1-KB-Blöcke nach dem SPE-Schema?), bleibt [?] — für
   M2 genügt „aus nach Reset, nur Anwendercode schaltet“.
7. **NMI wird übergangen:** INIT legt bei FEE8H `RETN` (EDH,45H) ab, bei FEFDH (RST 38H) `JR $`
   [P 0C53/0C59].  In der ycdt-Fassung 30E springt die NMI dagegen in eine Routine, die die RAM-Kette
   neu anlegt (§4.1).
8. **Bildschirm:** Y zählt **nach oben** (Y = 0 unten): Musterbit 0 wird mit dem kleinsten Y
   gezeichnet, Bit 7 ist oben, die erste Textzeile liegt bei Y = F8H, jede weitere 10 tiefer
   [P 0133, 0043, 00DC].  X 0…255 je Hälfte, Port-Bit 3 = rechte Hälfte.  Der Zeichengenerator ist
   eine **Proportionalschrift** (Breite = Zeigerabstand), Zeiger bei **0CA4H** (FB40H nach RESET;
   [EGOS S.6] sagt „0C00H“).  Bildkommandos nur Löschen (00H) und Rollen um 10 Zeilen (1AH/2AH),
   danach eine feste Wartezeit von ca. 43 ms [P 022E] — der Emulator darf sofort ausführen.
9. **VIS2a-Lesepfad (M0 offener Punkt 5):** EGOS liest die VIS **nie**.  Die Segment-ROMs des MC80.31E
   tun es so: `LD C,0C3H` (bzw. 0CBH rechte Hälfte; Funktion 011 = „negierter Bildpunkt → IN-Register“)
   · `OUT (C),L` · `OUT (C),L` (B = Y, L = X) · `IN A,(C)` → **Bildpunkt in Bit 0**
   (`segmente/seg_20_4000-h.bin` 429AH ff., 4C46H ff.; `seg_10_4000-d.bin` 443CH ff., 48D4H ff.).
   Polarität [?]: die Routine 4C40H (h) liest mit C3H und schreibt bei Bit 0 = 1 „dunkel“, bei 0
   „hell“ zurück — ist sie (wie die Schleife über das ganze Bild nahelegt) „Bild invertieren“, dann
   liefert Funktion 011 den **unnegierten** Punkt; das ist in M3 am Bild zu prüfen.
10. **Tastaturprotokoll (M0 offener Punkt 6)** — §3.
11. **Magnetband (für K1/M7)** — §2: K5020 an **30H–37H**, nur **Laufwerk 1** ausprogrammiert,
    Blockformat `AAH | Daten | CRC-16/ARC (low, high) | AAH`, Bandmarke = Block ohne Daten,
    **gelesenes erstes Byte ist ABH**, Block vor/rück (11H/15H) **fehlt**.

## 2. Magnetbandsoftware 1000H–1FFFH

Drei Schichten [EGOS2 S.3–6]; die Schnittstelle zwischen logischem und physischem Treiber ist die
Sprungleiste **1FF7H** (MB_INIT) / **1FFAH** (MB_STATUS) / **1FFDH** (physischer Treiber).

### 2.1 Physischer Treiber (17C4H–1FF6H) — Hardwarezugriffe

- **Ports** (Basis aus IX+2 = **30H**, gesetzt von MB_INIT [P 1F76]): 30H Daten-PIO A (Ausgabe),
  31H Daten-PIO B (Eingabe), 32H/33H deren Steuerworte, 34H/35H Steuer-PIO A/B (Betriebsart 3),
  36H/37H deren Steuerworte.  Bitbelegung wie `doc/kassette/k5020.md` §3 — die Treiberlogik passt
  Bit für Bit: Tor A `01H` = KMBG 1 reserviert, `09H` angewählt, `50H`/`60H` schnell vor/zurück
  (TRV/TRR + HGE), Tor B Bit 4 `/NIB1`, Bit 5 `BMS`, Bit 6 `STA`, Bit 1 `AUF` [P 1EA2, 19F5, 191C].
- **Init** [P 1F8D, Tabelle 1FD1H]: 37H ← B6 CF FD 17 EF (Vektor B6H, nur AUF Ausgang, /NIB1
  überwacht, LOW-aktiv), 36H ← B8 CF 80 17 7F (Vektor B8H, AEB überwacht), 35H ← 00, 34H ← 03,
  33H ← B4 7F 03 (Eingabe, Vektor B4H), 32H ← B0 0F 03 (Ausgabe, Vektor B0H).  Vektoren FFB0H–FFBBH:
  1B69H (Ausgabe), 1BAEH (Mitlesen, Vektor B2H wird beim Schreiben auf die Daten-PIO B gelegt),
  1BE8H (Eingabe), 1CC7H (/NIB), 1CDFH (AEB), 1CF9H (RETI).  Dazu **ASP-CTC K2 (D6H) ← 47H, 02H**
  (Zähler ÷2 — Bittakt der umgebauten K5020/LW 1200 aus der ASP [?]) und **ASP-CTC K1 als
  Zeitgeber** (Vektor FFEAH, D5H ← A5H,60H = 10 ms bzw. A5H,00H = 26,7 ms je Takt) [P 1F05/1F1D].
  K1 ist der SIO-B-Takt — Magnetband und IFSS/Drucker an SIO B schließen sich aus [?].
- **Nur Laufwerk 1:** Subadresse (IX+3) ≠ 1 → Fehler 12H [P 17DC].  [EGOS2 S.5] nennt lwnr 1 oder 2.
- **Kommandos** (IX+4, Bit 3 ausgeblendet): 00H schreiben, 02H lesen, 2xH rückspulen, 31H/41H
  reservieren ein/aus, 5xH Bandmarke, 6xH Schlusslücke, 7xH n-te Bandmarke suchen (Bit 2 =
  rückwärts).  **11H/15H (Block vor/zurück, K5200) sind nicht ausprogrammiert** → Fehler 10H
  [P 1812] — anders als [EGOS2 S.3].
- **Schreibschutz-Erkennung** beim Reservieren: AUF = 1 setzen, STA lesen (STA = „AVE zusammen mit
  AUF“, K5020 BD S.10) → Status Bit 2 [P 193E].  Das Statusbit 6 (19 cm/s) wird nie gesetzt.
- **Blockformat schreiben** [P 18A1–18EF, 1B69]: Präambel **AAH** (erstes Byte direkt an 30H), dann
  Daten (**20H…100H Byte**, d. h. 32…256 [P 1E64]), **CRC
  low, CRC high**, Postambel **AAH**.  Vorlücke = Wartezeit 13 × 26,7 ms ≈ 350 ms [P 1E7D],
  Nachlücke 110 ms [P 1B90].
- **CRC = CRC-16/ARC** (Polynom 8005H gespiegelt, Start 0000H, Bit 0 zuerst, nur über die Daten;
  Prüfwert „123456789“ = BB3DH nachgerechnet) [P 1DD1].
- **Bandmarke = Block ohne Daten**: AAH, 00H, 00H, AAH [P 19C9].  Schlusslücke: 34 × 250 ms mit AUF.
- **Lesen** [P 1BE8–1C9A]: Interrupt je Byte; **das erste Byte muss ABH sein** (die Präambel AAH
  kommt mit gesetztem Bit 0 an), sonst wird es verworfen [P 1BFF]; beim Mitlesen während des
  Schreibens wird Bit 0 des ersten Bytes ausgeblendet [P 1BC5].  Ein gelesenes AAH ist Postambel,
  wenn danach BMS = 0 ist [P 1C3A].  Länge = Bytes − 3; 0 = Bandmarke.  Puffer F910H (erstes Byte),
  Daten ab F911H, max. bis FA13H.  **Folge für K3/K4:** das Bandmodell muss beim Lesen das erste
  Byte (Präambel) als ABH liefern, oder der EGOS-Treiber findet keinen Block [Befund; Ursache in
  der K5020-Bittaktgewinnung [?] — K1 prüft gegen die anderen Treiber].
- **Lese-nach-Schreiben**: die Daten-PIO B liest während des Schreibens mit (Vektor B2H), Vergleich
  mit dem Puffer; Fehler → Wiederholen (Fehler 22H nach Ablauf) [P 1BAE, 1F4F].
- **Bandmarke suchen** [P 19F5]: schnell vor/zurück, je Block BMS abtasten; ein kurzer Block
  (≤ 13H von 159 Abtastungen BMS = 1) gilt als Bandmarke [Deutung].
- **Zeiten** (ASP-CTC K1, 10 ms): reservieren 260 ms + anwählen 260 ms + 2,5 s; freigeben 2,5 s;
  Rückspulen mit Wartezeiten 2 s / 750 ms / 260 ms / 1 s [P 196F ff.].
- **Ablauf**: der Auftraggeber setzt Kommando und Eintrittspunkt (IX+5/6), ruft 1FFDH und wartet
  (`JR $`); der Treiber arbeitet interruptgesteuert und kehrt aus dem letzten Interrupt in den
  Eintrittspunkt zurück [P 13BB, 1CFB].  Fehlercodes (IX+1) = [EGOS2 S.4].

### 2.2 Logischer Treiber KLOG (1000H–1416H) und Bedienprogramme (1417H–177CH)

- KLOG = Kennbyte 02, log. Gerät 4 (ADT nach INIT); Anforderungen 00/04/06/0AH/0EH [P 101C].
- **Kassettenaufbau** [P 111C]: Block 0 = `89H 'DIRECTORY' 00H 01H FFH` + Nullen (128 Byte), dann
  14 × 128 Byte Verzeichnis (FFH + 7FH × 00H), Bandmarke, Schlusslücke.  Dateien stehen hinter je
  einer Bandmarke, gefunden über „n-te Bandmarke suchen“; Blöcke zu 128 Byte [?: Feinaufbau der
  Verzeichniseinträge und des Dateikopfes FA64H, s. Kommentare P 12D4, 15FB].
- Das Bedienkommando **FORMAT** [EGOS2 S.5] heißt im ROM **`KINIT`**.  READ/WRITE wie [EGOS2 S.5–6];
  Typ A = Arbeitsbereich Kennbyte 13H Name „E“, Typ B = 1CH Name „F“ [P 156D, ?].
- Das Directory im RAM (Kennbyte 04, INIT legt es bei BAF8H an) hält das Kassettenverzeichnis.

## 3. Tastatur (SIO A der ASP, [P 044B/046F])

- 9600 Bd, 7 Bit, ungerade Parität, 2 Stoppbit (M0 bestätigt).  Empfangsinterrupt **FFFCH → 044BH**;
  er legt das Zeichen nach FB45H und stößt **CTC-ASP K3** an (`OUT (D7H),91H`), dessen Interrupt
  (FFEEH → 046FH) die eigentliche Auswertung macht — so wie [EGOS S.21] es Anwendern empfiehlt.
- **Tastatur → Rechner:** Zeichen 00H–1BH und 20H–7FH.  **1CH–1FH sind Präfixe** (FB7CH), die für
  die folgenden Zeichen gelten:
  | Präfix | Wirkung | Sperre (FB7FH) |
  |---|---|---|
  | 1CH | Zeichen; bei FB7FH Bit 4 Groß/Klein getauscht (Vorgabe 90H → an) | Bit 2 |
  | 1DH | Zeichen unverändert (2. Ebene [?]) | Bit 0 |
  | 1EH | **Sondertaste**: das Zeichen ist eine Bitmaske, 40H→0, 20H→1 … 01H→6, 00H→7; Aussprung FED0H+3n, wenn dort C3H steht | Bit 1 |
  | 1FH | Zeichen \| 80H (Funktions-/Kursortasten [?]) | Bit 3 |
  Eine gesperrte Taste liefert 1BH [?].  Die genaue Zuordnung Taste → (Präfix, Code) liegt in der
  Tastatur-Firmware (U881), die nicht vorliegt; für M3 genügt: Zeichentasten mit Präfix 1CH, Groß/
  Klein über den Tausch, Sondertasten 1EH + Maske.  [?] ob die Tastatur das Präfix vor JEDEM Zeichen
  oder nur bei Wechsel sendet — EGOS kommt mit beidem aus.
- **Rechner → Tastatur** (`OUT (D0H)`): **07H Piep** (WBN, Magnetband, und nach jeder Taste, wenn
  FB7FH Bit 7 = 0), **14H/12H** wenn FB7FH Bit 6 auf 1/0 wechselt (Anzeige/LED [?]).  Bit 5 war
  vorgesehen, die Ausgabe ist durch NOPs ersetzt [P 050B].
- Zeilenpuffer-Eingabe läuft ganz im Interrupt (Editor 0599H); Endezeichen ist jedes nicht
  bearbeitete Steuerzeichen, die Kommandoschleife führt bei **11H** aus und zeigt bei **13H** neu „OK“
  [P 07FB] — 11H ist also die ENTER-Taste, 13H die OFF-/Abbruchtaste [?, EGOS S.10/11 „OFF“].

## 4. Fassungsvergleich

Verglichen byteweise gegen den MAME-Satz (= `egos.prn`); Werkzeug: Ersetzen des Bereichs im
10-KB-Bild und Disassemblieren beider Seiten.

| Abzug (`doc/EPROMS/MC80/`) | Bereich | Abweichungen zu MAME | Deutung |
|---|---|---|---|
| `mc80.30e-0000.bin` | 0000–07FF | 18 Byte: 0033–0036, 0067–0068, 0075–0080 | NMI → 0075H: `LD HL,0AAFDH / LD (4001H),HL / LD HL,4000H / JP 0033H` → `LD (HL),L / JP 0C42H` = RAM-Kette ab 4000H neu anlegen (Distanz AAFDH → EB00H) statt RETN |
| `mc80.30e-0000v2.bin` | 0000–07FF | wie oben (0033H jetzt `LD (HL),0`) **+ 05EFH**: 46H → 4EH | Zeilenlänge des Editors 70 → 78 Zeichen |
| `mc80.30e-0800v2.bin` | 0800–0FFF | 151 Byte | **andere Systemausprägung**: INIT neu (0B65H ff.), RESET-Routine 0B46H kopiert 540H Byte aus 3A00H nach E580H und legt dort ein 5. ADT-Gerät an (`85 01 E580`), Kette bei C000H, Directory bei EB00H statt BAF8H; Fehlercode FDH → 48H (RSF/RLL); LOUT bis FFH Zeichen; **Zeichensatz mit Umlauten** (5BH–5DH Ä Ö Ü, 7BH–7EH ä ö ü ß, DIN 66003) |
| `mc80.33-1000.bin` (2 KB) | 1000–17FF | 1000–17C3 gleich, ab 17C4H anders | gleicher logischer Treiber KLOG/KINIT/READ/WRITE, **anderer physischer Treiber** (PK 3), beginnt bei 17E0H; die zweite Hälfte 1800H–1FFFH (mit der Sprungleiste 1FF7H) **fehlt** → MC80.33-Magnetband ist mit den vorhandenen Abzügen **nicht vollständig** (M7) |
| `mc80.3s-2000.bin` | 2000–23FF | 225F–228C, 23F3–23FE | Zusatzroutine im VERA-Leerraum (2260H: Ausgabe über FB48H) + Sprünge `JP 0B51H` (23F3H), `JP 225FH` (23FCH) |
| `mc80.3s-2400.bin` = `mc80.31e-2400-d0d7.bin` | 2400–27FF | 166 Byte | Einschaltmeldung durch `RET` ersetzt, Text „CCD-Meßsystem (C) EKG/DE 1990“; RKET kürzer (Distanz 6EH), neue RESET-Routine 2740H (ADT 15H Byte, 5 Geräte) und Kommando **SERVICE** (2760H, „Beratung: electronicon GmbH Gera“) → Kette läuft in `3s-2800` (RAM, TRAN, FILL, HEX, DEZ, CALC, FIND, COMP, ED, QDEF, RESET-Routine 3940H → 4000H) |
| `mc80.31e-3000/3400` | 3000–37FF | = `3s-3000/3400` | Editor ED |
| `mc80.31e-3800-7a4c.bin` | 3800–3BFF | ≠ `3s-3800` | jedes Byte doppelt (`FF CD 1C 20 FF CD 1C 20 …`) — **schlechter Abzug** (Adressleitung?) [Befund] |
| `mc80.3-x-*`, `-rk-*`, `-ccd-*`, `3s-2c00/3c00` | 2800–3FFF | kein 256-Byte-Block gleich | Fremd-/Zusatz-EPROMs, nicht Teil von EGOS |

**Papierquellen:** `mc8.3x-bsys-3.0_hex-0000-277f.pdf` („BSYS V3.0, Stand 19.8.85“): Stichproben
S.1 (0000H–03BFH) und S.7 (23C0H–277FH) gleich dem MAME-Satz bis auf die Einschaltmeldung
**„mc 80.31.“** statt „mc 80.30.“ (25EBH) und eine Handnotiz bei 2740H („01 → 19 in
Standardversion“) — also derselbe Stand, als MC80.31 gekennzeichnet.  `mc80.3x-sw-0aff.pdf`
(Listing „MC.KERN 0000.0AFF“) zeigt bei 0066H `JMP 0075H` und bei 0033H `36 00` = Fassung
**30E v2** (ycdt), nicht MAME.

**Empfehlung für M2:** MAME-Satz als Vorgabe (vollständig, Magnetband passend zu K5020 an 30H, Stand
der Papierquelle 19.8.85).  30E-Fassung (`30e-0000v2` + `30e-0800v2`) nur zusammen verwenden —
ihr NMI springt in ein INIT, das es nur mit `0800v2` gibt — und nur mit einem RAM/ROM bei 3A00H
(Quelle des kopierten Treibers unbekannt [?]).

## 5. Offene Punkte

| # | Punkt | Wer |
|---|---|---|
| 1 | Anschluss 2800H→4000H ohne Erweiterungs-ROM (FFH oder 00H?) | M2/M5 (Spiele müssen per Name startbar sein) |
| 2 | Wirkung der 256 Ausgaben auf 8CH im Einzelnen | Schaltplan `zve_bst_3o.pdf`; für M2 unerheblich |
| 3 | Polarität des VIS-Lesepfads (Funktion 011) | M3 am Bild |
| 4 | Tastaturcodes je Taste (Präfix/Code), Bedeutung 12H/14H | Tastaturfoto/Gerät; M3 nur Zeichenebene |
| 5 | Ursache „erstes Byte ABH“ beim Lesen | K1 (andere Treiber), K4 |
| 6 | Laufwerk 2 und Block vor/zurück im EGOS nicht ausprogrammiert — gab es eine spätere Fassung? | Anwender |
| 7 | Feinaufbau Verzeichniseintrag/Dateikopf auf der Kassette | M7/K1 |
| 8 | MC80.33: zweite Hälfte des Magnetband-ROMs (1800H–1FFFH) fehlt | Anwender (Wunschliste) |
