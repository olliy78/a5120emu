<!-- Angelegt 2026-10-05 (AP-V10, doc/design/24_k8915_varianten.md).  Diese Datei gilt WIE
     CLAUDE.md, sobald an den K8915-Varianten (Generation 2 / Generation 1) gearbeitet wird —
     sie ist nur nicht in jeder Anfrage geladen.  Begruendung: doc/merkposten/README.md -->

# K8915-Varianten (Gen 2 / Gen 1) — Merkposten

Neben dem V3 (ZRE 045-8762, `doc/merkposten/k8915.md`) kennt `K8915Machine` eine zweite
Ausführung: **Generation 2** = das Gerät des Anwenders (ZRE K2521 mit drei 2708, RAM-Karte
K3528, sonst wie V3). **Generation 1** (Tastatur K7634, K7028.10) ist **gesperrt**. Plan,
Herleitungen, Befunde je AP: **`doc/design/24_k8915_varianten.md`**; Analysen in
`doc/k8915g2/` (`karten.md` = K3528/K2521/K3820, `k7634.md` = Tastatur + K7028.10,
`zre_rom.md` = ROM-Analyse mit Portkarte und Anforderungsliste); Abzüge, MD5 und Listings in
`doc/EPROMS/K8915G2/` (`README.md`, `k8915g2_zre.prn`, `k8915g2_pfs3820.prn`).

**Stand 2026-10-05:** V1a–V7b erledigt (Karten, Tastatur, Listings, ROM-Analyse, K3528,
Maschine, Werkzeuge, C-ABI/Python, Modellwahl), K7634-Bildschirmtastatur angelegt; V5
entfallen, V8 gestrichen, V9 (Gen 1) zurückgestellt. Gen 2 läuft vom Netz-Ein bis `A>` der
V3-Systemdiskette 901.

**Gemeinsame Bausteine** (`K2521`, `K7024`, `K7028`, `K5122`, `K1520Bus`) nur über **neue
Konfiguration mit unveränderter Vorgabe** — die Gen 2 kam ohne eine einzige Änderung an
K2521/K3528/K7024/K5122/K7028 aus (nur neue Fabriken). Die Vorgabe von `K8915Machine` bleibt
**V3**; alle `K8915Boot.*`, `K8915Scpx.*`, `K8915Physical.*`, `K8915Format.*`, `RafK8915.*`,
`K6022Maschine.K8915` müssen unverändert grün bleiben.

## Was man nicht aufweichen darf

### Gen 2 = V3-Lader + K2521 + K3528 — eine Klasse, nicht zwei
- Die Gen 2 unterscheidet sich vom V3 **nur in CPU-Karte und Speicher**: der Lader
  0400–0906H ist mit `k8915_boot_2732.bin` **byteidentisch** bis auf 041BH und 042AH
  (`zre_rom.md` §0). Ladekopf, `/WAIT`-K5122, Software-CRC, Warmstart 0406H, Portmenge
  (ATS K7028.30 40H–5FH, K7672 an 52H/53H, Latch 61H, K5122 10H–18H) sind **dieselben
  Objekte mit derselben Verdrahtung**. Eigen sind nur 175 (Selbsttest) und die
  Meldungsroutine (`E`/`ESC c` ⇒ Neubeginn).
- Deshalb **`K8915Machine::Config::generation`** (`V3` Vorgabe | `Gen2`), keine zweite
  Klasse. **Gen 1 steht NICHT in der Aufzählung** — kein toter Pfad, solange V9 ruht.
- Umbau-Muster (R1): `zre8762_` (nur V3), `k2521_` + `ops_` (nur Gen 2) als `unique_ptr`,
  alle anderen Member Wert-Member in **derselben Reihenfolge** (Zerstörungsreihenfolge des
  Hubs). Weichen `cpuRef()`/`zreTakt()`/`zreInt()`/`memCpu(W)`. **`zre()` gilt nur am V3**
  (`assert`), `k2521()`/`ops()` nur an der Gen 2 — Werkzeuge fragen `generation()`.
- Interruptkette wie V3: K5122 → K2521 (CTC → PIO, `IeiQuelle::System`) → ATS
  [?, F22]. K2521-PIO 84H–87H vorhanden, vom ROM unbenutzt. Bild: K7024 mit dem
  A5120-Zeichensatz v171/v172 (`forK8915Gen2()`; Abzüge 171/172 byteidentisch).
- Wächter: `K8915Gen2Boot.*`, `K8915Gen2Scpx.LaedtDieV3SystemdisketteBisZumPrompt`
  (`k1520_test_k8915g2_boot`), `K7024.Gen2HatDenA5120Zeichensatz`.

