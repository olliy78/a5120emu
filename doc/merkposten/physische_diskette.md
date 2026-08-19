<!-- Ausgelagert aus CLAUDE.md am 2026-08-19.  Diese Datei gilt WIE CLAUDE.md,
     sobald an diesem Teilsystem gearbeitet wird — sie ist nur nicht mehr in jeder
     Anfrage geladen.  Begruendung: doc/merkposten/README.md -->

# Physische Diskette am Greaseweazle — Merkposten

Neben der Datei (`.img`/`.hfe`/`.dmk`) gibt es seit 2026-08-15 eine **zweite Art von
Bindung** des internen Mediums: ein **echtes Laufwerk** an einem
[Greaseweazle](https://github.com/keirf/greaseweazle)-Adapter.  Der Unterschied ist
nicht das Medium, sondern die **Körnung** — gelesen und geschrieben wird **spurweise
nach Bedarf**, der Zwischenschritt „ganze Diskette in eine Datei" entfällt.  Voller
Entwurf: **`doc/design/14_physische_diskette.md`**, Medium-Sicht:
`doc/design/09_floppy_drive.md` §12, Architektur: `doc/K1520_architecture.md` §8.8.

```
                                                    ┌── K5122 (Emulator)
echte Diskette ◄─gw─► Arbeitsfaden ◄─Aufträge─► DiskMedium
                       (app/gw/)                    └── DiskVolume (DiskTool)
```

Was man beim Weiterarbeiten wissen muss:

- **Der Kern kennt Greaseweazle NICHT.**  Kein USB, kein Import, kein Rückruf in die
  Anwendung.  `TrackSync` hat **keinen eigenen Faden**; ein fremder Arbeitsfaden holt
  Aufträge ab (`k1520s_take_job` — die einzige blockierende ABI-Funktion, ctypes gibt
  dabei die GIL frei) und liefert **HFE-Bitzellen** zurück, die durch denselben
  `BitCodec::decode` laufen wie eine `.hfe`-Datei.  Ein anderer Adapter wäre ein anderes
  `device` in `app/gw/`, keine Kernänderung.
- **Je Spur ein Zustand** statt eines Dirty-Bits: `Unknown` (nie gelesen, Inhalt
  **ungültig**) / `Clean` / `Dirty`.  Das ist **ein** Konzept für alle Medien — bei
  einer Datei tritt `Unknown` nur nie auf.  Der Unterschied, an dem alles hängt:
  **`loadTrack` (gelesen) macht sauber, `setTrack` (geschrieben) macht schmutzig**.  **`Unknown` ≠ „unformatiert"** — letzteres ist eine
  belegte Aussage über die Diskette, ersteres gar keine; deshalb melden
  `formatted()`/`rawCompatible()` zusätzlich `complete()`, sonst erklärte sich eine halb
  gelesene Diskette für leer und ein `.img` schriebe die ungelesenen Spuren als
  Füllbytes fest.
- **Nachgeladen wird in `DiskMedium::track()` — und NUR dort.**  Das ist die einzige
  Stelle, durch die jeder Verbraucher geht (K5122 über `FloppyDriveV2`, DiskTool über
  `DiskVolume`, Erkennung über `GeometryProbe`); der Aufruf **blockiert** (0,5–0,8 s je
  Spur, am echten Gerät gemessen).  Medienweite Reihenläufe benutzen **`peek()`** und
  laden nie nach — sonst zieht eine beiläufige Statusabfrage die ganze Diskette ein.
  `mutableTrack()` lädt nach (Sektorschreiben ist Lesen-Ändern-Schreiben), `setTrack()`
  nicht (Vollspur-FORMAT ersetzt die Spur) — daran hängt, dass eine Leerdiskette im
  echten Laufwerk formatiert werden kann, ohne vorher gelesen zu werden.
  Wächter: `TrackSync.ReihenlaufLaedtNichtNach`.
- **Ein Sektor mit falscher Pruefsumme wird NACHGELESEN** (2026-08-18, Entwurf §5.4a).
  Der Arbeitsfaden tastet je Auftrag nur EINE Umdrehung ab; auf einer gealterten
  Diskette liefert das gelegentlich einen Sektor mit falscher Daten-CRC, der beim
  naechsten Versuch heil zurueckkommt (an der P8000-Diskette von 1988 gemessen: einer
  unter 2560).  Ohne Wiederholung wanderte der Ausrutscher **unbemerkt in eine Datei**.
  `TrackSync` zaehlt daher nach dem Dekodieren die Sektoren mit falscher ID- **oder**
  Daten-CRC und stellt die Spur erneut ein (`read_crc_retries`, Vorgabe 2);
  uebernommen wird der **beste** Versuch, nicht der letzte, und ein Rest wird gemeldet
  (`read_crc_bad`) statt die Spur zu verweigern.  Eine **markenlose** Spur ist
  unformatiert und wird nie wiederholt — sonst kostete jede Leerspur die dreifache
  Zeit.  Waechter: `TrackSync.EinLeseausrutscher*`, `TrackSync.EineDauerhaft*`,
  `TrackSync.EineHeileSpurWirdNiemalsZweimalGelesen`.
- **Geschrieben gilt erst nach dem ZURUECKLESEN** (Entwurf §7.1).  Der Verify-Lauf des
  Gastsystems (`FORMAT`) prüft das **Speicherabbild gegen sich selbst** und sieht eine
  Schadstelle der Diskette nie — deshalb folgt jedem `Write` ein `Verify` (Spur
  zurücklesen, auf **Sektorebene** vergleichen: IDs, Nutzdaten, Anhang hinter der
  Daten-CRC und **beide CRCs des Sektors** — ID-Feld *und* Datenfeld tragen je eine,
  und eine kaputte ID-CRC macht den Sektor unauffindbar, auch wenn die Daten heil sind;
  nachlaufende Gap-Bytes werden abgeschnitten, byteweise gleich sind zwei Aufnahmen nie).  Erst dann wird `Dirty` gelöscht.  Stimmt es nicht:
  **einmal** neu schreiben und erneut prüfen, sonst gilt die Spur als **schadhaft** —
  sie bleibt `Dirty`, `flushPending()` meldet Misserfolg mit Spurnummer, und die
  Oberfläche bietet **„Diskette neu beschreiben"** (`rewriteAll()`, stellt jede
  **bekannte** Spur erneut ein; unbekannte bleiben weg, sie trügen Müll auf die neue
  Diskette).  **Das Zurückgelesene wird NIE ins Abbild übernommen** — sonst
  überschriebe ein misslungener Schreibvorgang genau die Daten, die er zerstört hat.
  Für den Arbeitsfaden ist `Verify` dasselbe wie `Read`.  Abschaltbar über
  `verify_writes`, Vorgabe **an**.
