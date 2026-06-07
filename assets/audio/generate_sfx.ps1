param(
    [string]$OutputDir = (Split-Path -Parent $PSCommandPath),
    [int]$SampleRate = 44100
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$script:SampleRate = $SampleRate
$script:Rng = New-Object System.Random 20260605

function New-Buffer {
    param([double]$Seconds)
    $count = [Math]::Max(1, [int][Math]::Ceiling($Seconds * $script:SampleRate))
    return New-Object double[] $count
}

function Get-Envelope {
    param(
        [double]$Time,
        [double]$Duration,
        [double]$Attack,
        [double]$Release
    )

    $env = 1.0
    if ($Attack -gt 0 -and $Time -lt $Attack) {
        $env = $Time / $Attack
    }
    if ($Release -gt 0 -and $Time -gt ($Duration - $Release)) {
        $releaseEnv = ($Duration - $Time) / $Release
        if ($releaseEnv -lt $env) { $env = $releaseEnv }
    }
    if ($env -lt 0) { return 0.0 }
    if ($env -gt 1) { return 1.0 }
    return [Math]::Sin($env * [Math]::PI * 0.5)
}

function Add-Sine {
    param(
        [double[]]$Buffer,
        [double]$Start,
        [double]$Duration,
        [double]$FreqStart,
        [double]$FreqEnd,
        [double]$Amp,
        [double]$Attack = 0.005,
        [double]$Release = 0.020
    )

    if ($FreqEnd -le 0) { $FreqEnd = $FreqStart }
    $startIndex = [Math]::Max(0, [int][Math]::Floor($Start * $script:SampleRate))
    $sampleCount = [Math]::Max(1, [int][Math]::Ceiling($Duration * $script:SampleRate))
    $endIndex = [Math]::Min($Buffer.Length, $startIndex + $sampleCount)
    $phase = 0.0

    for ($i = $startIndex; $i -lt $endIndex; $i++) {
        $localIndex = $i - $startIndex
        $t = $localIndex / $script:SampleRate
        $u = if ($sampleCount -le 1) { 0.0 } else { $localIndex / ($sampleCount - 1.0) }
        $freq = $FreqStart + (($FreqEnd - $FreqStart) * $u)
        $phase += 2.0 * [Math]::PI * $freq / $script:SampleRate
        $env = Get-Envelope $t $Duration $Attack $Release
        $Buffer[$i] += [Math]::Sin($phase) * $Amp * $env
    }
}

function Add-Noise {
    param(
        [double[]]$Buffer,
        [double]$Start,
        [double]$Duration,
        [double]$Amp,
        [double]$Attack = 0.002,
        [double]$Release = 0.030
    )

    $startIndex = [Math]::Max(0, [int][Math]::Floor($Start * $script:SampleRate))
    $sampleCount = [Math]::Max(1, [int][Math]::Ceiling($Duration * $script:SampleRate))
    $endIndex = [Math]::Min($Buffer.Length, $startIndex + $sampleCount)
    $last = 0.0

    for ($i = $startIndex; $i -lt $endIndex; $i++) {
        $localIndex = $i - $startIndex
        $t = $localIndex / $script:SampleRate
        $white = ($script:Rng.NextDouble() * 2.0) - 1.0
        $last = ($last * 0.72) + ($white * 0.28)
        $env = Get-Envelope $t $Duration $Attack $Release
        $Buffer[$i] += $last * $Amp * $env
    }
}

function Add-SilenceTail {
    param([double[]]$Buffer)
    # Placeholder helper keeps definitions readable when a sound only needs tail room.
}

function Write-Wav {
    param(
        [string]$Path,
        [double[]]$Buffer,
        [double]$Gain = 0.90
    )

    $peak = 0.001
    foreach ($sample in $Buffer) {
        $abs = [Math]::Abs($sample)
        if ($abs -gt $peak) { $peak = $abs }
    }

    $scale = if ($peak -gt 1.0) { $Gain / $peak } else { $Gain }
    $bytesPerSample = 2
    $dataSize = $Buffer.Length * $bytesPerSample

    $stream = [System.IO.File]::Create($Path)
    try {
        $writer = New-Object System.IO.BinaryWriter $stream
        try {
            $ascii = [System.Text.Encoding]::ASCII
            $writer.Write($ascii.GetBytes('RIFF'))
            $writer.Write([int](36 + $dataSize))
            $writer.Write($ascii.GetBytes('WAVE'))
            $writer.Write($ascii.GetBytes('fmt '))
            $writer.Write([int]16)
            $writer.Write([int16]1)
            $writer.Write([int16]1)
            $writer.Write([int]$script:SampleRate)
            $writer.Write([int]($script:SampleRate * $bytesPerSample))
            $writer.Write([int16]$bytesPerSample)
            $writer.Write([int16]16)
            $writer.Write($ascii.GetBytes('data'))
            $writer.Write([int]$dataSize)

            foreach ($sample in $Buffer) {
                $clamped = [Math]::Max(-1.0, [Math]::Min(1.0, $sample * $scale))
                $writer.Write([int16][Math]::Round($clamped * 32767.0))
            }
        }
        finally {
            $writer.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function New-Sfx {
    param(
        [string]$Name,
        [double]$Duration,
        [scriptblock]$Build
    )

    $buffer = New-Buffer $Duration
    & $Build $buffer
    $path = Join-Path $OutputDir $Name
    Write-Wav $path $buffer
    Write-Host "Generated $Name"
}

$defs = @(
    @{ Name = 'ui_button_click.wav'; Duration = 0.090; Build = { param($b) Add-Sine $b 0.000 0.035 980 640 0.36 0.001 0.025; Add-Noise $b 0.000 0.020 0.09 0.001 0.012 } },
    @{ Name = 'ui_confirm.wav'; Duration = 0.180; Build = { param($b) Add-Sine $b 0.000 0.070 660 760 0.24 0.004 0.035; Add-Sine $b 0.065 0.090 880 980 0.28 0.004 0.040 } },
    @{ Name = 'ui_cancel.wav'; Duration = 0.160; Build = { param($b) Add-Sine $b 0.000 0.080 520 410 0.28 0.004 0.045; Add-Sine $b 0.045 0.080 390 300 0.18 0.004 0.040 } },
    @{ Name = 'ui_toast.wav'; Duration = 0.220; Build = { param($b) Add-Sine $b 0.000 0.075 740 740 0.20 0.003 0.040; Add-Sine $b 0.055 0.095 1110 1110 0.16 0.003 0.050 } },
    @{ Name = 'ui_pause.wav'; Duration = 0.180; Build = { param($b) Add-Sine $b 0.000 0.070 520 520 0.25 0.003 0.045; Add-Sine $b 0.055 0.070 390 390 0.22 0.003 0.045 } },
    @{ Name = 'ui_resume.wav'; Duration = 0.180; Build = { param($b) Add-Sine $b 0.000 0.070 390 390 0.20 0.003 0.045; Add-Sine $b 0.055 0.080 590 660 0.26 0.003 0.045 } },

    @{ Name = 'card_select.wav'; Duration = 0.110; Build = { param($b) Add-Sine $b 0.000 0.055 620 880 0.32 0.002 0.030; Add-Sine $b 0.018 0.060 1240 1480 0.11 0.002 0.030 } },
    @{ Name = 'card_deselect.wav'; Duration = 0.110; Build = { param($b) Add-Sine $b 0.000 0.065 620 360 0.25 0.002 0.035; Add-Sine $b 0.010 0.055 930 620 0.10 0.002 0.030 } },
    @{ Name = 'card_deal.wav'; Duration = 0.120; Build = { param($b) Add-Noise $b 0.000 0.075 0.22 0.001 0.035; Add-Sine $b 0.010 0.045 1500 900 0.07 0.001 0.030 } },
    @{ Name = 'card_play.wav'; Duration = 0.240; Build = { param($b) Add-Noise $b 0.000 0.105 0.33 0.001 0.060; Add-Sine $b 0.000 0.090 180 120 0.30 0.001 0.070; Add-Noise $b 0.075 0.080 0.12 0.001 0.050 } },
    @{ Name = 'card_pass.wav'; Duration = 0.230; Build = { param($b) Add-Sine $b 0.000 0.110 440 300 0.24 0.005 0.075; Add-Sine $b 0.055 0.100 330 260 0.16 0.005 0.070 } },
    @{ Name = 'card_hint.wav'; Duration = 0.320; Build = { param($b) Add-Sine $b 0.000 0.060 660 660 0.18 0.003 0.035; Add-Sine $b 0.070 0.060 820 820 0.20 0.003 0.035; Add-Sine $b 0.140 0.085 990 1120 0.24 0.003 0.050 } },

    @{ Name = 'game_invalid_move.wav'; Duration = 0.340; Build = { param($b) Add-Sine $b 0.000 0.095 190 170 0.32 0.002 0.050; Add-Sine $b 0.115 0.115 170 135 0.34 0.002 0.070; Add-Noise $b 0.000 0.040 0.08 0.001 0.020 } },
    @{ Name = 'game_turn_prompt.wav'; Duration = 0.420; Build = { param($b) Add-Sine $b 0.000 0.115 620 700 0.20 0.006 0.065; Add-Sine $b 0.105 0.150 930 1040 0.22 0.006 0.080; Add-Sine $b 0.210 0.120 1240 1240 0.11 0.006 0.070 } },
    @{ Name = 'game_round_start.wav'; Duration = 0.700; Build = { param($b) Add-Noise $b 0.000 0.500 0.16 0.010 0.120; Add-Sine $b 0.050 0.090 420 560 0.12 0.004 0.060; Add-Sine $b 0.200 0.110 520 690 0.14 0.004 0.070; Add-Sine $b 0.360 0.130 640 840 0.16 0.004 0.080 } },
    @{ Name = 'game_round_end.wav'; Duration = 0.500; Build = { param($b) Add-Sine $b 0.000 0.120 540 540 0.20 0.004 0.075; Add-Sine $b 0.100 0.130 720 720 0.21 0.004 0.080; Add-Sine $b 0.210 0.150 900 900 0.18 0.004 0.100 } },

    @{ Name = 'event_bomb.wav'; Duration = 0.950; Build = { param($b) Add-Sine $b 0.000 0.260 95 55 0.70 0.002 0.220; Add-Noise $b 0.015 0.300 0.48 0.001 0.220; Add-Sine $b 0.080 0.400 48 38 0.35 0.001 0.320; Add-Noise $b 0.320 0.320 0.13 0.001 0.250 } },
    @{ Name = 'event_spring.wav'; Duration = 0.950; Build = { param($b) Add-Sine $b 0.000 0.100 520 620 0.22 0.004 0.060; Add-Sine $b 0.105 0.105 660 760 0.24 0.004 0.065; Add-Sine $b 0.215 0.120 820 960 0.26 0.004 0.075; Add-Sine $b 0.345 0.210 1040 1240 0.28 0.004 0.130; Add-Sine $b 0.360 0.320 1560 1560 0.08 0.004 0.200 } },
    @{ Name = 'event_win.wav'; Duration = 1.100; Build = { param($b) Add-Sine $b 0.000 0.120 660 660 0.23 0.006 0.070; Add-Sine $b 0.115 0.120 820 820 0.24 0.006 0.070; Add-Sine $b 0.230 0.135 990 990 0.25 0.006 0.080; Add-Sine $b 0.370 0.260 1320 1320 0.30 0.006 0.170; Add-Sine $b 0.390 0.360 1980 1980 0.08 0.006 0.230 } },
    @{ Name = 'event_lose.wav'; Duration = 0.900; Build = { param($b) Add-Sine $b 0.000 0.170 520 480 0.21 0.006 0.110; Add-Sine $b 0.160 0.190 390 350 0.20 0.006 0.130; Add-Sine $b 0.340 0.230 300 260 0.17 0.006 0.170 } },
    @{ Name = 'event_ai_talk.wav'; Duration = 0.280; Build = { param($b) Add-Sine $b 0.000 0.060 760 900 0.18 0.003 0.035; Add-Sine $b 0.055 0.075 1080 1180 0.14 0.003 0.045; Add-Noise $b 0.000 0.030 0.045 0.001 0.015 } }
)

foreach ($def in $defs) {
    New-Sfx $def.Name $def.Duration $def.Build
}

Write-Host "Done. Output: $OutputDir"