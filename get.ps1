#Requires -Version 5.1

Write-Host "B does not currently support Windows." -ForegroundColor Red
Write-Host "See the Platform Support section of the README."
exit 1


param(
    [string]$Action = ""
)

$ErrorActionPreference = "Stop"

$RepoUrl = "https://github.com/ital87/B.git"
$RepoBranch = "main"
$InstallRoot = Join-Path $env:USERPROFILE ".b"
$RepoDir = Join-Path $InstallRoot "repo"
$BinDir = Join-Path $InstallRoot "bin"

function Invoke-Install {
    Write-Host "=========================================="
    Write-Host "B wird heruntergeladen und installiert..."
    Write-Host "=========================================="
    Write-Host ""

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        if (Get-Command winget -ErrorAction SilentlyContinue) {
            Write-Host "Installiere git..."
            winget install -e --id Git.Git --accept-source-agreements --accept-package-agreements | Out-Null
        } else {
            Write-Host "Error: git wird benötigt. Bitte installieren und erneut ausführen."
            exit 1
        }
    }

    New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null
    if (Test-Path $RepoDir) {
        Remove-Item -Recurse -Force $RepoDir
    }
    git clone --depth=1 --branch $RepoBranch $RepoUrl $RepoDir

    & powershell -ExecutionPolicy Bypass -File (Join-Path $RepoDir "install.ps1")
}

function Write-ChangeSummary($oldHead, $newHead) {
    if (-not $oldHead -or ($oldHead -eq $newHead)) {
        Write-Host "Bereits auf dem neuesten Stand."
        return
    }

    $diffOutput = git diff --name-status $oldHead $newHead 2>$null
    $added = ($diffOutput | Where-Object { $_ -match '^A' }).Count
    $deleted = ($diffOutput | Where-Object { $_ -match '^D' }).Count
    $modified = ($diffOutput | Where-Object { $_ -match '^M' }).Count

    Write-Host "Änderungen seit deiner letzten Version:"
    Write-Host "  Neue Dateien:      $added"
    Write-Host "  Geänderte Dateien: $modified"
    Write-Host "  Gelöschte Dateien: $deleted"
}

function Invoke-Update {
    Write-Host "=========================================="
    Write-Host "B wird aktualisiert..."
    Write-Host "=========================================="
    Write-Host ""

    Push-Location $RepoDir
    $oldHead = git rev-parse HEAD 2>$null

    git fetch --depth=1 origin $RepoBranch
    git reset --hard "origin/$RepoBranch"

    $newHead = git rev-parse HEAD 2>$null
    Pop-Location

    Write-Host ""
    Write-ChangeSummary $oldHead $newHead
    Write-Host ""

    & powershell -ExecutionPolicy Bypass -File (Join-Path $RepoDir "install.ps1")

    Write-Host ""
    Write-Host "Update abgeschlossen."
}

function Invoke-Uninstall {
    Write-Host "=========================================="
    Write-Host "B wird deinstalliert..."
    Write-Host "=========================================="
    Write-Host ""

    Remove-Item -Recurse -Force $InstallRoot -ErrorAction SilentlyContinue

    $userPath = [System.Environment]::GetEnvironmentVariable("Path", "User")
    if ($userPath) {
        $parts = $userPath.Split(";") | Where-Object { $_ -ne $BinDir -and $_ -ne "" }
        $newPath = $parts -join ";"
        [System.Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    }

    Write-Host "B wurde vollständig entfernt."
    Write-Host "Öffne ein neues Terminal, damit der PATH-Eintrag verschwindet."
}

if ($Action -eq "update") {
    if (Test-Path (Join-Path $RepoDir ".git")) {
        Invoke-Update
    } else {
        Invoke-Install
    }
    exit 0
}

if ((Test-Path (Join-Path $RepoDir ".git")) -and (Test-Path (Join-Path $BinDir "b.exe"))) {
    Write-Host "=========================================="
    Write-Host "B ist bereits installiert."
    Write-Host "=========================================="
    Write-Host ""
    Write-Host "[1] Update"
    Write-Host "[2] Uninstall"
    Write-Host ""
    $choice = Read-Host "Auswahl [1/2]"

    switch ($choice) {
        "1" { Invoke-Update }
        "2" { Invoke-Uninstall }
        default {
            Write-Host "Ungültige Auswahl. Abbruch."
            exit 1
        }
    }
} else {
    Invoke-Install
}
