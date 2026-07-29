#Requires -Version 5.1
<#
.SYNOPSIS
    Installs the system prerequisites for building this project on Windows.

.DESCRIPTION
    Installs the MSVC build tools, CMake, Ninja and Git via winget. SDL3 uses the
    Windows SDK that comes with the MSVC toolchain, so there is nothing else to
    install: the game's own dependencies are fetched and pinned by the build.
    See docs/dependencies.md.

    Safe to re-run.

.EXAMPLE
    .\scripts\setup-windows.ps1
#>

$ErrorActionPreference = 'Stop'

function Write-Info { param($Message) Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Warn { param($Message) Write-Host "!!  $Message" -ForegroundColor Yellow }
function Write-Fail { param($Message) Write-Host "xx  $Message" -ForegroundColor Red; exit 1 }

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Warn 'winget is not available.'
    Write-Warn 'Install "App Installer" from the Microsoft Store, or install by hand:'
    Write-Warn '  - Visual Studio 2022 Build Tools with the C++ workload'
    Write-Warn '  - CMake >= 3.25   https://cmake.org/download/'
    Write-Warn '  - Ninja           https://github.com/ninja-build/ninja/releases'
    Write-Fail 'Cannot continue automatically.'
}

function Install-Package {
    param([string]$Id, [string]$Label, [string]$ExtraArgs = '')

    Write-Info "Installing $Label"
    # --accept-*-agreements keeps this non-interactive; a package that is already
    # present exits non-zero, which is not a failure for our purposes.
    $arguments = @(
        'install', '--id', $Id, '--exact', '--silent',
        '--accept-package-agreements', '--accept-source-agreements'
    )
    if ($ExtraArgs) { $arguments += $ExtraArgs.Split(' ') }

    & winget @arguments
    if ($LASTEXITCODE -ne 0) {
        Write-Warn "$Label may already be installed (winget exit $LASTEXITCODE); continuing."
    }
}

# The C++ workload is what provides cl.exe, the Windows SDK and the STL.
Install-Package -Id 'Microsoft.VisualStudio.2022.BuildTools' -Label 'MSVC build tools' `
    -ExtraArgs '--override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"'

Install-Package -Id 'Kitware.CMake' -Label 'CMake'
Install-Package -Id 'Ninja-build.Ninja' -Label 'Ninja'
Install-Package -Id 'Git.Git' -Label 'Git'

Write-Host ''
Write-Info 'Done.'
Write-Warn 'Open a NEW terminal so the updated PATH is picked up.'
Write-Host ''
Write-Info 'Then build from a "Developer PowerShell for VS 2022" (needed so that'
Write-Info 'Ninja can find cl.exe):'
Write-Host '    cmake --preset debug'
Write-Host '    cmake --build --preset debug'
Write-Host '    ctest --preset debug'
Write-Host '    .\build\debug\bin\game_client.exe'
Write-Host ''
Write-Info 'Alternatively, use the Visual Studio generator from any terminal:'
Write-Host '    cmake -S . -B build\vs -G "Visual Studio 17 2022" -A x64'
Write-Host '    cmake --build build\vs --config Debug'
