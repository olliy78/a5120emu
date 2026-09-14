# k1520DiskTool — Kurzhandbuch

Dieses Werkzeug holt Dateien von Disketten der K1520-Rechner (A5120, A5130, PC 1715 …)
auf den heutigen Rechner und schreibt sie zurück. Es liest und schreibt die
Dateisysteme von **CP/A**, **SCPX**, **UDOS/ZDOS**, **UDOS1715/NDOS** und
**SCP1700** (CP/M-86 des A7100) in den
Abbildformaten `.hfe`, `.dmk` und `.img`.

Es gibt dasselbe auch als Kommandozeilenwerkzeug (`k1520disktool`); beide benutzen
dieselbe Bibliothek und kommen deshalb immer zum selben Ergebnis.

## Der übliche Weg

1. **Diskette öffnen** — *Datei ▸ Abbild öffnen* (Strg+O).
   Das Werkzeug erkennt Format und Dateisystem selbst; was es erkannt hat, steht
   im Kopfbereich.
2. **Ansehen** — die Dateien stehen in der linken Liste, der Ordner des
   Wirtsystems rechts. Beim Start steht dort der Standardordner (siehe unten);
   von da aus navigiert man wie in einem Dateimanager: `..` ganz oben führt
   hinauf (auch die Rücktaste), ein Doppelklick auf einen Ordnernamen hinein.
   Die **Adresszeile** darüber lässt sich überschreiben — Pfad eintippen,
   Eingabetaste. Der Knopf mit dem Ordnersymbol daneben öffnet stattdessen den
   gewohnten Auswahldialog (auch *Übertragung ▸ Zielordner wählen*).
   Ein Rechtsklick in die rechte Liste legt einen **neuen Ordner** an
   (`neu`, `neu2`, …, Strg+Umschalt+N) oder **benennt** den angeklickten Eintrag
   **um** (F2). Beides geschieht im Feld an Ort und Stelle: Name eintippen,
   Eingabetaste oder ein Klick daneben übernimmt ihn, `Esc` verwirft.
   Findet das Werkzeug auf der Diskette keine Datei — leer formatiert oder mit
   einem unpassenden Format geöffnet —, steht das in der linken Hälfte:
   *Keine Dateien gefunden*.
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
Zahl der Dateien, freier Platz je Seite, der **Prüfbefund**, die Übertragungsart
und der Schreibschutz.

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

## SCP1700 — das CP/M-86 des A7100

Disketten des 16-Bit-Rechners **A7100** erkennt das Werkzeug von selbst und nennt
sie `scp1700`. Für die Bedienung gibt es dort nichts Besonderes: das Dateisystem
ist gewöhnliches CP/M, Attribute und Nutzerbereich verhalten sich wie bei einer
CP/A-Diskette.

Die Diskette selbst ist allerdings ungewöhnlich gebaut — ihre erste Spur ist mit
halber Geschwindigkeit in einem anderen Aufzeichnungsverfahren geschrieben als der
Rest. Das Werkzeug führt das mit, auch beim Zurückschreiben; zu sehen ist es im
Diskeditor und in der Formatzeile.

## UDOS1715 — dasselbe UDOS, anderes Dateisystem

Disketten vom **PC 1715** tragen UDOS mit dem Treiber **NDOS** statt ZDOS. Das
Werkzeug erkennt sie von selbst und nennt sie `udos1715`.

Dieselben Disketten schreibt auch der **Robotron P8000** (dort UDOS 2.2, z. B. seine
WEGA-Startdiskette). Die beiden Rechner haben nichts miteinander zu tun — sie legen
ihre Disketten nur gleich an; der Name `udos1715` benennt das Dateisystem, nicht die
Maschine. Für die Bedienung macht es keinen Unterschied.

Drei Dinge sind dort anders als bei den A5120-Disketten:

* Die Diskette ist **ein** Datenträger, nicht zwei Seiten — es gibt kein `Side0`
  und `Side1`, und beim Extrahieren entstehen keine Unterordner.
