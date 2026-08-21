# Eine einzeln herausgeholte Programmdatei ist beim Zurückschreiben zerstört

> **Stand:** 2026-08-21 · **Befund gemessen, Lösung entworfen, noch nicht umgesetzt**
> **Betrifft:** `core/filesystem/disk_volume.cpp` (Beiblätter, `extract`/`insert`),
> `core/filesystem/file_system.h` (`WriteOptions`), `tools/k1520disktool.cpp`
> (`cmd_get`/`cmd_put`), `app/disktool/` (Oberfläche)
> **Gehört zu:** `doc/design/13_k1520disktool.md` §13a (Beiblatt),
> `doc/udos_diskettenformat.md` §6 und §14 (Kopfsektor, Lader)

---

## 1. Der Befund

Eine UDOS-**Programmdatei**, die einzeln auf den PC geholt und später einzeln wieder
eingespielt wird, ist danach **nicht mehr lauffähig**. Der Dateiinhalt kommt bytegleich
an — verloren gehen die Angaben des **Kopfsektors**, und ohne die weiß UDOS nicht, dass
es sich um ein Programm handelt, wohin es geladen gehört und wo es anfängt.

Nachgestellt an `ACTIVATE` der Referenzdiskette (`tests/fixtures/disks/udos_boot_scp.hfe`):

```sh
k1520disktool get  quelle.hfe --to ordner 'Side0/ACTIVATE'
k1520disktool put  ziel.hfe   ordner/Side0/ACTIVATE --volume 0
```

| Angabe | vorher | nachher |
|---|---|---|
| Typ | **P** (Programm) | **B** (Binärdatei) |
| Eigenschaften | `WS` | – |
| ENTRY (Startadresse) | `4000` | `0000` |
| Satzlänge | 1024 | **128** |
| zweite Längenangabe | 1024 | 128 |
| Bytes im letzten Satz | 1024 | 128 |
| SEGMENT | `4000` + 1022 B | `0000` + 0 |
| LOW / HIGH / STACK | `4000` / `43FF` / `0080` | `FFFF` / `FFFF` / `FFFF` |
| erstellt | `791019` | *heute* |

`FFFF` bei LOW/HIGH ist dabei nicht bloß ein anderer Wert: der UDOS-Lader trägt sie in
die Nukleusvariablen `1275H`/`1277H` und lässt den Speicherverwalter zuteilen — steht
dort `FFFF`, bricht er mit `MEMORY PROTECT VIOLATION` ab
(`doc/udos_diskettenformat.md` §14).

### 1.1 Warum es passiert

Die Kopfsektorangaben stehen **nicht in der Datei**. Sie werden in einem *Beiblatt*
neben den Dateien geführt (`udos-dateiangaben.txt`, bei CP/M `cpm-dateiangaben.txt`),
und `insert` liest es beim Zurückschreiben von selbst wieder ein.

**Dieses Beiblatt entsteht aber nur beim VOLLEXPORT** (`DiskVolume::extractAll`). Ein
`get` mit Dateimuster ruft `DiskVolume::extract` je Datei — und das schreibt keines.
Damit steht `insert` ohne jede Angabe da und nimmt seine Vorgaben: Typ B, Satzlänge
128, keine Startadresse, keine Segmente.

### 1.2 Warum es niemandem auffällt

Drei Gründe, und der dritte ist der ärgerlichste:

* **Der Dateiinhalt ist korrekt.** Ein Vergleich der Bytes bestätigt „alles in
  Ordnung".
* **Es gibt keine Meldung.** Weder `get` noch `put` sagen etwas.
* **Die Dateisystemprüfung meldet „ohne Befund".** Sie *kann* es nicht wissen:
  `udos.kopf.speicher` beanstandet `FFFF` nur bei Typ **P** — und der Typ ist ja
  gerade mit verlorengegangen. Eine als **B** eingetragene Datei mit `FFFF` ist
  regelkonform. Der Schaden ist also **unsichtbar**, auch für das eigene Werkzeug.

### 1.3 Umfang

