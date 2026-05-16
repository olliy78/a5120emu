Analysiere das bestehende Projekt. Es handelt sich um einen einfachen Emulator, der einen A5120 Rechner (aus der Familie der K1520 Rechner - alles z80 basiert) emulieren kann. Ich möchte dieses Projekt überführen, in einen Emulator, der neben dem A5120 auch weitere Rechner aus der K1520 Rechnerfamilie emulieren kann. Darüber hinaus gibt es das Tool CPA run, das keinen Rechner Emuliert sindern nur eine CP/M runtime für CPM-80 Programme ist. Das kann so bleiben wie es ist.
Der eigentliche Emulator soll von einem Diskettenimage ein beliebiges Betriebssystem booten können. neben CPA existieren für die Rechner noch andere Betriebssysteme, die nicht CP/M-80 kompatiebel sind, aber dennoch auf dem Emulator laufen sollen.
Dafür muss die Hardware relativ Vollständig emuliert werden können. Analysierer, was notwendig ist, um auf dem Emulator ein beliebiges System booten zu können. Ich kann bei Bedard Dokumente Blockschaltpläne oder Eprominhalte zu den einzelnen Karten des Rechners besorgen
Ich möchte der Software eine Ansprechende UI verpassen. Da dass ganze möglicht Flexiebel und erweiterbar sein soll, würde ich gern einen Kern in C++ und eine darum gebaute Applikation im Python haben. (Sag mir, ob du das sinnvoll findest) Das UI soll ansprechend aussehen und mit Qt6 umgesetzt sein. das läst sich ja sowohl von c++ als auch von Python verwenden.
Die GUI soll vom Aussehen an den konfigurierten Rechner erinnern (Aussehen kann ich dir später beschreiben) Über die Konfiguration soll die verwendete Hardware z.B. Welche Karten verbaut sind konfiguriert werden. in die emulierten Diskettenlaufwerke soll man zur Laufzeit Diskettenimages Laden können, auf die der Rechner zugreifen kann.
Es soll aber auch möglich sein, den Rechner mit einem CLI Interface zu starten. In dem Fall werden Bildschirmausbaben in die Konsole geschrieben statt in den Framebuffer des emulierten Bildschirms (Umgehung des Zeichengenerators) Die PC-Tastatur an der Konsole dient als Eingabe
Erstelle Ein Konzept für eine Überarbeitete Architektur des Emulators unter doc/K1520_architecure.md
Beschreibe darin, wie der Emulator als ganzes aufgebaut ist bzw. wie er umgebaut werden muss. erkläre auch, wie der c++ und der Python teil zusammenarbeiten
Überlege konkret, welche Informationen zur Hardware oder zur in der Firmware vorhandenenFunktionen du benötigst und das System vollständig auszuplanen.
Hast du die Aufgabe verstanden?
Versuche das Problem selbständig zu Lösen und ein möglichst detailiertes Dokument zur Planung der Umsetzung zu erstellen


Sieh dir das Dokument doc/K1520_architecture.md noch mal an. und überarbeite bzw. erweitere es. 
Der neu Konzeptionierte Emulator hat im Grunde nichts mehr mit dem original Emulator zu tum. daher sollten die einzelnen Bestandzeile in eigenen src Verzeichnissen untergebracht werden. überlege dir eine passende Projektstruktur

Ich habe Dokumente und Eprominhalte zum Computer A5120 besorgt. Ich werde Später noch weitere Unterlagen zu den Computern PRG710, PRG 710-1 und K8915 und eventuell noch weiteren Computern aus der K1520 Reihe besorgen. Ich möchte, das du dir die Dokumente ansiehst und dir noch mal ausführliche Gedanken über die modulare struktur des Emulators machst. Das K1520 System besteht auch mehreren Karten, die über eine Backplane verbunden werden. Durch Variierung der Verwendeten Karten lassen sich verschiedene Computer zusammenstellen.
Das Grundprinzip des K1520 ist im Dokument doc/trascripted/Linieninterface BUS K 1520.md beschrieben.
Das Herz des A5120 ist die ZRE K2526 /home/olliy/projects/a5120emu/doc/trascripted/Zentrale Recheneinheit_K2526_K2527.md
Über das K1520 Bussysteme sind daran weitere Komponenten angeschlossen, wie du aus doc/trascripted/Büro Computer Robotron A5120.md entnehmen kannst
Die Tastatur K7637 ist im Grunde ein eigener kleiner Computer mit einer Seriellen Schnittstelle, die an den A5120 angeschlossen wird.

