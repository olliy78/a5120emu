# Feinentwurf: Physische Diskette am Greaseweazle

**Modul:** `core/peripherals/floppy_drive/` (`track_sync.*`, Erweiterung von `disk_medium.*`
und `disk_image.*`), `core/api/k1520_sync_api.*`, `app/gw/`
**Verwandt:** `doc/design/09_floppy_drive.md` (internes Medium + Container),
`doc/design/07_k5122_afs.md` (Controller), `doc/design/13_k1520disktool.md` (DiskTool),
`doc/K1520_architecture.md` §8.8
**Fremdsoftware:** [Greaseweazle](https://github.com/keirf/greaseweazle) (Keir Fraser,
Unlicense/Public Domain), Hosttools **1.23**, Gerät **Greaseweazle F1**, Firmware 1.6

---

## 1. Aufgabe und Leitidee

Bisher kennt der Floppy-Stack genau eine Quelle für eine Diskette: eine **Datei**
(`.img`/`.hfe`/`.dmk`).  Wer mit einer echten Diskette arbeiten will, muss sie
vorher als Ganzes einlesen und hinterher als Ganzes zurückschreiben — zwei Läufe
über 160 Spuren (≈ 2 min je Richtung), auch wenn nur eine einzige Datei gebraucht wird.

Die Anbindung des Greaseweazle soll diesen Umweg beseitigen.  Die Leitidee ist dabei
**nicht** „ein neues Dateiformat“, sondern:

> **Das echte Laufwerk ist eine zweite Art von Bindung des internen Mediums —
> und das Medium wird spurweise nachgeladen, statt in einem Stück.**

```
                              ┌──────────────── bisher ────────────────┐
Datei (.img | .hfe | .dmk) ──load/save──►  DiskMedium  ──►  K5122 / DiskVolume
                                              ▲
echte Diskette ──gw──► Arbeitsfaden ──Spur──► │   ◄── NEU: spurweise, nach Bedarf
                                              └──────── Schreibrückführung ──►
```

Daraus folgt der ganze Rest:

| Anforderung | Umsetzung |
|-------------|-----------|
| Kein Zwischenschritt über Dateien | Die physische Diskette **ist** die Bindung des Mediums; es gibt keine Abbilddatei (wohl aber „Speichern unter…“, das eine erzeugt) |
| Nur die gebrauchten Spuren lesen | Je Spur ein **Zustand**; eine Spur ohne gültigen Inhalt löst beim Zugriff ein Lesen aus (§4, §5) |
| Sofort dorthin, wo der Gast hinwill | **Priorität 1** (Lesen auf Anforderung) verdrängt jede Hintergrundarbeit — Spur 3 → Spur 22 ohne die Spuren dazwischen (§5) |
| Änderungen landen zeitnah auf der Diskette | **Priorität 2**: geänderte Spuren werden zurückgeschrieben, sobald sie kurz zur Ruhe gekommen sind (§7) |
| Schadstellen der Diskette fallen auf | Jede geschriebene Spur wird **zurückgelesen und verglichen**; erst dann gilt sie als sauber (§7.1) |
| Das Abbild soll möglichst vollständig werden | **Priorität 3**: in Ruhephasen wird vorausgelesen (§5.3) |
| Emulator **und** DiskTool | Die Erweiterung sitzt im `DiskMedium` — also unterhalb von beidem; beide Bibliotheken tragen dieselbe Schnittstelle (§10) |
| Ohne Hardware baubar und testbar | Der Kern kennt den Greaseweazle **nicht**; er kennt nur Aufträge (§9, §13) |

---

## 2. Warum die Gerätehälfte in Python liegt

Greaseweazle besteht aus einer Firmware und **Hosttools in Python**.  Eine
C++-Anwenderbibliothek gibt es nicht: das USB-Protokoll wäre nachbaubar, aber die
eigentliche Arbeit steckt nicht im Protokoll, sondern in der Flusswechsel-Auswertung
(PLL, Index-Ausrichtung, Mehrfachumdrehungen, Wiederholungen) — genau dem Teil, der
über Jahre an echten Disketten gereift ist.  Ihn nachzubauen hieße, den einzigen
schwierigen Teil selbst zu schreiben und den einfachen zu übernehmen.

Beide Anwendungen sind ohnehin **Python um eine Bibliothek herum**.  Der Greaseweazle
wird deshalb dort angebunden, wo die Anwendung ohnehin lebt; der Kern bekommt einen
neutralen Auftragsweg und weiß nicht, wer ihn bedient.

**Installation:** Die Hosttools liegen **nicht auf PyPI**; installiert wird ein
Freigabestand aus dem Quellzweig:

```sh
venv/bin/python3 -m pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"
```

Der Zweigkopf (`@master`) meldet sich als *TEST/PRE-RELEASE* und ist bewusst nicht die
Grundlage.  Das Paket ist eine **freiwillige** Abhängigkeit: fehlt es, fehlt der
Menüpunkt, sonst ändert sich nichts (§11.3).

---

## 3. Wo die Erweiterung ansetzt

```
K5122 (Controller)            DiskVolume / SectorSpace (DiskTool)
      │  track(head)                │  medium().track(cyl,head)
      ▼                             ▼
FloppyDriveV2                       │
      │                             │
      ▼                             ▼
DiskImage  ── Bindung: Datei (§09 6) ODER physische Diskette (hier)
      │
      ▼
DiskMedium ── je Spur: TrackImage + ZUSTAND   ◄── DAS ist die Erweiterung
      │
      └── TrackSync (optional) ── Warteschlange ──► fremder Arbeitsfaden ──► gw ──► Laufwerk
```

**Der Haken sitzt in `DiskMedium::track()`, nicht weiter oben.**  Das ist die einzige
Stelle, durch die *jeder* Verbraucher geht: der Controller über `FloppyDriveV2`, das
DiskTool über `DiskVolume`, die Formaterkennung über `GeometryProbe`.  Läge der Haken
in `DiskImage` oder `FloppyDriveV2`, käme das DiskTool nie an einer Nachladung vorbei,
weil es das Medium direkt anspricht.

Umgekehrt gilt: **die medienweiten Reihenläufe dürfen nicht nachladen.**
`formatted()`, `rawCompatible()` und die Codecs laufen über *alle* Spuren; ginge jeder
dieser Läufe durch den Haken, zöge eine beiläufige Statusabfrage der Oberfläche die
ganze Diskette ein.  Sie arbeiten deshalb auf dem internen Feld
(`peek(cyl,head)`), nicht über `track()`.

| Zugriff | lädt nach? | Begründung |
|---------|-----------|------------|
| `track(cyl,head)` | **ja** | der Verbraucher will den Inhalt sehen |
| `mutableTrack(cyl,head)` | **ja** | Sektorschreiben ist Lesen-Ändern-Schreiben; ohne den alten Inhalt entstünde eine Spur aus dem Nichts |
| `setTrack(cyl,head,t)` | **nein** | die Spur wird vollständig ersetzt (Vollspur-FORMAT, Codec-Ladepfad) — der alte Inhalt ist gleichgültig |
| `peek(cyl,head)` | nein | Reihenläufe, Codecs, Zustandsanzeigen |
| `formatted()`, `rawCompatible()`, `dirty()` | nein | Aussagen über das, **was bekannt ist** (§4.2) |

Der Unterschied zwischen `mutableTrack` und `setTrack` ist der Grund, warum eine
Leerdiskette im Laufwerk vom Gast formatiert werden kann, **ohne** dass sie vorher
gelesen wird: FORMAT schreibt ganze Spuren.

---

## 4. Der Spurzustand

### 4.1 Die drei Zustände

```
                    ┌──────────────────────────────────────────┐
                    │                                          │
   mount            ▼            Lesen erledigt                │
  ─────────►  ┌───────────┐  ──────────────────►  ┌─────────┐  │ Schreiben
              │ UNBEKANNT │                       │ SAUBER  │  │ (mutableTrack
              └───────────┘  ◄──────────────────  └─────────┘  │  / setTrack)
                    │           (nie zurück)           │       │
                    │ setTrack (Vollspur-FORMAT)       │       │
                    ▼                                  ▼       │
              ┌──────────────────────────────────────────────┐ │
              │                  GEÄNDERT                    │◄┘
              └──────────────────────────────────────────────┘
                    │  Rückschreiben erledigt
                    ▼
                 SAUBER
```

| Zustand | Bedeutung | Inhalt gültig? |
|---------|-----------|----------------|
| **Unbekannt** (`Unknown`) | noch nie von der Diskette gelesen | **nein** — die Bytes sind bedeutungslos |
| **Sauber** (`Clean`) | gelesen und seither nicht geändert | ja, gleich der Diskette |
| **Geändert** (`Dirty`) | im Abbild geändert, noch nicht zurückgeschrieben | ja, **neuer** als die Diskette |

**Der Zustand gilt für JEDES Medium, nicht nur für das echte Laufwerk.**  Bei einer
dateigebundenen Diskette tritt „Unbekannt" schlicht nie auf — der Container-Codec füllt
beim Laden alle Spuren, alles ist von Anfang an `Sauber`.  Es gibt also **ein** Konzept
und nicht zwei; `dirty()`/`trackDirty()`, mit denen der Autosave seit jeher arbeitet,
sind genau dieselben Bits.  Wächter: `DiskMedium.ZustandGiltAuchOhneLaufwerk_*`,
`…EinzelneSpurSauberMelden_*`, `…GeleseneSpurIstSauber_GeschriebeneSchmutzig` — der
letzte hält den Unterschied fest, an dem alles hängt: **`loadTrack` (gelesen) macht
sauber, `setTrack` (geschrieben) macht schmutzig**; wer beim Laden `setTrack` benutzt,
schriebe die frisch gelesene Spur sofort wieder hinaus.

Der Zustand ist damit eine echte Verallgemeinerung des bisherigen Dirty-Bits — `Dirty` bleibt exakt das, was der Autosave
schon immer benutzt hat (§09 6.1), und dieselben Bits tragen jetzt zusätzlich die
Rückführung auf die echte Diskette.

### 4.2 „Unbekannt“ ist keine leere Spur

