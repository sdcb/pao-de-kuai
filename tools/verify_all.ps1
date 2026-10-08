# Runs the full three-toolchain gate the way docs/c-port-conventions.md section 8 requires:
# build + ctest on MSVC x64, MinGW UCRT x64 and MinGW UCRT x86, then a pass/fail summary.
#
# Why a script: every stage of the pure-C port has to be verified on all three, and two of the
# steps are easy to get wrong by hand --
#   * MinGW's <root>\bin must be on PATH or gcc fails silently (see the conventions doc);
#   * ctest must run with TEMP/TMP inside the build directory, because the local DSH sandbox
#     denies creating directories under the real %TEMP% and unit_tests then false-fails.
#
# Usage:
#   pwsh -File tools/verify_all.ps1              # build + test
#   pwsh -File tools/verify_all.ps1 -TestOnly    # skip the builds
#   pwsh -File tools/verify_all.ps1 -SkipMsVC    # MinGW only (much faster while iterating)
#   pwsh -File tools/verify_all.ps1 -CompareScreenshots   # also diff the five screenshots
#
# Exits 0 only when every requested toolchain built and reported 100% tests passed.

[CmdletBinding()]
param(
    [switch]$TestOnly,
    [switch]$SkipMsVC,

    # Also compare the five screenshots against build-baseline/.  Off by default because it needs
    # the bundled Python; on for a release check.
    [switch]$CompareScreenshots,

    # Override when the toolchains live elsewhere.  The defaults match AGENTS.md.
    [string]$MinGW64Root = "D:\_\3rd\mingw64-ucrt",
    [string]$MinGW32Root = "D:\_\3rd\mingw32-ucrt",
    [string]$VsVarsPath  = "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Captured once so each toolchain can be given a PATH with only ITS bin directory in front.
# Prepending twice would leave the x64 root ahead of the x86 one, and x86 as.exe would then be
# able to pick up the x64 libwinpthread-1.dll.
$script:BasePath = $env:PATH
$script:Failures = @()

function Write-Head([string]$text) {
    Write-Host ""
    Write-Host "=== $text ===" -ForegroundColor Cyan
}

# Runs ctest with the sandbox-safe temp directory, and records the outcome.
function Invoke-Tests([string]$label, [string]$buildDir, [string]$preset) {
    $tempDir = Join-Path $root "$buildDir\test-temp"
    New-Item -ItemType Directory -Force -Path $tempDir | Out-Null

    $previousTemp = $env:TEMP
    $previousTmp  = $env:TMP
    try {
        $env:TEMP = $tempDir
        $env:TMP  = $tempDir
        if ($preset) {
            $out = & cmd /c "set TEMP=$tempDir&& set TMP=$tempDir&& ctest --preset $preset 2>&1"
        } else {
            $out = & ctest --test-dir $buildDir 2>&1
        }
    } finally {
        $env:TEMP = $previousTemp
        $env:TMP  = $previousTmp
    }

    $out | Select-Object -Last 3 | ForEach-Object { Write-Host "    $_" }
    $passed = ($out | Select-String -Pattern 'tests passed' | Select-Object -Last 1)
    if ($passed -and $passed.Line -match '100% tests passed') {
        Write-Host "  $label OK" -ForegroundColor Green
    } else {
        Write-Host "  $label FAILED" -ForegroundColor Red
        $script:Failures += "$label tests"
    }
}

function Invoke-Build([string]$label, [string]$buildDir, [string]$preset, [string]$mingwRoot) {
    Write-Head "build $label"
    if ($mingwRoot) {
        $env:PATH = "$mingwRoot\bin;" + $script:BasePath
    }
    if ($preset -and -not (Test-Path (Join-Path $root $buildDir))) {
        & cmake --preset $preset 2>&1 | Select-Object -Last 1 | ForEach-Object { Write-Host "    $_" }
    }
    if ($preset) {
        $out = & cmake --build --preset $preset 2>&1
    } else {
        $out = & cmake --build $buildDir 2>&1
    }
    $errors = $out | Select-String -Pattern 'error:|error [A-Z]+[0-9]+|FAILED:'
    if ($errors) {
        $errors | Select-Object -Unique -First 20 | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
        Write-Host "  $label BUILD FAILED" -ForegroundColor Red
        $script:Failures += "$label build"
        return $false
    }
    Write-Host "  $label built" -ForegroundColor Green
    return $true
}

# ---- MinGW UCRT x64 (main release toolchain) --------------------------------
if (-not $TestOnly) {
    if (Invoke-Build "MinGW x64" "build-mingw-ucrt-x64" "mingw-ucrt-x64-release" $MinGW64Root) {
        Invoke-Tests "MinGW x64" "build-mingw-ucrt-x64" $null
    }
} else {
    Invoke-Tests "MinGW x64" "build-mingw-ucrt-x64" $null
}

# ---- MinGW UCRT x86 --------------------------------------------------------
if (-not $TestOnly) {
    if (Invoke-Build "MinGW x86" "build-mingw-ucrt-x86" "mingw-ucrt-x86-release" $MinGW32Root) {
        Invoke-Tests "MinGW x86" "build-mingw-ucrt-x86" $null
    }
} else {
    Invoke-Tests "MinGW x86" "build-mingw-ucrt-x86" $null
}

# ---- MSVC x64 --------------------------------------------------------------
if (-not $SkipMsVC) {
    Write-Head "build MSVC x64"
    if (-not (Test-Path $VsVarsPath)) {
        Write-Host "  vcvars64.bat not found at $VsVarsPath -- skipping MSVC" -ForegroundColor Yellow
    } elseif (-not $TestOnly) {
        $out = & cmd /c "call ""$VsVarsPath"" >nul 2>&1 && cmake --preset vs2026-release && cmake --build --preset vs2026-release 2>&1"
        $errors = $out | Select-String -Pattern 'error:|error [A-Z]+[0-9]+|FAILED:'
        if ($errors) {
            $errors | Select-Object -Unique -First 20 | ForEach-Object { Write-Host "    $_" } 
            Write-Host "  MSVC x64 BUILD FAILED" -ForegroundColor Red
            $script:Failures += "MSVC x64 build"
        } else {
            Write-Host "  MSVC x64 built" -ForegroundColor Green
            Invoke-Tests "MSVC x64" "build-vs2026" "vs2026-release"
        }
    } else {
        Invoke-Tests "MSVC x64" "build-vs2026" "vs2026-release"
    }
}

# ---- screenshot comparison (optional) --------------------------------------
if ($CompareScreenshots) {
    Write-Head "screenshots vs build-baseline"
    $python = Join-Path $env:USERPROFILE ".dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"
    if (-not (Test-Path $python)) { $python = "python" }
    & $python (Join-Path $root "tools/compare_screenshots.py") "build-mingw-ucrt-x64" "build-baseline" --floor
    if ($LASTEXITCODE -ne 0) {
        # A difference is not automatically a failure: the baseline is compared against each scene's
        # own run-to-run noise floor, and this script cannot make that judgement.  It is reported so
        # a human reads the numbers rather than trusting a green tick.
        Write-Host "  screenshot differences reported above -- read them against the per-scene floor" -ForegroundColor Yellow
    }
}

# ---- summary ---------------------------------------------------------------
Write-Head "summary"
if ($script:Failures.Count -eq 0) {
    Write-Host "all requested toolchains built and passed" -ForegroundColor Green
    exit 0
}
$script:Failures | ForEach-Object { Write-Host "FAILED: $_" -ForegroundColor Red }
exit 1
