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

# One sketch built with extra compiler definitions, for the build switches.
function Compile-WithFlags {
    param(
        [string]$Label,
        [string]$Sketch,
        [string]$Flags
    )

    $buildPath = Join-Path $BuildRoot $Label
    Write-Host "Compiling $Label ($Flags)"
    & $ArduinoCli compile `
        --fqbn $Fqbn `
        --library $repoRoot `
        --build-path $buildPath `
        --build-property "compiler.cpp.extra_flags=$Flags" `
        $Sketch
    if ($LASTEXITCODE -ne 0) {
        throw "Arduino compile failed: $Label"
    }
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
Compile-SketchGroup "example" (Join-Path $repoRoot "examples")
Compile-SketchGroup "hardware-test" (Join-Path $repoRoot "extras\hardware-tests")

# Direction switches. A transmit-only build swaps TIM2 for SysTick in the
# envelope timing and a receive-only one drops TIM1, so both compile paths
# through UIAPIR.cpp have to build, not just the header. Kept in step with
# scripts/check.sh.
Compile-WithFlags "tx-only-SendProtocols" (Join-Path $repoRoot "examples\SendProtocols") "-DUIAPIR_ENABLE_RX=0"
Compile-WithFlags "tx-only-HT4_TransmitTiming" (Join-Path $repoRoot "extras\hardware-tests\HT4_TransmitTiming") "-DUIAPIR_ENABLE_RX=0"
Compile-WithFlags "rx-only-Receive" (Join-Path $repoRoot "examples\Receive") "-DUIAPIR_ENABLE_TX=0"
Compile-WithFlags "rx-only-HT5_ReceiveDump" (Join-Path $repoRoot "extras\hardware-tests\HT5_ReceiveDump") "-DUIAPIR_ENABLE_TX=0"

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
