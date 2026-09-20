param([string]$Compiler = 'g++.exe')
$ErrorActionPreference = 'Stop'
$compilerPath = (Get-Command $Compiler -ErrorAction Stop).Source
$env:PATH = (Split-Path -Parent $compilerPath) + ';' + $env:PATH
$output = Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $output | Out-Null
& $Compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-municode' '-mwindows' '-static' '-static-libgcc' '-static-libstdc++' (Join-Path $PSScriptRoot 'main.cpp') '-o' (Join-Path $output 'WinLtfsManager.exe') '-lole32' '-lshell32' '-luuid' '-lgdi32' '-luser32'
if ($LASTEXITCODE -ne 0) { throw 'Native GUI build failed.' }
Write-Output (Join-Path $output 'WinLtfsManager.exe')
