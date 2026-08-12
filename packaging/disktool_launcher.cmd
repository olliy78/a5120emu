@echo off
rem k1520DiskTool - Dateiaustausch mit K1520-Disketten (Windows).
rem @ROOT@ setzt der Installer.  Gegenstueck zu disktool_launcher.sh.
setlocal

set "ROOT=@ROOT@"

if not exist "%ROOT%\venv\Scripts\pythonw.exe" (
    echo k1520DiskTool: Laufzeitumgebung fehlt in %ROOT%\venv 1>&2
    echo Neu einrichten:  install.ps1 -Prefix "%ROOT%" 1>&2
    exit /b 1
)

set "K1520_HOME=%ROOT%"

rem Derselbe Datenordner wie beim Emulator-Starter - sonst oeffnete der
rem Dateidialog woanders, als der Emulator seine Disketten ablegt.
set "K1520_DATA=@DATEN@"
if "%K1520_DATA%"=="" set "K1520_DATA="

for /f "usebackq delims=" %%D in (`
    "%ROOT%\venv\Scripts\python.exe" -c "import sys; sys.path.insert(0, sys.argv[1]); from app import paths; print(paths.user_data_dir())" "%ROOT%" 2^>nul
`) do set "DATEN=%%D"

if defined DATEN (
    if not exist "%DATEN%" mkdir "%DATEN%" 2>nul
    if exist "%DATEN%" cd /d "%DATEN%"
)

"%ROOT%\venv\Scripts\pythonw.exe" "%ROOT%\app\disktool\main.py" %*