Für den OPS K3526 habe ich kein separates Dokument. Das ist aber im Grunde nur ein 64 KByte D-RAM. Ich denke, die Funktionalität ist recht naheliegend.

Möglicherweise ist es Sinnvoll, die einzelnen Karten als eigene LIBs (.dll oder.so) zu entwickeln, damit sie sich einzeln mit einem Python basierten Testsystem testen lassen. Bei Bedarf kann man es ja später immernoch statisch zusammenlinken. Bitte Überlege dir ein gutes Konzept, das Modular, erweiterbar und gut testbar ist.

Ein in C++ geschriebener Z80 Emulator ist bereits vorhanden. Möglicherweise ist es Sinnvoll Komplexe bausteine wie PIO, SIO, CTC, RAM und EPROM ebenfalls als eigenständige C++ Klassen zu schreiben, so das diese zum Aufbau der Steckkarten verwendet werden können.

Ich stelle mir den Emulator so vor, dass verschiedene Komponenten (z.B. Karten, Tastaturen, Bildschirme, Diskettenlauwerke, Computer-Grundgeräte mit Netzschalter, Reset-Chalter, ...) als eigene C++ Klassen implementiert werden. Beim Starten des Programmes wird eine Konfiguration geladen, die dann einen Teil der vorhandenen Klassen instantiiert und mit dem K1520 basierten Bussystem verbindet oder mit Pereferier. So kann z.B. die Tastatur (doc/trascripted/Serielle Tastatur K 7637.XX.md) mit der seriellen Schnitstellenkarte (doc/trascripted/Anschlußsteuerung K 8025.50 und K 8025.80.md) und bis zu 4 Diskettenlaufwerke mit der Floppy-Karte (doc/trascripted/Floppy Anschlußsteuerung K 5122.md) verbunden werden.

Die Nachbildung der einzelnen Hardwarebaugruppen soll möglichst genau gemäß der Dokumentation erfolgen. bei Bedarf kann ich auch noch Schaltpläne besorgen, die die Schaltung besser beschreiben.

Es gibt einen Daten und Adressbuss, auf den die Karten schreibend oder lesend zugreifen können. Bei der echten Hardware ist genau geregelt, wann wer schreibend zugreifen darf. Das sollte beim Emulator nach dem gleichen Funktionsprinziep auch umsetzbar sein.
Der K1520 enthällt zusätzlich einen Koppelbus, der im A5120 über Wickelbrücken in der Backplane realisiert ist. Darüber werden z.B. Interuptleitungen oder besondere IO-Leitungen verbunden. Es muss also im emulierten K1520 System die Möglichkeit geben, das Karten verschiedene Signale am Koppelbus anmelden und diese dann über eine Konfiguration, die den Wickelbrücken der Backplane entspricht, verbunden werden. Das muss nicht zur laufzeit geändert werden. Ich kann mir vorstellen, das verschiedene Konfigurationen für verschiedene Computer in .h files hinterlegt sind. und bei der Instantiierung des K1520 Grundmoduls wird eine der einkompilierten Konfigurationen gestartet. Da sind dann z.B. für den A5120 alle Signale vorhanden die von den Karten (K2526, K3526, K7024, K8025, K5122) benötigt werden. Die Umschaltung der Variante erfolgt dann über das C-ABI vor dem Start des Computers.

Falls dir die konkrete Ferdratung der Backplane nicht klar ist, sag bescheid, dann muss ich das noch mal ermitteln.