| Weg | Beiblatt | Ergebnis |
|---|---|---|
| `get` **ohne** Muster → `put <ordner>` | ja | ✅ unverändert |
| `get` **ohne** Muster → `put <ordner>/<datei>` | ja (wird im Elternordner gefunden) | ✅ unverändert |
| `get` **mit** Muster → `put` | **nein** | ❌ Angaben verloren |
| Oberfläche: einzelne Datei herausziehen (`_extrahieren_refs`) | **nein** | ❌ Angaben verloren |
| `recover --to` | ja (`recoverExtract` legt es an) | ✅ unverändert |

Betroffen ist die **ganze UDOS-Familie** (ZDOS und NDOS). Bei CP/M gehen ebenfalls
Angaben verloren — der **Nutzerbereich** und die Attribute `R/O`, `SYS`, `ARCHIV` —,
aber dort entsteht daraus keine unbrauchbare Datei, sondern eine Datei im Bereich 0
ohne Attribute.

### 1.4 Ein zweiter, kleinerer Fehler (bereits behoben)

Beim Suchen fiel auf, dass selbst **mit** Beiblatt zwei Felder verlorengingen: der
Schreibpfad las die `0` als „nicht angegeben" und setzte einen Ersatzwert („Bytes im
letzten Satz", LOW/HIGH/STACK). Behoben am 2026-08-21 mit `udos_bytes_in_last_gesetzt`
und `udos_mem_gesetzt`; Wächter
`DiskVolume.UdosRundlaufErhaeltAuchDieNullenImKopfsektor`. Das hier beschriebene
Problem ist davon unabhängig und bleibt bestehen.

---

## 2. Die Lösung

Zwei Teile, die zusammengehören: **(1)** die Angaben entstehen künftig zu *jeder*
extrahierten Datei, und **(2)** wo sie beim Zurückschreiben trotzdem fehlen, wird
nicht geraten, sondern gefragt.

### 2.1 (1) Je Datei ein `.fileinfo`

**Zu jeder extrahierten Datei wird eine gleichnamige Textdatei mit der Endung
`.fileinfo` abgelegt** — `ACTIVATE` → `ACTIVATE.fileinfo`.

Für **alle** Dateitypen, nicht nur für Programme: auch eine ASCII-Datei hat einen Typ,
Eigenschaften, eine Satzlänge und zwei Datumsvermerke, und auch eine CP/M-Datei hat
einen Nutzerbereich und Attribute. Ein Format, das nur manchmal entsteht, ist eines,
auf das sich niemand verlässt.

**Auch bei der Komplettsicherung**, obwohl dort schon ein `…-dateiangaben.txt`
entsteht. Die Angabe ist dann doppelt vorhanden — das ist gewollt: die einzelne Datei
soll für sich genommen vollständig sein, egal aus welchem Ordner sie stammt und wohin
sie später kopiert wird. Ein Ordner, dessen Sammelbeiblatt beim Umsortieren
zurückbleibt, ist genau der Fall, der heute schiefgeht.

**Inhalt.** Dieselben Schlüsselwörter wie die Sammelbeiblätter, nur eines je Zeile:

```
# k1520DiskTool — Angaben zum Kopfsektor von ACTIVATE
# Sie stehen NICHT in der Datei selbst.  Beim Einfuegen (`put`) werden sie
# wieder uebernommen; ohne sie kann das Werkzeug nicht wissen, was fuer eine
# Datei das ist.
fs=udos
name=ACTIVATE
typ=P
eig=WS
start=4000
satz=1024
block=1024
rest=1024
segment=4000:1022
mem=4000:43FF:0080
zusatz=0
erst=791019
geaend=900808
```

*(Wörtlich die Zeile, die `extractAll` heute für `ACTIVATE` schreibt — nur
umgebrochen. `segs=` kommt hinzu, sobald eine Programmdatei **mehr als ein**
Speichersegment hat; `ZLINK` der PC-1715-Diskette hat sechs.)*

Bei CP/M entsprechend `fs=cpm`, `name=3:SYSTEM.COM`, `attr=RSA`.

Drei Festlegungen dazu:

* **`fs=` steht in der ersten Zeile.** Ein `.fileinfo` einer CP/M-Datei darf nicht
  stillschweigend als UDOS-Angabe gelesen werden; passt es nicht zum Ziel, ist das
  eine Meldung, keine Vorgabe.
