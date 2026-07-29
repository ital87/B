#Requires -Version 5.1

Write-Host "B does not currently support Windows." -ForegroundColor Red
Write-Host "Its runtime allocator and I/O issue Linux syscalls directly, so a Windows"
Write-Host "build would produce programs that fault on the first allocation."
Write-Host "See the Platform Support section of the README."
exit 1


$ErrorActionPreference = "Stop"

Write-Host "=========================================="
Write-Host "B Compiler - Windows Installer"
Write-Host "=========================================="
Write-Host ""

function Test-Command($name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Import-VsDevShell {
    if (Test-Command "clang++") {
        return
    }

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        return
    }

    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        return
    }

    $vcVarsAll = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcVarsAll)) {
        return
    }

    Write-Host "Loading Visual Studio Build Tools environment..."
    $envDump = cmd /c "`"$vcVarsAll`" && set"
    foreach ($line in $envDump) {
        if ($line -match "^(.*?)=(.*)$") {
            [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
}

function Install-Dependencies {
    Write-Host "[1/3] Checking build dependencies..."

    if (-not (Test-Command "winget")) {
        Write-Host "Error: winget is required. Install 'App Installer' from the Microsoft Store, then re-run this script."
        exit 1
    }

    if (-not (Test-Command "git")) {
        Write-Host "Installing git..."
        winget install -e --id Git.Git --accept-source-agreements --accept-package-agreements | Out-Null
    }

    $llvmInstalled = Test-Command "clang++"
    if (-not $llvmInstalled) {
        Write-Host "Installing LLVM (clang, llc, lld)..."
        winget install -e --id LLVM.LLVM --accept-source-agreements --accept-package-agreements | Out-Null
    }

    $llvmBin = "${env:ProgramFiles}\LLVM\bin"
    if (Test-Path $llvmBin) {
        $env:Path = "$llvmBin;$env:Path"
    }

    if (-not (Test-Command "link") -and -not (Test-Command "cl")) {
        Write-Host "Installing Visual Studio Build Tools (C++ workload, needed for the Windows linker)..."
        winget install -e --id Microsoft.VisualStudio.2022.BuildTools --accept-source-agreements --accept-package-agreements --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" | Out-Null
    }

    Import-VsDevShell
}

function Test-Toolchain {
    $missing = @()
    foreach ($tool in @("clang++", "llc", "git")) {
        if (-not (Test-Command $tool)) {
            $missing += $tool
        }
    }
    return $missing
}

Install-Dependencies

$missing = Test-Toolchain
if ($missing.Count -gt 0) {
    Write-Host "Error: Missing required tools after installation: $($missing -join ', ')"
    Write-Host "Please install them manually and re-run this script."
    exit 1
}

Write-Host ""
Write-Host "[2/3] Compiling B compiler..."

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SrcFile = Join-Path $ScriptDir "src\b_combined.cpp"
$BuildDir = Join-Path $ScriptDir "build"
$BBinary = Join-Path $BuildDir "b.exe"

if (-not (Test-Path $SrcFile)) {
    Write-Host "Error: Source file not found at $SrcFile"
    exit 1
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$llvmRoot = "${env:ProgramFiles}\LLVM"
$includeDir = Join-Path $llvmRoot "include"
$libDir = Join-Path $llvmRoot "lib"

$llvmLibs = Get-ChildItem -Path $libDir -Filter "LLVM*.lib" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
if (-not $llvmLibs) {
    $singleLib = Join-Path $libDir "LLVM-C.lib"
    if (Test-Path $singleLib) {
        $llvmLibs = @($singleLib)
    }
}

$compileArgs = @(
    "-std=c++17",
    "-fexceptions",
    "-I`"$includeDir`"",
    "`"$SrcFile`"",
    "-o", "`"$BBinary`"",
    "-L`"$libDir`""
) + ($llvmLibs | ForEach-Object { "`"$_`"" })

$compileCmd = "clang++ " + ($compileArgs -join " ")
Write-Host "Running: $compileCmd"

$process = Start-Process -FilePath "clang++" -ArgumentList $compileArgs -NoNewWindow -Wait -PassThru
if ($process.ExitCode -ne 0) {
    Write-Host "Error: Compilation failed"
    exit 1
}

Write-Host "Compilation successful: $BBinary"
Write-Host ""

Write-Host "[3/3] Installing B compiler..."

$InstallDir = Join-Path $env:USERPROFILE ".b\bin"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$TargetExe = Join-Path $InstallDir "b.exe"
$BackupExe = Join-Path $InstallDir "b.exe.old"

if (Test-Path $TargetExe) {
    Remove-Item -Force $BackupExe -ErrorAction SilentlyContinue
    try {
        Rename-Item -Path $TargetExe -NewName "b.exe.old" -Force -ErrorAction Stop
    } catch {
    }
}

Copy-Item -Force $BBinary $TargetExe
Remove-Item -Force $BackupExe -ErrorAction SilentlyContinue

Write-Host "B compiler installed to: $InstallDir\b.exe"
Write-Host ""

$userPath = [System.Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$InstallDir*") {
    $newPath = if ($userPath) { "$userPath;$InstallDir" } else { $InstallDir }
    [System.Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "Added $InstallDir to your user PATH."
} else {
    Write-Host "PATH already contains $InstallDir."
}

$env:Path = "$InstallDir;$env:Path"

Write-Host ""
Write-Host "=========================================="
Write-Host "Installation complete!"
Write-Host "=========================================="
Write-Host ""
Write-Host "Test the B compiler:"
Write-Host "     b --version"
Write-Host ""
Write-Host "Compile a B file:"
Write-Host "     b examples\hello.b"
Write-Host ""
Write-Host "(PATH updated for this session. Open a new terminal to pick it up everywhere.)"