Eine **unformatierte** Spur (`TrackImage::empty()`) ist eine belegte Aussage über die
Diskette: dort stehen keine Adressmarken (§09 7).  Eine **unbekannte** Spur ist gar
keine Aussage.  Die beiden dürfen nie verwechselt werden, denn:

* `formatted()` und `rawCompatible()` urteilen über das Medium.  Solange Spuren
  unbekannt sind, ist das Urteil vorläufig — beide melden zusätzlich, **ob** das Medium
  vollständig ist (`complete()`).  Die Oberfläche zeigt „(noch nicht vollständig
  gelesen)“, statt eine halb gelesene Diskette für unformatiert zu erklären.
* `saveAs()`/`exportTo()` auf eine Datei **erzwingt Vollständigkeit**: eine Abbilddatei
  ist eine Aussage über die ganze Diskette, also werden fehlende Spuren vorher gelesen
  (mit Fortschritt, §11.2).  Das ist der einzige Ort, an dem der Kern von sich aus
  einen vollständigen Lauf anstößt.

---

## 5. Der Synchronisierer

`TrackSync` (`track_sync.{h,cpp}`) hält die Warteschlange und den Zustand.  Er ist
**passiv**: er hat keinen eigenen Faden, kennt kein USB und keine Zeitscheiben.  Gearbeitet
wird von einem **fremden Arbeitsfaden**, der sich seine Aufträge abholt (§9).

### 5.1 Auftragsarten und Prioritäten

| Prio | Auftrag | Ausgelöst durch | Wartet jemand? |
|------|---------|-----------------|----------------|
| **1** | `Read` — Spur lesen | Zugriff auf eine unbekannte Spur (`track`/`mutableTrack`) | **ja**, der Vordergrund steht (§6) |
| **2** | `Write` — Spur zurückschreiben | geänderte Spur, die zur Ruhe gekommen ist (§7) | nein |
| **2** | `Verify` — eben geschriebene Spur **zurücklesen** | jeder abgeschlossene `Write` (§7.1) | nein |
| **3** | `Readahead` — unbekannte Spur vorauslesen | niemand; entsteht, wenn sonst nichts zu tun ist (§5.3) | nein |

Ein ausstehendes `Verify` kommt **vor** neuen `Write`-Aufträgen: der Kopf steht ohnehin
schon dort, und die Spur gilt erst danach als erledigt.  Für den Arbeitsfaden ist
`Verify` dasselbe wie `Read` — er liest und liefert Bitzellen; dass verglichen statt
übernommen wird, entscheidet allein der Kern.

Die Warteschlange ist **kein FIFO**, sondern wird bei jeder Entnahme neu bewertet.  Ein
Prio-1-Auftrag, der eintrifft, während ein Prio-3-Auftrag ansteht, wird als nächstes
ausgeführt — nicht in Reihenfolge des Eintreffens.  Aufträge derselben Priorität gehen
in Eintreffreihenfolge, mit einer Ausnahme: Prio 3 wählt nach **Kopfweg** (§5.3).

> **Ein bereits laufender Auftrag wird nicht abgebrochen.**  Der Arbeitsfaden steckt
> dann in einer USB-Übertragung; ihn zu unterbrechen brächte einen halben Lesevorgang
> und Zustand im Gerät durcheinander.  Die Verdrängung wirkt also mit einer Latenz von
> höchstens einem Spurzugriff (≈ 0,5–0,8 s) — der Grund, warum das Vorauslesen
> spurweise arbeitet und nicht in Blöcken.

### 5.2 Ein Auftrag je Spur

Für dieselbe Spur gibt es nie zwei Aufträge.  Trifft eine Anforderung auf eine Spur, für
die schon ein Prio-3-Auftrag ansteht, wird **dessen Priorität angehoben** (und der
Wartende daran gehängt), statt einen zweiten Auftrag einzustellen.  Ohne das läse man
dieselbe Spur zweimal — einmal für den Wartenden, einmal für den Vorratsbau.

### 5.3 Vorauslesen (Prio 3)

Steht kein Auftrag an, erzeugt `TrackSync` bei der nächsten Abholung selbst einen: die
**unbekannte Spur mit dem kürzesten Kopfweg** zur zuletzt bearbeiteten Position,
Kopf 0 vor Kopf 1.  Damit füllt sich das Abbild von der zuletzt gebrauchten Stelle nach
außen — bei UDOS also rund um die Systemspuren und das Verzeichnis, wo als nächstes
gelesen wird, statt stur bei Spur 0 zu beginnen.

Das Vorauslesen ist **abschaltbar** (`setReadAhead(false)`) und ist es standardmäßig
in einem Fall: solange die Diskette **schreibbar** gemountet ist und noch geänderte
Spuren anstehen, ruht es nicht — es tritt nur hinter Prio 2 zurück.  Abgeschaltet wird es
von der Oberfläche, wenn der Bediener das Laufwerk still haben will.

### 5.4 Fehler

Ein gescheiterter Auftrag (Gerät weg, Spur unlesbar, Zeitüberschreitung) meldet einen
Text zurück.  Für den **Lesefall** ist eine unlesbare Spur **kein** Fehlerzustand des
Mediums: sie wird als unbekannt belassen und der Wartende bekommt ein leeres
`TrackImage` — für den Gast sieht das aus wie eine unformatierte Spur, also genau das,
was ein echtes Laufwerk an einer kaputten Spur liefert (Index-Timeout, §09 7).  Der
Fehlertext geht in `lastError()` und in die Statistik; die Spur wird nicht endlos
neu angefordert (`failed`-Markierung, erst ein neuer Zugriff versucht es wieder).

Für den **Schreibfall** bleibt die Spur `Geändert` und wird erneut eingestellt, bis es
klappt oder der Bediener das Laufwerk abmeldet.  Eine verlorene Änderung wäre der
schlimmere Ausgang: die Diskette im Laufwerk und das Abbild im Speicher lägen
auseinander, ohne dass es jemand merkt.

---

## 6. Lesen auf Anforderung — die Blockade

```cpp
const TrackImage& DiskMedium::track(uint8_t cyl, uint8_t head) const {
    if (sync_ && state(cyl,head) == TrackState::Unknown)
        sync_->ensureLoaded(cyl, head);          // stellt Prio 1 ein und WARTET
    return peek(cyl, head);
}
```

`ensureLoaded()` blockiert den aufrufenden Faden, bis der Arbeitsfaden die Spur
geliefert hat (oder die Frist abläuft, Vorgabe 30 s).  Das ist gewollt und harmlos,
solange man weiß, **welcher** Faden da wartet:

| Anwendung | wartender Faden | Folge |
|-----------|-----------------|-------|
| Emulator | der Maschinenfaden (`A5120Machine::run`) | Die emulierte Maschine steht ~0,5 s — genau wie eine echte, die auf ihr Laufwerk wartet. Die Oberfläche bleibt bedienbar. |
| DiskTool | der Faden, der die Bibliothek ruft | **Muss** ein Arbeitsfaden sein, nicht der Oberflächenfaden — sonst friert das Fenster ein (§11.2). |

Die Wartezeit ist **keine Emulationsungenauigkeit**: die Maschinenuhr läuft nicht mit,
der Gast erlebt nur einen etwas trägen Zugriff.  Der Index-Timeout des Gastes zählt in
Maschinentakten und kann daher nicht zuschlagen, während der Faden steht.

> **Rückrufe gibt es nicht.**  Der Kern ruft nie in die Anwendung hinein — weder eine
> Funktionszeiger-Schnittstelle noch eine Python-Rückrufmarke.  Der Arbeitsfaden holt
> sich Aufträge ab.  Das erspart die ganze Klasse von Verklemmungen, bei denen ein
> Rückruf, während der Kern eine Sperre hält, wieder in den Kern hineinruft — und es
> hält die GIL aus dem Kern heraus.

---

## 7. Schreibrückführung und die Schreibpause

Eine geänderte Spur wird **nicht sofort** eingestellt.  Der Gast schreibt sektorweise:
eine einzige UDOS-Dateioperation fasst dieselbe Spur dutzendfach an
(Daten, Verzeichnis, Belegungskarte).  Jede Änderung sofort auf die Diskette zu
schreiben hieße, dieselbe Spur dutzendfach zu schreiben — laut, langsam und für die
Diskette nicht gesund.

Es gilt dieselbe Regel wie beim Autosave in die Datei (§09 6.1), nur auf der Uhr der
Wirklichkeit statt der Maschinenuhr:

> Eine geänderte Spur wird eingestellt, wenn sie **`write_settle_ms` (Vorgabe 500 ms)
> lang nicht mehr angefasst** wurde.

Damit fasst ein Schreibburst zu einem Schreibvorgang zusammen, und „zeitnah“ bleibt
zeitnah: nach dem letzten Zugriff vergeht eine halbe Sekunde plus die Schreibdauer.

### 7.1 Geschrieben ist noch nicht angekommen — das Prüf-Lesen

Eine echte Diskette hat Schadstellen: Stellen, die keine Magnetisierung mehr halten.
Beim Formatieren fängt ein **Verify-Lauf** so etwas ab — nur läuft er bei uns ins Leere:

> Der Verify-Lauf des Gastsystems (`FORMAT`) liest die eben geschriebene Spur zurück
> und vergleicht — **aus dem Speicherabbild**.  Er vergleicht also das Abbild mit sich
> selbst und findet nie etwas.  Die Schadstelle liegt eine Schicht tiefer, auf der
> Scheibe, und die sieht er nicht.

Genau deshalb ist das zweistufige Schreiben (Abbild → Diskette) hier ein Vorteil: die
Prüfung lässt sich **entkoppeln** und dorthin legen, wo sie hingehört — an die
Rückführung.

```
   Gast schreibt          Rückführung (§7)            Prüf-Lesen (§7.1)
  ─────────────►  DIRTY  ──── Write ────►  DIRTY  ──── Verify ────►  CLEAN
                    ▲                        │                        ▲
                    │                        │  Vergleich stimmt nicht│
                    └────────────────────────┘                        │
                       höchstens EINE Wiederholung                    │
                                 │                                    │
                                 ▼ auch die misslingt                 │
                              DEFEKT (bleibt DIRTY, wird gemeldet) ───┘
                                     nach `rewriteAll()` auf neuer Diskette
```

**Der Ablauf im Einzelnen:**

