<#
.SYNOPSIS
    Einen Ordner in ein .zip packen — mit Schrägstrichen, wie die Spezifikation es verlangt.

.DESCRIPTION
    Klingt nach einer Zeile `Compress-Archive`, ist aber keine.  Unter **Windows
    PowerShell 5.1** schreiben sowohl `Compress-Archive` als auch
    `[IO.Compression.ZipFile]::CreateFromDirectory` den PLATTFORM-Trenner in die
    Eintragsnamen, also einen Backslash.  Die ZIP-Spezifikation (APPNOTE 4.4.17.1)
    verlangt Schrägstriche; behoben ist das erst in .NET 5, und die Laufzeit von
    5.1 ist .NET Framework.

    Der Explorer verzeiht Backslashes — andere Werkzeuge nicht: Pythons `zipfile`
    sieht dann gar keine Verzeichnisse, `unzip` unter Linux und macOS legt
    Dateien mit einem Backslash IM NAMEN an.  Für ein Archiv, das an einem
    öffentlichen Release hängt, ist das nicht hinnehmbar.

    Deshalb werden die Eintragsnamen hier SELBST gebildet.  Das ist auf jeder
    .NET-Fassung richtig und hängt an keiner PowerShell-Version.

    Eigene Datei statt einer `-Command`-Zeile aus dem Shell-Skript heraus, weil
    das Escaping durch zwei Ebenen (Bourne-Shell → PowerShell) nicht überlebt:
    aus `-replace '\\','/'` wurde dabei ein ungültiger regulärer Ausdruck.

.PARAMETER Quelle
    Der zu packende Ordner.  Sein NAME wird oberster Eintrag im Archiv, damit
    das Auspacken nicht alles ins aktuelle Verzeichnis streut.

.PARAMETER Ziel
    Die zu erzeugende .zip-Datei (wird überschrieben).

.EXAMPLE
    powershell -File zip_ordner.ps1 -Quelle D:\dist\paket -Ziel D:\dist\paket.zip
#>
param(
    [Parameter(Mandatory)] [string] $Quelle,
    [Parameter(Mandatory)] [string] $Ziel
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.IO.Compression.FileSystem

$Quelle = (Resolve-Path -LiteralPath $Quelle).Path.TrimEnd('\', '/')
$oben   = Split-Path -Leaf $Quelle

if (Test-Path -LiteralPath $Ziel) { Remove-Item -LiteralPath $Ziel -Force }

$zip = [System.IO.Compression.ZipFile]::Open($Ziel, 'Create')
try {
    foreach ($datei in Get-ChildItem -Recurse -File -LiteralPath $Quelle) {
        # Relativ zum Quellordner, mit dessen Namen davor — und ausdrücklich
        # mit Schrägstrichen.  Genau das ist der Zweck dieser Datei.
        $rel = $datei.FullName.Substring($Quelle.Length).TrimStart('\', '/')
        $eintrag = ($oben + '/' + $rel) -replace '\\', '/'
        [void] [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $datei.FullName, $eintrag,
            [System.IO.Compression.CompressionLevel]::Optimal)
    }
} finally {
    $zip.Dispose()
}

# Sofort nachsehen, statt es anzunehmen: der Weg über CreateFromDirectory sah
# ebenfalls richtig aus und war es nicht.
$pruef = [System.IO.Compression.ZipFile]::OpenRead($Ziel)
try {
    $namen = $pruef.Entries | ForEach-Object { $_.FullName }
} finally {
    $pruef.Dispose()
}
$schlecht = $namen | Where-Object { $_ -like '*\*' }
if ($schlecht) {
    throw "Das erzeugte Archiv hat $($schlecht.Count) Eintraege mit Backslash, z. B. $($schlecht[0])"
}
Write-Host "     $($namen.Count) Eintraege, alle mit Schraegstrich"
