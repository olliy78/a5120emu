; ─────────────────────────────────────────────────────────────────────────────
; K1520-Emulator — Windows-Installationsprogramm (Inno Setup 6)
; ─────────────────────────────────────────────────────────────────────────────
;
; Erzeugt aus dem geschnuerten Paket eine `K1520emu-<version>-win-x64-setup.exe`.
; Gebaut wird das von packaging/build_payload.sh --setup (dort steht auch der
; iscc-Aufruf); Entwurf und Begruendungen: doc/design/13_distribution.md §5.1.
;
;   iscc /DVersion=1.2.3 /DPaket="C:\...\k1520emu-1.2.3-windows-x86_64" k1520emu.iss
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
; ── Die zwei Entscheidungen, die man kennen muss ────────────────────────────
;
; 1. DAS SETUP INSTALLIERT NICHT SELBST, es ruft install.ps1.
;    Inno kopiert nur das Paket in ein Temporaerverzeichnis; alles Weitere —
;    Python und Qt nachladen, venv, Schlankmachen, Rauchtest — macht dasselbe
;    Skript, das auch dem .zip beiliegt.  So gibt es EINEN Installationsweg, der
;    auch wirklich benutzt (und in der CI gefahren) wird, statt zweier, von denen
;    einer stillschweigend veraltet.
;
; 2. DAS SETUP LOESCHT NICHT SELBST, es ruft install.ps1 -Uninstall.
;    Ein `[UninstallDelete] Type: filesandordirs; Name: "{app}"` waere einfacher,
;    umginge aber die beiden Riegel: geloescht wird nur, was sich ausweist, und
;    daran nur das Inventar aus dem Ausweis — Fremdes im selben Ordner ueberlebt.
;    Das Zielverzeichnis ist im Assistenten aenderbar, also kann dort alles
;    stehen; unter Linux hat genau diese Konstellation einmal ein
;    Heimatverzeichnis gekostet.  install.ps1 kommt deshalb mit nach {app} und
;    kann von dort allein deinstallieren (sein -Uninstall-Zweig laeuft VOR der
;    Payload-Pruefung).

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Paket
  #error "Bitte /DPaket=<Verzeichnis des geschnuerten Pakets> angeben"
#endif

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