- **Drei Prioritäten:** 1 Lesen auf Anforderung (jemand wartet) → 2 geänderte Spuren
  zurückschreiben (samt Prüf-Lesen, das **vor** neuen Schreibvorgängen kommt) →
  3 unbekannte Spuren vorauslesen (kürzester Kopfweg zuerst).
  Prio 1 **verdrängt**, unterbricht aber **keinen laufenden** Zugriff (der Faden steckt
  in einer Übertragung).  Zurückgeschrieben wird erst nach einer **Schreibpause**
  (≈ 0,5 s) — dieselbe Regel wie der Autosave, sonst schriebe eine UDOS-Dateioperation
  dieselbe Spur dutzendfach.  Eine gescheiterte Rückführung lässt die Spur `Dirty`
  (eine verlorene Änderung wäre der schlimmere Ausgang); Abmelden wartet darauf.
- **Physisch heißt schreibgeschützt, bis jemand widerspricht** — ein Fehler kostet hier
  nicht eine Kopie, sondern die einzige noch existierende Diskette.
- **Eine Rücknahme (`DiskVolume`-Transaktion) braucht `restoreFrom`**, nicht eine
  Zuweisung: was schon auf der echten Scheibe steht, holt keine Kopie im Speicher
  zurück — die zurückgesetzten Spuren müssen **erneut als geändert** gelten.
