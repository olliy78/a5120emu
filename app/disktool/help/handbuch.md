# k1520DiskTool — Kurzhandbuch

Dieses Werkzeug holt Dateien von Disketten der K1520-Rechner (A5120, A5130, PC 1715 …)
auf den heutigen Rechner und schreibt sie zurück. Es liest und schreibt die
Dateisysteme von **CP/A**, **SCPX** und **UDOS/ZDOS** in den Abbildformaten
`.hfe`, `.dmk` und `.img`.

Es gibt dasselbe auch als Kommandozeilenwerkzeug (`k1520disktool`); beide benutzen
dieselbe Bibliothek und kommen deshalb immer zum selben Ergebnis.

## Der übliche Weg

1. **Diskette öffnen** — *Datei ▸ Abbild öffnen* (Strg+O).
   Das Werkzeug erkennt Format und Dateisystem selbst; was es erkannt hat, steht
   im Kopfbereich.
2. **Ansehen** — die Dateien stehen in der linken Liste, der Linux-Ordner rechts.
   Den Ordner wählt man über seinen Pfad in der Überschrift oder über
   *Übertragung ▸ Zielordner wählen*.
3. **Holen** — Dateien links markieren, dann `→|`. Ganze Diskette: `→→|`.
4. **Ändern erlaubt?** Eine geöffnete Diskette ist zunächst **schreibgeschützt**.
   Wer schreiben will, hebt den Schutz bewusst auf: *Diskette ▸ Schreibschutz*
   (Strg+R) — der Knopf rechts in der Symbolleiste wechselt dann von 🔒 `R/O`
   auf 🔓 `R/W`.
5. **Schreiben** — Dateien rechts markieren, dann `|←`. Ganzer Ordner: `|←←`.
6. **Sichern** — *Datei ▸ Speichern* (Strg+S). Ein `*` im Fenstertitel zeigt an,
   dass noch etwas ungespeichert ist.

## Wo die Dateien liegen

Das Werkzeug arbeitet in denselben Ordnern wie der Emulator — im Dokumentenordner,
nicht dort, wo das Programm installiert ist:

| Ordner | Wofür |
|--------|-------|
| `K1520emu/Disketten` | die Abbilder (`.hfe`, `.dmk`, `.img`) |
| `K1520emu/Dateien` | was von den Disketten geholt und auf sie geschrieben wird |

Dort gehen die Dateidialoge auf, und die rechte Hälfte zeigt beim Start den
Dateiordner. Beim ersten Start nach einer Installation werden die mitgelieferten
Beispieldisketten dorthin ausgepackt.

Verschieben lässt sich das mit `K1520_DATA` (beide Ordner) bzw. `K1520_DISKS`
(nur die Abbilder). Wo das Werkzeug gerade sucht, sagt
`k1520disktool --paths` auf der Kommandozeile.

## Das Fenster

**Kopfbereich** — was dauerhaft über die geöffnete Diskette gilt: Datei, Format,
erkanntes Dateisystem, Zahl der Seiten. Rechts das Auswahlfeld, mit dem sich die
Erkennung übersteuern lässt.

**Meldungsstreifen** — erscheint nur, wenn es etwas Dauerhaftes zu beachten gibt
(„nicht eindeutig erkannt", „Altbestand im Medium"). Er lässt sich wegklicken.

**Statuszeile** — links das Ergebnis der letzten Aktion, rechts der Zustand:
Zahl der Dateien, freier Platz je Seite, Übertragungsart und der Schreibschutz.

**Protokoll** (F8) — alles, was das Werkzeug gemeldet hat, mit Uhrzeit. Es ist
beim Start zugeklappt und sammelt trotzdem mit; wer nachlesen will, klappt es auf.

**Symbolleiste** — die Abkürzung für die häufigen Wege. Sie lässt sich unter
*Ansicht ▸ Symbolleiste* ausblenden; **im Menü steht immer alles**.

## Dateien übertragen

Vier Knöpfe zwischen den Listen — außen die Stapel, innen die Auswahl:

| Knopf | Wirkung |
|-------|---------|
| `→→|` | die **ganze** Diskette in den Ordner |
| `→|`  | die **ausgewählten** Dateien in den Ordner |
| `|←`  | die **ausgewählten** Dateien auf die Diskette |
| `|←←` | den **ganzen** Ordner auf die Diskette |

