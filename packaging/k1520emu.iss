; ─────────────────────────────────────────────────────────────────────────────
; K1520-Emulator — Windows-Installationsprogramm (Inno Setup 6.5 oder neuer)
; ─────────────────────────────────────────────────────────────────────────────
;
; Erzeugt aus dem geschnuerten Paket eine `K1520emu-<version>-win-x64-setup.exe`.
; Gebaut wird das von packaging/build_payload.sh --setup (dort steht auch der
; iscc-Aufruf); Entwurf und Begruendungen: doc/design/13_distribution.md §5.1.
;
;   iscc /DVersion=1.2.3 /DPaket="C:\...\k1520emu-1.2.3-windows-x86_64" \
;        /DPyVersion=3.12.13 /DPyRelease=20260807 /DPySha256=… /DPySize=… \
;        /DGwWheel=greaseweazle-1.23-py3-none-any.whl /DGwVersion=1.23 \
;        k1520emu.iss
;
; Die vier Py*-Angaben kommen aus packaging/python_pins.txt und werden vom
; Bauskript durchgereicht — bewusst ohne Vorgabewert hier, damit ein veralteter
; Pin nicht stillschweigend mitgebaut wird.  Die Adresse des Archivs setzt die
; .iss daraus selbst zusammen (siehe PyUrl weiter unten).
;
; MERKE beim Bearbeiten, zweitens: Pascal-Kommentare stehen in geschweiften
; Klammern und SCHACHTELN NICHT.  Ein Inno-Platzhalter wie "app" in geschweiften
; Klammern beendet deshalb mitten im Kommentar den Kommentar, und der Rest der
; Zeile wird als Code gelesen ("Syntax error", passiert am 2026-08-12).  In
; Kommentaren deshalb spitze Klammern schreiben.
;
; MERKE beim Bearbeiten: eine Zeile, deren erstes Zeichen "[" ist, gilt Inno als
; ABSCHNITTSKOPF — auch mitten in einem Pascal-Kommentar.  Ein umgebrochener
; Satz, der zufaellig mit "[Run]" beginnt, endet als
; "Invalid section tag" (passiert am 2026-08-12).  Solche Namen deshalb nicht an
; den Zeilenanfang setzen.
;
; ── Die drei Entscheidungen, die man kennen muss ────────────────────────────
;
; 1. DER ASSISTENT INSTALLIERT SELBST — es gibt kein install.ps1 mehr.
;    Bis 2026-08-14 kopierte Inno nur die Payload und ueberliess alles Weitere
;    einem PowerShell-Skript.  Das kostete drei Dinge: ein schwarzes Fenster
;    ohne Rueckmeldung waehrend eines minutenlangen Vorgangs, eine Abhaengigkeit
;    von Windows PowerShell 5.1 samt Ausfuehrungsrichtlinie, und einen zweiten
;    Installationsweg, der eigene Fehler hatte.  Jetzt macht es der Assistent:
;    herunterladen (mit Fortschrittsbalken und Pruefsumme), auspacken,
;    Laufzeitumgebung einrichten, schlankmachen, Starter schreiben, Rauchtest —
;    jeder Schritt mit Klartext in der Statuszeile und im <app>\bootstrap.log.
;
; 1a. WANN der Bootstrap laeuft, ist eine Entscheidung ueber den FEHLSCHLAG.
;    Das Nachladen steckt in "PrepareToInstall", also VOR dem Kopieren, und
;    nicht — wie bis 2026-08-14 — in "ssPostInstall" danach.  Der Grund ist
;    belegt: eine Ausnahme in ssPostInstall raeumt NICHTS zurueck.  Der
;    Probelauf hinterliess eine halbe Installation samt drei
;    Startmenue-Eintraegen, die ins Leere zeigten — genau der Ausgang, den §3.1
;    ausschliessen will.  Scheitert dagegen PrepareToInstall, ist noch keine
;    Datei kopiert, kein Symbol angelegt und kein Deinstallierer eingetragen;
;    der Assistent zeigt den Grund und bleibt stehen.  Was der Bootstrap bis
;    dahin selbst angelegt hat (python\, venv\), raeumt er in dem Fall weg.
;    Den Fortschritt zeigt dabei eine eigene Seite: die Statuszeile des
;    Kopierschritts gibt es zu diesem Zeitpunkt noch nicht.
;    Im Nachlauf bleibt nur, was die Payload braucht: Starter, Ausweis,
;    Rauchtest.
;
; 2. PYTHON KOMMT DIREKT VON python-build-standalone, NICHT ueber uv.
;    `uv python install` legt zum Schluss einen Junction auf die Nebenversion
;    an; wo OneDrive „Dateien bei Bedarf" laeuft, verweigert dessen
;    Filtertreiber das (os error 448, STATUS_UNTRUSTED_MOUNT_POINT — genau der
;    Fehlschlag auf dem Testgeraet am 2026-08-14, astral-sh/uv #19616).
;    Abschalten laesst sich der Junction nicht.  Das Archiv wird deshalb selbst
;    geladen und ausgepackt: ein gewoehnliches Verzeichnis, kein Reparse-Punkt.
;    Die Pins stehen in packaging/python_pins.txt.  Unter Linux bleibt uv.
;
; 3. DAS DEINSTALLIEREN MACHT INNO SELBST.
;    Frueher rief es install.ps1, weil dessen Loeschriegel („nur was sich
;    ausweist, und daran nur das eigene Inventar") ein pauschales
;    `Type: filesandordirs; Name: "<app>"` ersetzen mussten.  Inno braucht den
;    Riegel nicht: es entfernt von sich aus NUR die Dateien, die es selbst
;    angelegt hat.  Was erst der Bootstrap erzeugt (python\, venv\, bin\ mit den
;    Startern, Protokolle), kennt es nicht — das steht namentlich im Abschnitt
;    zum Deinstallieren.  Fremdes im selben Ordner ueberlebt beides.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Paket
  #error "Bitte /DPaket=<Verzeichnis des geschnuerten Pakets> angeben"
#endif
#ifndef PyVersion
  #error "Bitte /DPyVersion=… angeben (aus packaging/python_pins.txt)"
#endif
#ifndef PyRelease
  #error "Bitte /DPyRelease=… angeben (aus packaging/python_pins.txt)"
#endif
#ifndef PySha256
  #error "Bitte /DPySha256=… angeben (aus packaging/python_pins.txt)"
#endif
#ifndef PySize
  #error "Bitte /DPySize=… angeben (aus packaging/python_pins.txt)"
#endif

; Die Greaseweazle-Anbindung (Zugriff auf ECHTE Diskettenlaufwerke).  Sie liegt
; als fertiges wheel im Paket (Endung .whl, das einspielfertige Format fuer
; Python-Pakete) — nicht auf PyPI und nicht als Quellarchiv, weil
; dessen C-Erweiterung beim Anwender uebersetzt werden muesste und der unter
; Windows keinen Uebersetzer hat (packaging/gw_pins.txt).
;
; LEER ist erlaubt und heisst „ohne Greaseweazle" (build_payload.sh --no-gw):
; dann faellt der Schritt aus, und in der Oberflaeche bleibt der Menuepunkt fuer
; das echte Laufwerk gesperrt.  Ein Fehler ist das nicht — deshalb hier kein
; #error wie bei den Py*-Angaben.
#ifndef GwWheel
  #define GwWheel ""
#endif
#ifndef GwVersion
  #define GwVersion ""
#endif

; Die Adresse setzt die .iss selbst zusammen — sie taucht dadurch nie als
; Argument auf der Kommandozeile auf.  Das ist kein Schoenheitsgrund: die
; MSYS-Shell, mit der gebaut wird, rechnet Argumente, die wie Pfade aussehen,
; in Windows-Pfade um und machte aus "https://…" ein "https:\…".
; `Str` wandelt das Release, das ISPP als ZAHL liest (20260807), in Text.
#define PyUrl "https://github.com/astral-sh/python-build-standalone/releases/download/" \
  + Str(PyRelease) + "/cpython-" + PyVersion + "+" + Str(PyRelease) \
  + "-x86_64-pc-windows-msvc-install_only_stripped.tar.gz"

#define Produkt   "K1520emu"
#define Programm  "A5120-Emulator"
#define Anbieter  "Olaf Krieger"

[Setup]
; Feste Kennung der Anwendung — daran erkennt ein Update seine Vorgaengerin
; und der Deinstallierer seinen Eintrag.  DARF SICH NIE AENDERN.
AppId={{8F3A6B24-15C7-4E9D-9A72-3D1C0B5E7F48}
AppName={#Produkt}
AppVersion={#Version}
AppVerName={#Produkt} {#Version}
AppPublisher={#Anbieter}
VersionInfoVersion=0.0.0.0

; ── Wohin, und mit welchen Rechten ──────────────────────────────────────────
;
; Der Assistent FRAGT (`PrivilegesRequiredOverridesAllowed=dialog`), und
; `{autopf}` folgt der Antwort:
;
;   „Fuer alle Benutzer"  (Administrator)  →  C:\Program Files\K1520emu
;   „Nur fuer mich"       (ohne UAC)       →  %LOCALAPPDATA%\Programs\K1520emu
;
; Bis 2026-08-14 stand hier fest `{localappdata}\K1520emu`.  Das war nicht
; falsch — ohne Administratorrechte KANN nichts nach „Programme" —, aber der
; Ort ist ungewoehnlich: `%LOCALAPPDATA%` ist versteckt, und wer sein Programm
; sucht, findet es dort nicht (so berichtet, 2026-08-14).  Deshalb jetzt
; beides: der uebliche Systemort fuer den, der die Rechte hat, und
; `%LOCALAPPDATA%\Programs` fuer den, der sie nicht hat — das ist der Ort, den
; sich per-user-Installationen unter Windows teilen (VS Code, Signal …), nicht
; `%LOCALAPPDATA%` selbst.
;
; Die Vorgabe bleibt „nur fuer mich" (`PrivilegesRequired=lowest`): eine
; Installation ohne UAC ist der Normalfall, und mehr braucht der Emulator
; nicht.  Zur Laufzeit schreibt er NICHT in sein eigenes Verzeichnis — die
; Arbeitsdisketten liegen im Dokumentenordner, das Protokoll daneben —,
; deshalb ist auch „Programme" ein tauglicher Ort.
;
; Ein VC-Redist entfaellt in beiden Faellen: die Kernbibliothek bringt ihre
; C-Laufzeit selbst mit (/MT, §5.1).
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#Produkt}
DefaultGroupName={#Produkt}
DisableDirPage=no
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Uninstallable=yes
OutputBaseFilename={#Produkt}-{#Version}-win-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Python kommt als .tar.gz — das kann nur die volle Auspackmethode (die
; einfache beherrscht ausschliesslich .7z).  Kostet rund 465 KB im Setup.
ArchiveExtraction=full
; Der Bootstrap laedt Python und Qt nach — das Paket selbst ist winzig, die
; Installation braucht am Ende aber gut 150 MB.
ExtraDiskSpaceRequired=157286400
LicenseFile={#Paket}\LICENSE
; Das Symbol des Programms — fuer das Setup selbst, fuer den Eintrag unter
; „Apps" und (unten) fuer die Verknuepfungen.  Ohne das traegt alles davon das
; Python-Symbol, weil die Verknuepfung auf pythonw.exe zeigt.
SetupIconFile={#Paket}\payload\share\icons\a5120emu.ico
UninstallDisplayIcon={app}\share\icons\a5120emu.ico

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
; ── Die Payload: genau das, was in die Installation gehoert ─────────────────
; `payload\` ist so geschnuert, dass es 1:1 der Wurzel entspricht (bin\, app\,
; share\).  Fruehere Fassungen legten das Paket erst nach <tmp> und kopierten
; von dort — das war noetig, solange install.ps1 kopierte, und ist es nicht
; mehr.  Inno kopiert direkt und weiss dadurch beim Deinstallieren, was ihm
; gehoert.
Source: "{#Paket}\payload\*"; DestDir: "{app}"; \
  Flags: recursesubdirs createallsubdirs ignoreversion
Source: "{#Paket}\VERSION";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#Paket}\requirements.lock"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Paket}\LICENSE";           DestDir: "{app}"; Flags: ignoreversion

; ── Nur waehrend der Installation gebraucht ─────────────────────────────────
; `dontcopy` statt `DestDir: <tmp>`: diese vier braucht der Code SCHON in
; PrepareToInstall, und dieser Abschnitt wird erst DANACH abgearbeitet.  Ein
; `DestDir: <tmp>`-Eintrag laege also noch nicht da, wenn pip ihn braucht.
; Geholt werden sie einzeln mit ExtractTemporaryFile; <tmp> raeumt Inno am Ende
; selbst weg.
Source: "{#Paket}\slim.py";                Flags: dontcopy
Source: "{#Paket}\requirements.lock";      Flags: dontcopy
Source: "{#Paket}\launcher.cmd";           Flags: dontcopy
Source: "{#Paket}\disktool_launcher.cmd";  Flags: dontcopy
; Die Eingabeaufforderung fuer die Konsolenwerkzeuge (Debugger,
; k1520disktool-cli).  Wie die uebrigen Starter eine VORLAGE — der
; Installationsordner wird beim Einrichten eingetragen.
Source: "{#Paket}\k1520dbg.cmd";           Flags: dontcopy
; Das Greaseweazle-wheel.  `dontcopy` aus demselben Grund wie requirements.lock:
; es wird in PrepareToInstall gebraucht, also VOR dem Kopieren.  Der Eintrag
; entfaellt, wenn ohne Greaseweazle geschnuert wurde.
#if GwWheel != ""
Source: "{#Paket}\wheels\{#GwWheel}";        Flags: dontcopy
#endif

; Python selbst steht NICHT hier: es wird schon vor dem Kopieren gebraucht
; (Punkt 1a im Kopf) und deshalb im Code-Abschnitt geladen — mit
; Pruefsummenkontrolle, abbrechbar, ueber WinHTTP und damit ueber den
; Systemproxy.

[Icons]
; Verknuepft wird pythonw.exe DIREKT, nicht die .cmd: eine Verknuepfung auf eine
; Batchdatei oeffnet immer ein Konsolenfenster, das hinter der Oberflaeche
; stehen bliebe.  Die .cmd bleibt fuer den Aufruf von Hand.
; IconFilename ist hier PFLICHT und keine Zier: die Verknuepfung zeigt auf
; pythonw.exe, und ohne eigene Angabe steht im Startmenue das Python-Symbol.
Name: "{group}\{#Programm}"; Filename: "{app}\venv\Scripts\pythonw.exe"; \
  Parameters: """{app}\app\main.py"""; WorkingDir: "{app}"; Comment: "{#Programm}"; \
  IconFilename: "{app}\share\icons\a5120emu.ico"
Name: "{group}\k1520DiskTool"; Filename: "{app}\venv\Scripts\pythonw.exe"; \
  Parameters: """{app}\app\disktool\main.py"""; WorkingDir: "{app}"; Comment: "Dateiaustausch mit K1520-Disketten"; \
  IconFilename: "{app}\share\icons\a5120emu.ico"
; Die Werkzeug-Eingabeaufforderung — NICHT der Debugger.  Der Unterschied ist
; der Grund, warum dieser Eintrag den Zuschnitt aus §10a.2 nicht bricht: ein
; Symbol auf k1520dbg.exe selbst oeffnete ein Fenster, das mangels Diskette
; sofort wieder zuginge; diese .cmd dagegen OEFFNET eine Eingabeaufforderung
; (`cmd /k`) mit gesetztem PATH und bleibt stehen.  Ohne sie findet ein
; Anwender den Debugger nie — er liegt in bin\ und steht in keinem Menue.
; Die Datei entsteht erst im Nachlauf (StarterSchreiben); die Verknuepfung
; darauf darf trotzdem hier stehen, Inno prueft das Ziel nicht.
Name: "{group}\K1520-Werkzeuge (Eingabeaufforderung)"; Filename: "{app}\bin\k1520dbg.cmd"; \
  WorkingDir: "{app}"; Comment: "Debugger k1520dbg und k1520disktool-cli in der Konsole"; \
  IconFilename: "{app}\share\icons\a5120emu.ico"
Name: "{group}\{cm:UninstallProgram,{#Produkt}}"; Filename: "{uninstallexe}"

[UninstallDelete]
; Was INNO angelegt hat, raeumt Inno von selbst weg — hier steht nur, was erst
; waehrend der Installation entsteht und in keiner Dateiliste steht:
;   python\ venv\   die nachgeladene Laufzeitumgebung
;   bin\            die geschriebenen Starter (die DLLs darin kennt Inno)
;   app\ share\     __pycache__ des ersten Laufs neben den bekannten Dateien
;   logs\ *.log     Protokolle des Kerns, falls jemand aus der Installation startet
; Bewusst NICHT `filesandordirs` auf die Wurzel: das Ziel ist im Assistenten
; aenderbar, dort kann also Fremdes liegen.  `dirifempty` raeumt die Wurzel nur
; auf, wenn nach alldem nichts uebrig ist.
Type: filesandordirs; Name: "{app}\python"
Type: filesandordirs; Name: "{app}\venv"
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\app"
Type: filesandordirs; Name: "{app}\share"
Type: filesandordirs; Name: "{app}\logs"
Type: files;          Name: "{app}\k1520_*.log"
Type: files;          Name: "{app}\bootstrap.log"
Type: files;          Name: "{app}\.rauchtest.py"
Type: files;          Name: "{app}\.k1520emu-installation"
; Hinterlassenschaft der Fassungen bis 2026-08-14: dort lag der Installer als
; PowerShell-Skript im Zielverzeichnis, und der Deinstallierer rief ihn auf.
; Wer darueber hinweg aktualisiert, soll die Datei nicht behalten.
Type: files;          Name: "{app}\install.ps1"
Type: dirifempty;     Name: "{app}"

[Code]

const
  { Marken auf dem Fortschrittsbalken der eigenen Vorbereitungsseite.  Er laeuft
    von 0 bis 1000; die Abstaende sind grob nach Dauer bemessen — Qt laden ist
    die laengste Strecke, das Auspacken von Python die zweitlaengste. }
  MarkeAuspacken   = 30;
  MarkeAusgepackt  = 300;
  MarkeVenv        = 340;
  MarkeQt          = 380;
  MarkeQtFertig    = 860;
  MarkeGw          = 890;
  MarkeSchlank     = 960;
  MarkeFertig      = 1000;

var
  HinweisSeite:      TOutputMsgWizardPage;
  AbschlussSeite:    TOutputMsgWizardPage;
  DatenSeite:        TInputDirWizardPage;
  LadeSeite:         TDownloadWizardPage;
  Fortschrittsseite: TOutputProgressWizardPage;
  Schritt:           String;   { Kopfzeile der Fortschrittsseite }
  Protokoll:         String;   { <app>\bootstrap.log, sobald <app> feststeht }
  BalkenVon:         Integer;  { Strecke, auf die eine Rueckmeldung abgebildet wird }
  BalkenBis:         Integer;
  BalkenJetzt:       Integer;

{ ── Rueckmeldung: Statuszeile, Balken, Protokoll ────────────────────────────

  Alle drei Kanaele haengen an denselben Aufrufen, damit sie nicht auseinander
  laufen.  Das Protokoll ist dabei der wichtigste: es ueberlebt das Fenster und
  ist das Erste, wonach man bei einem Fehlschlag fragt. }

procedure Notiere(const Zeile: String);
var
  Puffer: TArrayOfString;
begin
  Log(Zeile);
  { UTF-8 MIT Kennung (BOM): das Protokoll wird von drei Seiten gelesen — vom
    Anwender im Editor, von `Get-Content` unter PowerShell 5.1 (nimmt sonst die
    ANSI-Kodepage an) und von PowerShell 7 in der CI (nimmt UTF-8 an).  Nur mit
    Kennung stimmen alle drei; ohne sie hat eine der drei Kauderwelsch, und das
    ausgerechnet in dem Text, den man bei einem Fehlschlag liest. }
  if Protokoll = '' then
    Exit;
  SetArrayLength(Puffer, 1);
  Puffer[0] := Zeile;
  SaveStringsToUTF8File(Protokoll, Puffer, True);
end;

procedure ZeigeZeile(const S: String);
begin
  if (Fortschrittsseite <> nil) and (Trim(S) <> '') then
    Fortschrittsseite.SetText(Schritt, S);
end;

procedure Melde(const Text: String; const Marke: Integer);
begin
  Notiere('==> ' + Text);
  Schritt := Text;
  BalkenJetzt := Marke;
  if Fortschrittsseite <> nil then begin
    Fortschrittsseite.SetText(Text, '');
    Fortschrittsseite.SetProgress(Marke, MarkeFertig);
  end;
end;

procedure Vorruecken(const Weite, Grenze: Integer);
begin
  { Fuer Vorgaenge, deren Dauer man nicht kennt: der Balken kriecht mit jeder
    Meldung ein Stueck weiter, bleibt aber unter der naechsten Marke stehen. }
  if BalkenJetzt + Weite < Grenze then
    BalkenJetzt := BalkenJetzt + Weite
  else
    BalkenJetzt := Grenze;
  if Fortschrittsseite <> nil then
    Fortschrittsseite.SetProgress(BalkenJetzt, MarkeFertig);
end;

{ ── Fremde Programme aufrufen ───────────────────────────────────────────────

  ExecAndLogOutput ruft die Rueckmeldung ZEILENWEISE waehrend des Laufs auf —
  daran haengen Statuszeile und Protokoll.  Ohne das saehe der Anwender bei
  einem Vorgang von mehreren Minuten genau nichts, und genau das war die Klage
  ueber die alte Fassung. }

procedure AusgabeAufnehmen(const S: String; const Error, FirstLine: Boolean);
begin
  Notiere('    ' + S);
  ZeigeZeile(S);
  { pip nennt jedes Paket beim Laden und beim Einbauen — ein brauchbarer Takt
    fuer den Balken, ohne die Ausgabe wirklich auszuwerten. }
  if (Pos('Downloading ', S) > 0) or (Pos('Installing ', S) > 0) then
    Vorruecken(12, MarkeQtFertig);
end;

function Laufe(const Programm, Parameter, Verzeichnis: String): Integer;
var
  Code: Integer;
begin
  Notiere('--- ' + Programm + ' ' + Parameter);
  if not ExecAndLogOutput(Programm, Parameter, Verzeichnis, SW_HIDE,
                          ewWaitUntilTerminated, Code, @AusgabeAufnehmen) then
  begin
    Notiere('!!! liess sich nicht starten (' + SysErrorMessage(DLLGetLastError) + ')');
    Result := -1;
  end else begin
    Notiere('--- Rueckgabe ' + IntToStr(Code));
    Result := Code;
  end;
end;

{ ── Datenordner ─────────────────────────────────────────────────────────────
  Dieselbe Frage wie `install.sh --data`, und aus demselben Grund: dort
  schreibt der Emulator per Autosave in die Diskettendateien zurueck. }

function DatenVorgabe: String;
begin
  { <userdocs> ist die Known-Folder-API, also derselbe Eintrag, den OneDrive
    beim Umleiten umschreibt — dieselbe Quelle wie app/paths.py. }
  Result := ExpandConstant('{userdocs}\{#Produkt}');
end;

function DatenOrdner: String;
begin
  { Drei Quellen in dieser Reihenfolge: die Seite im Assistenten, `/Daten=…`
    auf der Kommandozeile (im stillen Betrieb ist die Seite leer — /VERYSILENT
    zeigt sie nie), und zuletzt der Vorschlag. }
  Result := '';
  if DatenSeite <> nil then
    Result := Trim(DatenSeite.Values[0]);
  if Result = '' then
    Result := Trim(ExpandConstant('{param:Daten|}'));
  if Result = '' then
    Result := DatenVorgabe;
end;

function AbweichenderDatenOrdner: String;
begin
  { Nur eine ABWEICHENDE Wahl wird in die Starter geschrieben.  Bei der Vorgabe
    bleibt K1520_DATA leer, und app/paths.py loest den Dokumentenordner zur
    Laufzeit auf — folgt damit auch einem spaeteren Umleiten nach OneDrive. }
  Result := DatenOrdner;
  if CompareText(RemoveBackslashUnlessRoot(Result),
                 RemoveBackslashUnlessRoot(DatenVorgabe)) = 0 then
    Result := '';
end;

{ ── Auspacken ───────────────────────────────────────────────────────────────

  Ein .tar.gz braucht ZWEI Durchgaenge: der erste nimmt die Kompression weg und
  hinterlaesst die .tar, der zweite packt sie aus.  Das ist kein Umweg, sondern
  wie 7-Zip (und damit Inno) geschachtelte Archive sieht. }

function Auspackfortschritt(const ArchivName, DateiName: String;
                            const Fortschritt, FortschrittMax: Int64): Boolean;
var
  Anteil: Integer;
begin
  if (FortschrittMax > 0) and (Fortschrittsseite <> nil) then begin
    Anteil := (Fortschritt * 100) / FortschrittMax;
    Fortschrittsseite.SetProgress(
      BalkenVon + ((BalkenBis - BalkenVon) * Anteil) / 100, MarkeFertig);
  end;
  Result := True;
end;

{ ── Der Bootstrap ───────────────────────────────────────────────────────────

  Laeuft VOR dem Kopieren (Punkt 1a im Kopf).  Jeder Schritt gibt bei Erfolg ''
  zurueck und sonst den Grund im Klartext — den zeigt der Assistent dann an,
  ohne irgendetwas installiert zu haben. }

function PythonAuspacken: String;
var
  Archiv, Tar, PyExe: String;
begin
  Result := '';
  Melde('Python {#PyVersion} wird ausgepackt…', MarkeAuspacken);
  BalkenVon := MarkeAuspacken;
  BalkenBis := MarkeAusgepackt;

  Archiv := ExpandConstant('{tmp}\python.tar.gz');
  Tar    := ExpandConstant('{tmp}\python.tar');
  if not FileExists(Archiv) then begin
    Result := 'Das Python-Archiv wurde nicht geladen.';
    Exit;
  end;

  try
    ExtractArchive(Archiv, ExpandConstant('{tmp}'), '', False, @Auspackfortschritt);
  except
    Result := 'Das Python-Archiv liess sich nicht entpacken: ' + GetExceptionMessage;
    Exit;
  end;
  if not FileExists(Tar) then begin
    Result := 'Nach dem Entpacken fehlt python.tar.';
    Exit;
  end;

  { Ein Update packt NICHT ueber die alte Fassung: sonst blieben Dateien einer
    aelteren Fehlerstandes daneben liegen, die dort nichts mehr zu suchen haben. }
  DelTree(ExpandConstant('{app}\python'), True, True, True);
  try
    ExtractArchive(Tar, ExpandConstant('{app}'), '', True, @Auspackfortschritt);
  except
    Result := 'Python liess sich nicht auspacken: ' + GetExceptionMessage;
    Exit;
  end;
  DeleteFile(Tar);

  { Das Archiv traegt <python> als oberste Ebene — daraus wird <app>\python. }
  PyExe := ExpandConstant('{app}\python\python.exe');
  if not FileExists(PyExe) then begin
    Result := 'Python fehlt nach dem Auspacken: ' + PyExe;
    Exit;
  end;
  Notiere('    Python liegt in ' + ExpandConstant('{app}\python'));
end;

function LaufzeitumgebungEinrichten: String;
var
  PyExe, VenvPy, Lock: String;
begin
  Result := '';
  PyExe  := ExpandConstant('{app}\python\python.exe');
  VenvPy := ExpandConstant('{app}\venv\Scripts\python.exe');
  Lock   := ExpandConstant('{tmp}\requirements.lock');

  { Eine vorhandene Laufzeitumgebung wird ERNEUERT, nicht weiterbenutzt: nach
    einem Update zeigen ihre Verweise auf die alte Python-Fassung. }
  Melde('Laufzeitumgebung anlegen', MarkeVenv);
  DelTree(ExpandConstant('{app}\venv'), True, True, True);
  if Laufe(PyExe, '-m venv "' + ExpandConstant('{app}\venv') + '"',
           ExpandConstant('{app}')) <> 0 then begin
    Result := 'Die Laufzeitumgebung liess sich nicht anlegen.';
    Exit;
  end;
  if not FileExists(VenvPy) then begin
    Result := 'Nach dem Anlegen fehlt ' + VenvPy;
    Exit;
  end;

  { --require-hashes: installiert wird nur, was in requirements.lock steht UND
    dessen Pruefsumme stimmt.  --no-input, weil niemand da ist, der antworten
    koennte.  Die Liste kommt aus <tmp>: nach <app> kopiert wird sie erst im
    naechsten Schritt. }
  Melde('Qt und Abhängigkeiten laden (~70 MB) — das dauert einen Moment…', MarkeQt);
  ExtractTemporaryFile('requirements.lock');
  if Laufe(VenvPy,
           '-m pip install --disable-pip-version-check --no-input --no-color'
           + ' --no-warn-script-location --require-hashes -r "' + Lock + '"',
           ExpandConstant('{app}')) <> 0 then begin
    Result := 'Qt und die übrigen Abhängigkeiten liessen sich nicht laden.'
            + #13#10 + 'Kein Netz? Ein Proxy? Ein Virenscanner dazwischen?';
    Exit;
  end;

  { ── Die Anbindung an ECHTE Diskettenlaufwerke ─────────────────────────────

    Das wheel liegt fertig im Paket; seine vier Abhaengigkeiten (crcmod,
    bitarray, pyserial, requests) kamen gerade mit requirements.lock — daher
    `--no-deps`: hier soll nichts mehr aus dem Netz kommen.

    Scheitert es, ist das KEIN Grund, die Installation hinzuwerfen.  Emulator
    und Diskettenwerkzeug laufen ohne; es fehlt nur der Zugriff auf ein echtes
    Laufwerk, und die Oberflaeche sagt das dann von selbst (app\gw\session.py:
    `verfuegbarkeit` sperrt den Menuepunkt mit dem Grund im Tooltip). }
#if GwWheel != ""
  Melde('Greaseweazle einrichten (echte Diskettenlaufwerke)', MarkeGw);
  ExtractTemporaryFile('{#GwWheel}');
  if Laufe(VenvPy,
           '-m pip install --disable-pip-version-check --no-input --no-color'
           + ' --no-warn-script-location --no-deps "'
           + ExpandConstant('{tmp}\{#GwWheel}') + '"',
           ExpandConstant('{app}')) <> 0 then
    Notiere('Warnung: Greaseweazle liess sich nicht einspielen — der Zugriff auf'
          + ' ein echtes Diskettenlaufwerk fehlt.')
  else
    Notiere('    Greaseweazle {#GwVersion} eingerichtet');
#endif

  { Python und Qt bringen ~300 MB mit, keine 15 davon gehoeren dem Emulator.
    slim.py schneidet QML/Quick, Entwicklungswerkzeug, ungenutzte Bindungen und
    CPythons Testsuite heraus; welche Qt-Bibliotheken bleiben, bestimmt dabei
    die PE-Importtabelle und keine Vermutung.  Scheitert das, ist die
    Installation vollstaendig — also gross, aber brauchbar; das ist kein Grund,
    sie hinzuwerfen. }
  Melde('Überflüssiges entfernen', MarkeQtFertig);
  ExtractTemporaryFile('slim.py');
  if Laufe(VenvPy, '"' + ExpandConstant('{tmp}\slim.py') + '" "'
                 + ExpandConstant('{app}') + '"', ExpandConstant('{app}')) <> 0 then
    Notiere('Warnung: Schlankmachen fehlgeschlagen — die Installation bleibt vollständig');

  Melde('Laufzeitumgebung steht', MarkeSchlank);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  { Alles Nachladbare passiert HIER — vor der ersten kopierten Datei.  Was
    dieser Rueckgabewert enthaelt, zeigt der Assistent an; die Installation
    findet dann nicht statt. }
  ForceDirectories(ExpandConstant('{app}'));
  Protokoll := ExpandConstant('{app}\bootstrap.log');
  DeleteFile(Protokoll);
  Notiere('K1520emu {#Version} — Installation');
  Notiere('    Ziel:      ' + ExpandConstant('{app}'));
  Notiere('    Disketten: ' + DatenOrdner);
  Notiere('    Python:    {#PyVersion} (eigene Laufzeitumgebung, kein Systempython)');

  Fortschrittsseite.Show;
  try
    Result := PythonAuspacken;
    if Result = '' then
      Result := LaufzeitumgebungEinrichten;
  finally
    Fortschrittsseite.Hide;
  end;

  if Result <> '' then begin
    { Zurueckgeraeumt wird, was bis hierher entstand — der Anwender soll nach
      einem Fehlschlag keine 150 MB Bruchstueck behalten.  Das Protokoll bleibt
      stehen, es ist ja der Grund. }
    Notiere('!!! ' + Result);
    DelTree(ExpandConstant('{app}\venv'), True, True, True);
    DelTree(ExpandConstant('{app}\python'), True, True, True);
    Result := Result + #13#10 + #13#10 + 'Einzelheiten stehen in:' + #13#10 + Protokoll;
  end;
end;

{ ── Nachlauf: Starter, Ausweis, Rauchtest ───────────────────────────────────

  Hier laeuft die Kopierseite des Assistenten — die hat ihre eigene Statuszeile,
  die eigene Fortschrittsseite ist zu diesem Zeitpunkt wieder weg. }

procedure MeldeImNachlauf(const Text: String);
begin
  Notiere('==> ' + Text);
  if WizardForm <> nil then begin
    WizardForm.StatusLabel.Caption := Text;
    WizardForm.FilenameLabel.Caption := '';
  end;
end;

procedure VorlageSchreiben(const Vorlage, Ziel: String);
var
  Roh:  AnsiString;
  Text: String;
begin
  if not LoadStringFromFile(Vorlage, Roh) then
    RaiseException('Vorlage nicht lesbar: ' + Vorlage);
  Text := Roh;
  StringChangeEx(Text, '@ROOT@', ExpandConstant('{app}'), True);
  StringChangeEx(Text, '@DATEN@', AbweichenderDatenOrdner, True);
  Roh := Text;
  if not SaveStringToFile(Ziel, Roh, False) then
    RaiseException('Starter nicht schreibbar: ' + Ziel);
  Notiere('    ' + Ziel);
end;

procedure StarterSchreiben;
begin
  { Je Maschine EINZELN, nicht ueber eine Schleife: jede braucht ihren eigenen
    Einstiegspunkt.  Kommt der naechste K1520-Rechner, entsteht hier ein
    zweiter Block — und ein zweiter Eintrag im Abschnitt fuer die Symbole. }
  MeldeImNachlauf('Starter schreiben');
  ExtractTemporaryFile('launcher.cmd');
  ExtractTemporaryFile('disktool_launcher.cmd');
  CreateDir(ExpandConstant('{app}\bin'));
  VorlageSchreiben(ExpandConstant('{tmp}\launcher.cmd'),
                   ExpandConstant('{app}\bin\a5120emu.cmd'));
  VorlageSchreiben(ExpandConstant('{tmp}\disktool_launcher.cmd'),
                   ExpandConstant('{app}\bin\k1520disktool.cmd'));

  { Der Debugger bekommt KEIN Symbol und keinen Doppelklick-Starter: er ist
    kein Programm, das man oeffnet und wieder schliesst, sondern eines, das
    neben Editor und Assembler in der Konsole steht.  Statt dessen eine
    Eingabeaufforderung, in der er (und k1520disktool-cli) ohne Pfadangabe
    laufen — ausdruecklich zum Kopieren und Anpassen gedacht
    (doc/design/13_distribution.md §10a.2). }
  ExtractTemporaryFile('k1520dbg.cmd');
  VorlageSchreiben(ExpandConstant('{tmp}\k1520dbg.cmd'),
                   ExpandConstant('{app}\bin\k1520dbg.cmd'));
end;

procedure AusweisSchreiben;
var
  Zeilen: TArrayOfString;
begin
  { Der Ausweis sagt: dieses Verzeichnis gehoert uns.  Geloescht wird danach
    nicht mehr (das macht Inno nach seiner eigenen Buchfuehrung) — er bleibt,
    damit ein Installer, der spaeter hierher zeigt, eine Installation als solche
    erkennt statt sie fuer einen fremden Ordner zu halten. }
  SetArrayLength(Zeilen, 3);
  Zeilen[0] := 'k1520emu {#Version}';
  Zeilen[1] := '# Vom Installationsprogramm angelegt.';
  Zeilen[2] := '# Entfernt wird die Installation ueber Einstellungen -> Apps.';
  SaveStringsToFile(ExpandConstant('{app}\.k1520emu-installation'), Zeilen, False);
end;

function Rauchtest: Integer;
var
  Zeilen: TArrayOfString;
  Datei, Daten: String;
begin
  { Ein kaputter Startmenue-Eintrag ist schlimmer als eine abgebrochene
    Installation: hier wird geladen, was der Emulator beim Start laedt — Kern,
    Formatkatalog, Qt samt Plattform-Plugin und das Hauptfenster.
    Der Text ist bewusst reines ASCII: geschrieben wird er in der ANSI-Kodepage,
    gelesen von Python als UTF-8. }
  MeldeImNachlauf('Selbsttest: Kern, Formatkatalog und Oberfläche laden…');
  Daten := AbweichenderDatenOrdner;
  StringChangeEx(Daten, '\', '\\', True);

  SetArrayLength(Zeilen, 24);
  Zeilen[0]  := 'import ctypes, os, sys';
  Zeilen[1]  := 'os.environ["QT_QPA_PLATFORM"] = "offscreen"';
  Zeilen[2]  := 'sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))';
  { K1520_DATA MUSS hier mit: der Rauchtest legt ueber seed_user_disks() die
    Beispieldisketten beim Anwender an.  Ohne die Variable landen sie an der
    VORGABEstelle statt in dem Ordner, den er gerade gewaehlt hat. }
  Zeilen[3]  := 'daten = "' + Daten + '"';
  Zeilen[4]  := 'if daten: os.environ["K1520_DATA"] = daten';
  Zeilen[5]  := 'from app import paths';
  Zeilen[6]  := 'paths.prepare_library_load()';
  Zeilen[7]  := 'lib = ctypes.CDLL(str(paths.core_library()))';
  Zeilen[8]  := 'lib.k1520_version.restype = ctypes.c_char_p';
  Zeilen[9]  := 'print("Kern:      ", lib.k1520_version().decode())';
  Zeilen[10] := 'import PySide6';
  Zeilen[11] := 'print("PySide6:   ", PySide6.__version__)';
  Zeilen[12] := 'fmt = paths.formats_file()';
  Zeilen[13] := 'if fmt is None:';
  Zeilen[14] := '    sys.exit("formats.yaml nicht gefunden: " + paths.describe())';
  Zeilen[15] := 'print("Katalog:   ", fmt)';
  Zeilen[16] := 'print("Disketten: ", paths.seed_user_disks(), "kopiert nach", paths.user_disks_dir())';
  Zeilen[17] := 'from PySide6.QtWidgets import QApplication';
  Zeilen[18] := 'from app.ui.main_window import MainWindow';
  Zeilen[19] := 'qt = QApplication([])';
  Zeilen[20] := 'MainWindow().close()';
  Zeilen[21] := 'print("Oberflaeche: baut auf")';
  { Die Anbindung an echte Laufwerke ist FREIWILLIG — sie wird gemeldet, nicht
    geprueft.  Ein fehlendes Greaseweazle darf den Rauchtest nicht kippen. }
  Zeilen[22] := 'from app.gw import verfuegbar';
  Zeilen[23] := 'print("Greaseweazle:", "einsatzbereit" if verfuegbar() else "NICHT installiert")';

  Datei := ExpandConstant('{app}\.rauchtest.py');
  if not SaveStringsToFile(Datei, Zeilen, False) then
    RaiseException('Rauchtest liess sich nicht schreiben: ' + Datei);

  Result := Laufe(ExpandConstant('{app}\venv\Scripts\python.exe'),
                  '"' + Datei + '"', ExpandConstant('{app}'));
  DeleteFile(Datei);
end;

procedure Aufraeumen;
var
  Fund: TFindRec;
begin
  { Der Kern legt beim Erzeugen einer Maschine ein Protokoll unter <logs> im
    ARBEITSVERZEICHNIS an — beim Rauchtest also in der frischen Installation.
    Die soll aber nur enthalten, was hineingehoert. }
  DelTree(ExpandConstant('{app}\logs'), True, True, True);
  if FindFirst(ExpandConstant('{app}\k1520_*.log'), Fund) then begin
    try
      repeat
        DeleteFile(ExpandConstant('{app}\') + Fund.Name);
      until not FindNext(Fund);
    finally
      FindClose(Fund);
    end;
  end;
end;

procedure VerknuepfungenEntfernen;
var
  Fund: TFindRec;
  Ordner: String;
begin
  { Nur fuer den Fehlschlag im Nachlauf: eine Verknuepfung, die ins Leere zeigt,
    ist schlimmer als gar keine.  Der Deinstallierer ist zu diesem Zeitpunkt
    schon eingetragen und raeumt den Rest. }
  Ordner := ExpandConstant('{group}');
  if FindFirst(Ordner + '\*.lnk', Fund) then begin
    try
      repeat
        DeleteFile(Ordner + '\' + Fund.Name);
      until not FindNext(Fund);
    finally
      FindClose(Fund);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep <> ssPostInstall then
    Exit;

  { Hier steht nur noch, was die kopierte Payload braucht.  Das Nachladen ist
    zu diesem Zeitpunkt laengst durch (PrepareToInstall). }
  StarterSchreiben;
  AusweisSchreiben;

  if Rauchtest <> 0 then begin
    VerknuepfungenEntfernen;
    Aufraeumen;
    RaiseException('Der Rauchtest ist fehlgeschlagen — die Installation wäre unbrauchbar.'
      + #13#10 + #13#10 + 'Einzelheiten stehen in:' + #13#10 + Protokoll);
  end;
  Aufraeumen;
  Notiere('==> Fertig.');
end;

{ ── Assistent ───────────────────────────────────────────────────────────────}

procedure InitializeWizard;
begin
  { ── Was gleich passieren wird ──────────────────────────────────────────────

    Diese Seite ist keine Hoeflichkeit.  Der Assistent tut zwei Dinge, die ein
    Anwender nicht erwartet und die er erklaert bekommen muss, BEVOR er auf
    „Weiter" drueckt: er laedt waehrend der Installation rund 120 MB aus dem
    Netz, und er braucht dafuer ein paar Minuten.  Ohne die Ansage sieht das
    aus wie ein haengendes Setup.  Der zweite Teil beantwortet die Frage, die
    jeder bei einem unbekannten Programm hat: was macht das mit meinem
    System?  Antwort: nichts ausserhalb seines eigenen Ordners. }
  HinweisSeite := CreateOutputMsgPage(wpLicense,
    'Bevor es losgeht',
    'Was der Assistent tut — und was er nicht anfasst',
    'Der Emulator bringt seine eigene Laufzeitumgebung mit.' + #13#10 + #13#10 +
    'WÄHREND DER INSTALLATION WERDEN RUND 120 MB GELADEN' + #13#10 +
    'Python {#PyVersion} und die Qt-Oberfläche holt der Assistent aus dem Netz —' + #13#10 +
    'einmalig, dafür läuft der Emulator danach ohne Internetverbindung.' + #13#10 +
    'Das dauert je nach Leitung ein paar Minuten; solange sieht der Balken' + #13#10 +
    'unten, wo er gerade steht.' + #13#10 + #13#10 +
    'ECHTE DISKETTEN: DIE GREASEWEAZLE-ANBINDUNG WIRD MIT EINGERICHTET' + #13#10 +
    'Mit einem Greaseweazle-Adapter lesen und beschreiben beide Programme' + #13#10 +
    'echte 5,25"- und 8"-Disketten in einem angeschlossenen Laufwerk.' + #13#10 +
    'Die dafür nötigen Bausteine (Greaseweazle {#GwVersion} und vier kleine' + #13#10 +
    'Python-Pakete) richtet der Assistent gleich mit ein — ohne Adapter' + #13#10 +
    'stören sie nicht, der Menüpunkt bleibt dann einfach gesperrt.' + #13#10 + #13#10 +
    'ALLES BLEIBT IM INSTALLATIONSORDNER' + #13#10 +
    'Python und Qt landen INNERHALB der Installation. Ein vorhandenes Python' + #13#10 +
    'auf diesem Rechner wird nicht angefasst, nichts wird registriert, keine' + #13#10 +
    'Systemdatei geändert. Beim Entfernen über „Einstellungen → Apps" bleibt' + #13#10 +
    'nichts zurück.' + #13#10 + #13#10 +
    'Am Ende sind rund 120 MB belegt. Ihre Arbeitsdisketten liegen bewusst' + #13#10 +
    'ausserhalb davon — wo, fragt die übernächste Seite.');

  { Eigene Seite fuer den Datenordner — dieselbe Frage wie `install.sh --data`,
    und aus demselben Grund: dort schreibt der Emulator per Autosave in die
    Diskettendateien zurueck, und „Dokumente" ist unter Windows haeufig nach
    OneDrive umgeleitet.  Ob jede Diskettenaenderung eine Synchronisation
    ausloest, ist eine Entscheidung des Anwenders. }
  DatenSeite := CreateInputDirPage(wpSelectDir,
    'Arbeitsdisketten',
    'Wo sollen Ihre Disketten liegen?',
    'Der Emulator schreibt Änderungen an einer Diskette in diese Dateien zurück.' + #13#10 +
    'Ein Update fasst den Ordner nie an.' + #13#10 + #13#10 +
    'Liegt der Vorschlag in OneDrive, löst jede Änderung eine Synchronisation aus — ' +
    'wählen Sie dann besser einen Ordner außerhalb.',
    False, '');
  DatenSeite.Add('');
  { `/Daten=…` darf den Vorschlag ueberschreiben — dieselbe Angabe wirkt damit
    im Assistenten wie im stillen Betrieb. }
  DatenSeite.Values[0] := Trim(ExpandConstant('{param:Daten|}'));
  if DatenSeite.Values[0] = '' then
    DatenSeite.Values[0] := DatenVorgabe;

  LadeSeite := CreateDownloadPage('Python wird geladen',
    'Der Emulator bringt seine eigene Laufzeitumgebung mit — sie wird jetzt geholt.', nil);
  LadeSeite.ShowBaseNameInsteadOfUrl := True;

  Fortschrittsseite := CreateOutputProgressPage('Laufzeitumgebung einrichten',
    'Python und Qt werden eingerichtet. Das dauert einige Minuten.');

  { ── Was noch mitkam, aber in keinem Startmenue steht ───────────────────────

    Der Debugger ist das dritte Programm im Paket und bekommt bewusst KEIN
    Symbol (§10a.2 des Entwurfs): er wird in einen vorhandenen Arbeitsablauf
    aus Editor, Assembler und Konsole eingebunden, nicht doppelgeklickt.  Genau
    deshalb braucht er diese Seite — wer nicht erfaehrt, dass es ihn gibt und
    wo sein Handbuch liegt, benutzt ihn nie.

    Sie steht NACH dem Kopieren (wpInfoAfter): vorher waeren die genannten
    Pfade noch leer, und wer abbricht, soll nicht mit Wegbeschreibungen zu
    Dateien zurueckbleiben, die es nicht gibt. }
  AbschlussSeite := CreateOutputMsgPage(wpInfoAfter,
    'Auch mit dabei',
    'Der Debugger k1520dbg — und der Zugriff auf echte Disketten',
    'DER DEBUGGER k1520dbg' + #13#10 +
    'Er untersucht fremde Programme Schritt für Schritt: Haltepunkte,' + #13#10 +
    'Register, Speicher, Rückwärtslaufen, Disassemblat mit Ihrem eigenen' + #13#10 +
    'Quelltext daneben. Kein Fenster, sondern ein Werkzeug für die Konsole —' + #13#10 +
    'deshalb steht es in keinem Startmenü.' + #13#10 + #13#10 +
    '  Programm:  <Installation>\bin\k1520dbg.exe' + #13#10 +
    '  Handbuch:  <Installation>\share\doc\handbuch_k1520dbg.md' + #13#10 +
    '  Dazu:      <Installation>\share\tools\z80_disasm2.py' + #13#10 + #13#10 +
    'Am einfachsten über bin\k1520dbg.cmd: das öffnet eine Eingabe-' + #13#10 +
    'aufforderung, in der k1520dbg und k1520disktool-cli ohne Pfadangabe' + #13#10 +
    'laufen. Die Datei ist zum Kopieren und Anpassen gedacht — Arbeitsordner' + #13#10 +
    'und eigener Assembler stehen als Beispielzeilen darin.' + #13#10 + #13#10 +
    'ECHTE DISKETTEN' + #13#10 +
    'Die Greaseweazle-Anbindung ist eingerichtet. Mit einem angeschlossenen' + #13#10 +
    'Adapter lesen und beschreiben beide Programme echte Disketten:' + #13#10 +
    'im Emulator „Physisch…" am Laufwerkskasten, im Diskettenwerkzeug' + #13#10 +
    'unter „Datei ▸ Physisches Laufwerk…".' + #13#10 + #13#10 +
    'LIZENZEN' + #13#10 +
    'Die Texte der mitgelieferten Fremdsoftware (isocline für die' + #13#10 +
    'Zeilenbearbeitung des Debuggers, Greaseweazle) liegen unter' + #13#10 +
    '<Installation>\share\doc\lizenzen\.');
end;

function UpdateReadyMemo(const Space, NewLine, MemoUserInfoInfo, MemoDirInfo,
  MemoTypeInfo, MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  { Die letzte Seite vor dem Zugriff — hier steht sonst nur das Zielverzeichnis.
    Wer bis hierher geklickt hat, ohne die Hinweisseite zu lesen, soll wenigstens
    DAS noch sehen: es wird geladen, und es dauert. }
  Result := MemoDirInfo + NewLine + NewLine
    + 'Arbeitsdisketten:' + NewLine
    + Space + DatenOrdner + NewLine + NewLine
    + 'Wird aus dem Netz geladen (einmalig, rund 120 MB):' + NewLine
    + Space + 'Python {#PyVersion} und die Qt-Oberfläche' + NewLine
    + Space + 'die Bausteine der Greaseweazle-Anbindung (echte Laufwerke)' + NewLine + NewLine
    + 'Mitgeliefert, also ohne Download:' + NewLine
    + Space + 'Emulator, Diskettenwerkzeug und der Debugger k1520dbg' + NewLine + NewLine
    + 'Alles landet innerhalb der Installation — das übrige System' + NewLine
    + 'bleibt unberührt.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID <> wpReady then
    Exit;

  { Geladen wird VOR der Vorbereitung — und die laeuft vor dem Kopieren.  Bricht
    der Anwender hier ab oder gibt es kein Netz, ist noch nichts geschehen.
    Die Pruefsumme steht im Paket (packaging/python_pins.txt) und reist damit
    mit; eine nebenher geladene Pruefsummendatei deckte nur Uebertragungsfehler
    ab, nicht eine ausgetauschte Quelle. }
  LadeSeite.Clear;
  LadeSeite.Add('{#PyUrl}', 'python.tar.gz', '{#PySha256}');
  LadeSeite.Show;
  try
    try
      LadeSeite.Download;
    except
      if LadeSeite.AbortedByUser then
        Log('Vom Anwender abgebrochen.')
      else
        SuppressibleMsgBox('Python liess sich nicht laden:' + #13#10
          + GetExceptionMessage + #13#10 + #13#10
          + 'Kein Netz? Ein Proxy? Ein Virenscanner dazwischen?',
          mbCriticalError, MB_OK, IDOK);
      Result := False;
    end;
  finally
    LadeSeite.Hide;
  end;
end;
