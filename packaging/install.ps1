<#
.SYNOPSIS
    K1520-Emulator — Installation unter Windows, ohne Administratorrechte.

.DESCRIPTION
    Gegenstück zu install.sh und bewusst Zeile für Zeile daran entlang gebaut:
    dieselben Schritte, dieselben Riegel, dieselben Meldungen.  Wer eines von
    beiden ändert, ändert das andere mit.

    Installiert die mitgelieferte Payload (Kernbibliothek + GUI + Daten) in ein
    benutzereigenes Verzeichnis und richtet dort eine eigene Laufzeitumgebung
    ein: uv holt einen Python und legt ein venv mit PySide6 an.  Nichts davon
    berührt das System — keine Administratorrechte, kein Systempython, keine
    Registrierungseinträge unter HKLM, kein VC-Redist (die DLL bringt ihre
    C-Laufzeit selbst mit, siehe doc/design/13_distribution.md §5.1).

.PARAMETER Prefix
    Installationsverzeichnis.  Ohne Angabe wird gefragt.

.PARAMETER Data
    Ordner für Arbeitsdisketten und Zustände.  Ohne Angabe wird gefragt.

.PARAMETER Yes
    Nicht fragen, Vorschläge verwenden.

.PARAMETER NoSlim
    Python und Qt vollständig lassen (~316 statt ~150 MB).

.PARAMETER Uninstall
    Installation entfernen (Benutzerdaten bleiben, siehe -Purge).

.EXAMPLE
    .\install.ps1
    .\install.ps1 -Prefix D:\K1520emu -Data D:\Disketten
    .\install.ps1 -Uninstall -Purge

.NOTES
    Entwurf: doc/design/13_distribution.md

    DIESE DATEI BRAUCHT IHREN UTF-8-BOM.  Windows PowerShell 5.1 — das, was auf
    jedem Windows vorhanden ist und das der Inno-Setup-Bootstrap aufruft — liest
    eine .ps1 OHNE BOM in der ANSI-Codepage, nicht als UTF-8.  Aus den 120
    Zeilen mit Umlauten und Gedankenstrichen wird dann Kauderwelsch, und der
    Parser bricht mit „Unexpected token" ab, lange bevor eine Zeile ausgefuehrt
    wird.  PowerShell 7 (pwsh) nimmt UTF-8 als Vorgabe an und merkt davon
    nichts — genau deshalb lief das Skript im CI-Schritt und im Setup nicht.
    Guard: test_ps1_hat_utf8_bom.
#>
[CmdletBinding()]
param(
    [string] $Prefix,
    [string] $Data,
    [string] $PythonVersion,
    [switch] $Yes,
    [switch] $NoShortcut,
    [switch] $NoSlim,
    [switch] $KeepCache,
    [switch] $Uninstall,
    [switch] $Purge
)

# Jeder Fehler bricht ab — ein halb eingerichteter Startmenü-Eintrag ist
# schlimmer als eine abgebrochene Installation.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$SelfDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# ─── Meldungen ───────────────────────────────────────────────────────────────
# Wortlaut wie in lib/common.sh, damit Anleitungen für beide Systeme gelten.

function Info($t) { Write-Host "==> $t"       -ForegroundColor Cyan }
function Ok($t)   { Write-Host "  ok $t"      -ForegroundColor Green }
function Warn($t) { Write-Host "Warnung: $t"  -ForegroundColor Yellow }
function Die($t)  { Write-Host "Fehler: $t"   -ForegroundColor Red; exit 1 }

# ─── Vorgaben ────────────────────────────────────────────────────────────────

#: Dieselbe Python-Fassung wie beim Schnüren — sonst passen die Wheels aus
#: requirements.lock nicht (K1520_PY_VERSION in lib/common.sh).
$PyVersion = if ($PythonVersion) { $PythonVersion } else { '3.12' }

#: Vorgeschlagenes Ziel.  %LOCALAPPDATA% ist der per-user-Ort für Programme
#: (§5.1) — kein Schreibrecht unter %ProgramFiles% nötig, kein UAC.
$PrefixVorgabe = Join-Path $env:LOCALAPPDATA 'K1520emu'

#: Erkennungsmerkmal einer Installation (identisch mit INSTALL_MARKER in
#: lib/common.sh — beide Installer müssen dieselbe Installation erkennen).
$InstallMarker = '.k1520emu-installation'

#: Was der Installer im Zielverzeichnis anlegt — und ausschließlich das entfernt
#: das Deinstallieren wieder.  Die Liste wandert in den Ausweis, sodass eine
#: ältere Installation nach ihrer EIGENEN Liste abgeräumt wird.
$Inventar = @('bin', 'app', 'share', 'venv', 'python', 'tools',
              '.cache-uv', 'VERSION', 'requirements.lock', 'LICENSE')