* **`name=` trägt den echten Namen auf der Diskette.** Bei CP/M mit Nutzerbereich
  (`3:SYSTEM.COM`), der im Linux-Dateinamen als `3_SYSTEM.COM` steht. Das leistet
  heute das Sammelbeiblatt; die Einzeldatei muss es genauso können.
* **`.fileinfo` ist Zubehör, nicht Nutzdatei.** Beim Einfügen eines Ordners wird es
  *ausgewertet*, aber nicht mitkopiert — sonst landet es beim nächsten
  `put <ordner>` als Datei auf der Diskette. Was geschieht, wenn jemand es
  **ausdrücklich einzeln** einfügen will, steht in §2.5. Eine Datei, die auf der
  Diskette wirklich `X.FILEINFO` heißt, kollidiert mit dieser Regel; das ist
  hinzunehmen und gehört in die Bedienungsanleitung.

**Wo geschrieben wird:** in `DiskVolume::extract` — also an *einer* Stelle, durch die
alle Wege laufen (CLI `get`, Oberfläche, `extractAll`). `recoverExtract` schreibt es
ebenfalls, zusätzlich zum vorhandenen `udos-dateiangaben.txt`.

### 2.2 (2) Beim Zurückschreiben: suchen, sonst fragen

**Rangfolge beim `insert`:**

1. `<datei>.fileinfo` neben der Datei — die genaueste Auskunft, sie gehört zu *dieser*
   Datei.
2. `udos-dateiangaben.txt` bzw. `cpm-dateiangaben.txt` im Ordner der Datei oder eine
   Ebene darüber (der heutige Weg, bleibt unverändert).
3. Ausdrückliche Angaben des Aufrufers (`put --type P1 --record-len 1024 …`) — die
   gehen **immer** vor, auch über ein vorhandenes `.fileinfo`.
4. **Nichts davon** ⇒ das Werkzeug kann nicht wissen, was für eine Datei das ist.

Fall 4 wird **nur bei der UDOS-Familie** behandelt; bei CP/M entsteht ohne Rückfrage
eine Datei im Nutzerbereich 0 ohne Attribute (§2.4).

**Fall 4 in der Kommandozeile: Abbruch mit Fehlermeldung.**

```
Fehler: Zu 'ACTIVATE' gibt es keine Angaben (weder ACTIVATE.fileinfo noch
        udos-dateiangaben.txt).  UDOS braucht Typ, Satzlaenge und — bei einem
        Programm — Startadresse und Speicherangaben; ohne sie entstuende eine
        Datei, die nicht laeuft.
        Entweder die Datei mit ihrem .fileinfo herueberholen, oder die Angaben
        mitgeben:  --type P --record-len 1024 --entry 4000 --mem 4000:43FF:0080
```

**Fall 4 in der Oberfläche: ein Dialog, in dem sich alles eingeben lässt.**

### 2.3 Der Eingabedialog (`app/disktool/ui/fileinfo_dialog.py`)

Er geht auf, wenn eine Datei ohne Angaben eingefügt werden soll, und zeigt, was das
Zieldateisystem braucht — **Auswahlfelder, wo es eine feste Menge gibt**, und
**abgeblendete Felder, wo die Angabe zum gewählten Typ nicht passt**.

| Feld | Art | gilt bei |
|---|---|---|
| Typ | Auswahl `A` · `B` · `P` · `P1`…`P15` | UDOS |
| Eigenschaften | Ankreuzfelder `W` `E` `L` `S` `R` `F` | UDOS |
| Satzlänge | Auswahl 128 · 256 · 512 · 1024 | UDOS |
| zweite Längenangabe | Auswahl *(wie Satzlänge)* · `0` | UDOS, Fachleute |
| Startadresse (ENTRY) | Eingabe, hexadezimal | **nur P/P1** |
| Speichersegmente | Eingabe `4000+03FE, …` | **nur P/P1** |
| LOW / HIGH / STACK | drei Eingaben, hexadezimal | **nur P/P1** |
| Zusatz (Offset 44–47) | Eingabe, hexadezimal | **nur P/P1** |
| erstellt / geändert | Eingabe `JJMMTT` | UDOS |
| Nutzerbereich | Auswahl 0…15 | CP/M *(s. u.)* |
| Attribute | Ankreuzfelder `R/O` `SYS` `ARCHIV` | CP/M *(s. u.)* |