Auch die einzelnen Karten können konfiguriert werden. Dafür sind sie mit DIP-Schaltern oder Wickelbrücken ausgestattet. Desweiteren kann sich der Inhalt der EPROMS bei den verschiedenen Computern unterscheiden. Das onzept dahinter war, das die Firma Robotron diese Karten hergestellt und vertrieben hatt und die Hersteller der Computer diese durch setzen der DIP-Schalter oder Wickelbrücken z.B. auf die richtigen IO-Adressen oder Interupts verknüpfen konnten. Auch hir kann ich mir vorstellen, das die Konfiguration und die EPROM Inhalte in .h files liegt und einkompiliert wird und zur Laufzeit nur festgelegt werden kann od es z.B. eine A5120 Konfiguration oder eine PRG710 Konfiguration handelt.

Ich habe den EPROM der ZRE K2526 in Binärform ausgelesen (doc/EPROMS/Prozessoreinheit_ZRE_K2525_A26.bin). Vermutlich ist es sinnvoll diesen in ein HEX Vormat zu überführen und zum besseren Verständniss zu decompilieren. Dafür gibt es bereits Scripte unter tools/ vielleicht kannst du diese verwenden oder erweitern um den Inhalt des EPROMS zu verstehen.
Die Beiden EPROMS der Bildschirmkarte doc/trascripted/Bildschirmkarte K 7024.30 und K 7024.40.md doc/EPROMS/Bildschirm_ABS_K7024_A103.bin und doc/EPROMS/Bildschirm_ABS_K7024_A123.bin enthalten keinen Z80 Code sondern die Zeichensätze für Latein und Kyrillisch. Vermutlich ist es trozden sinnvoll sie in das HexVormat umzuwandeln um sie zu verstehen und in den Emulator einzubinden.
Ich bin mir nich sicher, was am elegantesten ist, aber ich denke, man kann die EPROM-Inhalte in eine .h Datei überführen und dann einfach von der entsprechenden Karte includieren und fest eincolpilieren. Ein Austauch der EPROM Inhalte ohne neukompilieren ist nicht notwendig

Ich bin mir noch nicht ganz Sicher, wie man den Bildschirm emuliert. Die Applikation soll ja einen Kern aus C++ und eine GUI aus Python + QT haben. Überlege dir eine gute Lösung, wie die in C++ geschriebene Emulation der Bildschirmkarte in einen Framebuffer (mit bekannter Größe) schreibt, der dann in der GUI als grüner CRT Bildschirm ausgegeben wird.

Es soll ja, wie in der doc/K1520_architecture.md beschrieben auch einen Komandozeilenmodus geben. Ich denke, das ist dann eine andere Konfiguration der Bildschirmkarte K7024 und der Tastatur. Die C-API kann ja beides anbieten, ein Framebuffer Interface, das den Zeichengenerator der K7024 verwendet und eine Unicode Schnittstelle, die für eine Consolenausgabe verwendet werden kann. 
Die Tastatur-Komponente, kann ähnlich aufgebaut sein, so dass man entweder die Konsoleneingabe rein pipen kann oder eine in Python erstellte emulierte Tastatur anbinden kann, die vom Aussehen her der original Tastatur entspricht.

Der Floppy Controler, kann etwas vereinfacht werden. Es soll ja nicht tatsächlich analoge Signale von einer Diskette lesen sondern Image Dateien vom Dateisystem des Host einlesen bzw schreiben können. Ein Beispiel für eine solche Datei ist z.B. disk_b.img. Dazu muss noch eine Konfigurationsdatei eingelesen werden, di die Sektoraufteilung beschreibt. siehe dafür: /home/olliy/projects/CPA_Workbench/cpaFormates.cfg. Der Floppy Emulator benötigt also einen Parser um die Konfigurationsdatei lesen zu können. Über die C-ABI teilt die Python GUI dem Floppy Emulator die zu verwendente .img Datei sowie die zu verwendende Laufwerksgeometrie mit. Der A5120 z.B. benötigt eine bestimmte Geometrie der ersten Spuren der Diskette um davon booten zu können. Alle anderen Spuren können eine andere Geometrie haben. Disketten, die nur Daten enthalten brauchen keine Boot Spuren.

