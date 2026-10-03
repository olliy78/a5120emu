# a5120emu / k8915emu / prg710emu — Kurzhandbuch

Dieses Programm ist ein Emulator des Bürocomputers **robotron A5120** und seiner
Verwandten am K1520-Bus. Es gibt ihn in drei Gestalten: den **A5120 Emulator**
(`a5120emu`), den **K8915 Emulator** (`k8915emu`) und den **PRG710 Emulator**
(`prg710emu`) — dasselbe Programm mit eigener Konfiguration und eigener Tastatur;
was nur den K8915 bzw. den PRG betrifft, steht in den Abschnitten „Der K8915
Emulator" und „Der PRG710 Emulator". Nachgebildet werden **Bus und Steckkarten** — Z80,
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
Schreibschutz, das erkannte Format, und die Knöpfe zum Einlegen, Anlegen,
Speichern unter und für die echte Diskette.

**Einstellungen** — vier Reiter: *Allgemein* (Takt), *Laufwerke*
(welcher Laufwerkstyp in welchem Steckplatz steckt), *Schnittstellen* (die seriellen
Anschlüsse des Rechners nach außen, ein Block je Schnittstelle — siehe *Serielle
Schnittstellen* unten) und *CRT* (das Aussehen der Bildröhre).

Jeder dieser Kästen lässt sich zuklappen, herausziehen und woanders andocken;
*Ansicht* holt ihn zurück.

**Symbolleiste** — die Abkürzung für die häufigen Wege; ausblendbar unter
*Ansicht ▸ Symbolleiste* und einrichtbar (siehe unten). **Im Menü steht immer
alles** — was man aus der Leiste wirft, bleibt erreichbar.

**Statuszeile** — links die letzte Meldung, rechts der Zustand der Maschine:
Takt, und je Laufwerk eine Leuchte mit dem Namen der eingelegten Diskette.
Dazu kommen — **nur wenn es etwas zu sagen gibt** — die seriellen Schnittstellen.

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
| leerer Kreis | keine Diskette im Laufwerk, kein Zugriff |
| schwarzer Kreis | Diskette eingelegt |
| roter Kreis | das Laufwerk ist angesprochen |

Rot leuchtet auch ein **leeres** Laufwerk, sobald es angesprochen wird — genau
wie die Leuchte am echten Gerät. Das ist die Auskunft, auf die es dann ankommt:
das Gastsystem wartet auf eine Diskette, die niemand eingelegt hat.

Daneben `A: cpa780.hfe  R/W`: Buchstabe, Name der eingelegten Abbilddatei und ob
die Maschine darauf schreiben darf (`R/W`) oder nicht (`R/O`). Der volle Pfad
steht im Tooltip. Ein nicht bestückter Steckplatz bekommt weder Leuchte noch
Feld. So sieht man den Diskettenzugriff auch bei zugeklapptem Laufwerkskasten.

**Serielle Schnittstellen** — zwei Felder hinter dem Takt, die nur erscheinen,
wenn sie etwas zu sagen haben: `Telnet/RFC2217 Server Port: 5000` (auf welchen Ports
ein Server tatsächlich lauscht — das ist nicht immer der eingestellte, siehe unten)
und `V.24 verbunden, Drucker verbunden` (welche Schnittstellen eine Verbindung
haben). Ein Client, der noch versucht, und eine Ausgabe in eine Datei erscheinen dort
nicht; sie stehen im Block. Der Tooltip nennt Protokoll und Gegenstelle.

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

**Nach dem Format wird gefragt — aber nur bei `.img`.** Ein `.hfe` oder `.dmk`
trägt seine Geometrie selbst; da gibt es nichts zu wählen. Ein rohes
Sektorabbild enthält dagegen nur die Sektorinhalte, und wie sie sich auf Spuren
verteilen, steht nirgends darin. Deshalb geht nach der Dateiauswahl ein Dialog
auf. Vorgeschlagen wird das Katalogformat, dessen Abbildgröße genau zur Datei
passt — das einzige Merkmal, das ein `.img` über sich selbst preisgibt.

**Hinter „Format:" steht der Befund**, nicht eine Einstellung: der Emulator
vermisst die eingelegte Diskette und hält das Gemessene gegen den Formatkatalog.
Steht dort **unbekannt**, passt kein Eintrag — oder es passen zwei gleich gut.
Lesen und Schreiben geht trotzdem, der Diskettencontroller arbeitet
formatunabhängig; nur als `.img` lässt sich so eine Diskette nicht speichern
(siehe unten). Der Befund zieht im laufenden Betrieb nach: formatiert das
Gastsystem die Diskette um, steht dort kurz darauf das neue Format.

**Schreibschutz** — der Haken *Write-Protect* wirkt sofort, auch bei laufender
Maschine, und steht in der Statuszeile als `R/O`. Bei einer unersetzlichen
Diskette ist er die billigste Versicherung.

**Änderungen gehen in die Datei zurück**, und zwar von selbst: kurz nach der
letzten Schreibbewegung (eine Schreibpause von etwa einer halben Sekunde
Maschinenzeit). Ein Formatierlauf schreibt die Datei deshalb einmal am Ende neu
und nicht einmal je Spur.

## Leere Disketten und „Speichern unter"

*Leere Diskette* legt eine **echte Leerdiskette** an — unformatiert, in der
Geometrie des Laufwerks. Genau das ist der Anwenderfall: das Gastsystem
formatiert sie selbst (`FORMAT.COM` unter CP/A, ebenso UDOS). Dafür braucht es
einen Behälter, der „unformatiert" ausdrücken kann, also `.hfe` oder `.dmk`. Ein
rohes `.img` kennt diesen Zustand nicht; wählt man es, wird vorformatiert
angelegt — und dann wird nach dem Format gefragt.

*Speichern unter…* schreibt die eingelegte Diskette unter neuem Namen — auch in
einen **anderen Behälter** (`.img` → `.hfe`, `.hfe` → `.dmk`) — und arbeitet ab
dann dort weiter. Ausgegeben wird im **erkannten** Format; gefragt wird hier
nicht. `.img` ist aus zwei Gründen verwehrt: wenn die Diskette etwas trägt, was
ein Sektorabbild nicht darstellen kann (eine unformatierte Spur, oder Daten
hinter der Datenprüfsumme — dort legt UDOS seine Verkettung ab), und wenn das
Format **unbekannt** ist. Im zweiten Fall wäre die Sektorreihenfolge des Abbilds
geraten, und ein geratenes Abbild sieht heil aus und ist es nicht.

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