### A8H: gleiches Register, bitkompatibel, anderer Träger
- Das Umschaltregister sitzt an der **K3528** (8212, A8H–ABH, nur schreibbar, Lesen → FFH)
  [?, F20/F21: im Plan handverdrahtet 88H]. Die K2521 schaltet **nicht** selbst um.
- **Bitkompatibel zum V3 für Bit 0/1/2/7**: Bit 0 = Seite 0000–3FFF, Bit 1 = 4000–7FFF,
  Bit 2 = 8000–FFFF, Bit 7 = `/MEMDI` (aktiv 1) sperrt die K2521 (ROM + RAM 0000–0FFF).
  Bit 3 = `/MEMDI1` ohne Verbraucher, Bit 4–6 wirkungslos — **keine Bank 2**. Vorrang:
  K3528-Seite gewählt → K3528; sonst 0000–0FFF ohne /MEMDI → K2521; sonst Bus (K7024 bei
  1000–17FFH, sonst FFH). Ein gewählter K3528-Zugriff erscheint **nicht** am Systembus.
- Gen-2-Werte nur 06H/0EH/87H/8FH (Bit 0 = Bit 7 immer); die Alternative „/MEMDI an Bit 0“
  ergibt dasselbe Bild und ist **Konfiguration** (`K3528::Belegung::memdiAnBit0()`), keine
  zweite Wahrheit. 1-KB-Ausblendadresse und Ausbau 48/32 KB: nicht modelliert.
- `K3528` meldet nur `registerIO`, **kein `registerMem`** (Speicherpfad wie `K8915Zre`);
  `ioWrite` bekommt die **absolute** Portnummer. Die ZRE-Seite ist ein Rückruf
  (`setZreWeg`) — die Karte kennt `K2521` nicht (Schichtung).
- Nach ROM-Fehler + `CR` (nur noch mit dem Abzug, s. u.) übergibt der Lader mit **0EH**, nicht
  06H (der Stub setzt 0EH, 06H erst nach dem RAM-Test, den der erste Fehler überspringt) —
  Gastverhalten, kein Befund. Mit der Vorgabe steht am Ende 06H.
- Wächter: `K3528Test.SpeicherbildJeRegisterwert`, `.ResetLoeschtRegisterRamBleibt`,
  `.RegisterHatVierPorts`, `.RegisterIstNichtLesbar`, `.GewaehlterSpeicherErscheintNichtAmBus`,
  `.MemdiSperrtDieZre`, `.Memdi1OhneWirkungAufDasBild`, `.Bits4Bis6Wirkungslos`,
  `.TraceSiehtRamUndZreAberNichtDenBus`, `K3528Config.MemdiAnBit0ErgibtDasselbeBildFuerAlleBekanntenWerte`,
  `K3528Config.BelegungIstKonfiguration`; am ROM `K8915Gen2Boot.RamTestSiehtUnterRomUndBild`.

### ROM 177: Vorgabe ist die REPARIERTE Fassung — den Abzug NIE ändern (F9 gelöst 2026-10-05)
- Je 1-KB-Baustein stehen in den letzten 3 Byte die 24-Bit-Summe (hoch..tief) der ersten
  3FDH Byte. **Im Abzug 177 stimmt sie nicht**: errechnet 00A680H, gespeichert 00A67CH; Byte
  0A33H = 04H im Füllbereich erklärt die Differenz genau. Zwei weitere Lesungen am Gerät sind
  byteidentisch ⇒ gekipptes Bit **im Baustein** (`doc/EPROMS/K8915G2/README.md` „177 repariert“).
- **Vorgabe des Kerns** (`K2521::Config::k8915g2()`) = `rom_k8915g2_repariert.h`
  (`K8915G2_ZRE_ROM_REPARIERT`, erzeugt mit `tools/eprom_to_h.py … --weitere …` aus 175 + 176 +
  `k8915g2_zre_0800_177_repariert.bin`): Selbsttest ROM → KEY → CTC → SIO → RAM läuft durch,
  ohne Tastendruck zur Kaltstartmeldung, A8H am Ende 06H.
- **`doc/EPROMS/K8915G2/*.bin` und `rom_k8915g2.h` (`K8915G2_ZRE_ROM` = Abzug) bleiben
  unverändert.** Den Abzug setzt nur ein Test über `Config::gen2_rom` (nur für Tests,
  `nullptr` = Vorgabe): dann bleibt der Selbsttest bei „ROM“ mit **`C`** stehen (ERROR-Lampe,
  61H = 7FH, 16 × BEL), **`CR` führt trotzdem zum Lader**.