; Kein UAC, kein Administrator: die ganze Installation ist benutzerlokal
; (Anforderung A4).  Damit entfaellt auch das VC-Redist — die Kernbibliothek
; bringt ihre C-Laufzeit selbst mit (/MT, §5.1).
PrivilegesRequired=lowest
DefaultDirName={localappdata}\{#Produkt}
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
; Der Bootstrap laedt Python und Qt nach — das Paket selbst ist winzig, die
; Installation braucht am Ende aber gut 150 MB.
ExtraDiskSpaceRequired=157286400
LicenseFile={#Paket}\LICENSE

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
; Das ganze Paket ins Temporaerverzeichnis — von dort arbeitet install.ps1.
Source: "{#Paket}\*"; DestDir: "{tmp}\paket"; Flags: recursesubdirs createallsubdirs ignoreversion
; …und der Installer zusaetzlich nach {app}, damit das Deinstallieren ihn spaeter
; noch findet (siehe Kopf, Punkt 2).
Source: "{#Paket}\install.ps1"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Verknuepft wird pythonw.exe DIREKT, nicht die .cmd: eine Verknuepfung auf eine
; Batchdatei oeffnet immer ein Konsolenfenster, das hinter der Oberflaeche
; stehen bliebe.  Deshalb bekommt install.ps1 oben auch -NoShortcut — die
; Eintraege macht das Setup, damit der Deinstallierer sie kennt.
Name: "{group}\{#Programm}"; Filename: "{app}\venv\Scripts\pythonw.exe"; \
  Parameters: """{app}\app\main.py"""; WorkingDir: "{app}"; Comment: "{#Programm}"
Name: "{group}\k1520DiskTool"; Filename: "{app}\venv\Scripts\pythonw.exe"; \
  Parameters: """{app}\app\disktool\main.py"""; WorkingDir: "{app}"; Comment: "Dateiaustausch mit K1520-Disketten"
Name: "{group}\{cm:UninstallProgram,{#Produkt}}"; Filename: "{uninstallexe}"

[UninstallDelete]
; Gezielt DIESE zwei Dateien, nicht das Verzeichnis: install.ps1 legt das Setup
; selbst dorthin (damit das Deinstallieren es findet), bootstrap.log entsteht
; beim Installieren.  Beide kennt das Inventar im Ausweis nicht, sie blieben
; sonst stehen.  Ein `Type: filesandordirs; Name: "{app}"` waere hier die
; bequeme, aber falsche Abkuerzung — siehe Kopf, Punkt 2.
Type: files; Name: "{app}\install.ps1"
Type: files; Name: "{app}\bootstrap.log"
Type: dirifempty; Name: "{app}"

[UninstallRun]
; Siehe Kopf, Punkt 2: das Loeschen macht install.ps1 mit seinen beiden Riegeln,
; nicht Inno mit einem pauschalen rmdir.
Filename: "powershell.exe"; \
  Parameters: "-ExecutionPolicy Bypass -NoProfile -File ""{app}\install.ps1"" -Prefix ""{app}"" -Uninstall"; \
  RunOnceId: "K1520emuDeinstallieren"; Flags: waituntilterminated

[Code]
var
  DatenSeite: TInputDirWizardPage;

procedure InitializeWizard;
begin
  { Eigene Seite fuer den Datenordner — dieselbe Frage wie `install.ps1 -Data`
    und `install.sh --data`, und aus demselben Grund: dort schreibt der Emulator
    per Autosave in die Diskettendateien zurueck, und „Dokumente" ist unter
    Windows haeufig nach OneDrive umgeleitet.  Ob jede Diskettenaenderung eine
    Synchronisation ausloest, ist eine Entscheidung des Anwenders. }
  DatenSeite := CreateInputDirPage(wpSelectDir,
    'Arbeitsdisketten',
    'Wo sollen Ihre Disketten liegen?',
    'Der Emulator schreibt Änderungen an einer Diskette in diese Dateien zurück.' + #13#10 +
    'Ein Update fasst den Ordner nie an.' + #13#10 + #13#10 +
    'Liegt der Vorschlag in OneDrive, löst jede Änderung eine Synchronisation aus — ' +
    'wählen Sie dann besser einen Ordner außerhalb.',
    False, '');
  DatenSeite.Add('');
  DatenSeite.Values[0] := ExpandConstant('{userdocs}\{#Produkt}');
end;

function DatenOrdner(Param: String): String;
begin
  { Im stillen Betrieb (/VERYSILENT) wird der Assistent nie gezeigt — dann steht
    hier nichts, und der Vorschlag muss einspringen.  Ohne diese Reserve bekaeme
    install.ps1 einen leeren -Data-Wert. }
  Result := '';
  if DatenSeite <> nil then
    Result := Trim(DatenSeite.Values[0]);
  if Result = '' then
    Result := ExpandConstant('{userdocs}\{#Produkt}');
end;

{ ── Der Bootstrap ────────────────────────────────────────────────────────────

  Bewusst hier statt im Abschnitt "Run": **Inno ignoriert dessen Exit-Code
  stillschweigend.**  Scheiterte install.ps1, meldete das Setup trotzdem Erfolg
  und hinterliesse einen Startmenue-Eintrag, der ins Leere zeigt — der
  unangenehmste aller Ausgaenge (§3.1: „ein kaputter Startmenue-Eintrag ist
  schlimmer als eine abgebrochene Installation").  Genau so ist es am
  2026-08-12 im ersten Lauf passiert: Setup gruen, Installation leer.

  Die Ausgabe geht zusaetzlich in <app>\bootstrap.log — bei einem Vorgang, der
  Minuten dauert und Netz braucht, ist das Protokoll die halbe Fehlersuche.
  (Die geschweiften Klammern sind hier bewusst spitze: ein "app" in geschweiften
  Klammern BEENDET diesen Kommentar, Pascal schachtelt sie nicht.) }
procedure CurStepChanged(CurStep: TSetupStep);
var
  Befehl, Protokoll: String;
  Code: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;

  Protokoll := ExpandConstant('{app}\bootstrap.log');
  { Ueber cmd.exe, weil nur so die Ausgabe umgeleitet werden kann.
    -ExecutionPolicy Bypass: das Skript ist nicht signiert. }
  Befehl := '/c powershell.exe -ExecutionPolicy Bypass -NoProfile -File "'
          + ExpandConstant('{tmp}\paket\install.ps1') + '"'
          + ' -Prefix "' + ExpandConstant('{app}') + '"'
          + ' -Data "' + DatenOrdner('') + '"'
          + ' -Yes -NoShortcut > "' + Protokoll + '" 2>&1';

  if WizardForm <> nil then
    WizardForm.StatusLabel.Caption :=
      'Python und Qt werden geladen (~120 MB) — das dauert einen Moment…';

  if not Exec(ExpandConstant('{cmd}'), Befehl, '', SW_SHOW,
              ewWaitUntilTerminated, Code) then
    RaiseException('Der Bootstrap liess sich nicht starten (PowerShell nicht gefunden?).');

  if Code <> 0 then
    RaiseException('Die Installation ist fehlgeschlagen (Code ' + IntToStr(Code) + ').'
      + #13#10 + 'Einzelheiten stehen in:' + #13#10 + Protokoll);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
end;
