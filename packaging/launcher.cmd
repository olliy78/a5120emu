@echo off
rem K1520-Emulator - Starter des A5120 (Windows).  @ROOT@ setzt der Installer.
rem
rem Gegenstueck zu launcher.sh.  Fuer den Aufruf von Hand und aus der
rem Eingabeaufforderung; die Verknuepfung im Startmenue zeigt dagegen direkt auf
rem pythonw.exe, weil eine Verknuepfung auf eine Batchdatei immer ein
rem Konsolenfenster oeffnet, das hinter der Oberflaeche stehen bliebe.
setlocal

set "ROOT=@ROOT@"

if not exist "%ROOT%\venv\Scripts\pythonw.exe" (
    echo K1520-Emulator: Laufzeitumgebung fehlt in %ROOT%\venv 1>&2
    echo Neu einrichten:  install.ps1 -Prefix "%ROOT%" 1>&2
    exit /b 1
)

set "K1520_HOME=%ROOT%"

rem Datenordner, falls der Anwender beim Installieren einen ABWEICHENDEN gewaehlt
rem hat; sonst bleibt die Zeile leer und app/paths.py loest den Dokumentenordner
rem selbst auf (folgt damit auch einer spaeteren Umleitung nach OneDrive).
set "K1520_DATA=@DATEN@"
if "%K1520_DATA%"=="" set "K1520_DATA="

rem Arbeitsverzeichnis = Benutzerdaten.  Der Kern legt sein Protokoll unter
rem `logs\` im ARBEITSVERZEICHNIS an (k1520_api.cpp); ohne den Wechsel entstuende
rem es dort, wo der Anwender gerade zufaellig steht.  Gefragt wird die
rem Pfadaufloesung des Emulators, statt die Regel hier ein drittes Mal
rem aufzuschreiben - sie kostet einen Interpreterstart, bleibt dafuer richtig,
rem wenn der Dokumentenordner spaeter umgeleitet wird.
for /f "usebackq delims=" %%D in (`
    "%ROOT%\venv\Scripts\python.exe" -c "import sys; sys.path.insert(0, sys.argv[1]); from app import paths; print(paths.user_data_dir())" "%ROOT%" 2^>nul
`) do set "DATEN=%%D"

if defined DATEN (
    if not exist "%DATEN%" mkdir "%DATEN%" 2>nul
    if exist "%DATEN%" cd /d "%DATEN%"
)

"%ROOT%\venv\Scripts\pythonw.exe" "%ROOT%\app\main.py" %*