Die Python GUI stelle ich mir so vor. Der Emulator z.B. das er einen A5120 emulieren soll und dieser drei Diskettenlaufwerke an einem K5120 Controller hat. Diese sind horizontal untereinander rechts neben dem Bildschirm angeordnet. Jedes "Diskettenlaufwerk" hat ein Auswahlfeld "Diskettentyp" und einen "Selekt" button zum öffen der Datei (über Datei-Dialog des Systems).

Die Seriellen Schnittstellen können über eine ABI aus dem C++ Kern nach außen geführt sein. Da kann man dann später per Python einen emulierten Drucker oder ein Terminal anschließen.

Ich möchte, das du zuerst das Architekturdokument aktualiesierst. Danach möchte ich, das du für jedes Modul, das du für den Emulator benötigst einen detailierten Feinentwurf in einem Separaten Dokument machst. Die Schnittstellen, also wie eine emulierte Karte an den emulierten K1520 ankoppelt soll genau beschrieben sein. inkl der per serieller Schnittstelle angebundenen Tastatur.
Es somm möglich sein, jedes Modul einzeln zu testen. Zazu soll eine Python basierte Testumgebung erzeugt werden.

Ich vermute, es gibt noch technische Fragen, die vor der Umsetzung geklärt werden müssen. Kläre diese mit mir und erstelle dann die notwendigen Dokumente.
 Dinge, die konzeptionell klrr sind, können schon direkt selbständig von dir umgesetzt und getestet werden.


Transcription von Dokumenten
ich habe ein eingescanntes Dokument das zur Betriebsdokumentation eines historischen Computers gehört. Ich möchte dieses Dokument in ein Markdown Dokument überführen. Die Scan-Qualität ist leider zu schlecht für eine normale OCR. Dabei entsteht recht viel "Buchstabensalat". Das Dokument ist in gutem verständlichen Deutsch mit einigen Üblichen Fachbegriffen verfasst. Ich möchte, das du den Text erfasst und als Markdown exportierst. Das Dokument ist 1,1 MB groß und hat 31 Seiten. Ich möchte, das du jede Seite Einzeln transcribierst, damit das Kontextfenster nicht zu groß wird. Gib den transkribierten Text hintereinander aus. Du brauchst keine Pausen zwischen den Seiten zu machen und braucht auch keine Trennlinien für den Seitenwechsel einzufügen. Seitenzahlen sind ebenfalls nicht notwendig. Wichtig ist, das der Text Wort wörtlich 1:1 übertragen wird. Das Dokument enthält Tabellen, die ebenfalls in Markdown Tabellen überführt werden sollen. Wichtig ist die korrekte Wiedergabe des Inhaltes. Das Dokument enthält einige Abbildungen. Versuche aus diesen Abbildungen die wichtigen Informationen zu extrahieren und füge anstelle des Bildes einen kursiven Textblock ein "KI generierte Interpretation der Abbildung: Die Abbildung zeigt, ...". Die Bildunterschriften sollen unverändert erhalten bleiben.
Falls du keine weiteren Fragen hasst, beginne mit der schrittweisen transcription des Kompletten Dokumentes ohne zwischendurch anzuhalten.


Temenspeicher:
Im Architekturdokument ist die Beispielhafte Datesellung der Karten Im Rahmen des A5120 falsch dargestellt.
Richige Reihenfolge (links nach rechts):
1: OPS K2526 RAM
2: AFS K5122 Floppy
3: ASS K8025 Serielle Schnittstellen
4: ZRE K2626 Zentrale Recheneinheit
5: ABS K7024 Bildschirm

beim A5120.16:
1: ASS K8025 Serielle Schnittstellen
2: 062-9005 z8000 CPU
3: 062-9000 z8000 RAM
4: ZRE K2626 Zentrale Recheneinheit
5: ABS K7024 Bildschirm
6: AFS K5122 Floppy
7: OPS K2526 RAM