#: Maschinen und Werkzeuge, die einen Starter bekommen (MASCHINEN/WERKZEUGE in
#: install.sh).  Ein neuer Name gehört hier hinein UND braucht einen Block beim
#: Starterschreiben — das Deinstallieren räumt danach von selbst mit auf.
$Maschinen = @('a5120emu')
$Werkzeuge = @('k1520disktool')
$Starter   = $Maschinen + $Werkzeuge

$StartMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'

function Datei-Pruefsumme([string] $datei) {
    <#
      SHA-256 einer Datei — über .NET, nicht über `Get-FileHash`.

      Warum: dieses Skript läuft auf FREMDEN Rechnern, und `Get-FileHash`,
      `Invoke-WebRequest` und `Expand-Archive` stecken in nachladbaren Modulen.
      Ist das Nachladen gestört — ein geerbter PSModulePath, eine
      Gruppenrichtlinie —, fehlen sie, und der Installer bricht mit „is not
      recognized as the name of a cmdlet" ab.  Genau so am 2026-08-12 im
      Setup-Lauf, während derselbe Aufruf direkt funktionierte.

      Die .NET-Grundtypen sind immer da.  Ein Installer soll so wenig
      voraussetzen wie möglich — hier ist das keine Vorsicht, sondern belegt.
    #>
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $strom = [System.IO.File]::OpenRead($datei)
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($strom)) -replace '-', '').ToLower()
    } finally {
        $strom.Dispose(); $sha.Dispose()
    }
}

# ─── Fremde Programme aufrufen ───────────────────────────────────────────────

function Fuehre-Aus {
    <#
      Ein natives Programm starten und seinen Exit-Code zurückgeben.

      Warum eigens dafür eine Funktion: `$ErrorActionPreference = 'Stop'` gilt in
      PowerShell auch für die STANDARDFEHLERAUSGABE eines nativen Programms.
      Schreibt uv dort seine Fortschrittsmeldung („Downloading cpython…
      (20.9MiB)"), wirft PowerShell einen NativeCommandError — und bricht damit
      eine Installation ab, die gerade erfolgreich läuft.  Genau so am
      2026-08-12 passiert.

      Innerhalb dieses Aufrufs gilt deshalb 'Continue'; geurteilt wird über den
      EXIT-CODE, nicht über die Ausgabe.  Das ist auch sachlich richtig: ob ein
      Programm erfolgreich war, sagt sein Rückgabewert, nicht welchen Kanal es
      für seine Meldungen benutzt.
    #>
    param(
        [Parameter(Mandatory)] [string]   $Programm,
        [string[]] $Argumente = @(),
        [switch]   $Leise
    )
    $alt = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($Leise) {
            & $Programm @Argumente 2>&1 | Out-Null
        } else {
            & $Programm @Argumente 2>&1 | ForEach-Object { Write-Host $_ }
        }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $alt
    }
}

# ─── Pfad-Hilfsmittel ────────────────────────────────────────────────────────

function Absoluter-Pfad([string] $p) {
    <#
      Gegenstück zu abs_path.  `Resolve-Path` scheitert an noch nicht
      existierenden Verzeichnissen — genau der Normalfall hier.

      Der bereits absolute Fall MUSS eigens abgefangen werden: PowerShells
      `Join-Path` hängt blind aneinander, auch wenn der zweite Teil schon eine
      Wurzel hat (anders als .NETs `Path.Combine`).  Aus `-Prefix D:\temp\x`
      wurde so `D:\a\repo\D:\temp\x` — die Installation landete im Arbeits-
      verzeichnis, und die Sperrliste prüfte einen Pfad, den es nie gab.
      `install.sh` unterscheidet an derselben Stelle (`case "$_p" in /*)`).
    #>
    if ([string]::IsNullOrWhiteSpace($p)) { return '' }
    $p = [Environment]::ExpandEnvironmentVariables($p)
    if ($p.StartsWith('~')) { $p = Join-Path $env:USERPROFILE $p.Substring(1).TrimStart('\', '/') }
    if (-not [System.IO.Path]::IsPathRooted($p)) {
        $p = Join-Path (Get-Location).Path $p
    }
    return [System.IO.Path]::GetFullPath($p)
}

function Dokumente-Dir {
    <#
      Der Dokumentenordner — DIESELBE Quelle wie app/paths.py::_windows_documents.

      GetFolderPath('MyDocuments') liest die Known-Folder-API, also den Eintrag
      `User Shell Folders → Personal`, den OneDrive beim Umleiten umschreibt.
      Ein fest verdrahtetes %USERPROFILE%\Documents läge bei umgeleitetem Ordner
      daneben — und `-Purge` räumte dann woanders auf, als der Emulator schreibt.
      Guard: test_dokumentenordner_ps1_und_python_stimmen_ueberein.
    #>
    $d = [Environment]::GetFolderPath('MyDocuments')
    if ($d -and (Test-Path -LiteralPath $d -PathType Container)) { return $d }
    return $null
}

function Benutzerdaten-Dir {
    $docs = Dokumente-Dir
    if ($docs) { return (Join-Path $docs 'K1520emu') }
    return (Join-Path $env:APPDATA 'K1520emu')
}

function Ist-Installation([string] $dir) {
    # Gegenstück zu ist_installation.  Ältere Installationen ohne Ausweis werden
    # am Inventar erkannt, damit ein Update über sie hinweggeht.
    if (Test-Path -LiteralPath (Join-Path $dir $InstallMarker) -PathType Leaf) { return $true }
    return ((Test-Path -LiteralPath (Join-Path $dir 'VERSION')      -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $dir 'app\paths.py') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $dir 'share\k1520emu') -PathType Container))
}