- Wächter: `K2521Rom.K8915Gen2AbzugUnveraendert` (Abzug: Summen, 0A33H = 04H),
  `K2521Rom.K8915Gen2VorgabeIstRepariertesRom` (Vorgabe = Abzug bis auf genau 0A33H = 00H, alle
  drei Summen stimmen), `cli_k8915g2_prn_passt_zur_quelle` (MD5 aller Abzüge, jedes Byte genau
  einmal im Listing), `K8915Gen2Boot.VorgabeSelbsttestFehlerfreiBisColdstart`,
  `K8915Gen2Boot.AbzugRomFehlerCDannCrZurKaltstartmeldung`, `cli_bt_k8915g2_coldstart`
  (verbietet „Selbsttestfehler“).
- Selbsttest-Reihenfolge **ROM → KEY → CTC → SIO → RAM** (≈ 17 Mio. Takte); Prüfstecker
  und Tastatur wie V3 (ohne Stecker `G` unter SIO, ohne Tastatur `A` unter KEY). NMI im ROM
  = `RETN`. „System im RAM“ (`C3` bei 0000H/0005H) überspringt den Selbsttest. Wächter
  `K8915Gen2Boot.OhnePruefsteckerScheitertSio`, `.OhneTastaturScheitertKeyMitA`,
  `.SystemImRamFuehrtZumLader`, `.EOderEscCStartetNeu`, `.NmiImRomWirkungslos`.
- `RADE` (Autostart der 901) meldet „no RAM-device configurated or fatal RAM-error !!!“
  — **zwingend** (F13, statisch): seine Maschinenprobe (A8H = 0EH, 00H nach 0C00H schreiben
  und zurücklesen) findet am V2 das K2521-RAM, wählt deshalb Port **A0H** (RAM-Karte am Bus)
  statt A8H, und dort antwortet nichts. Der Wächter prüft den Prompt danach, nicht RADE.

### Gen 1 ist gesperrt (F11) — kein ROM nacherfinden
- Die Kartenchips „3C00“/„3000“ der PFS K3820 sind ein **Gen-1-Urlader** (Arbeitszellen
  0Cxx, Stub 095BH, Tastatur über `IN E1H`/`IN E0H`, Codes `1FH`/`9DH` der K7634). Der dazu
  gehörige **Lader-Baustein für 0400H fehlt** (Platz 0400 der Karte leer, ZRE-176 passt
  nicht). Ohne ihn ist Gen 1 **nicht startfähig**; aus 176 wird nichts zusammengebaut.
  Er **existiert** (F11): `anflad.rom` (3 KB, Forum 5713 Beitrag 007/028) enthält ein 176
  mit Zellen 0Cxx, dazu eine weitere Fassung von „3C00“ und „3000“ — nicht öffentlich.
- Das Gen-2-ROM fragt die K7634 **nicht** ab (`KEY` liest SIO2-B 52H/53H = K7672).
  Gen 1 brauchte außerdem eine **andere ATS** (K7028.10, E0H–FFH; Tastatur E0H Daten,
  E1H Status Bit 3, E2H Kommando) — die K7028.30 im Kern trägt die K7634 nicht, und die
  Option K6022 (E0H–E7H) kollidierte.
- Schnittstellen: `k1520_create_k8915(2, …)` → NULL mit Grund „Gen 1: kein startfaehiger
  Urlader (F11)“; im `k8915emu` als **ausgegrauter** Eintrag (`gesperrte_modelle`,
  „kein Urlader-Baustein für 0400H (F11)“). `model: k8915-g1` in der Konfiguration wird wie
  jeder unbekannte Schlüssel zum V3. Wiedervorlage (V9) erst bei Antwort auf F11 oder F5/F8.
- Wächter: `test_generation_two_of_the_k8915_is_refused_with_a_reason` (`py_k8915_smoke`),
  `test_the_k8915_offers_v3_and_gen2_and_shows_gen1_locked`,
  `test_an_unknown_k8915_model_falls_back_to_v3` (`py_k8915emu_gui`).

### K7634-Bildschirmtastatur: angelegt, NICHT eingehängt
- `app/ui/keyboard_k7634.py` (`KeyboardK7634Widget`): Layout nach dem Foto des Anwenders,
  Codetabelle **K7634.04** aus `doc/k8915g2/k7634.md` §5 (Berichtigung `E02` b = `22H` [?]).
  Eine Taste sendet ihre **Position** (`0x03000000 | Rechenadresse`) — es gibt **noch kein
  Kerngegenstück**, deshalb steht sie in keinem Programmprofil. Erst mit V9 einhängen.
