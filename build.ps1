[CmdletBinding()]
param(
    [switch]$SkipInstaller,
    [switch]$AutoInstallInnoSetup,
    [ValidatePattern('^\d+\.\d+\.\d+(\.\d+)?$')]
    [string]$Version = '0.0.0',
    [string]$Msys2Root = $env:MSYS2_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Root      = $PSScriptRoot
$DistDir   = Join-Path $Root 'dist'
$RedistDir = Join-Path $Root 'redist'
$IssFile   = Join-Path $Root 'installer.iss'

$WinFsp = @{
    File   = 'winfsp.msi'
    Url    = 'https://github.com/winfsp/winfsp/releases/download/v2.1/winfsp-2.1.25156.msi'
    Sha256 = '073A70E00F77423E34BED98B86E600DEF93393BA5822204FAC57A29324DB9F7A'
}

function Step($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Info($m) { Write-Host "    $m" }

function Find-Msys2Bash {
    $roots = @()
    if ($Msys2Root) { $roots += $Msys2Root }
    $roots += @('C:\msys64', 'C:\msys2', (Join-Path $env:SystemDrive 'msys64'))
    foreach ($r in $roots) {
        $b = Join-Path $r 'usr\bin\bash.exe'
        if (Test-Path $b) { return $b }
    }
    $cmd = Get-Command bash.exe -ErrorAction SilentlyContinue |
           Where-Object { $_.Source -match 'msys' } | Select-Object -First 1
    if ($cmd) { return $cmd.Source }
    return $null
}

function Find-Iscc {
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")) {
        if ($p -and (Test-Path $p)) { return $p }
    }
    return $null
}

function Resolve-WinFspMsi {
    New-Item -ItemType Directory -Force -Path $RedistDir | Out-Null
    $path = Join-Path $RedistDir $WinFsp.File
    if (Test-Path $path) {
        if ((Get-FileHash $path -Algorithm SHA256).Hash -eq $WinFsp.Sha256) {
            Info "have $($WinFsp.File)"; return
        }
        Info "$($WinFsp.File) mismatched - re-downloading"
        Remove-Item $path -Force
    }
    Info "downloading $($WinFsp.File)"
    Invoke-WebRequest -Uri $WinFsp.Url -OutFile $path -UseBasicParsing
    $actual = (Get-FileHash $path -Algorithm SHA256).Hash
    if ($actual -ne $WinFsp.Sha256) {
        throw "SHA256 mismatch for $($WinFsp.File): got $actual, expected $($WinFsp.Sha256)."
    }
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()

Step 'Building native LTFS engine (MSYS2 MINGW64: setup.sh + build.sh)'
$bash = Find-Msys2Bash
if (-not $bash) {
    throw "MSYS2 bash not found. Install MSYS2 (https://www.msys2.org) or set " +
          "-Msys2Root / `$env:MSYS2_ROOT."
}
Info "using $bash"
$env:MSYSTEM = 'MINGW64'
$env:CHERE_INVOKING = '1'
Push-Location $Root
try {
    & $bash -lc './setup.sh && ./build.sh all'
    if ($LASTEXITCODE -ne 0) { throw "engine build failed (exit $LASTEXITCODE)." }
} finally {
    Pop-Location
}
if (-not (Test-Path (Join-Path $DistDir 'ltfs.exe'))) {
    throw "engine build did not produce dist\ltfs.exe."
}
Info 'engine staged into dist\'

if ($SkipInstaller) {
    Step 'Skipping installer (-SkipInstaller)'
} else {
    Step 'Bundling WinFsp MSI'
    Resolve-WinFspMsi

    Step 'Building Windows installer'
    $iscc = Find-Iscc
    if (-not $iscc) {
        $doInstall = $AutoInstallInnoSetup
        if (-not $doInstall) {
            $doInstall = ((Read-Host "    Install Inno Setup 6 via winget now? [y/N]") -match '^[Yy]')
        }
        if ($doInstall) {
            if (-not (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
                throw "winget not available. Install Inno Setup 6.3+ from https://jrsoftware.org/isdl.php"
            }
            winget install --id JRSoftware.InnoSetup --exact --silent `
                --accept-package-agreements --accept-source-agreements
            $iscc = Find-Iscc
        }
        if (-not $iscc) { throw "Inno Setup compiler not found. Install it from https://jrsoftware.org/isdl.php" }
    }
    Info "using $iscc"
    & $iscc "/DMyAppVersion=$Version" $IssFile
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE." }
}

$sw.Stop()
Step ('Build complete in {0:n0}s' -f $sw.Elapsed.TotalSeconds)
if (Test-Path $DistDir) {
    $distMB = [math]::Round(((Get-ChildItem $DistDir -Recurse -File | Measure-Object Length -Sum).Sum) / 1MB, 1)
    Write-Host ("    dist\        {0} MB (installed footprint)" -f $distMB) -ForegroundColor Green
}
$setup = Get-ChildItem (Join-Path $Root 'Output') -Filter '*.exe' -ErrorAction SilentlyContinue |
         Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($setup) {
    $setupMB = [math]::Round($setup.Length / 1MB, 1)
    Write-Host ("    installer    {0} ({1} MB)" -f $setup.FullName, $setupMB) -ForegroundColor Green
}