- **Schreiben braucht die GEMESSENE Drehzahl.**  Die Bitzellen kommen mit der
  *nominellen* Zellrate herein, das Laufwerk dreht mit seiner eigenen Drehzahl; die
  Flusszeiten müssen auf `usb.read_track(2).ticks_per_rev` gestreckt werden (einmal je
  Sitzung gemessen, je Spur `faktor = takte/fluss.ticks_to_index` mit mitgeschlepptem
  Rundungsrest — wie `gw write`).  Ohne das bricht der Adapter mit **`Flux Underflow`**
  ab; das war der einzige Stolperstein des Schreibpfads.
- **Die Oberflächen liegen in `app/ui/physical_disk.py`** — Dialog, `PhysicalSession`
  (Sync + Arbeitsfaden in einem, `close()` in der Reihenfolge Faden → Synchronisierer)
  und `mit_fortschritt()`.  **Beide** Programme benutzen dasselbe Stück: der Emulator
  einen Knopf „Physisch…" je Laufwerkskasten samt Füllstandszeile, das DiskTool
  „Physisches Laufwerk…" mit Fortschrittsanzeige (das Öffnen misst nur eine Stichprobe, ~10 s).  Eine physische Diskette gehört **nicht** in `get_mounts()` und wird von
  `remount_all()` nicht angefasst (kein Pfad, Handle verbraucht).
- **Die Hosttools liegen nicht auf PyPI** und sind eine **freiwillige** Abhängigkeit:
  `pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"` (der Zweigkopf
  meldet sich als Pre-Release).  Fehlt das Paket, fehlt nur der Menüpunkt.
- **Tests brauchen keine Hardware** (in der CI ist nie ein Laufwerk):
  `TrackSync.*` (29 Fälle) mit einem Ersatz-Arbeitsfaden aus dem RAM — inkl.
  **Schadstelle** (die Spur meldet Schreiberfolg, liefert beim Lesen aber den alten
  Inhalt) —, `PhysicalBoot.*`
  (**CP/A bootet spurweise bis `A>` und holt dabei weniger als die halbe Diskette**),
  `py_gw_physical` (Ersatzlaufwerk über einer `.hfe` — **ohne** `greaseweazle`-Import;
  dieselbe Diskette einmal als Datei und einmal „physisch" muss dasselbe liefern; dazu
  ein **Drift-Wächter**, der die `ctypes.Structure` gegen den C-Kopf hält — eine
  vertauschte Feldreihenfolge stürzt nicht ab, sie liefert still falsche Zahlen) und
  `py_gw_gui` (beide Oberflächen, inkl. Schadstellen-Meldung und Ausweg).
  Die echten Hardware-Tests liegen in `tests/python/test_gw_hardware.py`, sind **nicht**
  in ctest registriert und laufen nur mit `K1520_GW_HARDWARE=1` (Schreiben zusätzlich
  nur mit `K1520_GW_WRITE=1`).
- **An echter Hardware nachgewiesen** (Greaseweazle F1 + K5601 + UDOS 4.3): Lesen
  0,5–0,8 s je Spur; **Emulator-Kaltstart von der eingelegten Diskette** (UDOS meldet
  sich bei erst 62 von 160 gelesenen Spuren); **Datei auf die echte Diskette
  geschrieben** (4 Spuren zurückgeführt, danach die ganze Diskette neu eingelesen →
  byteweise gleich); beide Oberflächen einmal durchgefahren.  Vor Schreibversuchen die
  Diskette sichern (`gw read` über alle Spuren, 2 MB `.hfe`).