Die beiden CP/M-Zeilen stehen hier der Vollständigkeit halber: **von selbst geht der
Dialog bei CP/M nie auf** (§2.4). Sie beschreiben, was er zeigte, wenn er einmal
ausdrücklich aufgerufen würde — dieselben Felder führt schon der
Eigenschaften-Dialog.

**Das Abblenden ist die eigentliche Leistung des Dialogs.** Bei Typ `A` oder `B` sind
ENTRY, Segmente und LOW/HIGH/STACK **kein Anwenderinhalt, sondern schlicht
unbelegt** — dort etwas einzutragen erzeugt einen Kopfsektor, den es so auf keiner
echten Diskette gibt. Die Felder werden deshalb beim Wechsel des Typs abgeblendet und
auf 0 gesetzt, nicht bloß ignoriert.

Drei Bedienzusagen:

* **Vorbelegung aus dem Augenschein**: Endet die Datei auf `.COM`/`.OBJ` oder beginnt
  sie mit einem Z80-Einsprungmuster (`C3`, `31`, `F3`), wird `P` vorgeschlagen, sonst
  `A` bei überwiegend druckbarem Inhalt und `B` sonst. Ein *Vorschlag*, kein
  Automatismus — dieselbe Haltung wie bei den Namensresten der Wiederherstellung.
* **„Für alle übernehmen"** beim Einfügen mehrerer Dateien: sonst steht der Anwender
  bei einem Ordner mit dreißig Dateien dreißigmal vor demselben Fenster.
* **„Angaben als `.fileinfo` speichern"** (vorausgewählt): was hier eingetippt wurde,
  soll beim nächsten Mal nicht wieder eingetippt werden müssen.

### 2.4 Bei CP/M geschieht nichts davon — entschieden

**Der ganze Abschnitt 2.2 gilt nur für die UDOS-Familie.** Bei CP/M wird ohne
Angaben weder abgebrochen noch gefragt noch gewarnt: die Datei wird **ohne Rückfrage**
im **Nutzerbereich 0 ohne Attribute** angelegt.

Der Unterschied ist kein Zugeständnis an die Bequemlichkeit, sondern folgt aus der
Sache. Bei UDOS *fehlen* die Angaben — ohne Typ und Satzlänge lässt sich der
Kopfsektor nicht sinnvoll füllen, und was entsteht, ist eine Datei, die nicht läuft.
Bei CP/M gibt es die Angaben so gar nicht: Nutzerbereich 0 und „keine Attribute" sind
kein Notbehelf, sondern der **Normalfall**, den auch das echte CP/M erzeugt, wenn eine
Datei neu angelegt wird. Es gibt nichts zu erraten und nichts zu beklagen.

Dazu kommt, dass beides **nachträglich erreichbar** ist: Rechtsklick auf die Datei in
der Diskettenliste → *Eigenschaften* (`act_eigenschaften` → `PropertiesDialog`) setzt
Nutzerbereich und die drei Attributbits; auf der Kommandozeile tut es
`attr <abbild> <datei> --user 3 --ro --sys`. Ein Dialog beim Einfügen würde also nur
eine Frage vorziehen, die in neun von zehn Fällen niemand hat — und `put datei.txt`
auf eine CP/A-Diskette ist der häufigste Vorgang, den dieses Werkzeug überhaupt kennt.

Ein `--strict`-Schalter ist ausdrücklich **nicht** vorgesehen: er wäre ein Schalter für
einen Fall, den es nicht gibt.

### 2.5 Wenn ein `.fileinfo` selbst eingefügt werden soll

Sobald neben jeder Datei ein `.fileinfo` liegt, ist der Ordner voll davon — und damit
wird die Frage praktisch, was beim Einfügen mit diesen Dateien geschieht. Sie hat zwei
Antworten, je nachdem, ob der Anwender sie *gemeint* haben kann.

**Mehrere Dateien ausgewählt (Ordner, Mehrfachauswahl): stillschweigend überspringen.**
Wer einen ganzen Ordner auf die Diskette zieht, meint dessen *Inhalt* — die
Zubehördateien sind für ihn nicht sichtbar Teil davon. Sie werden **ausgewertet**
(genau dafür sind sie da) und **nicht kopiert**. Eine Rückfrage wäre hier eine Plage:
bei dreißig Dateien käme sie dreißigmal, und die Antwort ist jedes Mal dieselbe.