* Sie lässt sich **sehr wohl als `.img`** ablegen. Der Floppycontroller des
  PC 1715 kann nichts hinter die Prüfsumme schreiben, also steht dort auch
  nichts: die Verkettung liegt in eigenen *Zeigersektoren* innerhalb der Diskette.
  (Eine einzelne Aufnahme kann trotzdem `.img` verweigern — dann trägt sie die
  Schreibnaht eines überschriebenen Sektors hinter einer Prüfsumme.)
* Ein Dateiname muss mit einem **Buchstaben** beginnen.

Dateityp, Eigenschaften und der ganze Rest des Kopfsektors sind dieselben wie bei
UDOS/ZDOS; Eigenschaften-Dialog und Beiblatt gelten unverändert.

Angelegt wird eine solche Diskette mit dem Dateisystem `udos1715`
(80 Spuren beidseitig, 640 KB), `udos1715_ss80` (320 KB) oder `udos1715_ss40`
(160 KB).

## Was eine Datei außer ihren Bytes hat

Rechtsklick auf eine Datei ▸ *Eigenschaften* (oder Doppelklick, oder Alt+Eingabe)
zeigt die Angaben, die ein Linux-Dateisystem nicht tragen kann:

* **UDOS** — den ganzen Kopfsektor: Dateityp, Eigenschaften (W/E/L/S), Satzlänge,
  Einsprungadresse, Speichersegmente, Lade- und Endadresse, Stapelgröße, Datum. Bei
  einer Programmdatei steuern diese Angaben, wie UDOS sie **lädt** — falsche Werte
  ergeben ein Programm, das nicht startet.
  Das Feld **Segmente** trägt sie alle, durch Leerzeichen getrennt
  (`4000+06A7 62A7+0002 …`): ein Programm kann mehrere Speicherbereiche belegen.
* **CP/M** — Nutzerbereich (0–15) und die Attribute R/O, SYS, ARC.

Der Nutzerbereich gehört zur **Identität** einer CP/M-Datei: ihn zu ändern
benennt die Datei um (`3:PIP.COM`).

### Wo diese Angaben bleiben, wenn eine Datei den Ordner erreicht

Beim Herausholen legt das Werkzeug neben **jede** Datei eine gleichnamige
Textdatei mit der Endung `.fileinfo` — `ACTIVATE` und `ACTIVATE.fileinfo`. Darin
steht alles, was oben im Eigenschaften-Fenster zu sehen ist. Schreibt man die Datei
später wieder auf eine Diskette, wird sie von selbst gelesen; die Datei ist damit
**für sich genommen vollständig**, gleich wohin man sie kopiert. Beim Herausholen
des *ganzen* Inhalts entsteht zusätzlich ein Sammelbeiblatt
(`udos-dateiangaben.txt` bzw. `cpm-dateiangaben.txt`) — es bleibt bestehen, aber
man ist nicht mehr darauf angewiesen.