#: Dateien, die das INSTALLATIONSPROGRAMM selbst im Ziel ablegt, bevor der
#: Bootstrap überhaupt läuft: Inno legt seinen Deinstallierer dorthin, dazu
#: kommen dieses Skript (damit das Deinstallieren es später findet) und sein
#: Protokoll.  Beim Urteil „ist das Verzeichnis frei?" zählen sie nicht mit —
#: sonst verweigerte der Riegel ausgerechnet die Installation, die ihn
#: mitbringt.  Die Liste ist bewusst kurz und namentlich: alles andere, was der
#: Anwender dort liegen hat, lässt den Riegel weiterhin zuschlagen.
$EigeneDateien = @('unins000.exe', 'unins000.dat', 'unins001.exe', 'unins001.dat',
                   'install.ps1', 'bootstrap.log')

function Ist-Leer([string] $dir) {
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) { return $true }
    $fremd = Get-ChildItem -LiteralPath $dir -Force |
             Where-Object { $EigeneDateien -notcontains $_.Name }
    return -not ($fremd | Select-Object -First 1)
}

function Ersetze-Platzhalter([string] $vorlage, [string] $wurzel, [string] $daten) {
    # @DATEN@ bleibt LEER, wenn der Anwender die Vorgabe genommen hat — der
    # Starter setzt K1520_DATA dann gar nicht, und app/paths.py löst den
    # Dokumentenordner weiter zur Laufzeit auf (folgt also einem späteren
    # Umleiten nach OneDrive).  Siehe ersetze_platzhalter in lib/common.sh.
    (Get-Content -LiteralPath $vorlage -Raw -Encoding UTF8).
        Replace('@ROOT@', $wurzel).Replace('@DATEN@', $daten)
}

# ─── Vorhandene Installation finden ──────────────────────────────────────────
#
# Gegenstück zu vorhandene_installation.  Unter Windows gibt es kein
# ~/.local/bin mit Symlinks; die Spur ist die Verknüpfung im Startmenü.  Ohne
# das schlüge der Installer beim Update den Standardort vor, und wer damals
# woanders installiert hat, bekäme eine ZWEITE Installation.

function Vorhandene-Installation {
    foreach ($m in $Maschinen) {
        $lnk = Join-Path $StartMenu "K1520emu\$m.lnk"
        if (-not (Test-Path -LiteralPath $lnk -PathType Leaf)) { continue }
        try {
            $sh = New-Object -ComObject WScript.Shell
            $ziel = $sh.CreateShortcut($lnk).TargetPath
        } catch { continue }
        if (-not $ziel) { continue }
        # <wurzel>\venv\Scripts\pythonw.exe  →  <wurzel>
        $wurzel = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $ziel))
        if ($wurzel -and (Test-Path -LiteralPath $wurzel -PathType Container)) { return $wurzel }
    }
    return $null
}

$PrefixGesetzt = [bool] $Prefix
$DatenGesetzt  = [bool] $Data
$Gefunden      = $false

if (-not $PrefixGesetzt) {
    $vorhanden = Vorhandene-Installation
    if ($vorhanden) { $Prefix = $vorhanden; $Gefunden = $true }
    else            { $Prefix = $PrefixVorgabe }
}

# ─── Ziel erfragen ───────────────────────────────────────────────────────────
#
# Ohne -Prefix wird gefragt, sofern eine Eingabe möglich ist.  Läuft der
# Installer unbeaufsichtigt (Skript, CI), gilt kommentarlos der Vorschlag.