1. Der `Write`-Auftrag ist fertig — die Spur bleibt **`Dirty`**.  Das ist die
   entscheidende Änderung: geschrieben zu haben ist keine Aussage darüber, dass es
   angekommen ist.
2. Ein `Verify`-Auftrag liest dieselbe Spur zurück.
3. Verglichen wird auf **Sektorebene**, nicht byteweise — zwei Aufnahmen derselben Spur
   sind nie bitgleich (Schreibnaht, Drehzahl-Jitter, Startwinkel).  Geprüft werden
   Sektorzahl und -folge, die Adressfelder, die Nutzdaten, der Anhang hinter der
   Daten-CRC (UDOS-Kontrollblock) und **beide Prüfsummen des Sektors**.  Nachlaufende
   Gap-Füllbytes werden auf beiden Seiten abgeschnitten: wie viele davon mitgelesen
   wurden, hängt an der Drehzahl und sagt nichts über die Gültigkeit.

   > **„Beide Prüfsummen" — pro SEKTOR, nicht pro Datenblock.**  Ein IBM-Sektor besteht
   > aus zwei Feldern, und jedes trägt seine eigene CRC:
   >
   > ```
   >   A1 A1 A1 FE  cyl head id sc  CRC CRC   …Gap…   A1 A1 A1 FB  <Daten>  CRC CRC
   >   └──────── ID-Feld ──────────┘                  └────────── Datenfeld ─────────┘
   > ```
   >
   > Über den Datenblock gibt es also genau **eine** CRC — die zweite gehört zum
   > Adressfeld.  Beide zählen, weil sie Verschiedenes schützen: eine kaputte
   > **Daten**-CRC gibt einen Lesefehler, eine kaputte **ID**-CRC macht den Sektor
   > **unauffindbar** („record not found"), auch wenn die Nutzdaten dahinter heil sind.
   > Deshalb wird zusätzlich der *Inhalt* des Adressfeldes verglichen (Zylinder, Kopf,
   > Sektornummer, Längencode): eine gültige CRC über eine falsche Adresse ist ebenso
   > unbrauchbar.  Im Code sind das `LogicalSector::id_crc_ok` und `data_crc_ok`.
4. **Stimmt es** → die Spur wird `Clean`.  Erst hier, nirgends vorher.
5. **Stimmt es nicht** → einmal neu schreiben und erneut prüfen
   (`write_verify_retries`, Vorgabe 1).
6. **Auch das misslingt** → die Spur gilt als **schadhaft**: sie bleibt im Abbild
   `Dirty`, es wird nichts mehr versucht, und der Bediener wird gemeldet (§7.2).

> **Das Zurückgelesene wird NIE ins Abbild übernommen.**  Täte man es, überschriebe ein
> misslungener Schreibvorgang genau die Daten, die er zerstört hat — und niemand merkte
> es je.  Der `Verify`-Zweig in `completeRead()` vergleicht und legt nichts ab; das ist
> der ganze Unterschied zu einem gewöhnlichen `Read`.  Wächter:
> `TrackSync.DasPruefLesenUeberschreibtDasAbbildNicht`.

Ein `Verify`, das **gar nicht lesen** kann (Gerätefehler, Zeitüberschreitung), zählt wie
ein falscher Inhalt: für die Diskette ist beides dasselbe.

**Der Preis** ist die doppelte Zeit je Schreibvorgang (≈ 1,5 s statt 0,8 s je Spur;
eine ganze Diskette zu beschreiben dauert damit ~4 min).  Abschaltbar ist es
(`verify_writes`), Vorgabe ist **an**: eine unbemerkt verlorene Datei ist teurer als
jede Wartezeit.

### 7.2 Wenn die Diskette nicht mehr trägt

Eine schadhafte Spur ist **kein Programmfehler und keine Panne des Abbilds** — das
Abbild im Speicher ist unversehrt, nur die Scheibe nimmt es nicht mehr an.  Daraus folgt
das Verhalten:

* Die Spur bleibt **`Dirty`**.  Sie ist ja wirklich nicht geschrieben, und auf einer
  anderen Diskette soll sie noch geschrieben werden können.
* `flushPending()` — und damit „Speichern" — meldet **Misserfolg**, mit der Spurnummer
  im Klartext.  Ein „gespeichert" wäre hier gelogen.
* Der Bediener wird **einmal je Spur** gewarnt (nicht bei jedem Zeitgeber-Tick) und
  bekommt beide Auswege genannt: das Abbild **in eine Datei** schreiben
  („Speichern unter…") oder **eine fehlerfreie Diskette einlegen** und
  **„Diskette neu beschreiben"** wählen.
* `rewriteAll()` stellt daraufhin **jede bekannte Spur** erneut zum Schreiben ein und
  löscht den Defektvermerk — auf der neuen Diskette gilt er nicht.  **Unbekannte Spuren
  bleiben unangetastet**: sie tragen bedeutungslose Bytes, sie zu schreiben ergäbe Müll.
  Ist das Abbild unvollständig, sagt die Oberfläche vorher, wie viele Spuren fehlen.

Damit findet das Verfahren nicht nur Schäden, die vor dem Formatieren schon da waren,
sondern auch solche, die **im Laufe der Zeit** entstehen — jeder Schreibvorgang ist
zugleich eine Prüfung der Stelle, auf die er geht.

Zwei weitere Festlegungen:

* **Beim Abmelden wird gewartet.**  `unmount()`/Schließen stellt alle geänderten Spuren
  sofort ein und wartet, bis sie geschrieben sind (mit Fortschritt und Abbruchmöglichkeit).
  Wer das Fenster schließt, während drei Spuren anstehen, muss es wissen.
* **Schreiben ist die Ausnahme, nicht die Vorgabe.**  Eine physische Diskette wird
  **schreibgeschützt** gemountet, solange der Bediener nicht ausdrücklich etwas anderes
  sagt.  Das Gegenstück zur Abbilddatei — dort kostet ein Fehler eine Kopie, hier die
  einzige noch existierende Diskette.

---

## 8. Das Austauschformat: HFE-Bitzellen

Zwischen Python und Kern gehen **Bitzellen einer Spurseite**, genau in der Form, die
auch in einer HFE-Datei steht: ein Byte je acht Zellen, **LSB zuerst**, dazu die Zahl
gültiger Zellen.

Das ist keine Bequemlichkeit, sondern der Punkt, an dem die Anbindung **keinen neuen
Codepfad** bekommt:

* Der Kern wandelt sie mit `BitCodec::decode()` in ein `TrackImage` — **derselbe**
  Decoder, mit dem HFE-Dateien gelesen werden, samt Neu-Einrasten an jeder Sync-Gruppe
  (unerlässlich bei echten Aufnahmen, `project_real_disk_hfe_readpath`) und samt
  Herunterrechnen überabgetasteter Aufnahmen (`downsampleCells`).
* Die Gegenrichtung ist `BitCodec::encode()`.
* Auf der Python-Seite ist es das, was Greaseweazle ohnehin erzeugt:
  `PLLTrack(clock=5e-4/bitrate, data=flux).get_revolution(0)` →
  `bits.tobytes()` + `bytereverse()` — dieselben drei Zeilen wie in
  `greaseweazle/image/hfe.py`.

**Gemessen** an der UDOS-Diskette im K5601 (5,25″, 250 kbit/s, 299 U/min):
100 363 Zellen = 12 546 Byte je Spurseite, 156 A1-Sync-Marken (26 Sektoren × 6 Felder).

### 8.1 Zellrate und Verfahren

Die **Zellrate** (`cell_rate_kbps`) wird beim Mounten festgelegt und kommt aus dem
`DriveProfile` (5,25″ DD: 250; 8″ MFM: 500; 8″ FM: 250).  Sie steht auf beiden Seiten:
der Arbeitsfaden taktet damit die PLL, der Kern codiert damit zurück.  Eine falsch
gewählte Rate erzeugt kein Kauderwelsch, sondern eine überabgetastete Spur — die
`downsampleCells` auffängt, solange es ein ganzzahliges Vielfaches ist.

> **Beim Schreiben zählt die gemessene Drehzahl, nicht die nominelle.**  Die Bitzellen
> kommen mit der *nominellen* Zellrate herein; das Laufwerk dreht aber mit seiner
> eigenen Drehzahl.  Die Flusszeiten müssen deshalb auf die **gemessene**
> Umdrehungsdauer gestreckt werden (`usb.read_track(2).ticks_per_rev`, einmal je
> Sitzung; danach je Spur `faktor = takte_je_umdrehung / fluss.ticks_to_index` mit
> mitgeschlepptem Rundungsrest — dasselbe Verfahren wie `gw write`).  Ohne diese
> Streckung ist der Datenstrom vor dem Indexloch zu Ende und der Adapter bricht mit
> **`Flux Underflow`** ab.  Das war der einzige Stolperstein des Schreibpfads.

Das **Verfahren** (FM/MFM) wird nicht gesetzt, sondern **erkannt**, spurweise: erst mit
dem Vorschlagsverfahren des Mediums decodieren, findet sich keine Marke, das andere
probieren — genau die Regel, die `HfeCodec` beim Laden schon anwendet und die hier
in eine gemeinsame Hilfsfunktion wandert.  Eine Diskette darf FM- und MFM-Spuren
mischen (§09 3.1), und bei einer echten weiß man es vorher schlicht nicht.

### 8.2 Was der Kern **nicht** bekommt

Flusswechsel.  Die Zeitwerte zwischen zwei Flanken bleiben in Python; im Kern kommt an,
was ein Datenseparator daraus gemacht hat.  Damit sind schwache Bits, Kopierschutz und
Flussschreibweisen außerhalb der Reichweite — für K1520-Disketten (Standard-IBM-FM/MFM,
§09 4.2) kein Verlust, und der Preis dafür ist, dass der gesamte vorhandene Lesepfad
unverändert trägt.

---

## 9. Der Vertrag mit dem Arbeitsfaden

```
Vordergrund (Maschine / DiskTool)        Hintergrund (fremder Faden, z. B. Python+gw)
──────────────────────────────────       ───────────────────────────────────────────
track(c,h)  ──► ensureLoaded  ──┐
                                │  ┌──►  takeJob(timeout)   blockiert bis Arbeit da
                             [Warteschlange]                       │
                                │  │                               ▼
                                │  │                     Spur lesen / schreiben (gw)
                wartet …        │  │                               │
                                │  └──  completeRead(id, cells) ◄──┘
                ◄───────────────┘       completeWrite(id) / failJob(id, text)
```

Fünf Regeln, die den Vertrag ausmachen:

1. **Genau ein Arbeitsfaden je Synchronisierer.**  Ein zweiter brächte zwei Köpfe auf
   einem Laufwerk durcheinander; `takeJob` weist ihn ab.
2. **Der Arbeitsfaden ruft nichts anderes im Kern.**  Nur `takeJob`, `fetchWrite`,
   `completeRead`, `completeWrite`, `failJob`.  Insbesondere fasst er das Medium nicht an.
   Ein `Verify`-Auftrag ist für ihn **dasselbe wie `Read`** — lesen und über
   `completeRead` abliefern; was damit geschieht, entscheidet der Kern (§7.1).
3. **Kein Auftrag wird ohne Abschluss liegengelassen.**  Wer `takeJob` bekommen hat,
   muss ihn abschließen oder scheitern lassen — sonst wartet der Vordergrund bis zur
   Frist.  Bricht der Faden weg, löst `shutdown()` alle Wartenden.
4. **Die Sperre wird nie über eine Übertragung gehalten.**  `takeJob` gibt die Sperre
   frei, bevor es zurückkehrt; der Vordergrund kann währenddessen weiterarbeiten und
   neue Aufträge einstellen.
5. **`shutdown()` ist endgültig.**  Danach liefert `takeJob` „Ende“, jeder Wartende
   bekommt sein leeres `TrackImage`, und neue Anforderungen warten nicht mehr.  Das ist
   der Weg, auf dem das Abmelden eines Laufwerks oder das Beenden der Anwendung
   garantiert nicht hängenbleibt.

---

## 10. C-ABI

Der Synchronisierer ist ein **eigenständiges Handle** — nicht an die Maschine gebunden,
denn das DiskTool hat keine.  Dieselben Funktionen stehen in **beiden** Bibliotheken
(`libk1520core` und `libk1520disk`); die Übersetzungseinheit ist dieselbe.

```c
typedef void* K1520Sync;

typedef struct {
    uint8_t  num_cyls, num_heads;   /* Reichweite des Laufwerks (K5601: 80 × 2)   */
    uint16_t cell_rate_kbps;        /* 250 / 500 — Vorgabe aus dem DriveProfile   */
    uint16_t rpm;                   /* 300 / 360 — nur für Ersatz-Spurlängen      */
    bool     writable;              /* false = die echte Diskette wird nie beschrieben */
    uint8_t  default_encoding;      /* 0 = FM, 1 = MFM (Vorschlag, s. §8.1)       */
    bool     verify_writes;         /* jede geschriebene Spur zurücklesen (§7.1)  */
    uint8_t  write_verify_retries;  /* Wiederholungen nach falschem Vergleich (1) */
} K1520SyncSpec;

K1520_API K1520Sync k1520s_create (const K1520SyncSpec* spec);
K1520_API void      k1520s_destroy(K1520Sync);
K1520_API void      k1520s_shutdown(K1520Sync);          /* weckt alle Wartenden  */

/* ── Arbeitsfaden ─────────────────────────────────────────────────────────── */
typedef struct {
    uint32_t id;                    /* 0 = kein Auftrag                          */
    uint8_t  kind;                  /* 1 = Read, 2 = Write, 3 = Stop, 4 = Verify */
    uint8_t  cyl, head, prio;
} K1520SyncJob;

K1520_API bool k1520s_take_job(K1520Sync, int timeout_ms, K1520SyncJob* out);
K1520_API int  k1520s_fetch_write(K1520Sync, uint32_t id,
                                  uint8_t* buf, int buf_len, uint32_t* bitcells);
K1520_API bool k1520s_complete_read(K1520Sync, uint32_t id, const uint8_t* cells,
                                    int len, uint32_t bitcells);
K1520_API bool k1520s_complete_write(K1520Sync, uint32_t id);
K1520_API void k1520s_fail_job(K1520Sync, uint32_t id, const char* msg);

/* ── Anzeige ──────────────────────────────────────────────────────────────── */
typedef struct {
    uint16_t tracks_total, tracks_known, tracks_dirty, tracks_failed;
    uint16_t tracks_defect;                    /* schadhaft — s. §7.2            */
    uint32_t reads_done, writes_done;
    uint32_t verifies_done, verify_failed;     /* Prüf-Lesen (§7.1)              */
    uint32_t errors;
    uint8_t  busy_kind, busy_cyl, busy_head;   /* was gerade läuft (255 = nichts) */
} K1520SyncStats;
K1520_API bool        k1520s_stats(K1520Sync, K1520SyncStats* out);
K1520_API const char* k1520s_last_error(K1520Sync);
/* Schadstellen und der Weg heraus (§7.2) */
K1520_API int         k1520s_defect_tracks(K1520Sync, char* buf, int len);
K1520_API int         k1520s_rewrite_all(K1520Sync);

/* ── Anbinden ─────────────────────────────────────────────────────────────── */
K1520_API bool      k1520_mount_physical(K1520Handle, int drive, K1520Sync, bool wp);
K1520_API K1520Disk k1520d_open_physical(K1520Sync, const char* fs_name, bool read_only);
```

`k1520s_take_job` ist die einzige blockierende Funktion der ABI.  Über `ctypes` gibt
Python währenddessen die GIL frei — der Arbeitsfaden hängt also nicht am Rest der
Anwendung.

---

## 11. Die Python-Seite

### 11.1 Aufbau

```
app/gw/
  device.py    Gerät finden/öffnen, Laufwerk wählen, Motor (dünne Hülle um greaseweazle)
  worker.py    der Arbeitsfaden: takeJob → lesen/schreiben → completeRead/Write
  errors.py    „kein Adapter“, „kein Paket“, „Diskette schreibgeschützt“ als Klartext
```

Der Arbeitsfaden ist ein gewöhnlicher `threading.Thread`:

```python
while True:
    job = sync.take_job(timeout_ms=1000)
    if job is None:            continue      # Zeitüberschreitung: nur weitermachen
    if job.kind == Kind.ENDE:  break         # shutdown()
    try:
        if job.kind == Kind.READ:
            cells, nbits = self.dev.read_track(job.cyl, job.head)
            sync.complete_read(job.id, cells, nbits)
        else:
            cells, nbits = sync.fetch_write(job.id)
            self.dev.write_track(job.cyl, job.head, cells, nbits)
            sync.complete_write(job.id)
    except Exception as e:
        sync.fail_job(job.id, str(e))
```

Das Laufwerk bleibt für die Dauer der Sitzung **gewählt**, der Motor läuft mit einer
Nachlaufzeit (Vorgabe 3 s ohne Auftrag → aus).  Ein Motoranlauf kostet rund eine halbe
Sekunde; ihn je Spur zu bezahlen machte das Vorauslesen sinnlos.

### 11.2 Wer ruft die Bibliothek?

* **Emulator:** unverändert — der Maschinenfaden blockiert im Kern, der gw-Faden
  arbeitet daneben.  Der Laufwerkskasten zeigt aus `k1520s_stats` den Füllstand
  („47 von 160 Spuren gelesen“) und was gerade läuft.
* **DiskTool:** hier ist die Umstellung nötig.  Bisher ruft die Oberfläche die
  Bibliothek direkt; mit einer physischen Diskette **muss** jeder Aufruf, der auf das
  Medium geht (Öffnen samt Formaterkennung, Holen, Schreiben, Diskeditor), in einen
  Arbeitsfaden mit Fortschrittsanzeige.  Das Öffnen ist dabei der teure Fall: die
  Formaterkennung sieht sich **jede** Spur an (§4.2), liest also die ganze Diskette.

### 11.3 Ohne Adapter, ohne Paket

`import greaseweazle` steht **nicht** auf oberster Ebene, sondern hinter einer Prüfung.
Fehlt das Paket oder das Gerät, bleibt der Menüpunkt „Physisches Laufwerk…“ gesperrt
und nennt im Hinweis den Grund.  Alles andere — Bauen, Tests, Betrieb mit Abbilddateien
— ist davon nicht berührt.

---

## 12. Bedienung

Beide Programme teilen sich `app/ui/physical_disk.py`: **derselbe Dialog**, dieselbe
Sitzung, dieselbe Statuszeile — „physisches Laufwerk einlegen" bedeutet in beiden
Programmen dasselbe.

| Baustein | Was |
|----------|-----|
| `verfuegbarkeit()` | `(True, "")` oder `(False, grund)`; der Grund ist für den Anwender geschrieben und taugt als Tooltip einer gesperrten Aktion |
| `PhysicalDiskDialog` | Laufwerk am Kabel (`a`/`b`/`0`…`3`), Zellrate (250/500), Vorauslesen, **Schreiben — ohne Haken** |
| `PhysicalSession` | hält Sync **und** Arbeitsfaden zusammen; `close()` schreibt Ausstehendes zurück, hält den Faden an und gibt erst dann den Synchronisierer frei (diese Reihenfolge ist Pflicht) |
| `mit_fortschritt()` | führt eine lange Arbeit im Arbeitsfaden aus und zeigt dabei den Füllstand (`QProgressDialog` mit Abbruch) |
| `neue_defekte()` / `defekt_meldung()` | Schadstellen **einmal je Spur** melden, mit beiden Auswegen im Text (§7.2) |
| `rewrite_all()` | „Diskette neu beschreiben" — alles Bekannte erneut einstellen |

### 12.1 Emulator

Im Laufwerkskasten steht neben *Mount* / *Neue Diskette* / *Speichern unter…* ein
vierter Knopf **„Physisch…"**.  Danach:

* Die Pfadzeile zeigt `[echtes Laufwerk A am Greaseweazle]`, der Mount-Knopf heißt
  **„Auswerfen"**, und „Physisch…" ist gesperrt (zweimal einlegen gibt es nicht).
* Darunter läuft die **Füllstandszeile** mit: `⏵ 63 von 160 Spuren gelesen · liest 5/1`
  — sie wird vom vorhandenen LED-Zeitgeber (120 ms) nachgeführt, kostet also keinen
  eigenen Zeitgeber.
* **Auswerfen** beendet die Sitzung (Wartecursor, weil das Zurückschreiben dauern kann)
  und hängt danach aus.
* Bei einer **Schadstelle** (§7.2) wird die Füllstandszeile rot, ein Meldungsfenster
  nennt die Spur und beide Auswege, und der Knopf **„Diskette neu beschreiben"**
  erscheint.  Er ist ohnehin nur da, wenn schreibend eingelegt wurde — ohne
  Schreibrecht gibt es nichts zurückzuschreiben.
* Eine physische Diskette steht **nicht** in `get_mounts()` und wird von `remount_all()`
  nicht angefasst: sie hat keinen Pfad, und ihr Sync-Handle ist nach dem Einlegen
  verbraucht.  Beim Fensterschließen räumt `close_physical_sessions()` auf.

### 12.2 k1520DiskTool

Im Menü ***Diskette*** stehen ganz oben die beiden Richtungen des echten Laufwerks
beieinander — *Physische Diskette laden…* (Strg+Umschalt+O) und *Physische Diskette
überschreiben…*; beide auch in der Symbolleiste.  Sie gehören zur **Diskette**, nicht
zu *Datei*: „Datei" meint das Abbild, hier geht es um den Datenträger im Laufwerk.  Das Öffnen läuft über `mit_fortschritt()`
— es misst eine Stichprobe (§11.2a) und braucht dafür rund zehn Sekunden; die Anzeige
zählt die Spuren mit und lässt sich abbrechen.
Danach ist die Diskette eine Diskette wie jede andere; der Kopf nennt sie
`Echtes Laufwerk A am Greaseweazle — udos_ds77 (nur lesen)`.

#### 11.2a Erkennen an acht Spuren statt an hundertsechzig

Bis 2026-08-16 lief `GeometryProbe::measure()` über **jede** Spur des Mediums, und weil
`DiskMedium::track()` am echten Laufwerk nachlädt, zog das Öffnen die ganze Diskette ein:
160 Spuren × 0,6 s ≈ **97 s**.  Für eine Datei ist das gratis, am Laufwerk ist es der
teuerste Vorgang des Programms — und unnötig.

Nachgerechnet über **alle 1770 Formatpaare** des Katalogs trennen **acht Spuren** alles,
was überhaupt trennbar ist:

| Sonde | trennt zusätzlich |
|-------|-------------------|
| **Zylinder 3, Kopf 0** | 1606 Paare |
| Zylinder 0, Kopf 1 | 99 |
| Zylinder 78, Kopf 0 | 35 |
| Zylinder 0, Kopf 0 | 15 |
| Zylinder 2 / 40 / 1 / 77 | je 1–4 |

Die wichtigste Sonde ist **Zylinder 3** — dieselbe, die das CP/A-BIOS liest (`dlgint`,
`doc/cpa_format_detection.md`).  Die innerste nötige ist 78, und sie beantwortet die
einzige Frage, die außen nicht zu klären ist: **40 oder 80 Spuren**.  Ein Formatpaar,
dessen Unterschied erst am inneren Rand *flächig* sichtbar würde, gibt es nicht.

Umgesetzt als `GeometryProbe::probeTracks()` + `measureTracks()`, ausgewählt in
`DiskVolume::open` an **`medium.loader() != nullptr`** — also an „die Spuren müssen teuer
beschafft werden", nicht an „Greaseweazle"; der Kern kennt den Adapter weiterhin nicht.
Genommen werden `{0,1,2,3, n/2, n-3, n-2}` auf **Kopf 0** plus `0/1` — zusammen **acht
Spuren**, ≈ 5 s.

> **Acht Spuren, nicht acht Zylinder.**  Der erste Wurf machte aus den acht berechneten
> *(Zylinder, Kopf)-Paaren* acht Zylinder × beide Köpfe und verdoppelte die Wartezeit auf
> nichts: Kopf 1 beantwortet genau eine Frage (ein- oder zweiseitig), und dafür genügt
> eine einzige Sonde.  Wächter `test_die_sondenzahl_bleibt_klein`.

Die ungeraden Zylinder (1, 3, n-3) sind Pflicht, nicht Beiwerk — ohne sie fände die
Doppelschritt-Regel ihre Lücken nicht und jedes `step: 2`-Format fiele durch.

Drei Dinge, die daran hängen:

1. **Die Vollmessung bleibt der Rückfall.**  Passt kein Katalogformat, wird die ganze
   Diskette gemessen, denn `synthesize()` leitet die Geometrie aus dem lückenlosen Bild
   ab — aus einer Stichprobe entstünde eine erfundene Geometrie.
2. **Die Auffälligkeiten sind dann Aussagen über die Stichprobe — und werden ersetzt,
   sobald die Diskette vollständig ist.**  Ein leeres `remarks` läse sich sonst als
   „Diskette makellos", obwohl 152 Spuren ungelesen sind; und eine Zählung liest sich
   wie ein Befund, obwohl sie nur die angesehenen Spuren zählt.  Auf der
   UDOS-Referenzdiskette meldet die Stichprobe **2** beschriebene Spuren hinter dem
   Format, die vollständige Messung **6** plus eine Spur mit fehlenden Sektoren.
   Deshalb:

   | Stand | Meldung |
   |-------|---------|
   | Stichprobe, ohne Befund | `erst 8 Spuren angesehen (Stichprobe der Formaterkennung), die übrigen sind ungeprüft` |
   | Stichprobe, mit Befund | `in einer Stichprobe von 8 Spuren: 2 … — die übrigen Spuren sind noch ungeprüft` |
   | vollständig | der wirkliche Befund, oder **gar keine Meldung** |

   `DetectionResult::examined_tracks` sagt, worüber der Satz urteilt (0 = ganze
   Diskette); `DiskVolume::refreshDetection()` misst einmal voll nach, sobald
   `DiskMedium::complete()` gilt, und der 500-ms-Zeitgeber der Oberfläche ruft es
   (`_befund_auffrischen`).  Zwei Feinheiten: gemessen wird gegen **das gemountete**
   Format (`match`, nicht `matchAll` — welches es ist, steht ja fest), und der Teil
   des Befunds, der **nicht** aus der Messung stammt (`befund_zusatz_`, z. B. „nach
   der CP/A-Regel abgeleitet …"), bleibt stehen.  Dasselbe Motiv wie `Unknown` ≠
   „unformatiert" (§4.2).  Wächter: `test_befund_gilt_erst_der_stichprobe_und_wird_dann_ersetzt`,
   `test_ohne_befund_verschwindet_der_streifen_wieder`.
3. **Bei Dateien ändert sich nichts.**  Dort liegt ohnehin alles im Speicher, und die
   vollständigen Zustandsaussagen sind mehr wert als die eingesparte Zeit.

> **Die Stichprobe darf nicht naiv sein — das kostete zwei Anläufe.**  Der erste Wurf
> nahm eine **feste** Spurauswahl, ausgerechnet über die Spur-Signaturen des Katalogs.
> Das ist das falsche Modell: `match()` entscheidet nicht über Signaturen, sondern
> **duldet** Altbestand und ordnet die Kandidaten nach **absoluten Zählungen**.  Eine
> Stichprobe schrumpft diese Zahlen ungleichmäßig — ein Format, das weniger Spuren
> abdeckt, verliert dadurch weniger.  Drei Disketten wurden falsch erkannt:
>
> | Diskette | Vollmessung | feste Stichprobe |
> |---|---|---|
> | `udos_boot_k5600_20` | `udos_ss77` | `udos_ss40` — **die halbe Diskette unsichtbar** |
> | `scpx17_cpa780_k5601` | `scpx640` | einseitiges Format auf beidseitiger Diskette |
> | `scpx17_5x1024_hardy` | `scpx798` | `cpa_auto` statt des Katalogprofils |
>
> Drei Ursachen, drei Gegenmittel:
> 1. **Seitenzahl war nur ein Zählnachteil** (`empty_tracks`, letzter Rang der
>    Sortierung).  Jetzt harte Regel: trägt eine Seite *beschriebene* Spuren, die das
>    Format nicht kennt, passt es nicht — unabhängig davon, wie viele Spuren man ansah.
> 2. **Verhältnisregeln urteilten über die Stichprobe** („mehr als ein Viertel
>    abweichender Spuren = anderes Format"): 2 von 7 Sondenspuren sind 28 %, dieselben
>    2 von 80 sind 2,5 %.  `match(..., stichprobe)` überspringt sie deshalb; bei der
>    Vollmessung gelten sie unverändert.
> 3. **Die Ausdehnung war geraten.**  Eine feste Sonde bei Zylinder 77 trifft auf
>    unformatierten Altbestand → die Diskette gilt als 41 Zylinder lang → das richtige
>    Format fällt mit „deklariert 77, beschrieben 41" durch.  Deshalb sucht
>    `measureSample()` das Ende **binär** (§ oben) statt es anzunehmen.

Wächter: `test_stichprobe_erkennt_dasselbe_wie_die_vollmessung` (`py_gw_physical`,
fünf Fixture-Disketten) — dieselbe Diskette als Datei und „physisch" muss dasselbe
Dateisystem ergeben, bei weniger als einer Spur je Zylinder.  Dazu
`test_erkennung_holt_nur_eine_stichprobe` (Menge) und
`test_die_sondenzahl_bleibt_klein` (Sondenzahl).

#### 11.2a-bis Eine leere Spur darf den Zellraten-Faktor nicht setzen

Beim Nachmessen der Stichprobe fiel ein **älterer Fehler im Lesepfad** auf, den die
geänderte Lesereihenfolge erst sichtbar machte — er hat mit der Stichprobe nichts zu
tun und träfe auch echte Hardware.

`TrackSync::completeRead` ermittelt einmal je Sitzung, ob der Adapter überabtastet
liefert (§8.1), und behält den Faktor für die ganze Diskette.  Gesucht wurde er aber
bei **jeder** Spur neu — und eine unformatierte Spur ist Rauschen: bei irgendeinem
falschen Faktor findet sich darin zufällig eine Marke, die ihn festschreibt.  Danach
war jede weitere Spur unlesbar (`scpx17_cpa780_k5601.hfe`, eine **einwandfreie**
Diskette mit 0 CRC-Fehlern: 6181-B-Spuren kamen als 2658 B mit einem Sektor).

Sequentiell fiel das nie auf, weil unformatierte Spuren am **Ende** liegen — das
Format war da längst erkannt.  Wer die Diskette in anderer Reihenfolge liest, verlor
die halbe Diskette.  Zwei Festlegungen:

1. **Steht der Faktor fest, gilt er.**  Findet sich damit keine Marke, ist die Spur
   unformatiert — das ist die richtige Auskunft, kein Anlass zu einer neuen Suche.
2. **Eine einzelne Marke stiftet keinen Faktor** (mindestens vier).  Genau so entsteht
   er sonst aus Rauschen; eine formatierte Spur trägt Dutzende.

Wächter: `test_eine_leere_spur_verdirbt_nicht_die_ganze_diskette` — liest erst die
leeren Spuren, dann echte, und prüft deren Länge.

#### 11.2b Was danach noch dauert — und warum

Erkennung ist nicht Anzeige.  Nach den acht Sonden holt das Werkzeug das **Verzeichnis**
(bei UDOS drei Spuren: 23/0, 22/0, 23/1) und kann es damit lesen — aber `list()` liest je
Verzeichniseintrag zusätzlich den **Kopfsektor der Datei** (`udos_fs.cpp`, `readHeader`),
denn bei UDOS stehen Länge, Typ und Attribute dort und nicht im Verzeichnis.  Die
Kopfsektoren liegen über die ganze Diskette verstreut; bei 69 Dateien sind das rund
24 weitere Spuren.

Gemessen an einer UDOS-Diskette (Ersatzlaufwerk, 50 ms je Spur statt echter 600):

| | Erkennung | bis der Inhalt steht |
|---|---|---|
| ohne Vorauslesen | 0,56 s / 11 Spuren | 1,80 s / 35 Spuren |
| mit Vorauslesen | 0,87 s / 17 Spuren | 2,71 s / 53 Spuren |

Das Vorauslesen kostet also rund die Hälfte obendrauf, obwohl es die **niedrigste**
Priorität hat: Priorität 1 verdrängt zwar die Warteschlange, unterbricht aber **keinen
laufenden Zugriff** (§5) — im Mittel wartet jede Vordergrundanforderung eine halbe
Spurzeit.  Das ist der Preis dafür, dass die Diskette nebenher vollständig wird.

**Das Verzeichnis gehört deshalb in den Arbeitsfaden, nicht in den Oberflächenfaden.**
`open_physical()` führt seit 2026-08-16 *Öffnen und `list()` gemeinsam* im Faden aus:

```python
def oeffnen_und_verzeichnis():
    werkzeug = DiskTool.open_physical(...)
    werkzeug.list()          # zieht die Kopfsektoren JETZT, im Arbeitsfaden
    return werkzeug
```

Vorher lief `list()` erst danach über `_reload()` im Oberflächenfaden — und solange
verarbeitete Qt **keine Ereignisse**: die Dateiliste blieb leer, der Fortschritt stand,
und ein Klick (etwa auf den Diskeditor) wurde erst eine halbe Diskette später
abgearbeitet.  Das Programm sah eingefroren aus, obwohl es arbeitete.  Danach ist
`_reload()` billig, weil jede nötige Spur schon bekannt ist.  Wächter:
`test_oberflaeche_laedt_nach_dem_oeffnen_keine_spur_mehr_nach`.

Die Anzeige unterscheidet die beiden Phasen, weil nur die erste ein bekanntes Ziel hat:

| Phase | Anzeige |
|-------|---------|
| Sondenspuren | `n von 8 Spuren für die Formaterkennung` (echter Balken) |
| Verzeichnis | `Verzeichnis wird gelesen… n Spuren geholt` (unbestimmter Balken) |

Die frühere Umschaltung auf die Spurzahl der Diskette (`10 von 160`) war irreführend:
sie behauptete eine Vollmessung, die gar nicht lief — es waren die Verzeichnisspuren.

**Umgesetzt (2026-08-16): die Dateiliste kommt zweistufig.**  `CAT` ist am echten Rechner
so schnell wie `DIR` unter CP/M, `CAT F=L` nicht — und der Unterschied ist genau dieser:
`CAT` liest nur das Verzeichnis, `CAT F=L` zusätzlich jeden Kopfsektor.  Unser `list()`
machte immer das Zweite, auch wenn niemand nach Größe und Datum fragte.

| | liest | UDOS-Referenzdiskette (69 Dateien) |
|---|---|---|
| `listNames()` | Verzeichnis | **1 Spur** |
| `list()` | + jeden Kopfsektor | 24 Spuren |

Bei **CP/M** sind beide identisch: dort steht alles im Verzeichniseintrag selbst, und das
Verzeichnis liegt ohnehin in den Sondenspuren — gemessen **0 zusätzliche Spuren** für
24 Dateien.  Das ist der sachliche Grund, warum `DIR` schnell ist und UDOS hier teurer.

Der Weg durch die Schichten:

```
UdosFileSystem::listNames()   Name + SECRET-Bit (beides steht im Verzeichnis, §5)
             ::detailsReady() liegt der Kopfsektor auf einer schon bekannten Spur?
             ::loadDetails()  Kopfsektor lesen und die Angaben eintragen
   → DiskVolume → k1520d_list_names / _entry_details_ready / _entry_load_details
   → DiskTool.list_names() → MainWindow._details_nachtragen() (am 500-ms-Zeitgeber)
```

Drei Festlegungen:

1. **Nachgetragen wird nur, was ohne Warten zu haben ist** (`detailsReady`).  Der
   Zeitgeber läuft im Oberflächenfaden; ein blockierender Zugriff hielte dort das Fenster
   an — und er triebe das Laufwerk an, statt ihm zu folgen.  Dieselbe Regel wie beim
   Diskeditor (§11.2c).
2. **Die leere Zelle zeigt „…", nicht „0".**  Eine Null wäre eine Behauptung über die
   Datei; der Strich sagt, dass die Angabe noch fehlt.  Gleiches Motiv wie schwarz ≠ grau.
3. **`list()` bleibt, was es war** — vollständig.  CLI, Archiv und die Prüfberichte
   brauchen alle Angaben auf einmal; nur die *Anzeige* darf sich Zeit lassen.

Wächter: `UdosVerzeichnisZweistufig.*` (Namen und Reihenfolge gleich, Angaben erst leer,
nachgetragen bitgleich mit `list()`) und `test_dateiliste_kommt_zweistufig` (`py_gw_gui`).

Gemessen am langsamen Ersatzlaufwerk (0,3 s/Spur, Vorauslesen an): bis die Dateien
**sichtbar** sind **5,4 s statt 16,3 s**.

#### 11.2c Der Diskeditor zeigt, was er weiß — schwarz ist „noch keine Aussage"

Der Editor las beim Öffnen **jede** Spur (`DiskSurface.load` → `tool.track()`), und weil
das an einem echten Laufwerk nachlädt, stand das Programm bis die Diskette vollständig
eingelesen war.  Er fragt jetzt zuerst den **Zustand** (`k1520d_track_state` →
`DiskMedium::state`, holt nichts) und zeichnet:

| Farbe | Bedeutung |
|-------|-----------|
| grün / rot | Sektor, CRCs gültig / fehlerhaft |
| orange | Gap |
| grau | **unformatiert** — eine Feststellung über die Diskette |
| **schwarz** | **noch nicht gelesen** — gar keine Feststellung |

Grau und Schwarz auseinanderzuhalten ist der Kern: „unformatiert" ist ein Befund,
„unbekannt" ist dessen Abwesenheit (dieselbe Unterscheidung wie `Unknown` ≠ leer, §4.2).
Eine schwarze Spur trägt deshalb auch keine Abschnitte — wir wissen ja nicht einmal, ob
sie formatiert ist.

Drei Regeln, die dabei gelten:

1. **Der Editor treibt das Laufwerk nicht an.**  Der Nachlauf-Zeitgeber (1 s) übernimmt
   nur, was **inzwischen ohnehin** gelesen wurde; er fordert nichts an.  Sonst zöge das
   blosse Offenhalten des Fensters die ganze Diskette ein — genau das, was abgeschafft
   werden sollte.  Sind alle Spuren bekannt, hält er sich selbst an.
2. **Ein Klick auf eine schwarze Spur holt sie** (`track_requested` → `_spur_anfordern`,
   mit Wartecursor).  Das ist der einzige Weg, auf dem der Editor ein Lesen auslöst, und
   er geht immer vom Bedienenden aus.
3. **Die Legende zeigt „noch nicht gelesen" nur, wenn es solche Spuren gibt** — bei einer
   Datei gäbe es sie nie, und ein Eintrag für einen unmöglichen Zustand verwirrt.

Wächter: `test_diskeditor_oeffnet_sofort_und_waechst_mit` (`py_gw_gui`).

`_close_tool()` schließt **erst das Werkzeug, dann die Sitzung** — `~DiskImage`
schreibt Ausstehendes noch über den Arbeitsfaden zurück und löst sich erst danach vom
Medium.  Ein danach geöffnetes Abbild lässt das echte Laufwerk also frei.

**„Speichern" bedeutet hier mehr als sonst:** es wartet, bis jede geänderte Spur
geschrieben **und zurückgelesen** ist (§7.1) — mit Fortschrittsanzeige, weil das
dauert.  Scheitert es an einer Schadstelle, nennt die Meldung die Spur — im
Meldungsfenster **und** im Streifen, der den Ausweg gleich als Knopf mitbringt —, und
*Diskette ▸ **Diskette neu beschreiben*** wird sichtbar.  Es schreibt nach einer
Rückfrage das ganze bekannte Abbild erneut hinaus (wieder mit Fortschritt) und meldet
am Ende, ob es diesmal fehlerfrei zurückkam.

**Seit dem Zusammenführen mit `create_disktool` (2026-08-16) sind es Aktionen, keine
Knöpfe.**  `act_physisch` und `act_neu_beschreiben` stehen in `_SPEC`
(`app/disktool/ui/actions.py`) wie alle anderen; damit sperrt und gibt sie
`_aktionen_pruefen()` an der einen dafür zuständigen Stelle frei (§20.3 des
DiskTool-Entwurfs).  Drei Feinheiten:

* Der **Ausweg ist unsichtbar statt gesperrt**, solange keine physische Sitzung
  läuft: an einer Datei gibt es keine Schadstelle, gegen die ein erneutes
  Wegschreiben helfen würde — ein dauerhaft grauer Menüpunkt behauptete das Gegenteil.
* **Fehlen die Hosttools, verschwindet der Menüpunkt nicht, er ist gesperrt** und trägt
  den Grund im Tooltip (`_physisch_verfuegbarkeit()`, einmal beim Aufbau geprüft —
  ob das Paket da ist, ändert sich im laufenden Programm nicht).  So sieht man, dass es
  die Möglichkeit gibt, und woran es liegt.
* Eine physische Diskette hat **keinen Pfad** (`DiskTool.open_physical` liefert
  `path=""`).  Kopfzeile und Fenstertitel bekommen ihre Beschriftung deshalb aus
  `_bezeichnung()` / `_kurzname()`, und `DiskHeader.setze(tool, name)` nimmt sie als
  zweites Argument entgegen.  Ohne das bliebe die Pfadzeile leer.

#### 12.2a Wenn die Erkennung nichts findet: von Hand wählen

Ein Sync-Handle ist nach dem Öffnen **verbraucht** — das Dateisystem lässt sich an
einer laufenden Sitzung nicht umstellen.  Das Übersteuern rief deshalb
`open_image(tool.path, name)`, und eine physische Diskette hat **keinen Pfad**: der
Aufruf ging auf `""`, warf alles weg und liess die Anzeige leer.  War die Erkennung
schon beim Öffnen gescheitert, gab es gar kein `tool` — dann tat die Wahl überhaupt
nichts, obwohl genau dann übersteuert werden soll.

`_physisch_erneut()` baut jetzt mit den **gemerkten Angaben** (`_physisch_wahl`) eine
neue Sitzung auf und öffnet mit dem gewählten Dateisystem; den Teil danach teilen sich
beide Wege (`_physisch_weiter`).  Das kostet den Erkennungslauf noch einmal (~10 s),
ist aber der einzige ehrliche Weg.  Dazu: die Meldung nennt den Ausweg
(`Dateisystem wählen…`), und das Dateisystem-Menü bleibt bedienbar, solange eine
physische Wahl gemerkt ist — gesperrt verstellte es genau den einzigen Weg hinein.

**Der Anlass ist real.**  Eine cpa800-Diskette, über die UDOS geschrieben wurde, trägt
auf Kopf 0 26×128 (UDOS) und auf Kopf 1 weiter 5×1024 (Altbestand).  Diese Mischung
steht in keinem Katalog, und die Regel aus §11.2a lehnt `udos_ss40` hier ausdrücklich
ab: ein einseitiges Format beschreibt keine Diskette, deren andere Seite beschrieben
ist.  Die Regel ist richtig — sie hält die Stichprobe zusammen —, aber sie macht diese
Disketten zu Handarbeit.  Von Hand gewählt liest sich die Diskette einwandfrei
(am echten Laufwerk nachgewiesen: 44 Dateien, Diskeditor bedienbar).

Wächter: `test_dateisystem_laesst_sich_auch_physisch_uebersteuern`,
`test_nicht_erkannt_bietet_den_ausweg_an`.

### 12.4 Eine Datei auf eine echte Diskette schreiben

Der Gegenweg zum Laden: Quelle ist das, was gerade offen ist — auch eine `.hfe` —,
Ziel ein echtes Laufwerk.  `DiskVolume::copyTo()` legt jede **bekannte** Spur per
`setTrack` in das Medium hinter dem Synchronisierer; damit gilt sie dort als
**geändert**, und der gewöhnliche Rückschreibweg (§7) erledigt den Rest, samt
Prüf-Lesen.  Es gibt also keinen zweiten Schreibpfad — nur eine zweite Quelle.

Drei Festlegungen:

1. **Erst fragen, dann alles andere.**  Die Rückfrage steht VOR dem Laufwerksdialog:
   hier geht kein Abbild verloren, sondern eine Diskette, unwiederbringlich.  Wer
   abbricht, soll nicht erst Laufwerk und Geometrie ausgefüllt haben.
2. **Passt es nicht, wird gar nichts geschrieben.**  Hat die Quelle mehr Zylinder oder
   Seiten als das eingestellte Laufwerk, bricht `copyTo` ab, bevor die erste Spur
   hinausgeht — eine halb überschriebene Diskette wäre das schlechteste Ergebnis: die
   alte ist fort, die neue unvollständig.
3. **Unbekannte Spuren der Quelle bleiben liegen.**  Ist die Quelle selbst ein
   Laufwerk, tragen sie bedeutungslose Bytes; sie zu kopieren schriebe Müll.

**Geschrieben wird im Hintergrund — ohne Meldungsfenster.**  `write_to_physical` stellt
die Spuren nur als geändert ein; hinaus schreibt sie der Arbeitsfaden (§7), und darauf
zu warten gäbe es keinen Grund.  Ein modaler Fortschrittsdialog hielte die Oberfläche
für Minuten an, ohne etwas zu gewinnen.  Stattdessen:

* die **Statuszeile** zählt mit (`Diskette wird beschrieben: 37 von 160 Spuren`) —
  gespeist vom selben 500-ms-Zeitgeber wie die geöffnete physische Diskette, und
  **rot hinterlegt**: das ist die Aussage „Diskette jetzt nicht entnehmen", wie die
  Betriebsleuchte am Laufwerk.  Es ist derselbe Rotton wie im Meldungsstreifen
  (`#c0504d`) — zwei verschiedene Rots wären schlechter als eine Doppelrolle, und
  der Füllstand beim **Lesen** bleibt unauffällig, dort ist nichts in Gefahr;
* der **Streifen** meldet Beginn („darf bis zum Ende nicht entnommen werden") und Ende;
* **Laden und Überschreiben sind gesperrt**, solange es läuft — das Laufwerk ist belegt;
* **das Fenster lässt sich nicht arglos schliessen**: läuft noch etwas, fragt
  `closeEvent` nach.  Eine halb beschriebene Diskette ist unbrauchbar, und man sieht
  dem Fenster nicht an, dass hinten noch etwas läuft.

Die Sitzung gehört **nicht** zum offenen Werkzeug — sie ist nur das Ziel dieses einen
Vorgangs und wird beendet, sobald keine Spur mehr aussteht (`_schreib_fertig`).
C-ABI: `k1520d_write_to_physical`.

> **Der Fortschritt muss zählen, was die Arbeit tut.**  `mit_fortschritt` las immer
> `tracks_known` — die *gelesenen* Spuren.  Beim Schreiben rührt sich das nicht (es
> geht aus dem Speicher hinaus), also stand der Balken still; und die Beschriftung war
> fest auf „Spuren für die Formaterkennung" verdrahtet, was über einem Schreibvorgang
> schlicht falsch ist.  Der Aufrufer gibt jetzt beides an: `zaehler` (woran der
> Fortschritt abzulesen ist — beim Schreiben `verifies_done`, denn erst das
> Prüf-Lesen macht eine Spur fertig) und `was` (das Substantiv der Beschriftung).
> Betroffen war auch *Diskette neu beschreiben*, dessen Balken aus demselben Grund
> stillstand.
Wächter: `test_eine_datei_laesst_sich_auf_eine_echte_diskette_schreiben` (schreibt eine
UDOS-Diskette auf ein Laufwerk, das eine CP/A-Diskette trägt, und liest sie zurück),
`test_ueberschreiben_verweigert_eine_zu_kleine_diskette`,
`test_ueberschreiben_fragt_vorher_und_bricht_bei_nein_ab`,
`test_beide_richtungen_stehen_im_diskettenmenue`.

### 12.3 Kommandozeile

Steht aus (`k1520disktool --physical …`), siehe §15.

## 13. Festlegungen, die man nicht aufweichen darf

1. **Der Kern kennt Greaseweazle nicht.**  Kein `#include`, keine Seriellschnittstelle,
   kein USB — nur Aufträge und Bitzellen.  Ein anderer Adapter (KryoFlux, FluxEngine,
   ein echtes Diskettenlaufwerk am PC) ist damit ein anderer Arbeitsfaden und **keine**
   Kernänderung.
2. **`track()` lädt nach, `peek()` nie.**  Wer einen medienweiten Reihenlauf schreibt
   und dabei `track()` benutzt, zieht ungewollt die ganze Diskette ein.  Der Wächter
   `TrackSync.ReihenlaeufeLesenNichtNach` hält das fest.
3. **„Unbekannt“ ≠ „unformatiert“.**  Sonst erklärt eine halb gelesene Diskette sich
   selbst für leer — und `rawCompatible()` gäbe grünes Licht für ein `.img`, das die
   ungelesenen Spuren als Füllbytes festschriebe.
4. **Prio 1 verdrängt, aber unterbricht nicht.**  Ein laufender Zugriff wird zu Ende
   geführt (§5.1).
5. **Geänderte Spuren gehen nie verloren.**  Ein gescheitertes Rückschreiben lässt die
   Spur geändert; das Abmelden wartet auf die Rückführung (§7).
6. **Physisch heißt schreibgeschützt, bis jemand widerspricht.**
7. **Ein Auftrag je Spur** (§5.2) — sonst liest das Vorauslesen gegen den Vordergrund an.
8. **Geschrieben gilt erst nach dem Zurücklesen** (§7.1).  Wer `Dirty` schon beim
   Abschluss des `Write` löscht, macht die ganze Prüfung wirkungslos.
9. **Das Prüf-Lesen wird verglichen, nicht übernommen** — sonst überschreibt ein
   misslungener Schreibvorgang genau die Daten, die er zerstört hat.
10. **Eine schadhafte Spur bleibt `Dirty`** und wird gemeldet, statt still zu
    verschwinden; nur so lässt sie sich auf einer heilen Diskette noch retten (§7.2).

---

## 14. Testbarkeit — und warum sie keine Hardware braucht

Das Laufwerk hängt nicht immer am Rechner, und in der CI/CD-Kette hängt es **nie**.
Der Entwurf ist deshalb so geschnitten, dass die Hardware nur in der äußersten Schale
vorkommt: alles unterhalb von „Aufträge und Bitzellen“ ist ohne Adapter prüfbar.

| Ebene | Test | Ersatz für die Hardware |
|-------|------|-------------------------|
| `TrackSync` (Warteschlange, Prioritäten, Zustände) | `TrackSync.*` — 20 Fälle in `tests/unit/peripherals/test_track_sync.cpp` | **Ersatz-Arbeitsfaden** in C++: bedient Aufträge aus einem `DiskMedium` im Speicher, mit anhaltbarer Auslieferung |
| Verdrängung | `TrackSync.LeseanforderungVerdraengtDasVorauslesen` | derselbe, Aufträge von Hand abgeholt (keine Zufallsreihenfolge) |
| Blockade | `TrackSync.ZugriffBlockiertBisDieSpurDaIst`, `…ZeitueberschreitungLiefertDieLeereSpur` | angehaltener Ersatzfaden bzw. gar keiner |
| Rückführung | `TrackSync.SchreibpauseFasstEinenBurstZusammen`, `…AbmeldenWartetAufDieRueckfuehrung`, `…GescheitertesSchreibenLaesstDieAenderungStehen`, `…SchreibgeschuetzteDisketteWirdNieBeschrieben` | derselbe |
| **Prüf-Lesen** (§7.1) | `TrackSync.NachJedemSchreibenWirdZurueckgelesen`, `…VorDemPruefLesenBleibtDieSpurGeaendert`, `…DasPruefLesenUeberschreibtDasAbbildNicht`, `…WirdWaehrendDesPruefLesensGeschriebenGiltDerNeueInhalt`, `…OhneVerifyGiltGeschriebenSofortAlsErledigt` | Ersatzfaden mit **Schadstelle**: die Spur meldet Schreiberfolg, liefert beim Lesen aber weiter den alten Inhalt — genau wie eine Diskette, die nicht mehr trägt |
| **Schadstelle** (§7.2) | `TrackSync.EineSchadstelleWirdGenauEinmalWiederholt`, `…DieSchadhafteSpurStehtImKlartext`, `…NeuBeschreibenRettetDasAbbildAufEineHeileDiskette`, `…NeuBeschreibenLaesstUnbekannteSpurenInRuhe` | derselbe |
| Zustände am Medium | `TrackSync.UnbekanntIstNichtUnformatiert`, `…ReihenlaufLaedtNichtNach`, `…RuecknahmeStelltDieSpurWiederAlsAenderungEin` | keiner nötig |
| Vertrag | `TrackSync.NurEinArbeitsfaden`, `…ShutdownLoestJedenWartenden` | keiner nötig |
| **Voller Emulator** | `PhysicalBoot.*` (`tests/integration/test_physical_boot.cpp`) | **Ersatzlaufwerk über einer `.hfe`-Fixture**: CP/A bootet spurweise bis `A>`, holt dabei **weniger als die halbe Diskette**, und das Vorauslesen bremst den Kaltstart nicht |
| C-ABI + Arbeitsfaden + DiskTool | `py_gw_physical` (`tests/python/test_gw_physical.py`, 17 Fälle) | `HfeDevice` (`tests/python/gw_fake.py`): liest HFE v1 von Hand, **ohne** `greaseweazle`-Import — dieselbe Diskette einmal als Datei und einmal „physisch" geöffnet muss dasselbe Verzeichnis und dieselben Dateibytes liefern; dazu die Schadstelle über `HfeDevice.schadhaft` |
| **ABI-Drift** | `py_gw_physical::test_die_ctypes_struktur_passt_zum_c_kopf`, `…die_auftragsarten_stimmen_ueberein` | keiner — liest den C-Kopf und vergleicht Feldnamen **und Reihenfolge** mit den `ctypes.Structure`.  Nötig, weil eine vertauschte Reihenfolge nicht abstürzt, sondern still falsche Zahlen liefert |
| **Oberflächen** | `py_gw_gui` (`tests/python/test_gw_gui.py`, 13 Fälle) | dasselbe Ersatzlaufwerk: Knopf → Sitzung → angemeldete Diskette → Anzeige → Auswerfen, in **beiden** Programmen; die Zusicherung, dass die Dialogauswahl genau die Argumente von `PhysicalSession.start` sind; und der **volle Schadstellen-Weg** durch das DiskTool (`test_disktool_meldet_die_schadstelle_beim_speichern`: schreiben → prüfen → Warnung mit Spurnummer → Ausweg) |
| **Echte Hardware** | `tests/python/test_gw_hardware.py` | keiner — **übersprungen**, wenn `K1520_GW_HARDWARE` nicht gesetzt ist; **nicht** in ctest registriert |

Der Kniff ist der **Ersatzfaden über einer `.hfe`-Datei**: er liefert dieselben
Bitzellen, die der Greaseweazle liefern würde (die Aufnahme *ist* ja eine), inklusive
Phasenversatz und Jitter einer echten Aufnahme, wenn man eine echte Aufnahme nimmt.
Damit ist der gesamte Weg — Zustandsverwaltung, Warteschlange, Decodierung, Boot —
in der Regression, und die Hardware fügt nur noch USB hinzu.

Die Hardware-Tests laufen von Hand:

```sh
K1520_GW_HARDWARE=1 venv/bin/python3 -m pytest tests/python/test_gw_hardware.py -v
```

Sie sind **lesend**, solange nicht zusätzlich `K1520_GW_WRITE=1` gesetzt ist — ein
Schreibtest beschreibt eine echte Diskette, und welche das ist, entscheidet kein Test
von sich aus.

---

## 15. Stand der Umsetzung (2026-08-16)

**Fertig und in der Regression** (1018/1018 ctest grün):

* `TrackSync` samt Spurzuständen im `DiskMedium`, drei Prioritäten, Blockade,
  Schreibpause, Rücknahme (`restoreFrom`).
* **Prüf-Lesen nach jedem Schreiben** samt Wiederholung, Defektvermerk und
  `rewriteAll()` (§7.1/§7.2).
* C-ABI `k1520s_*` in **beiden** Bibliotheken; `k1520_mount_physical` (Emulator) und
  `k1520d_open_physical` (DiskTool) — beide auch in den ctypes-Bindungen, gegen Drift
  mechanisch abgesichert.
* `app/gw/` (Gerätehülle, Arbeitsfaden, Bindung), `app/ui/physical_disk.py`
  (Dialog, Sitzung, Fortschritt, Defektmeldung) und die **Oberflächen beider
  Programme** (§12), einschließlich „Diskette neu beschreiben".

**Am echten Gerät nachgewiesen** (Greaseweazle F1, K5601, UDOS-4.3-Diskette):

| Vorgang | Ergebnis |
|---------|----------|
| eine Spur lesen (1 Umdrehung + Kopfweg) | 0,5–0,8 s; erste Spur 1,8 s (Motoranlauf) |
| Zellstrom je Spurseite bei 250 kbit/s | 100 363 Zellen = 12 546 Byte (299 U/min) |
| UDOS-Spur, roh gelesen | 156 A1-Sync-Marken = 26 Sektoren × 6 Felder |
| ganze Diskette im DiskTool öffnen | **97 s** für 160 Spuren, Verzeichnis beider Seiten vollständig (70 Dateien) |
| **Emulator-Kaltstart von der echten Diskette** | **UDOS 4.3 meldet sich mit Banner und Datumsabfrage** — bei erst 62–70 von 160 gelesenen Spuren |
| **Datei auf die echte Diskette schreiben** | über das DiskTool eingefügt, **4 Spuren** zurückgeschrieben, 0 Fehler; die Diskette danach **komplett neu eingelesen** → Datei byteweise gleich |
| beide Oberflächen | Einlegen, Füllstand, Auswerfen bzw. Öffnen mit Fortschritt — je einmal gegen die echte Hardware durchgefahren |

> **Das Prüf-Lesen (§7.1) ist an echter Hardware noch NICHT gegengeprüft.**  Der
> Adapter meldete sich unmittelbar davor nicht mehr am USB ab (weder `lsusb` noch
> `/dev/ttyACM*`).  Am Ersatzlaufwerk ist der Weg vollständig abgedeckt, aber die
> Bestätigung an der Scheibe fehlt.  Nachzuholen mit:
>
> ```sh
> K1520_GW_HARDWARE=1 K1520_GW_WRITE=1 \
>   venv/bin/python3 -m pytest tests/python/test_gw_hardware.py -v -s -k schreibt_eine_datei
> ```
>
> Der Test prüft seit dieser Änderung zusätzlich `verifies_done > 0` und
> `tracks_defect == 0`.

Vor dem ersten Schreibversuch wurde die Diskette gesichert (`gw read` über alle 160
Spuren, 2 MB `.hfe`) — bei einem Original ohne zweite Kopie gehört das dazu.

**Noch offen** (bewusst, nicht vergessen):

* **CLI** `k1520disktool --physical` (§12.3).
* **Die menügeführte DiskTool-Oberfläche** liegt unmerged auf `create_disktool`; beim
  Zusammenführen gehören „Physisches Laufwerk öffnen…" **und** „Diskette neu
  beschreiben" in `ui/actions.py` statt als freistehende Knöpfe (§12.2).
* **Die Sitzungsparameter merkt sich niemand** — Laufwerk und Zellrate müssen bei
  jedem Einlegen neu gewählt werden.
* **Ein zweites physisches Laufwerk am selben Adapter** ist ungetestet (§16).

---

## 16. Grenzen

* **Kein Flusszugriff** (§8.2): kein Kopierschutz, keine schwachen Bits.
* **Eine Diskette je Adapter.**  Ein Greaseweazle bedient zwei Laufwerke am Kabel, aber
  nur eines zur Zeit; vier emulierte Laufwerke gleichzeitig physisch zu betreiben,
  bräuchte vier Adapter.  Der Entwurf verbietet es nicht — jedes `DiskImage` hat seinen
  eigenen Synchronisierer —, die Praxis begrenzt es.
* **Das Öffnen misst nur eine Stichprobe** (seit 2026-08-16, s. §11.2a) — vorher las es
  die ganze Diskette, was 97 s kostete.
* **Drehzahl und Schreibnaht** werden nicht nachgebildet: zurückgeschrieben wird eine
  ganze Spur ab Index, wie sie ein Formatierlauf schreibt.
