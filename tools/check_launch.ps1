# Smoke check that an executable actually starts.
#
# Why this exists: the ctest suite runs scene_viewer.exe, never the shipping executable.  So when a
# link-option change made the loader reject pao_de_kuai.exe with STATUS_INVALID_IMAGE_FORMAT
# (0xC000007B -- "应用程序无法正常启动"), all fifteen tests stayed green and the broken exe looked
# verified.  Loading and starting the real executable is the one thing no other test does.
#
# 0xC000007B comes back at process-creation time, so a rejected image is gone within a second and
# its exit code says so.  A healthy start leaves the game window up, and its title distinguishes a
# real start from a loader error dialog.  The process is always killed before returning.
#
# Usage: pwsh -NoProfile -File tools/check_launch.ps1 <exe> [-ExpectWindowTitle <text>]
# Exit code 0 when the image started, 1 otherwise.

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Exe,

    # Substring the window title must contain; empty skips the check.  A loader error dialog would
    # have a different title, so this is what separates "started" from "showed an error box".
    [string]$ExpectWindowTitle = "跑得快",

    # How long to keep asking whether the process is still alive.  A rejected image dies well
    # inside the first half second; the rest is headroom for a cold start on a loaded CI runner.
    [int]$TimeoutSeconds = 8
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $Exe)) {
    Write-Host ("MISSING: {0}" -f $Exe)
    exit 1
}

$full = (Resolve-Path $Exe).Path
$process = Start-Process -FilePath $full -WorkingDirectory (Split-Path -Parent $full) -PassThru `
    -ErrorAction SilentlyContinue
if (-not $process) {
    Write-Host ("could not be created: {0}" -f $full)
    exit 1
}

$exited = $false
for ($tick = 0; $tick -lt ($TimeoutSeconds * 2); $tick++) {
    Start-Sleep -Milliseconds 500
    if ($process.HasExited) { $exited = $true; break }
    # A window means it really started, so stop waiting -- otherwise this test would idle for the
    # full timeout on every one of the CI jobs.
    if ($process.MainWindowTitle) { break }
    $process.Refresh()
}

$verdict = 0
if ($exited) {
    $code = $process.ExitCode
    if ($code -eq -1073741701) {
        Write-Host ("{0} : LOADER REJECTED the image (0xC000007B / STATUS_INVALID_IMAGE_FORMAT)" -f $full)
    } else {
        Write-Host ("{0} : exited 0x{1:X8}" -f $full, $code)
    }
    $verdict = 1
} else {
    $title = $process.MainWindowTitle
    if ($ExpectWindowTitle -and $title -notlike "*$ExpectWindowTitle*") {
        Write-Host ("{0} : running but window title is [{1}], expected one containing [{2}]" -f `
            $full, $title, $ExpectWindowTitle)
        $verdict = 1
    } else {
        Write-Host ("{0} : started, window [{1}]" -f $full, $title)
    }
}

# Always clean up, whatever happened above.
Get-Process -Name ([System.IO.Path]::GetFileNameWithoutExtension($full)) -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
exit $verdict