- Befunde am Foto: Funktionsreihe lückenlos G01…G17, G51…G53 ⇒ die zwei **fehlenden
  Kappen** sind **G04 REC (`FDH`)** und **G05 PF1 (`C1H`)**; `CLEAR TAB` sitzt auf G03 =
  CNCL (`FEH`). Im Cursorblock fehlt **A15 ↵ (`0AH`)**. Beschriftungen ohne Tabelleneintrag
  (STRG CHOI PICK LOC, UPDATE ⇑ ⇓) liegen auf unbestückten Positionen [?]; TAB←/TAB→/HOLD/
  NEXT PAGE/CR/LF/ESC/DEL tragen die Codes ihrer Position. **Die gelieferte Tastatur ist
  eine andere Fassung als .04 (F7)** — die Fremdquellen erwarten TYP `A0H`, ENTER `9DH`,
  RESET `1FH`, PF1 `91H`: das ist die **K7634.01** (ROM Y708-I 27, Tabelle in
  tiffe `misc/Tastaturen/Tastaturen_K_7632_34_35_36_Betriebsdokumentation_.pdf` S. 21–23,
  CTRL A99). Umstellen der Widget-Tabelle auf .01 = eigenes AP, erst nach Aufdruck/ROM der
  Anwendertastatur.
- Wächter: `py_keyboard_k7634` (Tabelle ≙ Doku, jede Taste ein Code, keine
  Doppelbelegung, die drei ergänzten Kappen).

### PFS K3820 (2708-Karte): Karte im Kern, von KEINER Maschine benutzt
- Steckt im V2 des Anwenders (F2: 012-7040), wird dort nie angesprochen; 14 von 16 Chips
  leer, zwei belegt (Gen-1-Urlader, s. o.). Auf Anwenderwunsch (AP-V11, löst V5 ab) steht
  sie als `core/cards/k3820/` (`k1520_k3820`) im Kern — **nicht eingebaut**, keine C-ABI,
  keine Oberfläche; ein Einbau (K8911/K8912) wäre eine steckbare Option (Muster RAF/K6022).
- Brücken nur als `Config`: Start X8/X9 in 4-KB-Schritten (`ausBruecken`, gebrückt = Bit
  [?, F22]), 16 KB jenseits FFFFH laufen auf 0000H über [?], X6/X7 = `Memdi`/`Memdi1`/
  `Memdi2`/`Keine`, X10–X11 = `wait_m1` (nur Angabe). Leere Sockel lesen FFH, Schreiben
  wirkungslos. Bus-/MEMDI sperrt über den Bus je Zugriff (`MemdiDriver`, unterscheidet
  keine Brücke); /MEMDI1/2 statisch über `setMemdi1/2` → Karte meldet sich ab/an.
- Wächter: `K3820.*` (`k1520_test_k3820`, liest die Abzüge in `doc/EPROMS/K8915G2/`),
  Listing weiter `cli_k8915g2_prn_passt_zur_quelle`.

### Namen und Werkzeuge
- Schlüssel sind technisch und werden **nicht** umbenannt; nur Anzeigetexte folgen F1/F6:
  Kern `Generation::Gen2`, C-ABI `k1520_create_k8915(1, d0..d3)` /
  `k1520_k8915_generation(h)` (0/1, −1 bei anderen Maschinen), `k1520_machine_type` = 2,
  Python `K1520Emulator(machine="k8915-g2")` + `k8915_generation()`, Konfiguration
  `general.model: k8915-g2` (fehlend = V3; `data/default_config_k8915.yaml` trägt **kein**
  `general.model`), Anzeige „K8915 Gen 2 (ZRE K2521, 64 KB)“ [?, F6]. Ein EM gibt es am
  K8915 nie (`a5120.16` → `k8915`). Wächter `py_c_api`,
  `test_gen2_reaches_the_coldstart_message_after_rom_error_c`,
  `test_k8915_generation_is_reported_per_machine`,
  `test_switching_the_k8915_model_rebuilds_the_machine`,
  `test_only_the_a5120_offers_the_a5120_16_model`.
