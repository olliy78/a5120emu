# Büro Computer Robotron A5120

Quelle des Textes: https://www.robotrontechnik.de/

Der Rechner A5120 (werksinterne Bezeichnung GBG=Großes Bildschirmgerät) war 1982 der Auftakt der Bürocomputer des [VEB Buchungsmaschinenwerk Karl-Marx-Stadt](https://www.robotrontechnik.de/html/standorte/buma.htm).  
Er zeichnete sich vor allem durch seine flexible Konfigurationsmöglichkeit, kompakte Bauweise und Servicefreundlichkeit aus und war eine der am meisten durchdachten und interessantesten Rechnerkonzeptionen der DDR. Sein Einsatzgebiet war in erster Linie die normale Büroarbeit. Aber auch zur

- Softwareentwicklung
- Maschinensteuerung
- als Messplatz
- als Terminal und
- zur Datenkonvertierung

konnte er benutzt werden.  
  
Der A5120 bestand weitgehend aus genormten Einheitsbaugruppen: [K1520-Platinen](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm), [Stromversorgungsmodule](https://www.robotrontechnik.de/html/komponenten/stv.htm#stv2), Standard-Gehäusebaugruppen. Im Inneren des Rechners befand sich eine Rückverdrahtungseinheit mit 11 (später 7) Slots, in die die Platinen gesteckt wurden und die auch Platz für Erweiterungen boten. Interessant war der Aufbau der ZVE-Einheit mit zwei mit 2,25 MHz getakteten Prozessoren [U880](https://www.robotrontechnik.de/html/komponenten/ic.htm#u880): Ein Prozessor war für die normale Arbeit zuständig, der andere arbeitete als DMA-Prozessor und ermöglichte einen schellen Datenaustausch mit externen Einheiten, beispielsweise [8-Zoll-Diskettenlaufwerken](https://www.robotrontechnik.de/html/komponenten/fs.htm#k5602). Die Rechenleistung des Prozessors betrug bis zu 625.000 Operationen pro Sekunde, ein Wert, der wenige Jahre davor noch [Großrechnern](https://www.robotrontechnik.de/html/computer/grossrechner.htm) vorbehalten war.  
Da hochintegrierte Diskettencontrollerschaltkreise zu dieser Zeit in der DDR noch nicht verfügbar waren, wurde deren Funktion teilweise durch den Prozessor wahrgenommen. Dies bedingte allerdings Besonderheiten in der Software. Daher sind die Betriebssysteme des A5120 nicht direkt mit westlichen Rechnern austauschbar.  
Speicherseitig war der A5120 anfangs mit 16 KByte, teilweise sogar noch weniger, bestückt. Das änderte sich dann Mitte der 1980er Jahre mit dem Aufkommen der [64-KByte-Speicherplatine](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8600) und dem [Betriebssystem SCP](https://www.robotrontechnik.de/html/software/scp.htm).  
  
Die meisten A5120 waren farblich in braun gehalten, es gab aber auch weiße und orange Gehäuse. Die ersten Geräte hatten weiß leuchtende Bildröhren, die mit einer äußerlichen Beschichtung grün gemacht wurden, später hatte man grün leuchtende Bildröhren eingesetzt.  
  
Nahe Verwandte des A5120 waren die [Bürocomputer](https://www.robotrontechnik.de/html/computer/buerocomputer.htm) [A5130](https://www.robotrontechnik.de/html/computer/a5130.htm), [K8924](https://www.robotrontechnik.de/html/computer/k8924.htm) und [DORAM](https://www.robotrontechnik.de/html/computer/doram.htm).  
Im gleichen Gehäuse wie der A5120 wurden die Computer [A5310](https://www.robotrontechnik.de/html/computer/a5310.htm), [K8927](https://www.robotrontechnik.de/html/computer/k8927.htm), [K8931](https://www.robotrontechnik.de/html/computer/k8931.htm) und [BC25](https://www.robotrontechnik.de/html/computer/bc25.htm) gebaut.  
Es gab auch eine abgewandelte Version des A5120 speziell für Mess-und Steuerzwecke an Telefonanlagen namens [USAR](https://www.robotrontechnik.de/html/computer/usar.htm).

Die verbreitetste Version des A5120 war mit drei Diskettenlaufwerken [K5601](https://www.robotrontechnik.de/html/komponenten/fs.htm#k5601) bestückt. Die maximale Speichergröße pro Diskette wuchs damit auf 800 KByte. In dieser Form (Auslieferung ab Januar 1985) etablierte sich der Rechner als Standard-Bürocomputer, bis er später durch den [PC1715](https://www.robotrontechnik.de/html/computer/pc1715.htm) verdrängt wurde.  
  

|   |   |
|---|---|
|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120_4_k.jpg)  <br>Computer A5120 mit drei K5601-Laufwerken|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120_innen_k.jpg)  <br>Computer A5120, innen.  <br>In der Mitte der Lüfter, rechts die Netzteile.|

  
Da Diskettenlaufwerke teuer und selten waren, wurden viele Rechner mit nur zwei Diskettenlaufwerken ausgeliefert. Dafür gab es entweder eine spezielle Frontblende oder der dritte Diskettenschacht wurde einfach mit einer Platte zugeklebt. Gegenüber den Vorgängermodellen wurden die Diskettenlaufwerke jetzt über ein Flachbandkabel angesteuert, das wie ein Bus von Laufwerk zu Laufwerk lief.  
  

|   |
|---|
|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120_7_k.jpg)  <br>Sparvariante mit nur zwei K5601-Laufwerken|

  
Als Eingabegerät wurde die serielle Tastatur [K7637](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7637) benutzt, die gegenüber den Vorgängermodellen eine schnellere Eingabe gestattete.  
Auch der Bildschirm änderte sich: er wurde jetzt durch einen [K7222](https://www.robotrontechnik.de/html/zubehoer/bildschirme.htm#k7222) in Einbauvariante realisiert, womit die Bildschirmauflösung auf 24 Zeilen á 80 Zeichen stieg.  
  
Platinenbestückung (Slots von links beginnend):

|K-Name|Platine|Kürzel|Bedeutung des Kürzels|Erläuterung|
|---|---|---|---|---|
|K3526|[062-8600](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8600)|OPS|Operationsspeicher|64 KByte RAM|
|K8025|[062-8440](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8440)|ASS|Adapter für serielle Systeme|Schnittstellen 3x [IFSS](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#ifss), 1x[V.24](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#v24), auch für Tastatur|
|K5122|[062-8390](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8390)|AFS|Anschlusssteuerung für Folienspeicher|Floppycontroller|
|K2526|[062-8110](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8110)|ZRE|Zentrale Recheneinheit|Prozessoreinheit|
|K7024|[012-6820](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#012-6820)|ABS|Adapter für Bildschirm|Bildschirmkarte (80x24 Zeichen)|

  
Durch die mittlerweile verfügbaren 64-KByte-RAM-Karten ([K3526](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8600)) konnte auf die bisherigen 16-KByte-RAM-Karten [K3525](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#012-7121) verzichtet werden, wodurch die Anzahl der freien Platinenslots stieg.  
Um Material zu sparen, wurde die Anzahl der Platinenslots meist auf 7 verringert.

### Bürocomputer A5120.16

Diese sehr seltene Variante des A5120 hatte gegenüber der Grundversion zwei zusätzliche Leiterplatten, die die Benutzung des Betriebssystems [MUTOS 8000](https://www.robotrontechnik.de/html/software/mutos.htm#mutos8000) ermöglichten. Die eine Zusatzplatine enthielt eine ZRE auf Basis des [U8001-Prozessors](https://www.robotrontechnik.de/html/komponenten/ic.htm#u8001), die andere war eine Speichererweiterung um 64 oder 256 KByte. Entsprechend nannten sich die Zusatzplatinen EM64 bzw. EM256. Beide Platinen stellen einen eigenen 16-Bit-Rechner dar (Busverbindung über zwei Flachbandkabel), die übrige 8-Bit-Hardware wurde während der Arbeit mit [MUTOS 8000](https://www.robotrontechnik.de/html/software/mutos.htm#mutos8000) nur noch als Terminal unter Steuerung des Betriebssystems [UDOS1526](https://www.robotrontechnik.de/html/software/udos.htm#udos1526) benutzt. Die Entwicklung der beiden Zusatzkarten erfolgte im Zentralinstitut für Kybernetik der [Akademie der Wissenschaften](https://www.robotrontechnik.de/html/standorte/adw.htm). Ein Auftraggeber des A5120.16 war die [NVA](https://www.robotrontechnik.de/html/arbeitsplaetze/nva.htm).  
  

|   |   |
|---|---|
|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120-16/A5120-16_k.jpg)  <br>Rechner A5120.16|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120-16/A5120-16_Innen_k.jpg)  <br>Innenansicht des A5120.16|

|   |   |
|---|---|
|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120-16/A5120.16_innen_3_k.jpg)  <br>Innenansicht des A5120.16|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120-16/A5120.16_innen_2_k.jpg)  <br>Bildschirm des A5120.16, hier ein [MON2.1](https://www.robotrontechnik.de/html/zubehoer/bildschirme.htm#mon2.1)|

|   |   |
|---|---|
|![](https://www.robotrontechnik.de/bilder/Komponenten/Platinen/K1520/062-9005_ZVE_k.jpg)  <br>16-Bit-Prozessorkarte 062-9005|![](https://www.robotrontechnik.de/bilder/Komponenten/Platinen/K1520/062-9000_OSS_256k_k.jpg)  <br>256k-RAM-Karte 062-9000|

  
Unter dem Betriebssystem [SCP1526](https://www.robotrontechnik.de/html/software/scp.htm#scp1526) konnte bei entsprechender Software die 256-KByte-Karte als RAM-Disk und die [U8001](https://www.robotrontechnik.de/html/komponenten/ic.htm#u8001)-Prozessorkarte als Coprozessor für numerische Berechnungen verwendet werden. Die meisten A5120.16 wurden mit [Achtzoll-Laufwerken](https://www.robotrontechnik.de/html/komponenten/fs.htm#k5602) ausgeliefert, vermutlich wegen der höheren Geschwindigkeit.  
  

|   |   |
|---|---|
|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120-16/Screenshots/RAMD256_2_k.jpg)  <br>Nutzung der RAMDisk unter SCP|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120-16/Screenshots/MUTOS8000_k.jpg)  <br>Betriebssystem MUTOS8000|

  
Der Preis des A5120.16 mit 256 KByte RAM lag 1986 bei 73.604 [Mark](https://www.robotrontechnik.de/html/standards/preise.htm), andere Quellen sprechen von 100.000 Mark.  
Von dieser Rechnervariante haben nur wenige Exemplare bis heute überlebt.

## Mechanischer Aufbau

Der A5120 bestand aus einer Bodenbaugruppe und einem zerlegbaren Geräterahmen, in den die eigentlichen Rechnerbaugruppen eingeschraubt waren und der nach oben mit einer Metallplatte und seitlich und vorn mit zwei PUR-Formteilen verkleidet war. Den Abschluss nach hinten bildete eine Rückwand aus Blech, die auch die Stecker der externen Kabel verbarg.  
  
Die Bodenbaugruppe beinhaltete die Netzentstörung, das Einschaltrelais für die Hauptnetzteile, ein oder zwei Gleichspannungswandler, eine Platine zur optischen Anzeige der Funktion der Netzteile sowie die Baugruppe [NÜ/NEST](https://www.robotrontechnik.de/html/komponenten/stv.htm#standby) (Netzüberwachung/Netzsteuerung). Die Netzüberwachung stellte die Betriebsspannungen (StandBy-Netzteil) für die anderen Steuerbaugruppen bereit und signalisierte außerdem Netzspannungsausfälle. Die Netzsteuerung sorgte für das Ein-und Ausschalten der Netzteile in der richtigen zeitlichen Reihenfolge, wertete die Betätigungen des Netztasters aus, ermöglichte das Ferneinschalten des Rechners per [V.24-Schnittstelle](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#v24) sowie das softwaregesteuerte Abschalten. Außerdem übernahm es die RESET-Generierung.  
  
Im Geräterahmen steckte links der Bildschirm, der aus dem 12V-Netzteil gespeist wurde und nach Lösen von drei Schrauben auf Schienen nach vorn herausgezogen werden konnte. Nach Herausziehen des Bildschirms und Entfernen der Schienen wurde das darunterliegende [NÜ/NEST-Modul](https://www.robotrontechnik.de/html/komponenten/stv.htm#standby) zugänglich. Hinter dem Bildschirm waren die beiden Hauptnetzteile ([K0362.03](https://www.robotrontechnik.de/html/komponenten/stv.htm#k0362-03) und [K0362.08](https://www.robotrontechnik.de/html/komponenten/stv.htm#k0362-08)) angeordnet. Deren Ausgangsspannungen waren auf eine Verteilerleiste geführt, von der aus die Kabel zum Platinenschacht gingen.  
  
Zur Kühlung des Rechners diente ein Lüfter, der temperaturgesteuert mit zwei Drehzahlen arbeitete und die Warmluft durch seitliche Schlitze in der Rückwand abführte. Die älteren Modelle waren mit den relativ lauten Lüftern aus Hartha bestückt, bei neueren Rechnern kamen leisere westliche Import-Lüfter zum Einsatz.  
  
Das Zerlegen des Rechners beginnt mit der Abnahme der Rückwand (2 Schrauben unten). Anschließend kann mit 2 Schrauben hinten oben die Deckplatte abgenommen werden. Daraufhin werden die Schrauben der Seitenwände zugänglich (jeweils 2 oben und 2 unten).  
Die Bewickelungsseite der Sloteinheit ist nach Abnahme der vorderen mittleren Frontblende (2 Schrauben unten) zugänglich. Auch die Diskettenlaufwerke sind von vorn zugänglich: Nach Abnahme der Frontblende (2 Schrauben unten), Lösen der seitlichen Fixierschraube rechts am Laufwerk und Wegdrücken des Schnappers links am Laufwerk lässt sich das betreffende Laufwerk auf Schienen nach vorn ziehen und verrasten. Durch eine unglückliche (nachträglich geänderte?) Verlegung des Flachbandkabels ist es manchmal doch notwendig, die Rechnerrückwand abzunehmen und die Kabel vorher abzuziehen.  
  
  

## Zubehör

Als Drucker wurden die [Typenrad](https://www.robotrontechnik.de/html/drucker/sd1152.htm)- und [Nadeldrucker](https://www.robotrontechnik.de/html/drucker/sd11xx.htm) der SD11xx-Serie sowie der [K6316](https://www.robotrontechnik.de/html/drucker/k631x.htm#k6316) benutzt. Die Standard-Druckereinstellungen waren: 9600 Baud, asynchron, 7 Datenbits, 1 Stopbit, Parität ungerade. Welche Druckerschnittstelle zu benutzen war, wurde durch Generierung im Betriebssystem oder [Textverarbeitungsprogramm](https://www.robotrontechnik.de/html/software/textprg.htm) festgelegt.  
Durch Verwendung eines Busadapters und eines Beistellgerätes konnte bei Bedarf die Anzahl der [K1520](https://www.robotrontechnik.de/html/computer/k1520.htm)-Slots (und damit der Platinen) vervielfacht werden.  
Als externe Speicher konnten auch Kassettenmagnetbandlaufwerke, wie das [K5221](https://www.robotrontechnik.de/html/zubehoer/kmbe.htm#k5221) verwendet werden. Softwareseitige Unterstützung dazu gab es unter den Betriebssystemen [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) und [SCP](https://www.robotrontechnik.de/html/software/scp.htm#scp1526).  
Zum Datenaustausch mit [Großrechnern](https://www.robotrontechnik.de/html/computer/grossrechner.htm) gab es auch eine Anschlussmöglichkeit für ein [Magnetbandlaufwerk](https://www.robotrontechnik.de/html/zubehoer/mbe.htm).  
Auch [Lochbandtechnik](https://www.robotrontechnik.de/html/zubehoer/lbg.htm), wie die Geräte [1215](https://www.robotrontechnik.de/html/zubehoer/lbg.htm#1215) und [1210](https://www.robotrontechnik.de/html/zubehoer/lbg.htm#1210), konnte benutzt werden. Softwareseitige Unterstützung dazu gab es unter den Betriebssystemen [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) und [SCP](https://www.robotrontechnik.de/html/software/scp.htm#scp1526).  
  
  

## Tastatur

Die ersten Modelle des A5120 wurden mit parallelen Tastaturen des Typs [K7636](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7636) ausgestattet.  
Um die Eingabegeschwindigkeit zu erhöhen und längere Kabel benutzen zu können, wurde später stattdessen die serielle Tastatur [K7637](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7637) ausgeliefert, die allerdings die Schnittstelle des 2. [IFSS](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#ifss)-Druckers belegte.  
  
## Tastatur K7636

Diese Parallel-Interface-Tastatur auf [Hall-Basis](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#kontaktverfahren) war der Nachfolger der [K7606](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7606) und wurde als ursprüngliche Eingabegerät für [K1520-Rechner](https://www.robotrontechnik.de/html/computer/k1520.htm) benutzt. Sie wurde in frühen Versionen des [A5120](https://www.robotrontechnik.de/html/computer/a5120.htm) sowie des [K8924](https://www.robotrontechnik.de/html/computer/k8924.htm) verwendet. Gegenüber der [K7606](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7606) war die K7636 mit einem [Mikroprozessor U880](https://www.robotrontechnik.de/html/komponenten/ic.htm#u880) ausgerüstet.  
  

|   |   |
|---|---|
|![](https://www.robotrontechnik.de/bilder/Zubehoer/Tastaturen/Tastatur_K7636_k.jpg)  <br>Tastatur K7636|![](https://www.robotrontechnik.de/bilder/Zubehoer/Tastaturen/Tastatur_K7636_innen_k.jpg)  <br>Tastatur K7636, geöffnet|

  
Der Sicherheitsstecker an der rechten Seite der Tastatur wurde unter dem Betriebssystem [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) zur Authentifizierung des Anwenders genutzt. Bei Verlassen des Rechners war der Stecker abzuziehen, was eine Blockierung der Tastatur und ein Anhalten des Programms bewirkte. Außerdem wurde über den Stecker die Berechtigung des Nutzers überprüft. Abhängig von der Codierung des Steckers wurde zwischen mehreren Nutzerklassen und dem Administrator unterschieden. Der Sicherheitsstecker wurde nur unter dem Betriebssystem [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) wirksam.  
  
Außerdem war auf der Tastatur der Ein- und Ausschalter für den Rechner angebracht (grüne Taste rechts oben), da die frühen [K1520-Rechner](https://www.robotrontechnik.de/html/computer/k1520.htm) keinen solchen Schalter direkt am Rechner hatten.  
  
Anschlussseitig wurde die K7636 direkt auf die [ZRE-Platine K2526](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8110) gesteckt. Um diese Tastatur unter den Betriebssystemen [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) oder [SCP](https://www.robotrontechnik.de/html/software/scp.htm#scp1526) nutzen zu können, muss das Betriebssystem entsprechend generiert sein. Ein probeweiser Austausch mit der [K7634](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7634) und der [K7606](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7606) ist möglich.  
  
Da die Tastatur für Arbeit für das Betriebssystem [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) entwickelt war, fehlte den meisten Exemplaren die CTRL-Taste. Diese wurde unter [SCP1526](https://www.robotrontechnik.de/html/software/scp.htm#scp1526) stattdessen durch die Taste INS-LN ersetzt.  
  
Die drei roten Tastenfelder oben in der Mitte konnten softwaregesteuert zum Blinken gebracht werden und zur Anzeige von Fehlern verwendet werden, beispielsweise bei einer nach einer Fehleingabe gesperrten Tastatur oder einem fehlenden Betriebssystem.  
  
Die K7636 wurde später von der [K7637](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7637) abgelöst und ist heute nur noch selten zu finden.

## Tastatur K7637

Diese serielle Tastatur auf [Hall-Basis](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#kontaktverfahren) war das verbreitetste Eingabegerät für [K1520-Rechner](https://www.robotrontechnik.de/html/computer/k1520.htm) und wurde ab Januar 1985 verkauft. Sie wurde z.B. am [A5120](https://www.robotrontechnik.de/html/computer/a5120.htm), [K8924](https://www.robotrontechnik.de/html/computer/k8924.htm), [DORAM](https://www.robotrontechnik.de/html/computer/doram.htm) benutzt und löste die [Tastatur K7636](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7636) ab, gegenüber der sie einige Vorteile hatte:  
Durch ein dünneres, längeres Kabel gestattete sie eine größere Freiheit bei der Aufstellung. Außerdem wurde gegenüber der [K7636](https://www.robotrontechnik.de/html/zubehoer/tastaturen.htm#k7636) das Problem des "Buchstaben-Verschluckens" bei sehr schneller Eingabe beseitigt.  
Nachteilig war, dass durch die K7637 der Anschluss des zweiten [IFSS](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#ifss)-Druckers wegfiel, da sie dessen Steckdose auf der [ASS-Platine](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8440) benutzte.  
  

|   |
|---|
|![](https://www.robotrontechnik.de/bilder/Zubehoer/Tastaturen/Tastatur_K7637_2.jpg)  <br>Tastatur K7637, rechts der Sicherheitsstecker|

  
Die K7637 hatte ein [IFSS-Interface](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#ifss) (passiv/passiv, ohne optische Trennung), wurde an die Tastaturbuchse rechts am Gerät (beim [K8924](https://www.robotrontechnik.de/html/computer/k8924.htm) die untere Buchse) angesteckt und rechnerintern mit der [ASS-Platine](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8440) (2. Anschluss von unten) verbunden. Die Betriebsspannung der K7637 wurde tastaturintern mit einem Spannungsregler aus der Rohspannung (12V, vom Rechner geliefert) gewonnen.  
  
Von der K7637 existierten mehrere Versionen. Neben der standardmäßig mit amerikanischen Tastaturlayout bestückten K7637 gab es:

- Varianten ohne Sicherheitsstecker (die dann nicht für [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) verwendet werden können),
- Tastaturen mit deutschem, russischem oder anderssprachigem Tastaturlayout (z.B. K7637.53 = Russisch+Deutsch),
- Tastaturen, die sich in der Beschriftung der Funktionstasten unterschieden,
- Tastaturen, die eine andere Anzahl an Status-LEDs hatten.

Außerdem existierte eine spezielle [CAD-Variante](https://www.robotrontechnik.de/html/computer/k8924.htm#cad), die mit zusätzlichen Cursortasten ausgerüstet war und zusammen mit einem [Digitalisiergerät K6404](https://www.robotrontechnik.de/html/zubehoer/digitizer.htm#k6404) an Konstruktionsrechnern sowie am [Textima-Mustererstellungsplatz](https://www.robotrontechnik.de/html/computer/textima.htm#tes9012-2) benutzt wurde.  
  

|   |
|---|
|![](https://www.robotrontechnik.de/bilder/Zubehoer/Tastaturen/Tastatur_K7637.50_k.jpg)  <br>CAD-Tastatur K7637.50|

  
Der Sicherheitsstecker an der rechten Seite der Tastatur wurde unter dem Betriebssystem [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) zur Authentifizierung des Anwenders genutzt. Bei Verlassen des Rechners war der Stecker abzuziehen, was eine Blockierung der Tastatur und ein Anhalten des Programms bewirkte. Außerdem wurde über den Stecker die Berechtigung des Nutzers überprüft. Abhängig von der Codierung des Steckers wurde zwischen mehreren Nutzerklassen und dem Administrator unterschieden. Der Sicherheitsstecker wurde nur unter dem Betriebssystem [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) wirksam.  
  
Die 5 LEDs oben links waren softwareseitig frei programmierbar, z.B. um auf die Nutzbarkeit der darunter liegenden Funktionstasten hinzuweisen.

## Schnittstellen (Standard-Variante)

- 1x [V.24](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#v24) (universell, für DFÜ), oberster Anschluss auf der [ASS-Karte](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8440).  
    Einstellungen: 50-9600 Baud, synchron / asynchron, Hardwareprotokoll / Softwareprotokoll, 7 oder 8 Datenbits, 1 oder 2 Stopbits, auf Wunsch Paritätskontrolle
- 1x [IFSS](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#ifss) (universell, für DFÜ), 2. Anschluss von oben auf der [ASS-Karte](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8440).  
    Einstellungen: 50-9600 Baud, asynchron, Softwareprotokoll, 7 oder 8 Datenbits, 1 oder 2 Stopbits, auf Wunsch Paritätskontrolle
- 1x [IFSS](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#ifss) (für Tastatur), 3. Anschluss von oben auf der [ASS-Karte](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8440).  
    Einstellungen: 9600 Baud, asynchron, Softwareprotokoll, 7 oder 8 Datenbits, 1 oder 2 Stopbits, auf Wunsch Paritätskontrolle
- 1x [IFSS](https://www.robotrontechnik.de/html/netzwerke/netzwerk.htm#ifss) (nur für Druckeranschluss), unterster Anschluss auf der [ASS-Karte](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm#062-8440).  
    Einstellungen: 9600 Baud, asynchron, Softwareprotokoll, 7 oder 8 Datenbits, 1 oder 2 Stopbits, auf Wunsch Paritätskontrolle

Durch Einsatz weiterer [K1520](https://www.robotrontechnik.de/html/komponenten/k1520pla.htm)-Platinen konnte die Anzahl der Schnittstellen nahezu beliebig ausgebaut werden.

## Stromversorgung

Der A5120 besaß zwei Hauptnetzteile [K0362.03](https://www.robotrontechnik.de/html/komponenten/stv.htm#k0362-03) und [K0362.08](https://www.robotrontechnik.de/html/komponenten/stv.htm#k0362-08), die die Spannungen + 5V und + 12V erzeugten und sich an der Rückseite des Rechners befanden. Die +5V diente hauptsächlich zur Versorgung der [Rechner-Schaltkreise](https://www.robotrontechnik.de/html/komponenten/ic.htm). Die +12V wurde für Leistungsstufen ([Diskettenlaufwerke](https://www.robotrontechnik.de/html/komponenten/fs.htm)), für Schnittstellen sowie zur Speisung des [Bildschirms](https://www.robotrontechnik.de/html/zubehoer/bildschirme.htm#k7222) verwendet. Außerdem werden aus der + 12V über einen Spannungswandler (im Fuß des Rechners eingebaut) die Spannungen -5V und -12V (für [EPROMs](https://www.robotrontechnik.de/html/komponenten/ic.htm#rom) und Schnittstellen) gewonnen. Ein weiteres [Spannungswandlermodul](https://www.robotrontechnik.de/html/komponenten/stv.htm#dcw) wurde (nur bei der [8-Zoll-Disketten-Variante](https://www.robotrontechnik.de/html/computer/a5120.htm#8zoll)) zur Bereitstellung der +24V (für [Diskettenlaufwerke](https://www.robotrontechnik.de/html/komponenten/fs.htm#k5602)) sowie (nur bei der [Magnetkassettenvariante](https://www.robotrontechnik.de/html/computer/a5120.htm#kmbg)) zur Bereitstellung der +15V (für [Kassettenlaufwerke](https://www.robotrontechnik.de/html/komponenten/kmbg.htm#k5200)) benutzt.  
  

|   |   |
|---|---|
|![](https://www.robotrontechnik.de/bilder/PCs/A5120/Komponenten/062-8010_und_062-8020_k.jpg)  <br>NÜ/NEST-Baugruppe, aufgeklappt|![](https://www.robotrontechnik.de/bilder/PCs/A5120/Komponenten/062-9015_k.jpg)  <br>Gleichspannungswandlermodul|

  
Im Rechnerfuß war ein [Stand-By-Netzteil](https://www.robotrontechnik.de/html/komponenten/stv.htm#standby) eingebaut, das die Steuerung des Netztasters übernahm sowie ein kontrolliertes Hoch- und Herunterfahren der Hauptnetzteile bewirkte. Dieses Stand-By-Netzteil arbeitete, sobald Strom auf dem Netzkabel lag und ermöglichte auch ein automatisches Starten des Rechners bei eingehendem [Modem](https://www.robotrontechnik.de/html/zubehoer/modems.htm)-Anruf sowie ein softwareseitiges Herunterfahren des Rechners. Ein ebenfalls im Rechnerfuß untergebrachtes [Netzfilter](https://www.robotrontechnik.de/html/komponenten/stv.htm#nfi) verhinderte, dass sich hochfrequente Störungen vom Rechner über das Netzkabel ausbreiten.  
Eine im Rechnerfuß untergebrachte Diagnose-Anzeige mit 6 roten LED signalisierte den Arbeitszustand der Netzteile: 2x StandBy-Netzteil, 2x Hauptnetzteile, 2x Spannungswandler.  
  

|   |
|---|
|![](https://www.robotrontechnik.de/bilder/PCs/A5120/Komponenten/Spannungsanzeige_k.jpg)  <br>Spannungsanzeige|

  
  

## Start des Rechners

Der Netzschalter befand sich bei den älteren Modellen auf der Tastatur (grüne Taste), bei den jüngeren Modellen vorn rechts am Rechner.  
Der A5120 wurde durch 1-maliges Drücken des Netztasters eingeschaltet. Alternativ dazu konnte er auch automatisch durch den Verbindungsaufbau eines [Modems](https://www.robotrontechnik.de/html/zubehoer/modems.htm) oder einer [GDN](https://www.robotrontechnik.de/html/zubehoer/gdn.htm) eingeschaltet werden.  
Zu beachten war, dass nach dem Ausschalten und nach dem Stecken des Netzsteckers der Einschalter für 15s blockiert war.  
  
Der Rechner suchte anschließend einmalig auf allen Diskettenlaufwerken und Kassettenlaufwerken nach einem Betriebssystem. Hatte er eine geeignete Diskette oder Kassette gefunden, bootete er von dieser. Welches Laufwerk zum Booten benutzt wurde, war egal. Erst das geladene Betriebssystem löschte den Bildschirm. Davor standen also Zufallszeichen auf dem Bildschirm.  
  
Einen RESET des Rechners erreichte man durch 2-maliges Drücken der Netztaste.  
  
Das Ausschalten des Rechners erfolgte durch 3-maliges Drücken der Netztaste.  
Alternativ dazu war ein softwaregesteuertes Ausschalten möglich.  
Nach dem Ausschalten war der Einschalter vor dem Wiedereinschalten für 15s blockiert.  
  
Da die Netzteile des Rechners automatisch kontrolliert ein- und ausgeschaltet wurden, war ein Öffnen der Diskettenlaufwerke vor dem Starten bzw. vor dem Ausschalten nicht unbedingt nötig.  
  
  

## Betriebssysteme

  

|   |
|---|
|![](https://www.robotrontechnik.de/bilder/PCs/A5120/A5120-alt/A5120_1_k.jpg)  <br>Computer A5120 mit Betriebssystem SIOS|

  

- [SIOS1526](https://www.robotrontechnik.de/html/software/sios.htm)...  
    ...war das ursprüngliche Betriebssystem für den A5120, für das er auch hardwareseitig zugeschnitten wurde. [SIOS](https://www.robotrontechnik.de/html/software/sios.htm) unterstützte nahezu die gesamte Hardwarepalette des [K1520](https://www.robotrontechnik.de/html/computer/k1520.htm)-Systems. Da dieses Betriebssystem nicht kompatibel zu internationalen Betriebssystemen war, hatte sich seine Anwendung hauptsächlich auf den kommerziellen Bereich, speziell im Bankwesen beschränkt.
- [UDOS1526](https://www.robotrontechnik.de/html/software/udos.htm#udos1526)  
    Als zweites Betriebssystem kam [UDOS](https://www.robotrontechnik.de/html/software/udos.htm#udos1526) hinzu. Es war hauptsächlich zur Programmentwicklung gedacht, konnte sich aber nie richtig durchsetzen, was auch an dem geringen Softwareangebot lag.
- [MUTOS8000](https://www.robotrontechnik.de/html/software/mutos.htm#mutos8000)  
    Dieses UNIX-kompatible Betriebssystem lief nur auf dem [A5120.16](https://www.robotrontechnik.de/html/computer/a5120.htm#a5120-16). Zum Booten wurde dazu der 8-Bit-Teil des Rechners unter [UDOS](https://www.robotrontechnik.de/html/software/udos.htm#udos1526) hochgefahren und anschließend das [MUTOS](https://www.robotrontechnik.de/html/software/mutos.htm#mutos8000) von Diskette in den 16-Bit-Rechner geladen.
- [BCU880](https://www.robotrontechnik.de/html/software/scp.htm#bcu)  
    Dieses erste CP/M-kompatible Betriebssystem fand nur eine sehr geringe Verbreitung und wurde bald durch [SCP](https://www.robotrontechnik.de/html/software/scp.htm#scp1526) abgelöst.
- [SCP1526](https://www.robotrontechnik.de/html/software/scp.htm#scp1526)  
    Das wohl verbreitetste Betriebssystem für den A5120. Es ermöglichte neben einer komfortablen Diskettenarbeit die Kompatibilität mit Standardsoftware aus dem CP/M-Umfeld.
- [CP/A](https://www.robotrontechnik.de/html/software/scp.htm#cpa)  
    Dieses von der [AdW](https://www.robotrontechnik.de/html/standorte/adw.htm) hergestellte Betriebssystem hatte gegenüber [SCP](https://www.robotrontechnik.de/html/software/scp.htm#scp1526) eine Reihe von Vorzügen: Automatische Hardware-Erkennung beim Booten, automatische Diskettenformat-Erkennung und eine Nutzung der Disketten bis 800 KByte.
- [DAC](https://www.robotrontechnik.de/html/software/scp.htm#dac1526)  
    Dieses Betriebssystem vom [VEB Dampferzeugerbau](https://www.robotrontechnik.de/html/standorte/dampferzeugerbau.htm) ermöglichte Diskettenformate von 790 KByte und hatte bereits eine automatische Diskettenformat-Erkennung.
- [MICRODOS 1526](https://www.robotrontechnik.de/html/software/scp.htm#microdos)  
    Dieses recht seltene Betriebssystem hatte im wesentlichen dieselben Eigenschaften wie [SCP](https://www.robotrontechnik.de/html/software/scp.htm#scp1526).
- [CP/Z](https://www.robotrontechnik.de/html/software/scp.htm#cpz)  
    Dieses recht seltene SCP-ähnliche Betriebssystem wurde von [ZOAZ](https://www.robotrontechnik.de/html/standorte/zoaz.htm) erstellt.
- [CP/M](https://www.robotrontechnik.de/html/software/scp.htm#cpm)  
    Es gab mehrere Varianten von [CP/M](https://www.robotrontechnik.de/html/software/scp.htm#cpm), die meist von den Anwenderfirmen speziell für den A5120 angepasst wurden.

  
  

## Verbreitung

Der A5120 fand in der DDR eine weite Verbreitung, da er die Möglichkeit der dezentralen Datenverarbeitung mit einer großen Anzahl leistungsfähiger Anwendersoftware vereinte. Insgesamt wurden ca. 22.200 Exemplare gebaut. Der Preis des Rechners lag anfangs bei 39.384 [Mark](https://www.robotrontechnik.de/html/standards/preise.htm), später (1987) bei 26.807 [Mark](https://www.robotrontechnik.de/html/standards/preise.htm). Für einen [A5120.16](https://www.robotrontechnik.de/html/computer/a5120.htm#a5120-16) mussten anfangs 48.856,34 [Mark](https://www.robotrontechnik.de/html/standards/preise.htm), später 31.481 [Mark](https://www.robotrontechnik.de/html/standards/preise.htm), bezahlt werden.  
  
Ab 1985 wurde der A5120 zunehmend durch den preiswerteren [PC1715](https://www.robotrontechnik.de/html/computer/pc1715.htm) sowie durch die 16-Bit-Rechner [A7100](https://www.robotrontechnik.de/html/computer/a7100.htm), [A7150](https://www.robotrontechnik.de/html/computer/a7150.htm) und [EC1834](https://www.robotrontechnik.de/html/computer/ec1834.htm) verdrängt.  
  
Der A5120 wurde (ebenso wie der [A5130](https://www.robotrontechnik.de/html/computer/a5130.htm)) wurde unter der Bezeichnung "EC8577" in das [ESER-System](https://www.robotrontechnik.de/html/standards/eser.htm) eingegliedert.  
  
Heute ist der A5120 wegen seines markanten Aussehens und wegen seiner Vielseitigkeit ein geschätztes Sammlerstück.