Zwei Dinge, die dabei gesagt werden müssen:

* Die Zusammenfassung nennt die Zahl — „24 Dateien eingefügt, 24 `.fileinfo`
  ausgewertet". Stillschweigend heißt nicht heimlich; wer nachzählt, soll die
  Differenz erklärt bekommen.
* Bleibt nach dem Überspringen **nichts** übrig (ein Ordner, in dem nur Zubehör
  liegt), ist das eine Meldung und kein stiller Erfolg: „Der Ordner enthält nur
  `.fileinfo`-Dateien — es gibt nichts einzufügen."

**Eine einzelne Datei ausgewählt, und sie ist ein `.fileinfo`: nachfragen.** Das ist
fast sicher ein Versehen — jemand hat in der Dateiauswahl die falsche der beiden
gleichnamigen Zeilen erwischt. Stillschweigend zu überspringen wäre hier falsch: der
Anwender bekäme keine Datei auf die Diskette und keinen Grund dafür. Also ein
Abfragedialog:

```
Das ist eine .fileinfo-Datei

„ACTIVATE.fileinfo" enthält die Kopfsektorangaben zu „ACTIVATE" — vermutlich
ist nicht sie gemeint, sondern die Datei daneben.

Trotzdem als Datei auf die Diskette kopieren?         [Ja]  [Nein]
```

`Nein` ist die Vorgabe (der voreingestellte Knopf). Es *gibt* einen legitimen Grund
für `Ja` — etwa jemand, der eine Textdatei mit dieser Endung wirklich auf die Diskette
bringen will —, und deshalb ist es eine Frage und kein Verbot.

**In der Kommandozeile** gibt es keinen Dialog, also gilt dieselbe Aussage als
Verweigerung mit Ausweg:

```
Fehler: 'ACTIVATE.fileinfo' ist eine Angabendatei, keine Nutzdatei — gemeint ist
        vermutlich 'ACTIVATE'.  Mit --force wird sie trotzdem kopiert.
```

Beim Ordner-`put` (`insertAll`) wird dagegen wie in der Oberfläche stillschweigend
übersprungen und gezählt.

**Für die beiden Sammelbeiblätter gilt dasselbe.** `udos-dateiangaben.txt` und
`cpm-dateiangaben.txt` werden heute schon von `istBeiblatt()` aus dem Stapel
genommen; die Einzelabfrage bekommen sie mit derselben Begründung.

---

---

## 3. Umsetzung