- **`boot_trace`/`k1520dbg --machine k8915-g2`**: `map` zeigt A8H, Bild, /MEMDI/MEMDI1
  (Zeichen `Z` ZRE, `M` K3528-RAM, `.` Bus); `bank` meldet „nicht vorhanden“; `ctc`
  beschriftet „ZRE K2521“. `boot_trace` tippt `CR` beim Selbsttestfehler (Buchstabe Zeile
  23/Spalte 70, ERROR-Lampe, ≥ 16 BEL) **und** nach „\* Coldstart \*“ (`--no-cr` schaltet
  beides ab); `--skip-selftest` wird an der Gen 2 ignoriert (Warnung). Listing-Annotation
  `-l doc/EPROMS/K8915G2/k8915g2_zre.prn@0xFC00:0021-03FF` (Kopie von 175) und
  `…@:0400-0BFF` (unverschoben); `prn_listing.h` überspringt dafür die zweite Adressspalte
  „LAUF“. Wächter `cli_dbg_k8915g2_all_commands_smoke`, `cli_bt_k8915g2_coldstart`,
  `cli_bt_k8915g2_prompt`. Referenzen: `tools/k1520dbg.md` §11, `tools/boot_trace.md` §7.

## Offene Fragen an den Anwender (Volltext: Entwurf 24 §9)

| # | Kurz |
|---|---|
| F1 | Gen 2 = robotrontechnik „5¼″ V2“, Gen 1 = „V1“? (nur Anzeigenamen hängen daran) |
| F2 | ✔ K3528 045-8530 + PFS K3820 012-7040 (ungenutzt); Steckplätze offen |
| F3 | Brücken ZRE X6–X15 und RAM-Karte; Aufschriften/Plätze der ZRE-EPROMs |
| F4 | Laufwerke; Gen-2-Systemdiskette (sonst V8 bleibt gestrichen) |
| F5 | teilweise: Belege zeigen auf K7028.10; ROM = `anflad.rom` (F11), nicht 175–177; Diskette + Freigabe für Anfragen offen |
| F6 | Anzeigename der Variante |
| F7 | teilweise: **K7634.01** (ROM Y708-I 27, TYP A0H, RESET 1FH, ENTER 9DH, PF1 91H) gefunden (tiffe `misc/Tastaturen/…7632_34_35_36…pdf` S. 21–23); ob die Anwendertastatur eine .01 ist, nur am Aufdruck |
| F8 | teilweise: Schaltplan nicht gefunden (felgentreu „Heft13_K7028.20_30.pdf“ 404); Gen-1-ZRE = Satz wie `anflad.rom` (0000 ≈ Chip „3C00“, 0800 = Chip „3000“) |
| F9 | **gelöst 2026-10-05:** zwei weitere Lesungen identisch, Bitkipp 0A33H; Vorgabe = repariertes 177 |
| F10 | offen: B1H/B3H, E3H Bit 2, D001–D003H — keine Unterlage (schon 2010 im Forum ungeklärt) |
| F11 | teilweise: **existiert** als `anflad.rom` (3 KB, Forum 5713 Beitr. 007/028; 176 mit Zellen 0Cxx), nicht öffentlich — nur über Dritte (**Blocker für Gen 1** bleibt) |
| F12 | ROM-Teil geklärt: Reihenfolge ROM→KEY→CTC→SIO→RAM; SIO verlangt Echo auf SIO1-A/-B, SIO2-A (sonst `G`); Stecker oder ATS-Brücke = offen wie Entwurf 16 §6.10 |
| F13 | ✔ geklärt (statisch): RAM bei 0C00H ⇒ RADE wählt Port A0H (RAM-Karte am Bus) ⇒ Meldung auf jedem Weg, dann `A>` |
| F20 | Aufdruck der RAM-Karte; Wickelfeld X3 (Registeradresse A8H, Bank-Brücken) |
| F21 | Gilt die handschriftliche „88“ im K3528-Plan für dieses Gerät? |
| F22 | Brückenstand ZRE X6–X9/X14/X15 (Interruptkette), K3820 X6–X11 |
| F23 | Feld X3:23–45 (Registerbit → /MEMDI…/MEMDI3) |
| F24 | ✔ geklärt: 08H–0FH = ZVE-PIO/-CTC der **K2526** (Forum 5713 Beitr. 046, `cpabas.erl`); keine andere K2521-Dekodierung |


## Namen (2026-10-05, Antwort auf F1)

Anzeige/Handbuch: **K8915 V3** und **K8915 V2** (Gerät des Anwenders, 64 KB, K7672); **V1** (K7634) gesperrt. Interne Schlüssel `k8915-g2`/`Gen2` bleiben, nur Anzeigetexte tragen V2. Die K7634 stammt von einem K8912, nicht von einer V1 — „V1 = K7634 + K7028“ ist unbelegt.
