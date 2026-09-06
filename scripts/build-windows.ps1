# Configure, build, and test on Windows with Visual Studio 2022 (or any CMake
# generator on PATH). Run from a Developer PowerShell or with CMake installed.
#   .\scripts\build-windows.ps1            # Release build + tests
#   .\scripts\build-windows.ps1 -Config Debug
param([string]$Config = "Release")
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
cmake -S . -B build -A x64
cmake --build build --config $Config --parallel
ctest --test-dir build -C $Config --output-on-failure
Write-Host "mm-core: build\$Config\mm-core.exe"
