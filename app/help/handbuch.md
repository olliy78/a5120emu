# a5120emu — Kurzhandbuch

Dieses Programm ist ein Emulator des Bürocomputers **robotron A5120** und seiner
Verwandten am K1520-Bus. Nachgebildet werden **Bus und Steckkarten** — Z80,
Speicher, Bildschirmkarte, Tastatur und Diskettensteuerung; der Z80-Code von
Boot-ROM, BIOS und Betriebssystem läuft darin **unverändert**. Es gibt deshalb
keine eingebauten Abkürzungen und keine Betriebssystem-Nachbauten: was auf der
eingelegten Diskette steht, startet so, wie es 1985 gestartet ist — CP/A, SCPX,
UDOS.

Zum Dateiaustausch mit den Disketten gibt es das Schwesterprogramm
**k1520DiskTool**; es hat sein eigenes Handbuch.

## Der übliche Weg

1. **Starten.** Das Fenster kommt hoch, die Maschine ist bereits eingeschaltet
   und startet kalt — mit den Disketten, die beim letzten Mal eingelegt waren.
   Beim allerersten Start ist noch keine eingelegt; dann bleibt der Bildschirm
   nach dem Boot-ROM stehen.
2. **Diskette einlegen** — *Datei ▸ Diskette einlegen ▸ Laufwerk A:* oder der
   Knopf *Mount* im Kasten „Laufwerke". Angenommen werden `.hfe`, `.dmk`
   und `.img`.
3. **Kaltstart.** Eine frisch eingelegte Diskette bootet nicht von selbst — der
   Rechner sieht sie erst beim nächsten Start: *Maschine ▸ Rückstellen*
   (Strg+Umschalt+R).
4. **Arbeiten.** Ein Klick in die Bildröhre, dann tippen: die echte Tastatur
   bedient den emulierten Rechner. Die Bildschirmtastatur (*Ansicht ▸ Tastatur*)
   zeigt mit und nimmt auch Mausklicks an.