$Interaktiv = -not $Yes -and [Environment]::UserInteractive -and -not [Console]::IsInputRedirected

if (-not $Uninstall -and -not $PrefixGesetzt -and $Interaktiv) {
    if ($Gefunden) {
        Write-Host "`nEs gibt bereits eine Installation — Enter aktualisiert sie."
    } else {
        Write-Host "`nWohin soll der K1520-Emulator installiert werden?"
        Write-Host "  (Enter übernimmt den Vorschlag; das Verzeichnis wird angelegt)"
    }
    $antwort = Read-Host "Ziel [$Prefix]"
    if ($antwort) { $Prefix = $antwort }
    Write-Host ''
}

$Prefix = Absoluter-Pfad $Prefix

# ─── Datenordner erfragen ────────────────────────────────────────────────────
#
# Getrennt vom Installationsziel: hier liegen die ARBEITSDISKETTEN, in die der
# Emulator per Autosave zurückschreibt.  Sie sollen ein Update überleben und in
# der Datensicherung auftauchen — deshalb der Dokumentenordner als Vorschlag.
#
# Und deshalb wird gefragt: „Dokumente" ist unter Windows häufig nach OneDrive
# umgeleitet.  Ein Autosave bei JEDEM Diskettenzugriff in einen synchronisierten
# Ordner ist eine Entscheidung des Anwenders, keine des Entwurfs.

$DatenVorgabe = Benutzerdaten-Dir

if (-not $Uninstall -and -not $DatenGesetzt -and $Interaktiv -and $DatenVorgabe) {
    Write-Host 'Wo sollen die Arbeitsdisketten liegen?'
    Write-Host '  (dort schreibt der Emulator Änderungen zurück; ein Update fasst sie nie an)'
    if ($DatenVorgabe -like "*OneDrive*") {
        Write-Host '  Hinweis: der Vorschlag liegt in OneDrive — jede Änderung an einer'
        Write-Host '           Diskette löst dann eine Synchronisation aus.'
    }
    $antwort = Read-Host "Disketten [$DatenVorgabe]"
    if ($antwort) { $Data = $antwort }
    Write-Host ''
}

# Nur eine ABWEICHENDE Wahl wird festgeschrieben.
if ($Data) {
    $Data = Absoluter-Pfad $Data
    if ($Data -eq $DatenVorgabe) { $Data = '' }
}

# ─── Ziel prüfen ─────────────────────────────────────────────────────────────
#
# Das Deinstallieren löscht sein Ziel.  Seit danach GEFRAGT wird, ist ein
# Tippfehler eine Katastrophe — unter Linux hat die Antwort „~" einmal das
# Heimatverzeichnis gekostet.  Deshalb zwei Riegel: hier darf nur ein leeres
# oder bereits von uns belegtes Verzeichnis Ziel werden, und beim Löschen muss
# sich das Verzeichnis ausweisen (Ist-Installation).