* **Zwei Schlösser.** Der Haken *Auf die echte Diskette schreiben* im Dialog
  entscheidet, ob überhaupt auf die Scheibe geschrieben werden darf — er ist
  gesetzt, denn im Emulator wird die Diskette benutzt und nicht angesehen. Nimmt
  man ihn heraus, liegt sie schreibgeschützt im Laufwerk. Das zweite Schloss ist
  der Haken *Write-Protect* im Laufwerkskasten: er wirkt wie die Kerbe am Rand
  der echten Diskette, sofort und auch bei laufender Maschine, das Gastsystem
  meldet dann „schreibgeschützt" — und weil so gar keine geänderte Spur entsteht,
  geht auch nichts an den Adapter. Bei einer unersetzlichen Diskette ist er die
  billigste Versicherung; in der Pfadzeile steht, was gerade gilt.
* **Geschrieben gilt erst nach dem Zurücklesen.** Jede geschriebene Spur wird
  sofort wieder gelesen und verglichen; der Füllstand steht im Laufwerkskasten.
* **Eine Spur, die sich nicht schreiben lässt**, meldet sich mit einem Fenster.
  Dann liegt das Abbild im Speicher noch heil, die Diskette nicht mehr — der
  Ausweg ist *Diskette neu beschreiben* auf eine frische Scheibe.

Das Paket `greaseweazle` ist eine freiwillige Zutat; fehlt es, sagt der Knopf,
woran es liegt.

## Serielle Schnittstellen

Die seriellen Anschlüsse des Rechners lassen sich mit der Außenwelt verbinden: mit
einem Terminalprogramm, mit einem anderen Rechner im Netz, mit einem zweiten Emulator
oder — über einen Adapter — mit einem echten seriellen Gerät wie einem Drucker. Was
der Rechner sendet, geht über das Netz hinaus, was ankommt, landet im Empfänger seiner
Schnittstellenkarte; das Programm im Rechner merkt davon keinen Unterschied zu einem
Kabel.

Eingestellt wird das im Reiter **Schnittstellen** im Kasten *Einstellungen*
(*Ansicht ▸ Einstellungen*). Je Schnittstelle gibt es einen Block; **die Namen und was
sie können, kommen vom Rechner** — so, wie sie am Gerät beschriftet sind:

| Rechner | Schnittstellen |
|---------|----------------|
| A5120 (Anschlusssteuerung K8025) | DFÜ/V.24, DFÜ/IFSS, Drucker |
| K8915 (Anschlusssteuerung K7028) | Drucker/IFSS1 an X3, V.24 an X4, DFÜ/IFSS2 an X5 |

**V.24** ist die Spannungsschnittstelle mit Steuerleitungen (RTS, CTS, DTR, DSR, DCD),
**IFSS** die Stromschleife des DDR-Standards — sie kennt nur Senden und Empfangen. Die
Tastatur steht als Zeile „fest verdrahtet" darunter; an ihr gibt es nichts zu stellen.

### K8915: den Prüfstecker gesteckt lassen

Der Selbsttest im Boot-ROM des K8915 prüft beim Einschalten auch die drei seriellen
Schnittstellen: er sendet auf jeder ein Zeichen und erwartet, dass **dasselbe Zeichen
zurückkommt** — am Gerät steckt dafür ein Prüfstecker bzw. ist die Stromschleife
geschlossen. Im Emulator übernimmt das der Schalter **Rx/Tx-Loop**, und der ist beim
K8915 deshalb **von Anfang an gesetzt**.

Fehlt das Echo, meldet der Selbsttest einen **SIO-Fehler** (Kennbuchstabe in der
Diagnosezeile), und der Rechner **wartet auf eine Taste**:
**RETURN** setzt den Start fort (`#` wiederholt den Selbsttest). Das ist harmlos — das
Betriebssystem selbst braucht das Echo nicht —, kostet aber bei jedem Kaltstart einen
Tastendruck.

Empfehlung: den Loop an allen Schnittstellen gesetzt lassen, die nicht verbunden
werden, und ihn nur an der **einen** abschalten, die man braucht. Weil der Zustand des
Schalters gemerkt wird, kommt die Meldung dann bei jedem Kaltstart wieder; wer das nicht
will, setzt den Loop vor dem Rückstellen wieder und schaltet ihn nach dem Start ab.

### Telnet — Terminalprogramme und Rechner im Netz

**Telnet** ist das einfachste Protokoll, um Zeichen über eine Netzverbindung zu
schicken; fast jedes Terminalprogramm spricht es. Damit lässt sich zum Beispiel:

* **sich mit einem Terminalprogramm in den Emulator einwählen** — der Emulator ist
  **Server**, das Terminalprogramm verbindet sich zu ihm: unter Windows etwa
  **PuTTY** (Verbindungsart *Telnet*) oder **Tera Term**, unter Linux und macOS
  `telnet 127.0.0.1 5000`. Was man dort tippt, kommt am Rechner an; was ein Programm im
  Rechner auf die Schnittstelle ausgibt, steht im Terminalfenster. Das Echo macht —
  wie am Gerät — der Rechner, nicht das Terminalprogramm.
* **den Emulator als Terminal an einen anderen Rechner hängen** — der Emulator ist
  **Client** und wählt sich bei einem Telnet-Server ein, etwa einem Linux- oder
  Unix-Rechner mit `telnetd`, einem Mailbox-System oder einem zweiten Emulator. Ein
  Terminalprogramm im emulierten Rechner bedient dann die Gegenstelle.

**Die Steuerleitungen werden bei Telnet NICHT übertragen**, ebenso wenig Baudrate und
Zeichenformat. Was der Rechner auf RTS und DTR setzt, erfährt die Gegenseite nicht, und
die Eingänge CTS, DSR und DCD zeigen nur „verbunden" (aktiv, solange die Verbindung
steht). Für reinen Textaustausch ist das gleichgültig; ein Programm, das auf
Steuerleitungen achtet oder mit ihnen den Datenfluss bremst, braucht RFC 2217.

### RFC 2217 — eine echte V.24 über das Netz

**RFC 2217** (eigentlich „Telnet Com Port Control Option", festgelegt 1997 in der
gleichnamigen Norm RFC 2217 der IETF) ist eine **Erweiterung von Telnet**, die alles
mitnimmt, was eine serielle Leitung außer den Zeichen noch hat:

* die **Übertragungsparameter** — Baudrate, Datenbits, Parität, Stoppbits,
* die **Steuerleitungen** — RTS und DTR in die eine, CTS, DSR, DCD und RI in die andere
  Richtung —, dazu Break und das Leeren der Puffer.

Damit wird über das Netz eine **vollständige V.24 nachgebildet**: ein Programm im
Rechner, das mit RTS/CTS den Datenfluss steuert oder auf DCD wartet, verhält sich wie
am Kabel. Das Protokoll ist kein Allgemeingut, aber verbreitet: viele Netzwerk-Adapter
für serielle Geräte und die gängigen Werkzeuge zum Weiterreichen eines seriellen Ports
ins Netz sprechen es.

Zwei Dinge, die man dazu wissen sollte:

* **Maßgeblich ist der emulierte Rechner.** Baudrate und Format stellt das Programm im
  Rechner an seiner Schnittstellenkarte ein; der Emulator **meldet** sie der Gegenseite.
  Verlangt die Gegenseite andere Werte, wird das nicht übernommen, sondern unter
  *Gegenseite* mit Warnfarbe angezeigt. Ist der Emulator **Client** an einem echten
  Port, stellt sich dieser Port damit auf die Werte des Rechners ein.
* **Die Rolle bestimmt die Verdrahtung.** Als **Client** ist der Emulator ein Gerät an
  einem fernen Port — die Leitungen gehen gerade durch (RTS → RTS). Als **Server**
  spielt er selbst den Port, an den sich die Gegenseite anschließt; die Leitungen werden
  dann **wie in einem Nullmodemkabel gekreuzt** (RTS der Gegenseite → CTS des Rechners,
  DTR → DSR und DCD). Zwei Emulatoren, einer Server und einer Client, sind so über ein
  richtiges Nullmodemkabel verbunden.

### Echte serielle Geräte und Rechner anschließen

Der Emulator spricht keinen COM-Port des PCs direkt an. Stattdessen macht ein kleines
Programm einen **echten Port zum RFC-2217-Server**, und der Emulator verbindet sich als
**RFC2217-Client** dorthin. So lässt sich eine am PC eingebaute serielle Schnittstelle
ebenso anbinden wie ein **USB-Seriell-Adapter** — und daran ein echter Drucker, ein
Modem oder ein echter A5120 bzw. K8915 als Gegenstelle. Brauchbare Programme:

* **Linux:** `ser2net` (der übliche Weg, auch auf einem Raspberry Pi), `socat`
  (Pseudo-Port, der sich zu einem Server des Emulators verbindet).
* **Windows:** `com0com` mit `hub4com` (virtueller Port bzw. Weiterleitung eines echten
  Ports mit RFC 2217); für eine reine Telnet-Verbindung genügen PuTTY oder Tera Term.
* **Beide:** das Beispielprogramm `rfc2217_server.py` von **pyserial** (Python) macht
  aus einem lokalen Port einen RFC-2217-Server.

Es geht auch ohne PC dazwischen: **Netzwerk-Adapter für serielle Geräte** aus der
Industrie (sogenannte Geräteserver) beherrschen
RFC 2217, ebenso Bastelprojekte auf Basis von **Arduino**-Boards mit Netzanschluss
(ESP8266/ESP32) oder eines **Raspberry Pi** mit `ser2net`. Damit steht zum Beispiel ein
**echter serieller Drucker** irgendwo im Netz und druckt, was der emulierte Rechner
ausgibt.

Zwei Hinweise für die Hardware: eine V.24 arbeitet mit ±12 V, die Anschlüsse von
Arduino und Raspberry Pi mit 3,3 V bzw. 5 V — dazwischen gehört ein **Pegelwandler**.
Und eine **IFSS** ist eine Stromschleife (20 mA); für sie braucht es einen
**IFSS-Wandler**, ein V.24-Adapter allein genügt nicht.

### Der Reiter „Schnittstellen" im Einzelnen

**Betriebsart**

* **Telnet** — Zeichen über das Netz, ohne Steuerleitungen (siehe oben).
* **RFC2217** — wie Telnet, mit Baudrate, Format und Steuerleitungen.
* **Datei** — alles, was der Rechner sendet, wird in eine Datei geschrieben (etwa
  für einen Drucker). Beim Umschalten fragt ein Dialog nach dem Namen; **Starten**
  legt die Datei neu an (und überschreibt eine vorhandene).

