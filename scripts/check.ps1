param(
    [string]$ArduinoCli = $(if ($env:ARDUINO_CLI) { $env:ARDUINO_CLI } else { "arduino-cli" }),
    [string]$Fqbn = $(if ($env:UIAPIR_FQBN) { $env:UIAPIR_FQBN } else { "UIAP:ch32v:CH32V00x_EVT:pnum=CH32V003V1DOT4" }),
    [string]$BuildRoot = "",
    [switch]$SkipHostTests
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $BuildRoot) {
    $BuildRoot = Join-Path $repoRoot "build\arduino"
}

if (-not $SkipHostTests -and $env:SKIP_HOST_TESTS -ne "1") {
    & (Join-Path $repoRoot "tests\run.ps1")
    if ($LASTEXITCODE -ne 0) {
        throw "Host tests failed"
    }
}

if (-not (Get-Command $ArduinoCli -ErrorAction SilentlyContinue)) {
    throw "Arduino CLI was not found: $ArduinoCli"
}

function Compile-SketchGroup {
    param(
        [string]$Group,
        [string]$SketchRoot
    )

    Get-ChildItem -Path $SketchRoot -Directory | ForEach-Object {
        # HT6 is a PlatformIO project, not a sketch: `pio run` builds it.
        if (-not (Test-Path (Join-Path $_.FullName "$($_.Name).ino"))) { return }
        $buildPath = Join-Path $BuildRoot "$Group-$($_.Name)"
        Write-Host "Compiling $Group/$($_.Name)"
        & $ArduinoCli compile `
            --fqbn $Fqbn `
            --library $repoRoot `
            --build-path $buildPath `
            $_.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "Arduino compile failed: $Group/$($_.Name)"
        }
    }
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
Compile-SketchGroup "example" (Join-Path $repoRoot "examples")
Compile-SketchGroup "hardware-test" (Join-Path $repoRoot "extras\hardware-tests")

# HT6 is the two-board test and needs PlatformIO rather than arduino-cli.
# Skipped when pio is not installed, because most contributors will not have
# it and the rest of the suite is complete without it.
$pio = if ($env:PIO) { $env:PIO } else { "pio" }
if (Get-Command $pio -ErrorAction SilentlyContinue) {
    Write-Host "Compiling hardware-test/HT6_TwoBoard (PlatformIO)"
    Push-Location (Join-Path $repoRoot "extras\hardware-tests\HT6_TwoBoard")
    try {
        & $pio run
        if ($LASTEXITCODE -ne 0) {
            throw "PlatformIO build failed: HT6_TwoBoard"
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "Skipping hardware-test/HT6_TwoBoard: PlatformIO was not found: $pio"
}

Write-Host "All UIAPIR checks passed"