$Verboten = @(
    $env:USERPROFILE, $env:APPDATA, $env:LOCALAPPDATA, $env:ProgramFiles,
    $env:SystemRoot, $env:SystemDrive, (Dokumente-Dir)
) | Where-Object { $_ } | ForEach-Object { $_.TrimEnd('\') }

$PrefixNormal = $Prefix.TrimEnd('\')
if ($Verboten -contains $PrefixNormal -or $PrefixNormal -match '^[A-Za-z]:$') {
    Die "„$Prefix" darf kein Installationsverzeichnis sein — bitte einen eigenen Ordner angeben (z. B. $PrefixVorgabe)"
}

if (-not $Uninstall -and -not (Ist-Leer $Prefix) -and -not (Ist-Installation $Prefix)) {
    Die "$Prefix ist nicht leer und enthält keine Installation des Emulators.
     Bitte einen eigenen Ordner angeben — beim Deinstallieren würde dieser
     hier samt Inhalt gelöscht."
}

# ─── Deinstallieren ──────────────────────────────────────────────────────────

function Entferne-Installation([string] $wurzel) {
    <#
      Nur wegräumen, was der Installer anlegte.  Ein Löschen der ganzen Wurzel
      wäre einfacher, nähme dem Anwender aber alles mit, was er DANEBEN abgelegt
      hat.  Gelöscht wird nach dem Inventar aus dem Ausweis (eine ältere
      Installation nach ihrer eigenen Liste, nicht nach der dieser Fassung).
    #>
    $liste = @()
    $ausweis = Join-Path $wurzel $InstallMarker
    if (Test-Path -LiteralPath $ausweis -PathType Leaf) {
        $liste = Get-Content -LiteralPath $ausweis -Encoding UTF8 |
                 Where-Object { $_ -like 'eintrag *' } |
                 ForEach-Object { $_.Substring(8).Trim() }
    }
    if (-not $liste) { $liste = $Inventar }

    foreach ($e in $liste) {
        # Ein Eintrag ist ein NAME, kein Pfad — sonst zeigte ein verfälschter
        # Ausweis („..\..") aus der Installation heraus.
        if ($e -match '[\\/]' -or $e -in @('.', '..', '')) {
            Warn "übergangen: fragwürdiger Eintrag im Ausweis ($e)"
            continue
        }
        Remove-Item -LiteralPath (Join-Path $wurzel $e) -Recurse -Force -ErrorAction SilentlyContinue
    }
    # Protokolle des Emulators — sie entstehen im ARBEITSVERZEICHNIS, also hier,
    # sobald jemand aus der Installation heraus startet.
    Remove-Item -LiteralPath (Join-Path $wurzel 'logs') -Recurse -Force -ErrorAction SilentlyContinue
    Get-ChildItem -LiteralPath $wurzel -Filter 'k1520_*.log' -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $ausweis -Force -ErrorAction SilentlyContinue

    if (Ist-Leer $wurzel) {
        Remove-Item -LiteralPath $wurzel -Force -ErrorAction SilentlyContinue
        Ok 'Verzeichnis gelöscht'
    } else {
        Ok 'Programmdateien entfernt'
        Write-Host "     $wurzel bleibt stehen — darin liegt, was nicht vom Installer stammt:"
        Get-ChildItem -LiteralPath $wurzel -Force | ForEach-Object { Write-Host "       $($_.Name)" }
    }
}

if ($Uninstall) {
    Info "Installation entfernen: $Prefix"
    if (-not (Test-Path -LiteralPath $Prefix -PathType Container)) {
        Warn "nichts zu löschen — $Prefix gibt es nicht"
    } elseif (Ist-Installation $Prefix) {
        Entferne-Installation $Prefix
    } else {
        Warn "$Prefix sieht nicht nach einer Installation aus — es bleibt unangetastet"
    }

    $menue = Join-Path $StartMenu 'K1520emu'
    Remove-Item -LiteralPath $menue -Recurse -Force -ErrorAction SilentlyContinue
    Ok 'Startmenü-Einträge entfernt'

    $datadir = Benutzerdaten-Dir
    $confdir = Join-Path $env:APPDATA 'K1520emu'
    if ($Purge) {
        Remove-Item -LiteralPath $datadir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $confdir -Recurse -Force -ErrorAction SilentlyContinue
        Ok 'Disketten und Konfiguration gelöscht'
    } else {
        Write-Host "     Behalten: $datadir (Disketten), $confdir (Konfiguration)"
        Write-Host '     Mit -Purge löschen.'
    }
    exit 0
}

# ─── Vorprüfungen ────────────────────────────────────────────────────────────

if (-not (Test-Path -LiteralPath (Join-Path $SelfDir 'payload') -PathType Container)) {
    Die "payload\ fehlt neben $($MyInvocation.MyCommand.Name) — ist das Paket vollständig entpackt?"
}
if (-not (Test-Path -LiteralPath (Join-Path $SelfDir 'requirements.lock') -PathType Leaf)) {
    Die 'requirements.lock fehlt im Paket'
}

$Version = 'unbekannt'
$vdatei = Join-Path $SelfDir 'VERSION'
if (Test-Path -LiteralPath $vdatei -PathType Leaf) {
    $Version = (Get-Content -LiteralPath $vdatei -Raw -Encoding UTF8).Trim()
}

Info "K1520-Emulator $Version"
Write-Host "     Ziel:   $Prefix"
Write-Host "     Python: $PyVersion (eigene Laufzeitumgebung, kein Systempython)"
Write-Host "     PowerShell: $($PSVersionTable.PSVersion) ($($PSVersionTable.PSEdition))"

New-Item -ItemType Directory -Force -Path $Prefix | Out-Null

# Grob 500 MB werden gebraucht (Python + Qt + Payload).
try {
    $laufwerk = (Get-Item -LiteralPath $Prefix).PSDrive
    if ($laufwerk -and $laufwerk.Free -and ($laufwerk.Free / 1MB) -lt 500) {
        Warn ("nur {0:N0} MB frei auf {1}: — gebraucht werden ~500 MB" -f ($laufwerk.Free / 1MB), $laufwerk.Name)
    }
} catch { }

# ─── 1. Payload kopieren ─────────────────────────────────────────────────────

Info 'Programmdateien kopieren'
foreach ($d in @('bin', 'app', 'share')) {
    $ziel = Join-Path $Prefix $d
    Remove-Item -LiteralPath $ziel -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath (Join-Path $SelfDir "payload\$d") -Destination $ziel -Recurse
}
foreach ($f in @('VERSION', 'requirements.lock', 'LICENSE')) {
    $q = Join-Path $SelfDir $f
    if (Test-Path -LiteralPath $q -PathType Leaf) { Copy-Item -LiteralPath $q -Destination $Prefix -Force }
}

# Ausweis für das Deinstallieren: ob dieses Verzeichnis uns gehört (sonst wird
# nichts gelöscht) und WAS darin uns gehört.
$ausweisZeilen = @("k1520emu $Version",
                   '# Vom Installer angelegt — nur das entfernt -Uninstall wieder.')
$ausweisZeilen += ($Inventar | ForEach-Object { "eintrag $_" })
Set-Content -LiteralPath (Join-Path $Prefix $InstallMarker) -Value $ausweisZeilen -Encoding UTF8
Ok "Kern, GUI und Daten in $Prefix"

# ─── 2. Laufzeitumgebung ─────────────────────────────────────────────────────

function Hole-Uv([string] $zielDir, [string] $pins) {
    <#
      Gegenstück zu ensure_uv.  Ein systemweit vorhandenes uv wird bewusst NICHT
      benutzt: jede Installation soll dieselbe geprüfte Fassung verwenden und
      beim Deinstallieren restlos verschwinden.
    #>
    $uv = Join-Path $zielDir 'uv.exe'
    if (Test-Path -LiteralPath $uv -PathType Leaf) { return $uv }
    if (-not (Test-Path -LiteralPath $pins -PathType Leaf)) { Die "uv-Pins nicht gefunden: $pins" }

    $werte = @{}
    foreach ($z in Get-Content -LiteralPath $pins -Encoding UTF8) {
        $f = $z.Trim() -split '\s+'
        if ($f.Count -ge 2 -and -not $z.StartsWith('#')) { $werte[$f[0]] = $f[1] }
    }
    $ziel = 'x86_64-pc-windows-msvc'
    $ver  = $werte['version']
    $want = $werte[$ziel]
    if (-not $ver)  { Die "keine uv-Version in $pins" }
    if (-not $want) { Die "keine uv-Prüfsumme für $ziel in $pins" }

    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("k1520uv-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    $archiv = Join-Path $tmp 'uv.zip'
    $url = "https://github.com/astral-sh/uv/releases/download/$ver/uv-$ziel.zip"

    Info "uv $ver für $ziel laden (~15 MB)"
    try {
        # Über .NET statt Invoke-WebRequest — siehe Kommentar bei Datei-Pruefsumme:
        # dieses Skript darf sich nicht auf nachladbare Module verlassen.
        # Tls12 ausdrücklich: ältere .NET-Fassungen handeln sonst SSL3/TLS1.0 aus,
        # und GitHub weist das ab.
        [Net.ServicePointManager]::SecurityProtocol =
            [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        $netz = New-Object System.Net.WebClient
        try { $netz.DownloadFile($url, $archiv) } finally { $netz.Dispose() }
    } catch {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Die "Download fehlgeschlagen: $url  (kein Netz? Proxy?)"
    }

    $got = Datei-Pruefsumme $archiv
    if ($got -ne $want.ToLower()) {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Die "Prüfsumme von uv stimmt nicht:
  erwartet $want
  erhalten $got
Abbruch — die heruntergeladene Datei wird nicht ausgeführt."
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($archiv, $tmp)
    $gefunden = Get-ChildItem -LiteralPath $tmp -Recurse -Filter 'uv.exe' | Select-Object -First 1
    if (-not $gefunden) {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        Die 'uv.exe nicht auffindbar im Archiv'
    }
    New-Item -ItemType Directory -Force -Path $zielDir | Out-Null
    Copy-Item -LiteralPath $gefunden.FullName -Destination $uv -Force
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue

    # Virenscanner greifen sich gelegentlich frisch heruntergeladene
    # Binärdateien — das hier ist die verständliche Meldung dafür.
    if (-not (Test-Path -LiteralPath $uv -PathType Leaf)) {
        Die 'uv wurde nach dem Entpacken entfernt (Virenscanner? Richtlinie?)'
    }
    return $uv
}

$Uv = Hole-Uv (Join-Path $Prefix 'tools') (Join-Path $SelfDir 'uv_pins.txt')

# Alles, was uv anlegt, landet INNERHALB der Installation — Deinstallieren =
# Verzeichnis löschen.
$env:UV_PYTHON_INSTALL_DIR = Join-Path $Prefix 'python'
$env:UV_CACHE_DIR          = Join-Path $Prefix '.cache-uv'
$env:UV_NO_MODIFY_PATH     = '1'

Info "Python $PyVersion bereitstellen"
# Erst leise; scheitert es, noch einmal mit voller Ausgabe — dann steht der
# Grund im Protokoll, statt dass nur „fehlgeschlagen" dasteht.
if ((Fuehre-Aus $Uv @('python', 'install', '--no-bin', $PyVersion) -Leise) -ne 0) {
    if ((Fuehre-Aus $Uv @('python', 'install', '--no-bin', $PyVersion)) -ne 0) {
        Die "Python $PyVersion konnte nicht installiert werden (kein Netz? Proxy?)"
    }
}
Ok 'Python bereit'

# Eine vorhandene Laufzeitumgebung wird ERNEUERT, nicht weiterbenutzt: `uv venv`
# verweigert sonst den Dienst, und weiterbenutzen wäre auch nicht richtig.
Info 'Laufzeitumgebung anlegen'
$venvArgs = @('venv')
if (Test-Path -LiteralPath (Join-Path $Prefix 'venv') -PathType Container) {
    $venvArgs += '--clear'
    Write-Host '     (bestehende Laufzeitumgebung wird erneuert)'
}
$venvArgs += @('--python', $PyVersion, '--python-preference', 'only-managed', (Join-Path $Prefix 'venv'))
if ((Fuehre-Aus $Uv $venvArgs -Leise) -ne 0) { Die 'venv konnte nicht angelegt werden' }

$PyExe  = Join-Path $Prefix 'venv\Scripts\python.exe'
$PyWExe = Join-Path $Prefix 'venv\Scripts\pythonw.exe'

Info 'Qt und Abhängigkeiten installieren (~70 MB, dauert einen Moment)'
if ((Fuehre-Aus $Uv @('pip', 'install', '--python', $PyExe, '--require-hashes',
                      '-r', (Join-Path $Prefix 'requirements.lock'))) -ne 0) {
    Die 'Abhängigkeiten konnten nicht installiert werden (kein Netz? Proxy?)'
}
Ok 'Laufzeitumgebung fertig'

if (-not $KeepCache) {
    Remove-Item -LiteralPath (Join-Path $Prefix '.cache-uv') -Recurse -Force -ErrorAction SilentlyContinue
}

# ─── 3. Schlankmachen ────────────────────────────────────────────────────────
#
# Python und Qt bringen ~300 MB mit, keine 15 davon gehören dem Emulator.  Der
# Rest ist QML/Quick, Qt-Entwicklungswerkzeug, ungenutzte Bindungen, CPythons
# Testsuite und Tcl/Tk.  slim.py schneidet das heraus — welche Qt-Bibliotheken
# bleiben, bestimmt dabei die PE-Importtabelle und keine Vermutung.  Der
# Rauchtest gleich danach ist die Gegenprobe.
if (-not $NoSlim) {
    Info 'Überflüssiges entfernen'
    $slim = Join-Path $SelfDir 'slim.py'
    if (Test-Path -LiteralPath $slim -PathType Leaf) {
        $argumente = @($slim, $Prefix)
        if ($KeepCache) { $argumente += '--keep-tools' }
        if ((Fuehre-Aus $PyExe $argumente) -ne 0) {
            Warn 'Schlankmachen fehlgeschlagen — die Installation bleibt vollständig'
        }
    } else {
        Warn 'slim.py fehlt im Paket — die Installation bleibt vollständig'
    }
}

# ─── 4. Starter ──────────────────────────────────────────────────────────────
#
# Je Maschine EINZELN, nicht über eine Schleife: jede braucht ihren eigenen
# Einstiegspunkt.  Kommt der nächste K1520-Rechner, entsteht hier ein zweiter
# Block — und sein Name gehört zusätzlich in $Maschinen.

Info 'Starter schreiben'
New-Item -ItemType Directory -Force -Path (Join-Path $Prefix 'bin') | Out-Null

Ersetze-Platzhalter (Join-Path $SelfDir 'launcher.cmd') $Prefix $Data |
    Set-Content -LiteralPath (Join-Path $Prefix 'bin\a5120emu.cmd') -Encoding ASCII
Ersetze-Platzhalter (Join-Path $SelfDir 'disktool_launcher.cmd') $Prefix $Data |
    Set-Content -LiteralPath (Join-Path $Prefix 'bin\k1520disktool.cmd') -Encoding ASCII

if (-not $NoShortcut) {
    $menue = Join-Path $StartMenu 'K1520emu'
    New-Item -ItemType Directory -Force -Path $menue | Out-Null
    $sh = New-Object -ComObject WScript.Shell

    # Verknüpft wird pythonw.exe DIREKT, nicht die .cmd: eine Verknüpfung auf
    # eine Batchdatei öffnet immer ein Konsolenfenster, das hinter der GUI
    # stehen bliebe.  Die .cmd bleibt für den Aufruf von Hand.
    foreach ($s in @(
        @{ Name = 'a5120emu';      Ziel = 'app\main.py';          Titel = 'A5120-Emulator' },
        @{ Name = 'k1520disktool'; Ziel = 'app\disktool\main.py'; Titel = 'k1520DiskTool' })) {
        $lnk = $sh.CreateShortcut((Join-Path $menue "$($s.Name).lnk"))
        $lnk.TargetPath       = $PyWExe
        $lnk.Arguments        = '"' + (Join-Path $Prefix $s.Ziel) + '"'
        $lnk.WorkingDirectory = $Prefix
        $lnk.Description      = $s.Titel
        $symbol = Join-Path $Prefix 'share\icons\a5120emu.ico'
        if (Test-Path -LiteralPath $symbol -PathType Leaf) { $lnk.IconLocation = $symbol }
        $lnk.Save()
    }
    Ok "Startmenü-Einträge unter „K1520emu“"
}

# ─── 5. Rauchtest ────────────────────────────────────────────────────────────
#
# Ein kaputter Startmenü-Eintrag ist schlimmer als eine abgebrochene
# Installation: hier wird geladen, was der Emulator beim Start lädt — Kern,
# Formatkatalog, Qt samt Plattform-Plugin und das Hauptfenster.

Info 'Rauchtest'
$rauchtest = @'
import ctypes, sys
from app import paths

paths.prepare_library_load()
lib = ctypes.CDLL(str(paths.core_library()))
lib.k1520_version.restype = ctypes.c_char_p
print("     Kern:      ", lib.k1520_version().decode())

import PySide6
print("     PySide6:   ", PySide6.__version__)

fmt = paths.formats_file()
if fmt is None:
    sys.exit("formats.yaml nicht gefunden:\n" + paths.describe())
print("     Katalog:   ", fmt)
print("     Disketten: ", paths.seed_user_disks(), "kopiert nach", paths.user_disks_dir())

from PySide6.QtWidgets import QApplication
from app.ui.main_window import MainWindow
qt = QApplication([])
fenster = MainWindow()
fenster.close()
print("     Oberflaeche: baut auf")
'@
$rtDatei = Join-Path $Prefix '.rauchtest.py'
Set-Content -LiteralPath $rtDatei -Value $rauchtest -Encoding UTF8

Push-Location $Prefix
try {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:PYTHONPATH = $Prefix
    # K1520_DATA MUSS hier mit: der Rauchtest legt über seed_user_disks() die
    # Beispieldisketten beim Anwender an.  Ohne die Variable landen sie an der
    # VORGABEstelle statt in dem Ordner, den er gerade gewählt hat.
    if ($Data) { $env:K1520_DATA = $Data }
    if ((Fuehre-Aus $PyExe @($rtDatei)) -ne 0) {
        Die 'Rauchtest fehlgeschlagen — die Installation ist unbrauchbar'
    }
} finally {
    Pop-Location
    Remove-Item -LiteralPath $rtDatei -Force -ErrorAction SilentlyContinue
    Remove-Item Env:\QT_QPA_PLATFORM -ErrorAction SilentlyContinue
    Remove-Item Env:\PYTHONPATH -ErrorAction SilentlyContinue
    Remove-Item Env:\K1520_DATA -ErrorAction SilentlyContinue
}

# Der Kern legt beim Erzeugen einer Maschine ein Protokoll unter `logs\` im
# ARBEITSVERZEICHNIS an — hier also in der frischen Installation.  Die soll aber
# nur enthalten, was hineingehört.
Remove-Item -LiteralPath (Join-Path $Prefix 'logs') -Recurse -Force -ErrorAction SilentlyContinue
Get-ChildItem -LiteralPath $Prefix -Filter 'k1520_*.log' -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue
Ok 'läuft'

Write-Host ''
Info 'Fertig.'
Write-Host "     Installiert:  $Prefix"
Write-Host "     Starten:      $Prefix\bin\a5120emu.cmd  (oder über das Startmenü)"
Write-Host "     Diskettenwerkzeug: $Prefix\bin\k1520disktool.cmd"
Write-Host "     Entfernen:    .\install.ps1 -Uninstall"