5. **Alles wird gemerkt.** Fenstergröße (auch „maximiert"), die Aufteilung
   zwischen den Kästen, die Symbolleiste, die Bildröhre, die
   Laufwerksbestückung und die eingelegten Disketten stehen beim nächsten Start
   wieder so da. Von Hand speichern muss man nur, wer **mehrere** Einrichtungen
   nebeneinander führen will (*Datei ▸ Konfiguration speichern…*).

## Das Fenster

**Bildröhre** — die Nachbildung des Bildschirms, samt Nachleuchten, Zeilenraster
und Wölbung. Sie hat den Tastaturfokus: was man tippt, geht an den emulierten
Rechner, nicht an die Oberfläche. Doppelklick oder F11 macht sie zum Vollbild,
Esc kommt zurück.

**Tastatur** — eine maßstäbliche Nachbildung der K7637. Sie zeigt an, welche
Taste die echte Tastatur gerade anspricht, und trägt die Anzeigen des Rechners
(Feststeller, Betriebsanzeige). Beim ersten Start ist sie zugeklappt.

**Laufwerke** — je bestücktem Steckplatz ein Kasten: Leuchte, Dateiname,
Schreibschutz, Format, und die Knöpfe zum Einlegen, Anlegen, Speichern unter und
für die echte Diskette.

**Einstellungen** — drei Reiter: *Allgemein* (Takt), *Laufwerke*
(welcher Laufwerkstyp in welchem Steckplatz steckt) und *CRT* (das Aussehen der
Bildröhre).

Jeder dieser Kästen lässt sich zuklappen, herausziehen und woanders andocken;
*Ansicht* holt ihn zurück.

**Symbolleiste** — die Abkürzung für die häufigen Wege; ausblendbar unter
*Ansicht ▸ Symbolleiste* und einrichtbar (siehe unten). **Im Menü steht immer
alles** — was man aus der Leiste wirft, bleibt erreichbar.

**Statuszeile** — links die letzte Meldung, rechts der Zustand der Maschine:
Takt, und je Laufwerk eine Leuchte mit dem Namen der eingelegten Diskette.

## Die Symbolleiste einrichten

*Ansicht ▸ Symbolleiste einrichten…* öffnet eine Liste: angehakt heißt „steht in
der Leiste", und die Reihenfolge der Liste ist die der Leiste — Einträge lassen
sich mit der Maus verschieben. *Trennstrich einfügen* setzt eine Lücke zwischen
zwei Gruppen, *Standard* stellt die Leiste des ersten Starts wieder her.

Ob die Knöpfe Symbol, Text oder beides tragen, steht unter *Ansicht ▸
Symbolleistenstil*. Beides — Inhalt und Stil — wird mit der Konfiguration
gemerkt.

## Die Statuszeile lesen

**Takt** — der eingestellte Takt der Maschine, wortgleich mit dem Auswahlfeld
unter *Einstellungen ▸ Allgemein*: `2,45 MHz` (Echtzeit), `2 × 2,45 MHz`,
`5 × 2,45 MHz`, `10 × 2,45 MHz` oder `unbegrenzt`. Was der Wirtsrechner davon
tatsächlich hält, steht im **Tooltip** („Gemessen: 9,8 × 2,45 MHz") — samt dem
Hinweis, wenn er nicht mitkommt. Als Anzeige taugte der gemessene Wert nicht:
er schwankt von Sekunde zu Sekunde und liest sich wie ein Fehler, wo keiner ist.

**Je Laufwerk eine Leuchte und ein Feld:**

| Leuchte | Bedeutung |
|---------|-----------|
| leerer Kreis | keine Diskette im Laufwerk |
| schwarzer Kreis | Diskette eingelegt |
| roter Kreis | es wird gerade gelesen oder geschrieben |

Daneben `A: cpa780.hfe  R/W`: Buchstabe, Name der eingelegten Abbilddatei und ob
die Maschine darauf schreiben darf (`R/W`) oder nicht (`R/O`). Der volle Pfad
steht im Tooltip. Ein nicht bestückter Steckplatz bekommt weder Leuchte noch
Feld. So sieht man den Diskettenzugriff auch bei zugeklapptem Laufwerkskasten.

Zykluszähler und Bildrate standen hier früher. Beide sagen über die Maschine
nichts, was man beim Arbeiten wissen will.

## Ein- und Ausschalten, Rückstellen

**Einschalten ist immer ein Kaltstart**: das Boot-ROM wird wieder eingeblendet,
alle Disketten neu eingelegt, der Rechner läuft vom Anfang an. Das ist der
Unterschied zum echten Netzschalter nicht — beim A5120 war es genauso.

**Rückstellen** (Strg+Umschalt+R) ist das systemweite `/RESET`: Neustart vom
Boot-ROM, ohne dass der Rechner zwischendurch aus war. Das ist der Weg, nachdem
man eine Diskette gewechselt hat.

**Ausschalten** hält den Lauf an und macht die Röhre dunkel. Änderungen an einer
Diskette sind da längst in der Datei — zurückgeschrieben wird kurz nach jedem
Schreibzugriff, nicht erst beim Beenden.

## Disketten einlegen und auswerfen

Über *Datei ▸ Diskette einlegen ▸ Laufwerk …* oder den Knopf im Laufwerkskasten.
Ein Laufwerk nimmt nur eine Diskette: wo schon eine liegt, ist der Menüpunkt
gesperrt — erst auswerfen.

**Das Format** wählt man im Kasten daneben. Bei `.hfe` und `.dmk` ist das eine
Formsache — diese Behälter tragen ihre Geometrie selbst. Ein rohes Sektorabbild
(`.img`) trägt sie **nicht**; dort entscheidet die Wahl darüber, ob die Diskette
lesbar ist. Passt genau ein Katalogformat zur Dateigröße, wird es vorgeschlagen.

**Schreibschutz** — der Haken *Write-Protect* wirkt sofort, auch bei laufender
Maschine, und steht in der Statuszeile als `R/O`. Bei einer unersetzlichen
Diskette ist er die billigste Versicherung.

**Änderungen gehen in die Datei zurück**, und zwar von selbst: kurz nach der
letzten Schreibbewegung (eine Schreibpause von etwa einer halben Sekunde
Maschinenzeit). Ein Formatierlauf schreibt die Datei deshalb einmal am Ende neu
und nicht einmal je Spur.

## Neue Disketten und „Speichern unter"

*Neue Diskette* legt eine **echte Leerdiskette** an — unformatiert, in der
Geometrie des Laufwerks. Genau das ist der Anwenderfall: das Gastsystem
formatiert sie selbst (`FORMAT.COM` unter CP/A, ebenso UDOS). Dafür braucht es
einen Behälter, der „unformatiert" ausdrücken kann, also `.hfe` oder `.dmk`. Ein
rohes `.img` kennt diesen Zustand nicht; wählt man es, wird vorformatiert
angelegt.

*Speichern unter…* schreibt die eingelegte Diskette unter neuem Namen — auch in
einen **anderen Behälter** (`.img` → `.hfe`, `.hfe` → `.dmk`) — und arbeitet ab
dann dort weiter. `.img` wird dabei verweigert, sobald die Diskette etwas trägt,
was ein Sektorabbild nicht darstellen kann: eine unformatierte Spur oder Daten
hinter der Datenprüfsumme (UDOS legt dort seine Verkettung ab).

## Laufwerke bestücken

*Einstellungen ▸ Laufwerke* — vier Steckplätze der K5122, je einer wählbar:

| Typ | Was |
|-----|-----|
| K5601 | 5,25″ zweiseitig, 80 Spuren, MFM, 800K — die Standardbestückung |
| K5600.10 | 5,25″ einseitig, 40 Spuren, 200K |
| K5600.20 | 5,25″ einseitig, 80 Spuren, 400K |
| MF3200 | 8″ einseitig, 77 Spuren, nur FM, 300K |
| MF6400 | 8″ einseitig, 77 Spuren, FM und MFM, 600K |
| kein Laufwerk | der Steckplatz bleibt leer |

Der A5120 kam mit drei K5601 (A:, B:, C:); der vierte Platz war frei. Eine
Änderung baut die Maschine neu auf und startet sie kalt — der Laufwerkstyp steht
im Kern fest, sobald die Maschine läuft.

## Wenn die Diskette nicht zum Laufwerk passt

Sie wird trotzdem eingelegt — und **übersetzt**. 5,25″ gibt es mit 48 tpi (40
Spuren) und 96 tpi (80); ein Hinweis im Laufwerkskasten sagt dann, was gilt:

* *Double Step aktiviert* — 40-Spur-Diskette im 80-Spur-Laufwerk. Das Gastsystem
  muss sie schrittverdoppelt lesen, also im Betriebssystem einen
  40-Spur-Laufwerkstyp einstellen.
* *Laufwerk liest nur jede zweite Spur* — 80-Spur-Diskette im 40-Spur-Laufwerk.
  Bei einer einseitig im Doppelschritt beschriebenen Diskette ist das genau
  richtig.
* *Nur Seite 0 verwendbar* — zweiseitige Diskette im einseitigen Laufwerk.

Das ist kein Fehler, deshalb steht es im Kasten und nicht in einem
Meldungsfenster.

## Eine echte Diskette am Greaseweazle

Mit einem [Greaseweazle](https://github.com/keirf/greaseweazle)-Adapter und einem
echten Laufwerk lässt sich eine **echte Diskette** einlegen: Knopf *Physisch…*
im Laufwerkskasten. Gelesen und geschrieben wird **spurweise nach Bedarf** — der
Umweg „erst die ganze Diskette in eine Datei" entfällt, und der Rechner bootet
von der eingelegten Scheibe.

Drei Dinge, die man wissen sollte:

* **Schreibgeschützt, bis man widerspricht.** Ein Fehler kostet hier nicht eine
  Kopie, sondern die einzige noch existierende Diskette.
* **Geschrieben gilt erst nach dem Zurücklesen.** Jede geschriebene Spur wird
  sofort wieder gelesen und verglichen; der Füllstand steht im Laufwerkskasten.
* **Eine Spur, die sich nicht schreiben lässt**, meldet sich mit einem Fenster.
  Dann liegt das Abbild im Speicher noch heil, die Diskette nicht mehr — der
  Ausweg ist *Diskette neu beschreiben* auf eine frische Scheibe.

Das Paket `greaseweazle` ist eine freiwillige Zutat; fehlt es, sagt der Knopf,
woran es liegt.

## Der Takt der Maschine

*Einstellungen ▸ Allgemein ▸ Takt*: `2,45 MHz` — das ist der Takt des echten
A5120 — sowie `2 ×`, `5 ×` und `10 × 2,45 MHz` und *unbegrenzt* (so schnell, wie
der Wirtsrechner kann).

`2,45 MHz` ist das, was die Uhr des Gastsystems erwartet — sie zählt Taktzyklen
und geht bei jedem Vielfachen entsprechend falsch. Schneller ist gut, um einen
Kaltstart abzukürzen; wer die Uhr braucht, bleibt beim Nenntakt.

Der eingestellte Takt steht links in der Statuszeile, der **gemessene** in
dessen Tooltip.

## Die Bildröhre einstellen

*Einstellungen ▸ CRT*: Leuchtfarbe, Helligkeit, Kontrast, Wölbung, Rundung der
Ecken, Bildlage und -größe. Es sind dieselben Regler, die eine echte Röhre
hinten hatte, und sie wirken sofort. *Zurücksetzen* stellt die Vorgabe her.

Zwei davon sind mehr als Geschmack: **Wölbung** und **Bildgröße** entscheiden
darüber, ob die Ecken des Textbilds noch im sichtbaren Bereich liegen.

## Die Tastatur

Die echte Tastatur bedient den emulierten Rechner — sie geht durch die
Nachbildung hindurch, die dabei mitzeigt, welche K7637-Taste angesprochen wird.
Die Zuordnung folgt der **Position**: die Taste an der Stelle, an der sie auf dem
PC liegt, spricht die K7637-Taste an derselben Stelle an.

Weil jeder Tastendruck dem Gast gehört — **auch** `Strg+C`, `Strg+S`, `Strg+P`
und die Funktionstasten, die CP/M braucht —, trägt jede Bedienung des Fensters
`Strg+Umschalt`. Die einzige Ausnahme ist F11 (Vollbild).

## Konfiguration — was gemerkt wird und wo

Alles, was man einstellt, landet fortlaufend in
`~/.config/k1520emu/config.yaml` (Windows: `%APPDATA%\K1520emu`): Bildröhre,
Takt, Laufwerksbestückung, eingelegte Disketten, Größe und Lage des Fensters
(auch „maximiert"), die Lage und Breite der Kästen samt der Trennlinien
dazwischen, und der Inhalt der Symbolleiste.

*Datei ▸ Konfiguration speichern…* legt dieselbe Datei woanders ab — für mehrere
Einrichtungen nebeneinander (etwa „CP/A mit drei Laufwerken" und „UDOS mit 8″").
*Konfiguration laden…* holt sie zurück, startet die Maschine kalt und übernimmt
die geladene Einrichtung als die neue laufende.

## Wo die Dateien liegen

| Ordner | Wofür |
|--------|-------|
| `K1520emu/Disketten` (im Dokumentenordner) | die Abbilder |
| `~/.config/k1520emu/config.yaml` | die Konfiguration |

Beim ersten Start nach einer Installation werden die mitgelieferten
Beispieldisketten dorthin ausgepackt. Verschieben lässt sich das mit den
Umgebungsvariablen `K1520_DATA` (Arbeitsordner) und `K1520_DISKS` (nur die
Abbilder). Wo das Programm gerade sucht, sagt `a5120emu --paths`.

## Von der Kommandozeile

```
a5120emu [DISKETTE …]     bis zu vier Abbilder, in Laufwerksreihenfolge A: B: C: D:
a5120emu --paths          aufgelöste Pfade zeigen
a5120emu --help           Kurzhilfe
```

Die genannten Disketten liegen **beim Kaltstart schon im Laufwerk** — die
Maschine bootet also von der ersten. Sie ersetzen die gemerkte Belegung nur für
diesen Lauf; gespeichert wird davon nichts.

Ohne Oberfläche fährt dieselbe Maschine unter `k1520dbg` (Fehlersuche,
Skriptbetrieb); `k1520dbg DISKETTE --console` ist die Konsolenfassung.

## Tastenkürzel

| Kürzel | Wirkung |
|--------|---------|
| F11 | Vollbild ein/aus (Esc verlässt es ebenfalls) |
| Strg+Umschalt+O | Konfiguration laden |
| Strg+Umschalt+S | Konfiguration speichern |
| Strg+Umschalt+P | Rechner ein- oder ausschalten |
| Strg+Umschalt+R | Rückstellen (Neustart vom Boot-ROM) |
| Strg+Umschalt+H | Dieses Handbuch |
| Strg+Umschalt+Q | Beenden |

Alle übrigen Tasten gehören dem emulierten Rechner.

## Wenn etwas nicht geht

**Der Bildschirm bleibt nach dem Einschalten dunkel oder zeigt nur das
Boot-ROM.** Es liegt keine bootfähige Diskette in A:. Einlegen, dann
*Rückstellen*.

**Die neu eingelegte Diskette wird nicht gefunden.** Ein laufendes
Betriebssystem sieht den Wechsel nicht von selbst — *Rückstellen* (oder beim
Gastsystem einen Warmstart auslösen).

**Ein `.img` liest sich als Müll.** Das rohe Sektorabbild trägt seine Geometrie
nicht; im Laufwerkskasten das passende Format wählen und neu einlegen. `.hfe`
und `.dmk` haben dieses Problem nicht.

**Der Tooltip am Takt meldet, dass der Wirtsrechner nicht mitkommt.** Meist, weil die
Bildröhre mit allen Effekten auf einer großen Fläche gezeichnet wird. Fenster
kleiner machen oder unter *CRT* die aufwendigen Regler zurücknehmen.

**Die Tastatur tippt ins Leere.** Der Fokus liegt nicht auf der Röhre: einmal
hineinklicken. (Normalerweise holt das Fenster ihn von selbst zurück.)

**Gar nichts startet.** `a5120emu --paths` sagt, wo das Programm die
Kernbibliothek und den Formatkatalog sucht — das ist die erste Frage, wenn
etwas nicht gefunden wird.