**Rolle** (nur Telnet/RFC2217): als **Server** wartet der Emulator auf Verbindungen,
als **Client** wählt er sich selbst bei einer Gegenstelle ein (**Host** und **Port**).
Ein Server nimmt eine Verbindung zur Zeit an; jede weitere wird abgewiesen. Ist der
eingestellte Port belegt, nimmt **Starten** den nächsten freien darüber — der
tatsächliche steht im Kopf des Blocks („lauscht auf 5001") und in der Statuszeile.
Solange die Schnittstelle läuft, zeigt auch das (gesperrte) Port-Feld den **tatsächlich
benutzten** Port; nach dem Beenden steht wieder der eingestellte darin, und der wird
auch gespeichert.
Hinter dem Host steht, als was er gelesen wurde (IPv4, IPv6, Hostname oder
**ungültig** — dann bleibt der Knopf gesperrt).

**Der Knopf** heißt im Server *Starten*/*Beenden*, im Client *Verbinden*/*Trennen*.
Ein Client versucht **jede Sekunde** von neuem, bis die Gegenstelle antwortet — auch
nachdem sie die Verbindung getrennt hat. Das hört nur mit *Trennen* auf, und der Knopf
heißt schon während der Versuche so. Der Punkt im Kopf ist grau (aus), gelb (lauscht
oder verbindet), grün (verbunden) oder rot (Fehler); Fehler und Hinweise stehen als
Zeile im Block, nicht in einem Fenster. Solange eine Schnittstelle läuft, sind
Betriebsart, Rolle, Host, Port und Datei gesperrt.

**Weitere Einstellungen**

* **Rx/Tx-Loop** — der Prüfstecker: was der Rechner sendet, kommt am selben Anschluss
  wieder an; an einer V.24 sind zugleich RTS mit CTS und DTR mit DSR/DCD verbunden. Das
  schließt jede Verbindung aus; der Knopf ist gesperrt, solange es gesetzt ist, und
  eine laufende Verbindung wird beim Setzen beendet.
* **RTS/CTS-Brücke** (nur V.24) — am Stecker RTS mit CTS (und DTR mit DSR/DCD)
  verbunden: für Gegenstellen, die selbst keine Leitungen liefern (Telnet).
* **XON/XOFF** — den Empfang anhalten, solange der Rechner XOFF gesendet hat; so geht
  bei ungleich schnellen Enden nichts verloren. Bei Übertragungen von Binärdaten aus.
* **Takt** — wo der Rechner eine Brücke für die Taktquelle des Anschlusses hat.

Darunter zeigt der Block, womit der **Gast** (das Programm im Rechner) die
Schnittstelle gerade eingestellt hat, z. B. `Gast 9600 Bd 8N1`. Das Format steht in
der üblichen Schreibweise: Datenbits, Parität, Stoppbits — `8N1`, `7E1`, `8O1`
(Parität **N** keine, **E** gerade/even, **O** ungerade/odd, **M** mark, **S** space;
anderthalb Stoppbits als `1.5`). Der Tooltip erklärt das am konkreten Format.

Bei V.24 folgen die **Leitungen als Leuchten**: hinter **Ausgänge →** die Leitungen,
die der Rechner treibt (RTS, DTR), hinter **Eingänge ←** die, die er empfängt (CTS,
DSR, DCD). Die Beschriftung nennt die Richtung, nicht den Zustand. **Hellgrün
leuchtend** heißt aktiv, **gedimmt dunkelgrün** inaktiv, ein bloßer **Umriss**
unbekannt; der Tooltip nennt es ausgeschrieben („CTS aktiv"). Die Ausgänge leuchten
nur, wenn das Programm im Rechner sie setzt — CP/A und SCPX tun das nicht, dort
bleiben RTS und DTR dunkel. Die Eingänge sind unverbunden inaktiv; bei Telnet (und
Datei) werden sie aktiv, sobald eine Verbindung steht, bei RFC2217 zeigen sie, was die
Gegenseite meldet, mit **RTS/CTS-Brücke** oder **Rx/Tx-Loop** folgen sie RTS und DTR.
Angezeigt wird, was am Rechner anliegt — er muss dafür laufen; im angehaltenen Rechner
ändert sich nichts.

Bei RFC2217 steht eine weitere Zeile **Gegenseite**: Baudrate und Format, mit Warnzeichen
und Warnfarbe, wenn sie von denen des Gastes abweichen, dazu die Leitungen der Gegenseite
als Leuchten. Was bekannt ist, hängt von der Rolle ab: ein **Server** sieht die
Ausgänge RTS und DTR des Clients, ein **Client** die Eingänge CTS, DSR, DCD und RI des
Servers. Was die Gegenseite noch nicht gemeldet hat, bleibt Umriss.

**Beispiele**

* *Ein Terminalprogramm anschließen:* Schnittstelle auf **Telnet**, **Server**,
  **Starten**; dann in PuTTY Verbindungsart *Telnet*, Host `127.0.0.1`, Port 5000 (bzw.
  der Port aus der Anzeige) — oder `telnet 127.0.0.1 5000`.
* *Zwei Emulatoren koppeln:* im einen **Server**, im anderen **Client** mit
  Host `127.0.0.1` (oder dem Namen des anderen PCs) und dem Port des Servers, beide auf
  RFC2217 oder beide auf Telnet. Die Reihenfolge ist gleichgültig — der Client versucht,
  bis der Server da ist.
* *Einen echten Port des PCs verwenden:* den Port mit `ser2net` (Linux) bzw. `com0com`
  und `hub4com` (Windows) zum RFC-2217-Server machen; der Emulator verbindet sich als
  **RFC2217-Client** dorthin. Umgekehrt öffnet `socat -d -d pty,raw,echo=0
  tcp:127.0.0.1:5000` unter Linux einen Pseudo-Port auf den Server des Emulators.
* *Aus Python:* `serial.serial_for_url("rfc2217://127.0.0.1:5000")` (pyserial)
  gegen einen RFC2217-Server des Emulators.

**Sicherheit:** ein Server lauscht auf **allen Netzschnittstellen** des
PCs und verlangt **keine Anmeldung** — wer ihn erreicht, sitzt an der
Schnittstelle des Rechners. Für den Betrieb im offenen Netz gehört eine Firewall
davor, oder man bindet den Dienst über einen Tunnel (`ssh -L`) nach außen.

**Gemerkt wird** alles, was im Block steht, samt dem Zustand beim Beenden: was lief,
wird beim nächsten Start **wieder aufgenommen** — ein Server startet auf dem
eingestellten Port (ist der belegt, startet er **nicht**; der nächste freie wird ins
Feld eingetragen, und man startet von Hand), ein Client versucht wieder, eine Datei
wird **angehängt** statt überschrieben. Mit gesetztem Rx/Tx-Loop wird nichts
gestartet.

### Das Prüfprogramm SERTEST

**SERTEST** (*Serial Test*, `SERTEST.COM`) prüft die seriellen Schnittstellen eines
A5120 (unter CP/A) und eines K8915 (unter SCPX 8915) — im Emulator ebenso wie am echten
Gerät. Es steht auf den mitgelieferten Systemdisketten (`cpa_cpa780_*` für den A5120,
`k8915scpx_boot1.hfe` für den K8915); gestartet wird es am Prompt mit `SERTEST`. Es
erkennt selbst, auf welchem Rechner es läuft, stellt die geprüfte Schnittstelle für die
Dauer der Prüfung auf 9600 Bd 8N1 und hinterher wieder so ein, wie das Betriebssystem
sie erwartet. Die Tastatur prüft es nicht (sie bleibt die Eingabe des Programms).

**Bedienung.** Ohne Argumente fragt SERTEST alles ab. Nach dem Start zeigt es den
erkannten Rechner und die Liste der Schnittstellen mit ihren Nummern:

```
Serial Test V0.1
Rechner: A5120 (K8025)
Schnittstellen:
  1  DFUE/V.24      SIO A33 Kanal A   V.24
  2  DFUE/IFSS      SIO A33 Kanal B   IFSS
  3  Drucker        SIO A32 Kanal B   IFSS
  -  Tastatur K7637 SIO A32 Kanal A   (Tastatur)
Tester (Aktiv) oder Gegenstelle (Passiv)? T/G
```

Am K8915 sind es `1 Drucker/IFSS1`, `2 V.24` und `3 DFUE/IFSS2`. Mit **T** wird der
Rechner zum **Tester**: SERTEST fragt je Schnittstelle `Test der … ? J/N` und für jede
gewählte, ob mit **Prüfstecker** und/oder mit **Gegenstelle** geprüft werden soll; am
Ende stehen eine Zusammenfassung und `SERTEST ENDE OK` bzw. `SERTEST ENDE FEHLER`. Mit
**G** wird er zur **Gegenstelle** für einen anderen Rechner (siehe unten).
**Strg+C** bricht an jeder Stelle ab; SERTEST stellt die Schnittstellen dann zurück.

Dasselbe ohne Rückfragen über die Kommandozeile:

```
SERTEST T n [/P] [/G] [/A]   Tester an Schnittstelle n
SERTEST G n                  Gegenstelle an Schnittstelle n
  /P nur Prüfsteckertest, /G nur Test mit Gegenstelle (ohne beide: beide)
  /A automatisch: keine Rückfragen, kein Warten auf eine Taste
  /M:A bzw. /M:K   Rechner A5120 bzw. K8915 vorgeben (falls die Erkennung irrt)
```

Jedes Ergebnis steht als eigene Zeile da, z. B. `SERTEST DFUE/V.24 ECHO: OK`, sonst
`FEHLER` mit dem Grund oder `ENTFAELLT`, wenn ein Teil für diese Schnittstelle nicht
gilt (Steuerleitungen gibt es an einer IFSS nicht).

**Mit Prüfstecker** (`SERTEST T n /P`). Ein Prüfstecker verbindet am Anschluss den
Ausgang mit dem Eingang — was gesendet wird, kommt sofort zurück; an einer V.24 sind
außerdem RTS mit CTS und DTR mit DSR und DCD verbunden. Im Emulator ist das der
Schalter **Rx/Tx-Loop** der Schnittstelle, am Gerät ein gesteckter Prüfstecker bzw.
eine geschlossene Stromschleife. Geprüft werden:

* **DATEN-LOOP** — alle 256 Zeichenwerte, jedes muss unverändert zurückkommen. Ohne
  Prüfstecker endet das sofort mit `FEHLER KEIN ECHO BEI 00H`.
* **LEITUNGEN-LOOP** (nur V.24) — RTS und DTR werden in allen Kombinationen gesetzt;
  je Kombination steht eine Zeile mit dem, was an CTS und DCD gemessen wurde, und dem,
  was erwartet war.

Ein Prüfstecker sieht nicht alles: eine falsch eingestellte Baudrate fällt hier nicht
auf, denn Sender und Empfänger laufen mit demselben Takt. Das zeigt erst der Test mit
einer Gegenstelle.

**Mit einem anderen Rechner als Gegenstelle** (`SERTEST T n /G` auf dem einen,
`SERTEST G n` auf dem anderen). Zwei Rechner werden über ein **Nullmodemkabel**
verbunden (an einer IFSS: Sendeschleife des einen an die Empfangsschleife des anderen),
und zwar Schnittstelle gleicher Art — V.24 an V.24, IFSS an IFSS; die Nummern dürfen
sich unterscheiden. **Zuerst die Gegenstelle starten**, dann den Tester. Die Gegenstelle
schickt alles Empfangene zurück und läuft, bis man sie mit Strg+C beendet. Der Tester
prüft:

* **LEITUNGEN** (nur V.24) — schaltet RTS und DTR Schritt für Schritt und lässt sich von
  der Gegenstelle bestätigen, dass sie drüben ankommen (ohne `/A` je Schritt nach einem
  Tastendruck).
* **ECHO** — 4096 Bytes hin und zurück, Byte für Byte verglichen; dazu meldet die
  Gegenstelle ihre eigenen Empfangsfehler.
* **FLUSS-HW** (nur V.24) und **FLUSS-XON** — wie ECHO, aber die Gegenstelle bremst
  zwischendurch absichtlich, einmal über die Steuerleitung (RTS/CTS), einmal mit
  XOFF/XON. Bestanden ist der Teil nur, wenn sie wirklich gebremst hat und trotzdem
  nichts verloren ging.

Als Gegenstelle taugt ein zweiter Emulator ebenso wie ein echter Rechner:

* **Zwei Emulatoren:** im einen die Schnittstelle als **RFC2217-Server**, im anderen
  als **RFC2217-Client** auf dessen Port — das ergibt ein Nullmodemkabel samt
  Steuerleitungen. (Mit Telnet kommen nur ECHO und FLUSS-XON durch; die Leitungsteile
  brauchen RFC 2217.) Beim K8915 vorher den **Rx/Tx-Loop** dieser Schnittstelle
  abschalten. Für **FLUSS-XON** an der Schnittstelle der *Gegenstelle* den Schalter
  **XON/XOFF** setzen, für ECHO und FLUSS-HW dort **aus** lassen — dort gehen alle
  Bytewerte über die Leitung, auch das XOFF-Zeichen.
* **Ein echter A5120 oder K8915:** über ein Nullmodemkabel an einen Port des PCs, der
  wie oben beschrieben zum RFC-2217-Server gemacht ist; der Emulator ist
  **RFC2217-Client**. Ebenso lassen sich zwei echte Rechner direkt mit einem Kabel
  gegeneinander prüfen.

Typische Befunde: `ZEITUEBERLAUF BESTAETIGUNG` heißt, drüben läuft keine Gegenstelle,
das Kabel ist nicht gekreuzt oder die Baudraten passen nicht zusammen; `FEHLER RR1 …`
oder `FALSCH …` bei bestandenem Prüfstecker deuten auf einen falschen **Baudtakt**
einer Seite (am Gerät eine Brücke auf der Schnittstellenkarte). Ohne Gegenstelle endet
ein Tester immer mit `SERTEST ENDE FEHLER` — das ist richtig so.

## Der Takt der Maschine

*Einstellungen ▸ Allgemein ▸ Takt*: `2,45 MHz` — das ist der Takt des echten
A5120 — sowie `2 ×`, `5 ×` und `10 × 2,45 MHz` und *unbegrenzt* (so schnell, wie
der Wirtsrechner kann).

`2,45 MHz` ist das, was die Uhr des Gastsystems erwartet — sie zählt Taktzyklen
und geht bei jedem Vielfachen entsprechend falsch. Schneller ist gut, um einen
Kaltstart abzukürzen; wer die Uhr braucht, bleibt beim Nenntakt.

Der eingestellte Takt steht links in der Statuszeile, der **gemessene** in
dessen Tooltip.

## Modell: A5120 oder A5120.16

*Einstellungen ▸ Allgemein ▸ Modell*: **A5120** (die Vorgabe, ohne Erweiterung)
oder **A5120.16** — der A5120 mit der Erweiterungskarte EM256 und dem
16-Bit-Prozessor U8001 zusätzlich zum U880.

Ein Wechsel erzeugt die Maschine neu — wie ein Kaltstart: das Erweiterungsmodul
ist eine echte Steckkarte, keine Betriebsart, die sich während des Laufs an-
oder abschalten liesse.

Bei **A5120.16** zeigt die Statuszeile zwei zusätzliche Leuchten und den
Modus:

| Leuchte | Bedeutung |
|---------|-----------|
| V1 | RAMEN — das Erweiterungsmodul hat den Speicher eingeblendet |
| V2 | 8-Bit-Mode — die Steuerkarte fährt den U880-Bus |

**V1 zeigt nicht den Paritätsfehler**, auch wenn die Leuchte auf der Karte
dafür Platz hätte — nach dem Schaltplan hängt sie an RAMEN. Daneben steht
`Modus: 8-Bit` oder `Modus: 16-Bit`, je nachdem, welcher Prozessor gerade den
Bus hat. Beim schlichten A5120 fehlen beide Leuchten.

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

**K8915:** die Tastatur K7672 wiederholt eine gehaltene Taste selbst — nach etwa
einer Sekunde, dann etwa zehnmal je Sekunde (gerechnet, nicht am Gerät gemessen),
und wie am Gerät nicht bei Umschalt, Strg, Tab und den Ziffern 1 3 5 7 9. Das
Wiederholen des PCs wird deshalb nicht weitergereicht. Umschalt und Strg gehen als
eigene Tasten durch; ALT gibt es nur an der Bildschirmtastatur.

## Konfiguration — was gemerkt wird und wo

Alles, was man einstellt, landet fortlaufend in
`~/.config/k1520emu/a5120emu.yaml` bzw. `k8915emu.yaml` (Windows:
`%APPDATA%\K1520emu`) — jedes der beiden Programme hat seine eigene Datei, man
kann also den A5120 mit drei Laufwerken und sichtbarer Tastatur und den K8915 mit
zwei Laufwerken ohne Tastatur nebeneinander führen. Gemerkt werden Bildröhre,
Takt, Laufwerksbestückung, eingelegte Disketten, die seriellen Schnittstellen
(samt dem, was lief), Größe und Lage des Fensters
(auch „maximiert"), die Lage und Breite der Kästen samt der Trennlinien
dazwischen, und der Inhalt der Symbolleiste.

*Datei ▸ Konfiguration speichern…* legt dieselbe Datei woanders ab — für mehrere
Einrichtungen nebeneinander (etwa „CP/A mit drei Laufwerken" und „UDOS mit 8″").
*Konfiguration laden…* holt sie zurück, startet die Maschine kalt und übernimmt
die geladene Einrichtung als die neue laufende.

*Ansicht ▸ Standard zurücksetzen* geht den umgekehrten Weg: es stellt den
Zustand her, in dem der Emulator nach der Installation aufgeht — Bildröhre,
Takt, Laufwerksbestückung, Fenstergröße, Kästen und Symbolleiste — und
**überschreibt damit die gespeicherte Konfiguration**; deshalb wird gefragt. Die
eingelegten Disketten bleiben dabei liegen: was zurückgesetzt wird, ist die
Einrichtung, nicht die Maschine. Die Vorgabe selbst ist eine Datei des
Programms (`share/k1520emu/default_config_a5120.yaml` bzw.
`default_config_k8915.yaml`, im Quellbaum unter `data/`) und hat denselben
Aufbau wie eine gespeicherte Konfiguration — wer einen anderen
Auslieferungszustand will, kopiert den Inhalt seiner `a5120emu.yaml` dorthin.

Frühere Fassungen hießen die Datei des A5120 `config.yaml`. Findet der A5120
Emulator beim Start noch eine `config.yaml`, aber keine `a5120emu.yaml`, benennt
er sie **einmal** um — die Einrichtung geht beim Update also nicht verloren.

## Wo die Dateien liegen

| Ordner | Wofür |
|--------|-------|
| `K1520emu/Disketten` (im Dokumentenordner) | die Abbilder |
| `~/.config/k1520emu/a5120emu.yaml`, `k8915emu.yaml` | die Konfiguration je Programm |
| `share/k1520emu/default_config_a5120.yaml`, `…_k8915.yaml` (in der Installation) | der Auslieferungszustand |

Beim ersten Start nach einer Installation werden die mitgelieferten
Beispieldisketten dorthin ausgepackt. Verschieben lässt sich das mit den
Umgebungsvariablen `K1520_DATA` (Arbeitsordner) und `K1520_DISKS` (nur die
Abbilder). Wo das Programm gerade sucht, sagt `a5120emu --paths`.

## Von der Kommandozeile

```
a5120emu [DISKETTE …]     bis zu vier Abbilder, in Laufwerksreihenfolge A: B: C: D:
k8915emu [DISKETTE …]     bis zu zwei Abbilder, A: B:
a5120emu --paths          aufgelöste Pfade zeigen
a5120emu --help           Kurzhilfe
```

Im Quellbaum heißen die Starter `run_a5120emu.sh` und `run_k8915emu.sh`; beide
rufen `app/main.py` auf, der zweite mit `--machine k8915`.

Die genannten Disketten liegen **beim Kaltstart schon im Laufwerk** — die
Maschine bootet also von der ersten. Sie ersetzen die gemerkte Belegung nur für
diesen Lauf; gespeichert wird davon nichts.

Ohne Oberfläche fährt dieselbe Maschine unter `k1520dbg` (Fehlersuche,
Skriptbetrieb); `k1520dbg DISKETTE --console` ist die Konsolenfassung.

## Die anderen Werkzeuge

Im Menü **Werkzeuge** stehen die Programme, die zur selben Installation
gehören und dieselben Disketten anfassen:

* **K8915 Emulator starten** bzw. **A5120 Emulator starten** — der jeweils
  andere Emulator, als eigenes Programm mit eigener Konfiguration.

* **k1520DiskTool starten** — das Diskettenwerkzeug: Dateien von einer Diskette
  in einen Ordner holen und wieder zurückschreiben, Disketten anlegen, prüfen,
  reparieren. Es startet als eigenes Programm und läuft neben dem Emulator
  weiter; das Diskettenwerkzeug hat sein eigenes Handbuch.
* **Werkzeugkonsole öffnen** — ein Konsolenfenster, in dem der Debugger
  `k1520dbg` und die Kommandozeile des DiskTool **ohne Pfadangabe** laufen. Es
  steht bereits im Diskettenordner, und beim Öffnen steht ein Beispielaufruf
  mit einer wirklich vorhandenen Diskette da.

Die Konsole wird über eine Startdatei geöffnet, die im Konfigurationsordner
liegt (`werkzeugkonsole.sh`, unter Windows `werkzeugkonsole.cmd`). Sie wird bei
jedem Öffnen neu geschrieben — wer sie anpassen will (ein eigener Assembler im
Suchpfad, ein anderer Arbeitsordner), kopiert sie sich woandershin.

> Eine Diskette, die hier im Laufwerk liegt, darf zugleich unter `k1520dbg`
> offen sein: der Debugger arbeitet standardmäßig auf einer Kopie und schreibt
> nicht in die Datei zurück.

## Der K8915 Emulator

`k8915emu` ist derselbe Emulator für den **robotron K8915** (Version 3, 5¼″):
ZRE mit 128 KB, Bildschirmkarte K7024, Tastatur K7672, Diskettensteuerung K5122
mit zwei K5601. Was anders ist:

* **Einschalten** startet den Selbsttest des Boot-ROMs (ROM, RAM, KEY, CTC, SIO —
  in der letzten Zeile unter „DIAGNOSTIC"). Er dauert rund 12 Sekunden
  Maschinenzeit, bei der Vorgabe 10 × Takt also gut eine Sekunde. Danach steht
  `* Coldstart *  Disk on A: ready ? --> <ENTER>` — **RETURN** lädt das System
  von A:. SCPX 8915 richtet beim Start die RAM-Disk E: ein und meldet sich mit `A>`.
* **Takt** 2,4576 MHz (`10 × 2,4576 MHz` usw.).
* **Laufwerke:** zwei K5601 wie am Gerät; wählbar sind nur 5¼″-Laufwerke.
* **Die Frontplatte in der Statuszeile** — sechs Lampen, von links nach rechts
  wie am Gerät von oben nach unten, jede mit ihrem Schild daneben (`Run`, `Input`,
  `Output`, `Mode`, `Error`, `Power`; der volle Name steht im Tooltip):

  | Lampe | Farbe | leuchtet |
  |-------|-------|----------|
  | Run | grün | solange die Emulation läuft (am Gerät vermutlich `/HALT` der CPU — nicht belegt) |
  | Input File | gelb | beim Lesen von der Diskette |
  | Output File | gelb | beim Schreiben auf die Diskette |
  | RUN Mode | gelb | System bereit (erlischt während eines Diskettenzugriffs) |
  | ERROR | rot | Fehler — Selbsttest, Lesen oder Schreiben |
  | Power | rot | solange der Rechner eingeschaltet ist |

  Die vier mittleren schaltet das Betriebssystem selbst (Anzeigelatch 61H).
* **NMI-Taster** — *Maschine ▸ NMI-Taster* und in der Symbolleiste neben
  *Reset*, ohne Tastenkürzel. Wie am Gerät ist das kein Rückstellen: solange das
  Boot-ROM eingeblendet ist (Selbsttest, Lader), gehen die Lampen aus und der
  Selbsttest beginnt von vorn. **Unter SCPX** liegt an der Einsprungstelle RAM —
  die CPU springt dorthin, meist in einen Absturz. Das ist das Verhalten des
  Geräts nach den Unterlagen und wird bewusst nicht abgefangen; danach hilft
  *Rückstellen*.
* **Die Bildschirmtastatur ist die K7672** (*Ansicht ▸ Tastatur*): deutsche
  Belegung, Funktionsreihe CTRL · ALT1 ^S MOD2 PF1 · PF2–PF9 · CLEAR RESET BREAK,
  Mittelblock PF10–PF12, PA1–PA3, GRAPH und Kursorkreuz, Ziffernblock mit CE, `=`
  und ENTER. Die roten, grünen und grauen Zweitbeschriftungen sind die des
  Originals (rot = die PC-Bedeutung der Taste: `Pause`, `Pg Up`, `Prt Sc` …).
  Jede Taste sendet, was die Tastatur-Firmware für sie vorsieht: unter SCPX den
  PC-Scancode, im Boot-ROM das Zeichen. Tasten, für die es dort nichts gibt
  (die Funktionstasten im Boot-ROM, `CL` — schaltet am Gerät nur den
  Tastenklick um), federn zurück und senden nichts. **Umschalt** und **CTRL**
  rasten für genau eine Taste, **CAPS LOCK** ist eine echte Taste. Die drei
  Lampen **GRAPH**, **CAPS** und **READY** (grün: die Tastatur darf senden)
  zeigen den Zustand der Tastatur.
* Die **PC-Tastatur** geht wie beim A5120 an den Rechner; SCPX setzt die
  Tasten selbst in Zeichen um (deutsche Belegung: `z`/`y` getauscht). `Strg+Pause`
  des K8915 liegt auf `^S` der Bildschirmtastatur. Die **Rücktaste** des PCs
  ist die Kursortaste `←` der K7672: SCPX macht daraus `^H`, und die
  Eingabezeile löscht das Zeichen auch am Schirm. Die Taste `|←|` ergibt unter
  SCPX dagegen `DEL` — das Zeichen verschwindet aus der Eingabe, wird aber noch
  einmal angezeigt (`A>dirxx` führt `dir` aus), wie am Gerät. **Entf** ist die
  Taste `DEL` der K7672 (`^G`, in Editoren wie TP: Zeichen unter dem Kursor
  löschen).

### Bootdiskette mit DISGEN erstellen

DISGEN ist ein Maskenprogramm: In einem **Auswahlfeld** wählt der
**Anfangsbuchstabe** (klein genügt) oder **Esc** schaltet zum nächsten Eintrag,
**Return** übernimmt das Feld bzw. den ganzen Block, die **Kursortasten**
wandern zwischen den Feldern eines Blocks, **Strg+C** geht eine Ebene zurück
(im Befehlsfeld: DISGEN beenden).

1. In A: die Systemdiskette, in B: eine leere Diskette (*Leere Diskette*).
2. B: formatieren: `format` Return, Verfahren `24` Return, Laufwerk `B`
   (groß, mit Umschalt) Return, `00` Return, `79` Return, `01` Return, `Y`
   Return — nach „FUNCTION COMPLETE“ `Y` Return.
3. `disgen` Return, dann Return (Read system tracks) und Return (Laufwerk A).
4. **Nur mit der Diskette 901** (sie stellt B: auf 16 × 256 Byte ein):
   `c` Return (Change device properties), Return (number of drives),
   `b` Return, zweimal Pfeil ↓ bis `sector length`, zweimal Esc bis `1024`,
   Return, Return.
5. `w` Return (Write system tracks), `b` Return.
6. `e` Return (Exit). Die neue Diskette startet jetzt, in A: eingelegt, bis `A>`.

**`write error or device not ready`** beim Schreiben heißt fast immer: B: ist
in DISGEN anders eingestellt, als die Diskette formatiert ist (Schritt 4
vergessen). Return bringt nur ins Laufwerksfeld zurück — mit **Strg+C**
heraus, dann mit `e` Return beenden und bei Schritt 3 neu anfangen: eine
Umstellung **nach** dem Fehlschlag reicht nicht, erst der Neustart von DISGEN
räumt den Puffer des BIOS. Wer DISGEN mit Strg+C verlässt und danach
Steuerzeichen (`^D` …) statt Buchstaben sieht: einmal Strg drücken.

## Der PRG710 Emulator

`prg710emu` ist derselbe Emulator für die Programmiergeräte **robotron PRG 710**
und **PRG 710-1** (ZRE K2521, Speicher mit Seitenverwaltung, Bildschirmkarte
K7024, Diskettensteuerung K5122 mit zwei K5601). Was anders ist:

* **Modell** — *Einstellungen ▸ Allgemein ▸ Modell*: PRG 710 (Tastatur K7609 an
  einem 8279) oder PRG 710-1 (Tastatur K7672). Ein Wechsel erzeugt die Maschine
  neu und tauscht die Bildschirmtastatur; die Wahl wird in `prg710emu.yaml`
  gemerkt.
* **Takt** 2,4576 MHz, **Laufwerke** zwei K5601 wie am Gerät.
* **Einschalten** zeigt „NKM-LOADER“ und wartet auf die **Starttaste**: am 710
  **ET1** (die Taste `ET1` der Bildschirmtastatur oder **Return** des PCs), am
  710-1 **ENTER**. Ohne Diskette folgt „DISKERROR C2“ und „NO SYSTEM“.
* **UDOS 4.3** meldet sich mit der Datumsmaske „Neues Datum :__.__.19__“ — sechs
  Ziffern eintippen (TTMMJJ), **ohne** ET. Danach steht der Prompt `%`.
  Beispiele: `CAT D=0 P=&` (Verzeichnis; ohne `P=&` meldet das 710-1 „FILE NOT
  FOUND“), `DATE`, `COPY OS.INIT 1/KOPIE` (auf Laufwerk 1).
* **SCPX 1526** bootet am 710-1 bis `A>` (`DIR`, `STAT`, `PIP B:=…`).
* **Die Bildschirmtastatur des 710 (K7609)** sendet je Taste den Tastencode des
  Geräts. **ET1** und **ET2** (rot) sind Tasten wie alle anderen, ohne
  Kürzel erreichbar; **UMSCH** und **STRG** rasten für genau eine Taste. Das
  Tastenbild ist nach der Codetabelle gezeichnet, nicht am Gerät vermessen:
  `S1`–`S9` und `CL` haben keine bekannten Codes, sie federn zurück und senden
  nichts (Beschriftung „[?]“); Leertaste und `BS` sind vorläufig belegt.
* **Schnittstellen** — am 710 drei (V.24 X4, IFSS Hauptdrucker X6, ZIFSS
  Zusatzdrucker X5), am 710-1 zwei (V.24, ZIFSS); A32-B trägt dort die Tastatur
  und steht als feste Schnittstelle im Reiter. Dahinter folgt in beiden der
  **Fernschreiber** (siehe unten). `SET PRI TO V24` lädt unter UDOS
  den Druckertreiber `DRUCK.V24`, `SET PRI ON` spiegelt die Ausgabe dorthin.
* Die **PC-Tastatur** geht wie beim A5120 an den Rechner. Am 710-1 wiederholt die
  K7672 eine gehaltene Taste selbst; am 710 gibt es keine Wiederholung.

### Der EPROMmer

Der PRG brennt U555 (1 KB, wie 2708) und U2716 (2 KB) in **einem** Sockel; das
Programm dazu ist `PROG` (UDOS) bzw. `PROG.COM` (SCPX). Im Emulator ist der
Sockel **virtuell**: ein PROM ist ein rohes Abbild (`.bin`). Der Kasten
**EPROMmer** (*Ansicht ▸ EPROMmer*; die Bedienung auch unter *Maschine ▸
EPROMmer*) zeigt, was steckt, welchen Typ das Programm eingestellt hat und ob
Versorgung und Programmierspannung anliegen.

* **PROM-Abbild einlegen…** — eine Datei bis 1 KB wird ein U555, bis 2 KB ein
  U2716; kürzere werden mit FFH aufgefüllt.
* **Leeres PROM einlegen** — ein gelöschtes U555 oder U2716 (alle Zellen FFH).
* **PROM-Abbild speichern unter…** — den Inhalt sichern, etwa nach dem Brennen.
  Ein gebranntes, noch nicht gespeichertes PROM steht als „geändert“ da.
* **PROM UV-löschen** — wie im Löschgerät; die Software kann nicht löschen.
* **PROM entnehmen.**

Gebrannt wird wie am Gerät nur von 1 nach 0: ein zweites Brennen ohne Löschen
ergibt die UND-Verknüpfung. Stimmt der in `PROG` eingestellte Typ (`T`, „PROMGROESSE
1 ODER 2 K-BYTE“) nicht mit dem gesteckten PROM überein, wird gelesen, aber nicht
gebrannt — das Protokoll sagt es. Beispiel unter UDOS: `PROG`, `N`, dann `C`
(kopieren): Quell-PROM einlegen, `J`; bei „COPY-PROM STECKEN“ ein leeres PROM
einlegen, `J`, `J`. Das Brennen dauert in Maschinenzeit gut 50 ms je Byte (U2716)
bzw. 100 Durchläufe über das ganze PROM (U555). Die Antworten in `PROG` sind
einzelne Tasten ohne ET.

### Lochband und Fernschreiber

Der PRG hat einen **Lochbandleser** (daro 1210) und einen **Lochbandstanzer**
(daro 1215) an der Karte K6022 und einen **Fernschreiber** an der Karte 590069.

* **Ein Band ist eine Datei** mit den Bytes, wie sie gestanzt sind. *Maschine ▸
  Lochband ▸ Band in den Leser legen…* legt eine ein, *Band aus dem Leser nehmen*
  nimmt sie heraus (das Menü zeigt, wie weit gelesen ist). Der Leser gibt vorn und
  hinten ein Stück Leerband (Nullbytes) dazu — eine Textdatei des PCs lässt sich
  so direkt einlesen.
* Was gestanzt wird, sammelt sich im **Stanzband**: *Stanzband speichern…* schreibt
  es in eine Datei (das Menü zeigt die Länge), *Neues Stanzband einlegen* fängt
  von vorn an. *Stanzer eingeschaltet* abgehakt: der Stanzer quittiert nicht, der
  Treiber meldet nach etwa einer Sekunde `ERROR C2`. Beide Bänder überstehen ein
  Rückstellen.
* **Unter UDOS** stanzt `DO TWRITE.1215 DATEI F=A` eine Datei, `DO TREAD.1210 NEU
  F=A` liest das eingelegte Band in die Datei `NEU` (sie entsteht auf der zweiten
  Diskettenseite, `CAT` meldet „DRIVE 4“). **`F=A` gehört dazu**: der Treiber
  `PTAPE.6022` überträgt nur Text (gerade Parität, Zeilenende NL); das
  Vorgabeformat von `TAPE.WRITE` kommt nicht zurück (`ERROR C9`). `ERROR C2` beim
  Lesen heisst: kein Band im Leser.
* **Der Fernschreiber** ist ein Anschluss im Reiter *Schnittstellen* wie die
  anderen — als **Datei** wird daraus ein Fernschreibprotokoll, über **Telnet**
  liest ein Terminal mit. Nachgebildet ist der Fernschreiber selbst: hinaus geht
  Text, nicht die 5-Bit-Zeichen der Leitung. Benutzt wird er von SCPX am 710-1 mit
  einem BIOS `B17172FS`/`B17272FS` (Druckerausgabe, z. B. **^P** und `DIR`); er
  schreibt nur Großbuchstaben, Zeichen ohne Gegenstück im Fernschreibalphabet
  (`>` `*` `#` …) als Zwischenraum, mit 100 Baud — etwa 13 Zeichen je Sekunde.

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
nicht — im Formatdialog beim Einlegen wurde das falsche gewählt. Auswerfen, neu
einlegen, ein anderes Format nehmen. `.hfe` und `.dmk` haben dieses Problem
nicht.

**Der Tooltip am Takt meldet, dass der Wirtsrechner nicht mitkommt.** Meist, weil die
Bildröhre mit allen Effekten auf einer großen Fläche gezeichnet wird. Fenster
kleiner machen oder unter *CRT* die aufwendigen Regler zurücknehmen.

**Die Tastatur tippt ins Leere.** Der Fokus liegt nicht auf der Röhre: einmal
hineinklicken. (Normalerweise holt das Fenster ihn von selbst zurück.)

**Der K8915 bleibt nach dem Selbsttest mit einem Buchstaben stehen.** Der
Buchstabe hinter dem Testnamen ist die Fehlerkennung des Boot-ROMs; RETURN
lädt trotzdem.

**Gar nichts startet.** `a5120emu --paths` (bzw. `k8915emu --paths`) sagt, wo das Programm die
Kernbibliothek und den Formatkatalog sucht — das ist die erste Frage, wenn
etwas nicht gefunden wird.