Dasselbe geht mit der Maus: Dateien von einer Hälfte in die andere ziehen. Und
über *Bearbeiten* mit Strg+→ und Strg+←.

**Binär oder Text?** Im Menü *Übertragung*. Bei *Text* werden die Zeilenenden
umgesetzt (CR LF ↔ LF) — richtig für Quelltexte und Dokumente, **falsch für
Programme**. Was gerade gilt, steht rechts in der Statuszeile. Einzelne Dateien
mit den üblichen Textendungen (`.txt`, `.asm`, `.mac`, `.doc` …) werden auch im
Binärbetrieb als Text behandelt.

**Passt es nicht, wird gar nicht erst geschrieben.** Vor dem Einfügen eines
ganzen Ordners prüft das Werkzeug den Platz; reicht er nicht, bleibt die Diskette
unberührt und die Meldung sagt, woran es lag.

## Schreibschutz und Arbeitskopien

Der Schutz ist beim Öffnen gesetzt — beim bloßen Lesen soll nichts kaputtgehen
können. Ihn aufzuheben ist ein bewusster Schritt.

Bei einer unersetzlichen Diskette lohnt der Umweg: *Datei ▸ Speichern unter…*
legt eine Kopie an und arbeitet **ab dann an der Kopie**. Das geht auch bei
gesetztem Schutz; das Original bleibt, wie es war. Dabei lässt sich auch der
Behälter wechseln — aus `.img` wird `.hfe`, aus `.hfe` wird `.dmk`.

Eine Diskette, deren Geometrie nur **gemessen** und nicht aus dem Katalog erkannt
wurde, bleibt dauerhaft schreibgeschützt: was geraten ist, wird nicht beschrieben.

## UDOS-Disketten haben zwei Seiten

Bei UDOS ist jede Seite ein eigenes Dateisystem — für den Anwender aber **eine**
Diskette. Deshalb stehen beide Seiten in einer Liste, nach `Side0` und `Side1`
gruppiert.

Beim Extrahieren entstehen zwei Unterordner `Side0/` und `Side1/`; beim Einfügen
eines ganzen Ordners werden sie **verlangt**. Eine einzelne Datei landet auf der
Seite, auf die man sie zieht (oder auf der, in der gerade etwas markiert ist).

UDOS lässt sich nicht als `.img` ablegen: die Dateiverkettung steht dort hinter
der Daten-Prüfsumme, ein rohes Sektorabbild verlöre sie. Das Werkzeug lehnt das
darum ab, statt stillschweigend eine unbrauchbare Datei zu schreiben.

## Was eine Datei außer ihren Bytes hat

Rechtsklick auf eine Datei ▸ *Eigenschaften* (oder Doppelklick, oder Alt+Eingabe)
zeigt die Angaben, die ein Linux-Dateisystem nicht tragen kann:

* **UDOS** — den ganzen Kopfsektor: Dateityp, Eigenschaften (W/E/L/S), Satzlänge,
  Einsprungadresse, Segment, Lade- und Endadresse, Stapelgröße, Datum. Bei einer
  Programmdatei steuern diese Angaben, wie UDOS sie **lädt** — falsche Werte
  ergeben ein Programm, das nicht startet.
* **CP/M** — Nutzerbereich (0–15) und die Attribute R/O, SYS, ARC.

Der Nutzerbereich gehört zur **Identität** einer CP/M-Datei: ihn zu ändern
benennt die Datei um (`3:PIP.COM`).

Beim Extrahieren schreibt das Werkzeug diese Angaben in ein **Beiblatt**
(`udos-dateiangaben.txt` bzw. `cpm-dateiangaben.txt`) neben die Dateien. Legt man
den Ordner später wieder auf eine Diskette, werden sie daraus zurückgelesen —
ohne das Beiblatt gingen sie verloren.

## Neue Disketten und Bootdisketten

*Datei ▸ Neue Diskette* (Strg+N) legt eine formatierte, leere Diskette an. Gefragt
werden Dateisystem, Datei und Datenträgername.

Wo es Systemspuren gibt, wird zusätzlich gefragt, ob die Diskette **bootfähig**
sein soll. Dann braucht es ein Bootabbild (`.bin`) — das holt man sich mit
*Diskette ▸ Bootabbild sichern* aus einer vorhandenen Bootdiskette. Passt das
Abbild nicht in die Systemspuren, wird gar nichts angelegt und die Meldung nennt
beide Größen.