- **Im DiskTool ist es eine AKTION, kein Knopf** (2026-08-16, beim Zusammenführen mit
  `create_disktool`): `act_physisch` (*Datei ▸ Physisches Laufwerk…*, Strg+Umschalt+O)
  und `act_neu_beschreiben` (*Diskette ▸ …*) entstehen wie alle anderen in
  `app/disktool/ui/actions.py` — damit sperrt sie `_aktionen_pruefen()` an der EINEN
  dafür zuständigen Stelle.  Der Ausweg aus einer Schadstelle (§7.2) ist **unsichtbar
  statt gesperrt**, solange keine physische Sitzung läuft (an einer Datei gibt es keine
  Schadstelle); fehlen die Hosttools, ist `act_physisch` **gesperrt mit dem Grund im
  Tooltip**
  (`_physisch_verfuegbarkeit()`, einmal beim Aufbau) statt zu verschwinden.  Weil eine
  physische Diskette **keinen Pfad** hat (`DiskTool.open_physical` → `path=""`), nennen
  `_bezeichnung()`/`_kurzname()` die Herkunft für Kopfzeile und Fenstertitel, und
  `DiskHeader.setze()` nimmt sie als zweites Argument.
- **Das Prüf-Lesen ist an echter Hardware gegengeprüft** (2026-08-17, Greaseweazle F1
  + UDOS1715-Diskette): `K1520_GW_HARDWARE=1 K1520_GW_WRITE=1 … -k schreibt_eine_datei`
  meldet **4 Spuren zurückgeschrieben, 4 geprüft, 0 misslungene Vergleiche, 0
  Schadstellen**; die Datei kam beim zweiten, frischen Öffnen byteweise gleich zurück.
  Gegenprobe am Medium: eine Vollmessung vorher/nachher zeigt **genau die vier
  gemeldeten Spuren** geändert (c4h0, c5h0 = Descriptor/Zeigersektor+Record, c22h0
  Verzeichnis, c23h0 Belegungsplan) und sonst nichts, 2560/2560 Sektoren fehlerfrei.
  Danach die vier Spuren aus der Sicherung zurückgeschrieben (`gw write … --tracks`) —
  die Diskette ist wieder **byteweise die vom Anfang**.  Vorgehen bei so einem Test:
  erst sichern (`gw read` über alle Spuren), Identität der eingelegten Diskette gegen
  die Sicherung prüfen, mit einer NACHWEISLICH FREIEN Spur anfangen (der
  Belegungsplan sagt welche), dann erst über das Dateisystem schreiben.
- **`k1520disktool --physical` gibt es** (2026-08-17, Entwurf §12.3): `ls`, `info`,
  `check`, `get`, `put`, `rm`, `save-as`, `rewrite` gegen die eingelegte Diskette.
  Sie haengt am **Python**-Einstieg (`app/disktool/main.py`, wie `--paths` VOR den
  Qt-Importen) und nicht am C++-Werkzeug — der Kern kennt Greaseweazle nicht, der
  Arbeitsfaden ist Python; `k1520disktool-cli` bleibt der Dateiaustausch mit
  Abbildern.  Damit Oberflaeche und Kommandozeile dieselbe Sitzung aufmachen, liegt
  der Qt-freie Teil jetzt in **`app/gw/session.py`** (`PhysicalSession`,
  `verfuegbarkeit`, `LAUFWERKE`, `RATEN`); `app/ui/physical_disk.py` reicht ihn
  weiter.  Vier Festlegungen: **ohne `--write` wird abgelehnt, BEVOR der Motor
  anlaeuft** (Waechter prueft `geraet.gelesen == []`), **stdout ist die Nutzlast**
  (Fortschritt und Befund auf stderr), **Fortschritt aus einem Nebenfaden**, weil
  sonst zwei Minuten Schweigen wie ein Haenger aussehen, und eine **Schadstelle
  endet in Exit 1** samt Ausweg im Text.  Waechter `py_physical_cli` (14 Faelle,
  Ersatzlaufwerk aus `gw_fake.py`); am echten Laufwerk durchgefahren (§15.2).
- **Offen:** das Merken der Sitzungsparameter (Laufwerk und Zellrate muessen bei
  jedem Einlegen neu gewaehlt werden).