Ein `.fileinfo` ist **Zubehör, keine Datei der Diskette**: schreibt man einen
ganzen Ordner zurück, wird es ausgewertet und nicht mitkopiert (im Protokoll steht
dann „… (24 .fileinfo ausgewertet)"). Wählt man dagegen **nur** eine `.fileinfo`
aus, fragt das Werkzeug nach — das ist fast immer die falsche der beiden
gleichnamigen Zeilen.

**Bei UDOS entsteht das `.fileinfo` immer.** Dort ist es der Unterschied zwischen
einem lauffähigen Programm und einem, das mit `MEMORY PROTECT VIOLATION` abgewiesen
wird: ohne die Angaben wüsste UDOS nicht, dass es ein Programm ist, wohin es geladen
gehört und wo es anfängt. Fehlen sie beim Zurückschreiben, wird deshalb **gefragt
statt geraten** — ein Fenster erhebt Typ, Satzlänge und, bei einem Programm,
Einsprung und Speicherangaben. Bei mehreren Dateien genügt „Für alle übernehmen".

**Bei CP/M entsteht es nur auf Wunsch** — *Übertragung ▸ Bei CP/M je Datei ein
.fileinfo anlegen*. Dort gibt es nichts zu retten, was sonst verloren ginge:
Nutzerbereich 0 ohne Attribute ist der Normalfall, den auch das echte CP/M erzeugt,
und beides lässt sich jederzeit über *Eigenschaften* nachtragen. Wer eine Sammlung
führt und jede Datei für sich vollständig haben will, schaltet es ein.

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

### Dasselbe auf der Kommandozeile

```sh
k1520disktool --physical ls -l
k1520disktool --physical save-as sicherung.hfe
k1520disktool --physical --write put NEU.TXT
```

Befehle: `ls`, `info`, `check`, `get`, `put`, `rm`, `save-as`, `rewrite`; die
Laufwerksangaben aus dem Dialog heißen dort `--drive`, `--cyls`, `--heads`,
`--rate`, `--double-step`. **Ohne `--write` wird nichts verändert** — das ist
Absicht: an der Kommandozeile fragt niemand nach.

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

**Warum nicht?** Das Protokoll (F8) sagt es: die Erkennung probiert jedes in Frage
kommende Dateisystem durch, und jede Probe nennt, woran sie sich gestoßen hat —
„Verzeichnisplatz 0 trägt Nutzerbereich 0xDC", „Belegungskarte Spur 23: der
Zählerabgleich scheitert". Dieselbe Liste steht unter *Diskette ▸ Dateisystem
prüfen und reparieren…* (Strg+F), der auch ohne erkanntes Dateisystem aufgeht;
zu reparieren gibt es dort nichts, aber oft ist die Liste schon die ganze Auskunft
darüber, um was für eine Diskette es sich handelt.

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

## Die Diskette wird beim Öffnen geprüft

Sobald eine Diskette offen ist, sieht sich das Werkzeug ihr Dateisystem an — das
Verzeichnis und, wo es eines gibt, den Belegungsplan. Das kostet nichts: es sind
genau die Spuren, die zum Öffnen ohnehin gelesen wurden. Das Ergebnis steht

* **in der Statuszeile** rechts: `ohne Befund` oder etwa `⛔ 2`,
* **im Meldungsstreifen**, wenn etwas Ernstes dabei ist — mit dem Knopf
  *Befund ansehen…*,
* **im Protokoll** (F8) vollständig, mit Uhrzeit,
* **unter *Diskette ▸ Diskettenangaben…*** als Abschnitt „Prüfbericht".

Es gibt vier Schweregrade. Drei davon sind Auskunft; auf den vierten kommt es an:

| | |
|---|---|
| Hinweis | bemerkenswert, aber nicht falsch |
| Warnung | in sich widersprüchlich, ohne Folgen |
| Fehler | etwas ist schon jetzt unerreichbar oder falsch |
| **Gefahr** | **der nächste Schreibvorgang zerstört Daten** |

Bei **Gefahr** ist die Antwort immer dieselbe: den Schreibschutz gesetzt lassen,
erst ein Abbild sichern (*Speichern unter…*), und die Diskette nicht beschreiben.
Der häufigste Fall ist, dass zwei Verzeichniseinträge denselben Bereich der
Diskette beanspruchen — wer als Zweiter schreibt, überschreibt den Ersten.

**Die volle Prüfung** steht unter *Diskette ▸ Dateisystem prüfen und reparieren…*
(Strg+F) und, nur zum Lesen, unter *Diskette ▸ Diskettenangaben…* als Schaltfläche
*Vollprüfung*. Sie fasst jede Spur an und findet dadurch, was beim Öffnen nicht zu
sehen war: Kettenbrüche, doppelt belegte Bereiche, den Abgleich zwischen
Belegungsplan und Dateien und Sektoren mit falscher Prüfsumme.

**Sie läuft, sobald das Fenster aufgeht** — wer es öffnet, hat die Prüfung
verlangt; an einer Datei dauert sie den Bruchteil einer Sekunde. Der Knopf *Noch
einmal prüfen* wiederholt sie, etwa nach einem Eingriff im Diskeditor. Nur wenn
eine echte Diskette noch nicht vollständig gelesen ist, bleibt es bei der
Schnellprüfung.

Oben im Fenster steht die **Checkliste**: eine Zeile je Arbeitsschritt, mit Haken
und Ergebnis. Sie beantwortet die Frage, die „ohne Befund" offenlässt — *worauf*
wurde denn gesehen. Ein Schritt, der nicht gelaufen ist, steht mit dazu und nennt
den Grund (etwa „nur bei der Vollprüfung").

Bei einer **echten Diskette am Greaseweazle** kostet das die ganze Scheibe (0,5–0,8 s
je Spur). Deshalb lädt das Werkzeug die fehlenden Spuren **vorher** nach und zeigt
dabei einen Fortschrittsbalken mit „*x* von *y* Spuren geladen" — abbrechbar. Danach
liegt die Diskette vollständig im Speicher, und jeder weitere Lauf ist so schnell wie
an einer Datei.

Die Prüfung **ändert nie etwas**. Dasselbe geht auf der
Kommandozeile mit `k1520disktool check <abbild> --full`; das fasst jede Spur an
und findet zusätzlich Sektoren mit falscher Prüfsumme — und sagt dazu, **welche
Datei** darauf liegt.

**Stimmt die angegebene Größe?** Ja, das wird geprüft — und zwar für jede Datei.
Bei CP/M steht die Länge im Verzeichnis, die Daten stehen dort, wohin die
Blockzeiger des Eintrags weisen; die Prüfung rechnet nach, ob die Zeiger die
angesagte Länge überhaupt decken. Das ist wichtiger, als es klingt: fehlt ein
Zeiger, kommt die Datei beim Herausholen **trotzdem in voller Größe** heraus — der
Fehlbetrag besteht dann aus Nullen. Die Prüfung sagt es genau: *„14464 Byte
angesagt (113 Sätze), aber nur 4096 Byte durch Blockzeiger gedeckt — beim
Herausholen kämen 10368 Byte Nullen heraus."* Der Vorschlag dazu setzt die
Satzzahl auf das, was wirklich dasteht; er ist als *Datenverlust* gekennzeichnet
und deshalb nicht vorausgewählt, denn die Datei wird dadurch kürzer.

Bei UDOS und NDOS gibt es dieselbe Nachrechnung schon länger — dort steht die
Satzzahl im Kopfsektor und die Kette der Sätze ist die zweite Auskunft darüber
(*„Der Kopfsektor sagt 11 Sätze an, die Kette hat 7"*).

Ist die Prüfung ohne Befund, dann ist die Datei beim Herausholen **byte-genau so
groß, wie sie angegeben ist** — im Übertragungsmodus *binär*. Im Modus *Text*
werden Zeilenenden umgesetzt (CR LF ↔ LF), und dabei ändert sich die Bytezahl
gewollt.

Bei UDOS-Disketten (beide Ausprägungen) kann die volle Prüfung mehr als bei CP/M:
dort steht die Verkettung der Dateien in den Daten selbst und daneben ein
Belegungsplan — zwei Angaben über dieselbe Sache. Widersprechen sie einander,
sagt die Prüfung genau, welche Datei betroffen ist. Der ernste Fall heißt „ein
Sektor gehört zu einer Datei, steht aber als frei": die Datei ist heil, und der
nächste Schreibvorgang überschreibt sie.

Die volle Prüfung sieht dort außerdem in die **freien** Bereiche. Ein Sektor mit
falscher Prüfsumme, der zu keiner Datei gehört, ist nämlich nicht nichts: dort liegen
die gelöschten Dateien, und dorthin schreibt UDOS als nächstes. Es ist kein
Datenverlust — aber ein Grund, die Diskette zu kopieren, solange es noch geht.

**Von der Datei zu ihren Bytes:** ein Rechtsklick auf eine Datei in der linken
Liste bietet *Im Diskeditor öffnen* an — das schlägt ihren ersten Sektor auf (bei
UDOS den Kopfsektor mit Typ, Länge und Segmenten). Im Diskeditor sind bei UDOS die
Zeilen *zurück:* und *vor:* **anklickbar**: so folgt man der Kette einer Datei von
Satz zu Satz, ohne Spur und Sektor abzutippen.

## Reparieren

*Diskette ▸ Dateisystem prüfen und reparieren…* (Strg+F) zeigt denselben Befund
noch einmal — diesmal mit dem, was sich daran tun lässt. Jede Zeile trägt links
den Befund, rechts den Vorschlag; unten stehen die Einzelheiten mitsamt Ort. Ein
Doppelklick auf eine Zeile — oder der Knopf *Im Diskeditor zeigen* — schlägt genau
den **Sektor** auf, den der Befundtext nennt, nicht bloß die Spur.

Drei Dinge sind daran fest verabredet:

* **Vorausgewählt ist nur, was keine Daten verwirft.** Ein Vorschlag, der etwas
  wegwirft — eine gebrochene Kette kürzen, einen doppelt beanspruchten Bereich
  einer der beiden Dateien wegnehmen —, steht da, ist aber nicht angekreuzt.
  Er trägt die Marke `Datenverlust`.
* **Ein Befund ohne Vorschlag hat kein Ankreuzfeld.** Er ist eine Auskunft, kein
  Versäumnis: ein Sektor mit falscher Prüfsumme lässt sich nicht ausrechnen.
* **Manche Vorschläge sind gesperrt**, solange die Diskette nicht vollständig
  gelesen ist oder noch ein Kettenfehler offensteht. Der Grund steht daneben.
  Einen Belegungsplan aus halbem Wissen neu aufzubauen hiesse, die ungelesene
  Hälfte für frei zu erklären — und beim nächsten Schreiben zu überschreiben.

Ausgeführt wird alles Angekreuzte in **einem** Zug und in der richtigen Reihenfolge
(Verzeichnis → Ketten → Belegungsplan → Zähler); geht etwas schief, bleibt die
Diskette unverändert. Danach wird sofort neu geprüft, und das Protokoll (F8) trägt
die Bilanz: `vorher 3 Befunde (1 Gefahr) → nachher 0 Befunde (0 Gefahr)`.

Die Sicherung entsteht von allein: beim ersten Schreiben legt das Werkzeug
`<name>~` neben das Abbild. Bei einer **echten** Diskette gibt es die nicht — dort
ist *Speichern unter…* vor dem Eingriff die einzige Umkehr.

Dasselbe auf der Kommandozeile:

```
k1520disktool fsck <abbild> [--full] [--repair[=alle|<kennung>,…]] [--dry-run]
```

Ohne `--repair` wird nur geprüft. `--repair` führt die empfohlenen Reparaturen
**ohne** Datenverlust aus, `--repair=alle` auch die übrigen, und
`--repair=cpm.zeiger.streichen,…` genau die genannten. `--dry-run` sagt nur, was
geschähe.

## Gelöschte Dateien suchen

*Diskette ▸ Gelöschte Dateien suchen…* holt hervor, was ein Löschen übriggelassen
hat. Und das ist überraschend viel — **keines der Dateisysteme überschreibt beim
Löschen die Daten.** Nur *was* übrigbleibt, ist verschieden:

| | Was das Löschen tut | Was übrigbleibt |
|---|---|---|
| **CP/M** | setzt **ein Byte** — den Nutzerbereich des Verzeichnisplatzes auf 0xE5 | Name, Typ, Satzzahl und alle Blockzeiger stehen unverändert da |
| **UDOS / ZDOS / NDOS** | schneidet den **Verzeichniseintrag** heraus und löscht die Bits im Belegungsplan | Der Kopfsektor **vollständig** und die ganze Kette. Verloren ist **allein der Name** |

Daraus folgt der Unterschied, der beim Bedienen auffällt: bei CP/M kennt der Fund
seinen Namen, bei UDOS nicht. Dort heißt er `GERETTET.001` — es sei denn, im
Verzeichnis steht noch ein Namensrest, dann wird der **vorgeschlagen** (nie als
Tatsache ausgegeben; er lässt sich in der Liste ändern).

**Gesucht wird sofort beim Öffnen des Fensters**, und zwar über die ganze
Oberfläche — an einer Datei kostet das den Bruchteil einer Sekunde. Wie bei der
Prüfung steht oben eine **Checkliste** mit den Schritten des Laufs und dem, was
jeder gefunden hat.

Zwei Suchtiefen stehen oben im Fenster:

* **Verzeichnisreste** — bei CP/M nur die gelöschten Verzeichnisplätze, und das
  kostet nichts: das Verzeichnis ist ohnehin gelesen.
* **Ganze Oberfläche** — zusätzlich jeder freie Bereich der Diskette. Das findet die
  Bruchstücke *ohne* Verzeichnisplatz (Verzeichnis neu aufgesetzt, Diskette halb neu
  beschrieben).

**Bei UDOS kosten beide Tiefen gleich viel.** Nach dem Löschen steht im Verzeichnis
nichts Gesuchtes mehr — gesucht wird nach der Signatur des Kopfsektors, und dafür
müssen die Datenspuren angesehen werden. Die beiden Tiefen unterscheiden sich dort
nur darin, *welche* Sektoren als Kandidat gelten und ob Rohbereiche gesammelt werden.
An einer echten Diskette wird vorher geladen, mit Fortschrittsbalken; danach ist es
so schnell wie an einer Datei.

Jeder Fund trägt eine **Güte**, und die ist keine Schätzung, sondern eine Aussage
mit Belegen — sie steht als Tooltip und unter der Vorschau im Klartext:

| Güte | heißt |
|---|---|
| ✔ sicher | kein Block des Fundes gehört einer lebenden Datei — der Inhalt ist der von damals |
| ≈ wahrscheinlich | vollständig, aber mit benannten Vorbehalten (falsche Prüfsumme, zweiter Anspruch) |
| ✂ Bruchstück | ein Teil ist neu vergeben oder die Struktur bricht ab — nur der Anfang ist zu retten |

Drei Wege stehen unten:

* **In den Ordner retten…** ist der Vorgabeweg und geht **immer** — auch an einer
  schreibgeschützten Diskette, auch bei einem Bruchstück. Der übliche Fall ist
  „einmal alles retten, was noch da ist, dann die Diskette in Ruhe lassen".
  Fehlende Bereiche werden mit Füllbytes aufgefüllt, damit die Offsets der übrigen
  stimmen; daneben entsteht dann ein Beiblatt `<datei>.rettung.txt`, das genau das
  festhält. Für ein Textdokument ist das brauchbar, für ein Programm nicht.
* **Alles Sichere retten…** schreibt alle Funde der Güte *sicher* in einen Ordner.
* **Auf der Diskette wiederherstellen** trägt den Fund wieder ins Verzeichnis ein
  und verlangt Schreibrecht. **Das gibt es nur bei CP/M** — dort ist es ein einziges
  Byte, und der Name stimmt. Es geht nur, wenn kein Block des Fundes inzwischen einer
  lebenden Datei gehört — sonst entstünde genau die Kreuzbelegung, die die Prüfung als
  **Gefahr** meldet. Steht der Knopf still, sagt die Zeile darüber, warum. Der
  ursprüngliche **Nutzerbereich** ist übrigens nicht zu retten: er stand in ebendem
  Byte, das beim Löschen überschrieben wurde — wiederhergestellt wird nach Bereich 0.

**Bei UDOS führt der Weg zurück über den Ordner**, und er ist vollwertig:

1. Fund in den Ordner retten.
2. Der Datei dort den passenden Namen geben — und die `.fileinfo` daneben
   mitbenennen (aus `GERETTET.001` und `GERETTET.001.fileinfo` werden
   `NOTE.TO.SD` und `NOTE.TO.SD.fileinfo`); im Sammelbeiblatt
   `udos-dateiangaben.txt` steht der Name ebenfalls.
3. Mit *Einfügen* wieder auf die Diskette bringen.

Dass das trägt, liegt am Kopfsektor: er überlebt das Löschen **ganz**. Typ,
Eigenschaften, Startadresse, Satzlänge, alle Speichersegmente und beide
Datumsvermerke sind noch da, und das Werkzeug schreibt sie beim Retten in die
`.fileinfo` und ins Beiblatt. Verloren ist wirklich nur der Name — die Attribute
kommen von selbst mit.

### Erst ansehen, dann handeln

Neben der Vorschau steht die **Sektorliste** des Fundes, und *Im Diskeditor zeigen*
schlägt den gewählten Sektor auf. Damit lässt sich vor jeder Aktion nachsehen, was da
wirklich steht.

Das ist keine Bequemlichkeit, sondern bei UDOS die einzige Handhabe: es gibt keinen
Namen zum Wiedererkennen, und die Sätze einer Datei liegen **nicht** hintereinander.
`NOTE.TO.SD` der Referenzdiskette belegt auf Spur 21 die Sektoren 6, 7, 12, 23, 1,
8, … — verkettet und physisch verschränkt. Wer nur den ersten Sektor kennt, findet den
zweiten nicht. Der erste Eintrag der Liste ist bei UDOS der **Kopfsektor**; dort
stehen Typ, Startadresse und die Segmente — also genau das, woran sich eine namenlose
Datei erkennen lässt.

### Zwei Funde mit demselben Namen

Bei CP/M kann derselbe Name zweimal in der Liste stehen. Das ist kein Fehler: es sind
zwei nacheinander gelöschte, **verschiedene** Dateien. Der Nutzerbereich, der sie
unterscheiden würde, ist ja das gelöschte Byte — auseinandergehalten werden sie an der
Extentnummer. Der Text sagt es dazu („es gibt 2 gelöschte Einträge dieses Namens"),
und der zweite bekommt den Speichervorschlag `NAME.2`, damit *Alles Sichere retten*
den einen nicht mit dem anderen überschreibt.

Funde ohne Namen (die Bruchstücke) heißen `fragment_c12h0_b40-b47.bin`, bei UDOS
`fragment_c12h0_s6-s26.bin`; der Name lässt sich in der Liste ändern, bevor man
rettet.

Dasselbe auf der Kommandozeile:

```
k1520disktool recover <abbild> [--full] [--to ordner] [--list] [--restore N[=NAME]]
```

Ohne `--to` wird nur aufgelistet; `--full` ist die Oberflächensuche, und
`--restore 0=ALT.COM` trägt Fund 0 unter dem Namen `ALT.COM` wieder ein — **nur bei
CP/M**; bei UDOS lehnt es ab und nennt den Weg über den Ordner. `--json` gibt
dieselbe Liste maschinenlesbar aus, samt der Sektoren jedes Fundes.

## Archivieren

*Datei ▸ Archivieren* (Strg+Umschalt+A) packt in **eine** `.zip`:

* das verlustfreie Abbild als `.hfe` (auch wenn die Quelle ein `.img` war),
* alle Dateien einzeln, nach Seiten sortiert,
* ein lesbares Inhaltsverzeichnis mit allen Dateiangaben und einer Legende,
* ein maschinenlesbares Verzeichnis `diskarchive.yaml`,
* die maschinenlesbaren Beiblätter.

Vorher wird nach der **Beschriftung** der Diskette gefragt — dem Text auf dem
Aufkleber. Daraus entstehen der vorgeschlagene Dateiname und die Namen der
Dateien im Archiv, und sie steht im Kopf des Inhaltsverzeichnisses. Bei einer
**physischen** Diskette ist das die einzige Auskunft darüber, welche Diskette
archiviert wurde: sie hat keine Abbilddatei, von der sich ein Name ableiten
liesse. Vorgeschlagen wird der Dateiname der offenen Diskette, sonst ihr
Datenträgername.

Gedacht als Langzeitablage: aus dem Textteil allein lässt sich in zwanzig Jahren
noch nachvollziehen, was auf der Diskette stand. Archivieren ist eine reine
Leseoperation und geht auch mit gesetztem Schreibschutz.

**`diskarchive.yaml` — für die Inventur einer Sammlung.** Dieselbe Auskunft für
Programme: je Datei ihr Pfad im Archiv, Verzeichnis und Name, die Größe und eine
SHA-256-Prüfsumme, dazu die Prüfsumme des Abbilds als Kennzeichen der Diskette
selbst. Damit lässt sich über einen Stapel Archive auszählen, wie viele Fassungen
von `XYZ.COM` es gibt und auf welchen Disketten sie liegen — ohne eine einzige
`.zip` auszupacken:

```python
import zipfile, yaml, collections
fassungen = collections.defaultdict(set)
for archiv in Path("archive").glob("*.zip"):
    with zipfile.ZipFile(archiv) as z:
        for f in yaml.safe_load(z.read("diskarchive.yaml"))["files"]:
            fassungen[f["name"].upper()].add((f["sha256"], archiv.name))
```

**Auf der Kommandozeile** — für eine ganze Sammlung, Diskette für Diskette:

```sh
python3 -m app.disktool.archive abbild.hfe archiv.zip "CP/A Arbeit 7"
k1520disktool --physical archive archiv.zip --label "UDOS 4.3 Nr. 7"
```

Der zweite Weg archiviert die Diskette im **echten Laufwerk**; `--label` ist dort
der Aufkleber (ohne ihn gilt der Datenträgername). Ein vorhandenes Archiv wird
nicht überschrieben — dafür braucht es `--force`.

Angaben des Dateisystems stehen dort bewusst nicht — die führen das
Inhaltsverzeichnis und die Beiblätter. Der Kopf der Datei erklärt jedes Feld;
`diskarchive: 1` ist die Fassung des Formats, künftige Erweiterungen kommen als
zusätzliche Felder hinzu.

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
fragt beim Einfügen nach **Spurnummer** und **Verfahren** (FM oder MFM) und setzt
dort eine unformatierte Spur ein, in der sich anschliessend mit *Neuer Sektor* von
Hand formatieren lässt.

Beides ist wählbar, weil es in der K1520-Welt gemischte Formate gibt: eine
FM-Spur lässt sich auch **vor** alle bestehenden MFM-Spuren setzen (Spurnummer 0),
oder eine MFM-Spur hinter eine FM-Spur. Die neue Spur bekommt die eingegebene
Nummer; alles ab dort rückt nach hinten — aus 42 wird 43. Damit stutzt man ein Abbild zurecht — etwa von 82
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

Die Prüfung nimmt einem diese Arbeit weitgehend ab: sie probiert die übrigen Profile
still durch und sagt es, wenn eines besser durchkommt — „mit `scp1700` kommt diese
Diskette besser durch: 46 statt 43 sichtbare Dateien". **Die Wahl ändert sie nie**,
sie sagt sie nur an; entschieden wird im Auswahlfeld. Der zweite Teil dieser Auskunft
ist der wichtigere: ein Profil mit einem zu kleinen Verzeichnisbereich mountet
anstandslos, prüft ohne Befund — und **verschweigt einen Teil der Dateien**. An der
Zahl der Befunde ist das nicht zu erkennen, an der Zahl der Dateien schon.

## Die anderen Werkzeuge

Im Menü **Werkzeuge** steht *A5120-Emulator starten*: der Emulator des
Bürocomputers, der von einer Diskette bootet und das Betriebssystem wirklich
laufen lässt. Er startet als eigenes Programm und läuft neben dem
Diskettenwerkzeug weiter.

Beide Programme lesen dieselbe Datei, aber jedes für sich. Hat die Diskette hier
ungespeicherte Änderungen, wird vor dem Start danach gefragt — der Emulator
sieht sonst den Stand der **Datei**, nicht den auf dem Bildschirm. Und umgekehrt:
was der Emulator auf die Diskette schreibt, kommt hier erst an, wenn das Abbild
neu geöffnet wird — *Aktualisieren* (F5) baut nur die Ansicht aus dem
Speicherabbild neu auf und liest die Datei nicht noch einmal.

## Tastenkürzel

| Kürzel | Wirkung |
|--------|---------|
| Strg+O | Abbild öffnen |
| Strg+N | Neue Diskette |
| Strg+Umschalt+N | Neuen Ordner anlegen (rechte Hälfte) |
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
| Strg+F | Dateisystem prüfen und reparieren |
| F2 | Umbenennen (rechte Hälfte) |
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