Eine bootfähige Diskette braucht danach noch die Systemdateien: bei CP/A `@OS.COM`
und die Dienstprogramme, bei UDOS mindestens `OS` und `ZDOS`.

## Eine echte Diskette am Greaseweazle

*Diskette ▸ Physische Diskette laden* (Strg+Umschalt+O) öffnet keine Datei, sondern eine
**echte Diskette** in einem echten 5,25″- oder 8″-Laufwerk, das über einen
[Greaseweazle](https://github.com/keirf/greaseweazle)-Adapter am USB hängt. Ab
dann arbeitet das Werkzeug wie mit einem Abbild — Dateien holen, schreiben,
löschen, Diskeditor.

Im Dialog stehen dabei drei Angaben, die nur Sie kennen können:

* **Zylinder (80 oder 40)** — wie weit nach innen gefahren wird. „40" an einem
  80er-Laufwerk liest genau die äußeren 40 Zylinder.
* **Doppelschritt erzwingen** — Spur 1 liegt dann auf Zylinder 2, Spur 2 auf
  Zylinder 4 und so fort. So beschreibt ein 40-Spur-Laufwerk (K5600.10) eine
  Diskette; ein 80-Spur-Laufwerk erreicht dieselben Spuren nur mit doppeltem
  Schritt.
* **Nur Seite 0** — die Rückseite wird gar nicht angefahren.

Das ist mehr als eine Zeitersparnis. Wer eine 40-Spur-Diskette einliest, die
früher einmal zweiseitig mit 80 Spuren formatiert war, schleppt sonst den alten
Bestand mit: auf den ungeraden Spuren und auf der Rückseite steht noch das frühere
Format, und die Erkennung sieht eine Mischung, die es nirgends gibt. Wird dort gar
nicht erst gelesen, kommt eine saubere einseitige Diskette herein.

Beim **Schreiben** entscheidet der Doppelschritt, in welchem Rechner die Diskette
danach läuft: mit Haken auf jedem zweiten Zylinder — dann liest sie ein K5600.10;
ohne Haken dicht hintereinander — dann liest sie ein K5601.

Drei Unterschiede, die man kennen muss:

* **Das Öffnen dauert einen Moment.** Die Formaterkennung liest ein paar Spuren
  quer über die Diskette — rund zehn Sekunden. Ein Fortschrittsfenster zeigt, wo
  es steht, und lässt sich abbrechen. Passt keines der bekannten Formate, sieht
  sie doch die ganze Diskette an; dann dauert es gut anderthalb Minuten.
* **Geöffnet wird schreibgeschützt**, bis man widerspricht. Ein Fehler kostet
  hier nicht eine Kopie, sondern die einzige noch existierende Diskette.
* **Gespeichert ist erst, was zurückgelesen wurde.** *Speichern* schreibt jede
  geänderte Spur und **liest sie sofort wieder ein**, um sie zu vergleichen.
  Erst dann gilt sie als geschrieben. Das dauert, findet aber Schadstellen, die
  ein Schreiben ohne Gegenprobe verschweigt.

Der **Diskeditor** lässt sich dabei jederzeit öffnen, auch wenn die Diskette erst
zum Teil gelesen ist. Spuren, von denen das Werkzeug noch nichts weiß, sind
**schwarz** — das ist etwas anderes als grau („unformatiert"): grau ist ein
Befund, schwarz heißt nur, dass noch keiner vorliegt. Die Ansicht füllt sich,
während im Hintergrund weitergelesen wird; ein Klick auf eine schwarze Spur holt
sie sofort.

### Wenn kein Dateisystem erkannt wird

Dann ist die Diskette **trotzdem offen** — nur ungedeutet. Sie liegt im Speicher,
der Diskeditor geht, das Abbild lässt sich mit *Speichern unter* sichern; gesperrt
ist nur, was Dateien braucht. Im Hintergrund wird weitergelesen.

Danach gibt es zwei Wege:

* **Dateisystem im Kopfbereich wählen** — die Deutung wird am Speicherabbild
  wiederholt, die Diskette wird dafür *nicht* noch einmal gelesen.
* **Diskette ▸ Speicherabbild ändern** — zwei Schnitte, die eine Diskette lesbar
  machen können:
  * *Ungerade Spuren entfernen* — für eine 40-Spur-Diskette, die im Doppelschritt
    beschrieben, aber einfachschrittig gelesen wurde. Auf den ungeraden Spuren
    steht dann noch das frühere Format.
  * *Seite 1 entfernen* — wenn die Rückseite nur Altbestand trägt.

  Beide arbeiten am **Abbild**, nicht an der Diskette. Danach ist die Verbindung
  zum Laufwerk beendet: die Spurnummern stimmen nicht mehr mit den Kopfpositionen
  überein, es wird also nicht weitergelesen und nichts mehr zurückgeschrieben. Das
  Abbild bleibt vollständig — zurückschreiben lässt es sich mit *Physische
  Diskette überschreiben* und gesetztem Doppelschritt-Haken.

Nach jedem Schnitt wird die Erkennung erneut versucht.

Trägt die Diskette eine Spur nicht mehr, sagt das die Meldung mitsamt
Spurnummer — **das Abbild im Speicher ist dann noch heil**. Der Ausweg steht
im Streifen und unter *Diskette ▸ Diskette neu beschreiben*: neue Diskette
einlegen, alles noch einmal wegschreiben. Nur bereits gelesene Spuren können
dabei geschrieben werden; was nie gelesen wurde, ist keine Aussage über den
Inhalt und bleibt deshalb weg.

### Eine Diskette beschreiben

*Diskette ▸ Physische Diskette überschreiben* geht den umgekehrten Weg: was
gerade geöffnet ist — auch eine `.hfe`-Datei — wird auf eine **echte** Diskette
geschrieben. So bringt man ein Abbild zurück auf einen Datenträger.

Zuerst kommt die Rückfrage, denn hier geht kein Abbild verloren, sondern eine
Diskette: **ihr bisheriger Inhalt ist danach fort.** Dann wird das Laufwerk
gewählt — und dann läuft es **im Hintergrund**: die Statuszeile zählt die
geschriebenen Spuren mit, der Streifen meldet das Ende. Man kann derweil
weiterarbeiten; nur das Laufwerk ist belegt, und die Diskette darf bis zum Ende
nicht entnommen werden. Jede Spur wird geschrieben *und* zurückgelesen. Passt das Abbild nicht in die eingestellte Laufwerksgeometrie
(mehr Spuren oder Seiten, als das Laufwerk hat), wird **gar nichts** geschrieben;
eine halb überschriebene Diskette wäre das schlechteste Ergebnis.

Der Menüpunkt ist gesperrt, wenn die Greaseweazle-Hosttools fehlen; sein
Kurzhinweis sagt dann, was zu tun ist:

```
pip install "git+https://github.com/keirf/greaseweazle.git@v1.23"
```

## Archivieren

*Datei ▸ Archivieren* (Strg+Umschalt+A) packt in **eine** `.zip`:

* das verlustfreie Abbild als `.hfe` (auch wenn die Quelle ein `.img` war),
* alle Dateien einzeln, nach Seiten sortiert,
* ein lesbares Inhaltsverzeichnis mit allen Dateiangaben und einer Legende,
* die maschinenlesbaren Beiblätter.

Gedacht als Langzeitablage: aus dem Textteil allein lässt sich in zwanzig Jahren
noch nachvollziehen, was auf der Diskette stand. Archivieren ist eine reine
Leseoperation und geht auch mit gesetztem Schreibschutz.

## Der Diskeditor

*Diskette ▸ Diskeditor* (Strg+E) zeigt die Diskette eine Ebene unter dem
Dateisystem: zwei Scheiben, Spur 0 außen, Sektor 0 auf zwölf Uhr, Seite 1
gespiegelt. Grün ist ein gültiger Sektor, rot ein defekter, orange die Lücke
dazwischen, grau eine unformatierte Spur. Schwarz heißt „noch nicht gelesen"
(nur bei einer physischen Diskette).

**Dunkel- oder hellgrün?** Hellgrün ist ein Sektor, der zwar formatiert, aber
nie beschrieben wurde — sein Datenfeld trägt nur das Füllbyte des Formats. So
sieht man auf einen Blick, wie viel von der Diskette wirklich benutzt ist. Der
UDOS-Anhang hinter den Nutzdaten zählt dabei nicht mit: er ist auch auf einer
leeren Diskette belegt.

Ein Klick auf einen Sektor — oder die Wählerzeile darunter — zeigt seinen Inhalt
als Hexdump mit mitlaufender Textspalte, dazu die Prüfsumme. *Save Sektor*
schreibt **bis in die Datei**. Sektoren lassen sich anlegen und löschen; die
Prüfsumme ist absichtlich mitschreibbar, damit sich eine schadhafte Diskette
originalgetreu nachbilden lässt.

**Ganze Spuren** lassen sich löschen und einfügen: *Spur löschen* wirft die
gewählte Spur mit beiden Seiten heraus (alles dahinter rückt auf), *Spur einfügen*
setzt dahinter eine leere ein. Damit stutzt man ein Abbild zurecht — etwa von 82
auf 80 Spuren oder auf 77, damit es auf eine 8″-Diskette passt. Bei einer
physischen Diskette endet damit die Verbindung zum Laufwerk, denn die Spurnummern
stimmen danach nicht mehr mit den Kopfpositionen überein.

Trägt ein Sektor hinter den Nutzdaten einen **UDOS-Anhang** (4 Byte), zeigt der
Editor ihn und übersetzt ihn: die Verkettung zum vorigen und nächsten Satz. Das
entscheidet der Sektor selbst — auch auf einer gemischten oder gar nicht erkannten
Diskette wird er angezeigt. Wo stattdessen nur Füllbytes stehen (CP/M), bleibt die
Angabe weg.

## Wenn eine Diskette nicht erkannt wird

Das Werkzeug rät nicht. Es geht der Reihe nach vor:

1. **Katalogformat** — passt eine Geometrie aus `formats.yaml`, wird sie benutzt.
2. **Abgeleitet (`cpa_auto`)** — passt kein Dateisystemprofil, rechnet das Werkzeug
   den Aufbau nach derselben Regel aus, mit der auch das CP/A-BIOS beim LOGIN
   arbeitet. Über das Auswahlfeld im Kopfbereich lässt sich das erzwingen.
3. **Gemessen** — passt gar keine Katalogsgeometrie, wird sie an der Diskette
   selbst vermessen. Ein so geöffneter Datenträger ist **unaufhebbar
   schreibgeschützt**.
4. Ergibt auch das keinen zusammenhängenden Sektorraum, bleibt die Liste leer und
   die Meldung nennt die gemessenen Werte — sie taugt als Vorlage für einen neuen
   Katalogeintrag.

Steht im Streifen „nicht eindeutig erkannt", passen mehrere Dateisysteme gleich
gut. Dann hilft ein Blick in die Dateiliste: das falsche Profil zeigt Unsinn.
Über das Auswahlfeld im Kopf lässt sich das andere ausprobieren.

## Tastenkürzel

| Kürzel | Wirkung |
|--------|---------|
| Strg+O | Abbild öffnen |
| Strg+N | Neue Diskette |
| Strg+Umschalt+O | Physische Diskette laden |
| Strg+S | Speichern |
| Strg+Umschalt+S | Speichern unter |
| Strg+Umschalt+A | Archivieren |
| Strg+W | Diskette schließen |
| Strg+Q | Beenden |
| Strg+A | Alles auswählen |
| Strg+→ | In den Ordner holen |
| Strg+← | Auf die Diskette schreiben |
| Entf | Löschen |
| Alt+Eingabe | Eigenschaften |
| Strg+R | Schreibschutz |
| Strg+E | Diskeditor |
| F5 | Aktualisieren |
| F8 | Protokoll |
| F1 | Dieses Handbuch |

## Was das Werkzeug zusichert

* **Beim Lesen kann nichts kaputtgehen.** Geöffnet wird schreibgeschützt.
* **Passt es nicht, wird gar nicht erst geschrieben.** Stapeloperationen werden
  vorher geprüft; ein Fehler mittendrin wird zurückgenommen.
* **Sicherungskopie.** Beim ersten Zurückschreiben entsteht `<name>~`.
* **Kein Raten — und wo gerechnet wird, steht es dabei.** Was abgeleitet oder
  gemessen wurde, sagt der Kopfbereich; Gemessenes bleibt schreibgeschützt.
* **Die Ansicht ist immer frisch.** Nach jeder schreibenden Aktion wird das
  Verzeichnis neu aus dem Medium gelesen — es gibt keinen Zwischenspeicher, der
  etwas anderes behaupten könnte.