| # | Was | Wo |
|---|---|---|
| 1 | `FileAngaben` als gemeinsames Modell (Vereinigung von `UdosAngabe` und `CpmAngabe`), Schreiber und Leser für die Zeilenform | `core/filesystem/disk_volume.cpp` (Namensraum um `schreibeBeiblatt`/`leseBeiblatt`) |
| 2 | `.fileinfo` in `DiskVolume::extract` schreiben (damit auch in `extractAll`, `recoverExtract` und der Oberfläche) | `disk_volume.cpp` |
| 3 | `istBeiblatt()` um `.fileinfo` erweitern — sonst wandert es als Nutzdatei auf die Diskette | `disk_volume.cpp` |
| 4 | Rangfolge in `DiskVolume::insert`: `.fileinfo` → Sammelbeiblatt → Aufrufer | `disk_volume.cpp` |
| 4a | Stapel überspringt Zubehördateien und **zählt sie** („24 eingefügt, 24 ausgewertet"); leerer Rest ist eine Meldung (§2.5) | `disk_volume.cpp` (`insertAll`) |
| 4b | Einzelnes Einfügen einer Zubehördatei: eigener Grund „ist eine Angabendatei", damit CLI und Oberfläche verschieden darauf antworten können | `disk_volume.h`, C-ABI |
| 5 | Neuer Rückgabewert/Grund „Angaben fehlen" (nicht bloß `false`) — die Oberfläche muss den Fall vom gewöhnlichen Fehler unterscheiden können | `disk_volume.h`, C-ABI `k1520d_insert_*` |
| 6 | `cmd_put`: Abbruch mit der Meldung aus §2.2; Verweigerung mit `--force`-Ausweg für eine einzelne Zubehördatei (§2.5) | `tools/k1520disktool.cpp` |
| 7 | Eingabedialog (§2.3) + Rückfrage bei einer einzelnen Zubehördatei (§2.5), verdrahtet in `_einfuegen_pfade` | `app/disktool/ui/fileinfo_dialog.py`, `main_window.py` |
| 8 | Bedienungsanleitung: `.fileinfo` erklären, den Fallstrick benennen | `tools/k1520disktool.md`, `doc/design/13_k1520disktool.md` §13a |

### 3.1 Wächter

Ohne diese Tests wäre der Fehler in einem Jahr wieder da — er *war* schon einmal da,
und niemand hat es bemerkt, weil nur der Dateiinhalt verglichen wurde.

* `DiskToolFileinfo.EinzelneProgrammdateiUeberlebtDenRundlauf` — genau der Fall aus §1:
  `extract` einer Datei vom Typ P, `insert` auf eine zweite Diskette, danach **alle**
  Kopfsektorfelder vergleichen. Er muss mit dem heutigen Stand **fehlschlagen**.
* `DiskToolFileinfo.JedeExtrahierteDateiHatEinFileinfo` — über alle Dateien einer
  UDOS- und einer CP/M-Fixture, auch bei `extractAll`.
* `DiskToolFileinfo.FileinfoSchlaegtSammelbeiblatt` — beide vorhanden, verschiedene
  Werte: das `.fileinfo` gewinnt.
* `DiskToolFileinfo.FileinfoLandetNichtAlsDateiAufDerDiskette` — `put <ordner>` mit
  `.fileinfo`-Dateien darin.
* `DiskToolFileinfo.StapelUeberspringtZubehoerUndZaehltEs` — Ordner mit `.fileinfo`
  einfügen: keine davon landet auf der Diskette, alle werden ausgewertet, und die
  Zahl steht in der Zusammenfassung.
* `cli_dt_put_ohne_angaben` — bei UDOS Abbruch mit Exitcode ≠ 0, und die Meldung
  nennt den Ausweg (`--type`).
* `DiskToolFileinfo.CpmBrauchtKeineAngaben` — die Gegenrichtung und ein Wächter gegen
  Übereifer: eine Datei ohne jede Angabe auf eine CP/M-Diskette einfügen **gelingt**,
  landet im Nutzerbereich 0 ohne Attribute und erzeugt **keine** Meldung (§2.4).
* `cli_dt_put_fileinfo_einzeln` — Verweigerung mit Exitcode ≠ 0 und dem Hinweis auf
  die gemeinte Datei; mit `--force` geht es durch.
* `py_disktool_gui`: Eingabedialog geht auf, blendet ENTRY/Segmente/Speicher bei Typ
  `A` ab, „für alle übernehmen" fragt nur einmal — und die Rückfrage aus §2.5
  erscheint bei **einer** ausgewählten `.fileinfo`, aber **nicht** bei einer
  Mehrfachauswahl.

---

## 4. Was NICHT geändert wird

* **Das Sammelbeiblatt bleibt.** Es ist der Weg für den Vollexport, es steht in der
  Bedienungsanleitung, und vorhandene Ordner müssen weiter funktionieren. `.fileinfo`
  tritt daneben, nicht an seine Stelle.
* **Die Vorgaben von `insert` bleiben, wie sie sind** (Typ A/B, 128er Sätze). Sie
  gelten künftig nur dort, wo der Aufrufer sie ausdrücklich will — nicht mehr
  stillschweigend.
* **Am CP/M-Einfügen ändert sich nichts.** Kein Abbruch, keine Rückfrage, kein
  Hinweis (§2.4). Das `.fileinfo` entsteht dort trotzdem und wird gelesen, wenn es da
  ist — es nimmt nur niemandem etwas weg, wenn es fehlt.
* **Die Dateisystemprüfung wird nicht erweitert.** Sie kann den Schaden grundsätzlich
  nicht sehen (§1.2); ihn zu erraten hieße, jede als `B` eingetragene Datei zu
  verdächtigen, die wie Z80-Code aussieht. Das wäre genau die Falschmeldung, die der
  Prüfentwurf als schlimmer einstuft als eine fehlende (E10).
