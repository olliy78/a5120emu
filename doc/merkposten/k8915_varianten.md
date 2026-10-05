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
- Nach ROM-Fehler + `CR` übergibt der Lader mit **0EH**, nicht 06H (der Stub setzt 0EH,
  06H erst nach dem RAM-Test, den der erste Fehler überspringt) — Gastverhalten, kein Befund.
- Wächter: `K3528Test.SpeicherbildJeRegisterwert`, `.ResetLoeschtRegisterRamBleibt`,
  `.RegisterHatVierPorts`, `.RegisterIstNichtLesbar`, `.GewaehlterSpeicherErscheintNichtAmBus`,
  `.MemdiSperrtDieZre`, `.Memdi1OhneWirkungAufDasBild`, `.Bits4Bis6Wirkungslos`,
  `.TraceSiehtRamUndZreAberNichtDenBus`, `K3528Config.MemdiAnBit0ErgibtDasselbeBildFuerAlleBekanntenWerte`,
  `K3528Config.BelegungIstKonfiguration`; am ROM `K8915Gen2Boot.RamTestSiehtUnterRomUndBild`.

### ROM 177: Prüfsummenfehler, Selbsttest endet mit „ROM C“ — den Abzug NIE ändern
- Je 1-KB-Baustein stehen in den letzten 3 Byte die 24-Bit-Summe (hoch..tief) der ersten
  3FDH Byte. **177 stimmt nicht**: errechnet 00A680H, gespeichert 00A67CH; Byte 0A33H = 04H
  im Füllbereich erklärt die Differenz genau (Lese- oder Bitfehler im 2708, **F9**).
- Folge: der Selbsttest bleibt bei „ROM“ mit Kennbuchstabe **`C`** stehen (ERROR-Lampe,
  61H = 7FH, 16 × BEL), KEY…RAM ungeprüft; **`CR` führt trotzdem zum Lader**. Das ist das
  Verhalten des gelieferten Abzugs, kein Emulatorfehler.
- **`doc/EPROMS/K8915G2/*.bin` und `rom_k8915g2.h` bleiben unverändert.** Für den vollen
  Selbsttest legt ein Test eine **Kopie** an und setzt 0A33H := 00H (`Config::gen2_rom`,
  nur für Tests, Vorgabe `nullptr` = Abzug). Liefert der Anwender einen neu gelesenen 177,
  ersetzt der volle Selbsttest den Wächter `RomFehlerC…`.
- Wächter: `K2521Rom.K8915Gen2AbzugUnveraendert` (Inhalt + Summen, 0A33H = 04H),
  `cli_k8915g2_prn_passt_zur_quelle` (MD5 aller Abzüge, jedes Byte genau einmal im Listing),
  `K8915Gen2Boot.RomFehlerCDannCrZurKaltstartmeldung`,
  `K8915Gen2Boot.GeflickteSummeSelbsttestFehlerfreiBisColdstart`.
- Selbsttest-Reihenfolge **ROM → KEY → CTC → SIO → RAM** (≈ 17 Mio. Takte); Prüfstecker
  und Tastatur wie V3 (ohne Stecker `G` unter SIO, ohne Tastatur `A` unter KEY). NMI im ROM
  = `RETN`. „System im RAM“ (`C3` bei 0000H/0005H) überspringt den Selbsttest. Wächter
  `K8915Gen2Boot.OhnePruefsteckerScheitertSio`, `.OhneTastaturScheitertKeyMitA`,
  `.SystemImRamFuehrtZumLader`, `.EOderEscCStartetNeu`, `.NmiImRomWirkungslos`.
- `RADE` (Autostart der 901) meldet „no RAM-device configurated or fatal RAM-error !!!“
  (keine Bank 2) — **erwartet**, der Wächter prüft den Prompt danach, nicht RADE (F13).

### Gen 1 ist gesperrt (F11) — kein ROM nacherfinden
- Die Kartenchips „3C00“/„3000“ der PFS K3820 sind ein **Gen-1-Urlader** (Arbeitszellen
  0Cxx, Stub 095BH, Tastatur über `IN E1H`/`IN E0H`, Codes `1FH`/`9DH` der K7634). Der dazu
  gehörige **Lader-Baustein für 0400H fehlt** (Platz 0400 der Karte leer, ZRE-176 passt
  nicht). Ohne ihn ist Gen 1 **nicht startfähig**; aus 176 wird nichts zusammengebaut.
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
  RESET `1FH`, PF1 `91H`, keine gedruckte Tabelle hat das. CTRL hat in .04 keine Position.
- Wächter: `py_keyboard_k7634` (Tabelle ≙ Doku, jede Taste ein Code, keine
  Doppelbelegung, die drei ergänzten Kappen).

### PFS K3820 (2708-Karte) ist nur Beigabe
- 14 von 16 Chips leer, zwei belegt (Gen-1-Urlader, s. o.). Im Gen-2-Ablauf **nie
  angesprochen**, neben 64 KB RAM nicht betreibbar ⇒ **keine Karte im Kern** (V5 entfallen).
  Abzüge, MD5 und Listing liegen im Repo, bewacht von `cli_k8915g2_prn_passt_zur_quelle`.
  Erst als steckbare Option (Muster RAF/K6022) modellieren, wenn ein Gast sie benutzt.

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
| F2 | Kartenliste mit Platinennummern und Steckplätzen der Gen 2 |
| F3 | Brücken ZRE X6–X15 und RAM-Karte; Aufschriften/Plätze der ZRE-EPROMs |
| F4 | Laufwerke; Gen-2-Systemdiskette (sonst V8 bleibt gestrichen) |
| F5 | K7028-Fassung und ROM der Gen 1; Freigabe für Forenanfragen |
| F6 | Anzeigename der Variante |
| F7 | Fassung der K7634 (`K7634.xx`, ROM `Y708-I…`) — Foto weicht von .04 ab |
| F8 | Schaltplan K7028.10/.20 (E3H/E4H, Frontplatte); steckt „3C00“ in der Gen-1-ZRE? |
| F9 | Baustein 177 ein zweites Mal lesen; meldet das Gerät „ROM“-Fehler? |
| F10 | Gen-1-Chip „3C00“: B1H/B3H, E3H Bit 2, Prüfung D001–D003H — welche Karten? |
| F11 | Gibt es den Gen-1-Lader-Baustein für 0400H? (**Blocker für Gen 1**) |
| F12 | Braucht der Gen-2-Selbsttest am Gerät einen Prüfstecker; Testreihenfolge? |
| F13 | Läuft `RADE` am Gen-2-Gerät? (Emulator: Fehlermeldung, dann `A>`) |
| F20 | Aufdruck der RAM-Karte; Wickelfeld X3 (Registeradresse A8H, Bank-Brücken) |
| F21 | Gilt die handschriftliche „88“ im K3528-Plan für dieses Gerät? |
| F22 | Brückenstand ZRE X6–X9/X14/X15 (Interruptkette), K3820 X6–X11 |
| F23 | Feld X3:23–45 (Registerbit → /MEMDI…/MEMDI3) |
| F24 | Herkunft „PIO 08–0FH“ (Forum) — andere K2521-Dekodierung? |


## Namen (2026-10-05, Antwort auf F1)

Anzeige/Handbuch: **K8915 V3** und **K8915 V2** (Gerät des Anwenders, 64 KB, K7672); **V1** (K7634) gesperrt. Interne Schlüssel `k8915-g2`/`Gen2` bleiben, nur Anzeigetexte tragen V2. Die K7634 stammt von einem K8912, nicht von einer V1 — „V1 = K7634 + K7028“ ist unbelegt.
