param(
    [string]$Cxx = $(if ($env:CXX) { $env:CXX } else { "" }),
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not $OutDir) {
    $OutDir = Join-Path $repoRoot ".test-build\host"
}

if (-not $Cxx) {
    foreach ($candidate in @("c++", "g++", "clang++")) {
        if (Get-Command $candidate -ErrorAction SilentlyContinue) {
            $Cxx = $candidate
            break
        }
    }
}

if (-not $Cxx) {
    $bash = Get-Command "bash" -ErrorAction SilentlyContinue
    if ($bash) {
        Write-Host "Native C++ compiler not found; running tests through bash"
        Push-Location $repoRoot
        try {
            & $bash.Source "tests/run.sh"
            if ($LASTEXITCODE -ne 0) {
                throw "Host tests failed with exit code $LASTEXITCODE"
            }
        }
        finally {
            Pop-Location
        }
        return
    }
    throw "No C++ compiler was found. Install c++, g++, or clang++, or make bash available."
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Invoke-Checked {
    param(
        [string]$Command,
        [string[]]$Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $Command"
    }
}

Push-Location $repoRoot
try {
    $flags = @("-std=c++14", "-Wall", "-Wextra", "-Werror", "-Isrc")

    $protocolTests = Join-Path $OutDir "protocol_tests.exe"
    Invoke-Checked $Cxx ($flags + @(
        "tests/test_protocols.cpp",
        "src/UIAPIRProtocol.cpp",
        "-o", $protocolTests
    ))
    Invoke-Checked $protocolTests @()
    Write-Host "UIAPIR protocol tests passed"

    $captureTests = Join-Path $OutDir "capture_tests.exe"
    Invoke-Checked $Cxx ($flags + @(
        "tests/test_capture.cpp",
        "src/UIAPIRCapture.cpp",
        "src/UIAPIRProtocol.cpp",
        "-o", $captureTests
    ))
    Invoke-Checked $captureTests @()

    $apiObject = Join-Path $OutDir "api_tests.o"
    Invoke-Checked $Cxx ($flags + @(
        "-Itests",
        "-c", "tests/test_api.cpp",
        "-o", $apiObject
    ))
    Write-Host "UIAPIR API compile check passed"

    $linkTests = Join-Path $OutDir "link_tests.exe"
    Invoke-Checked $Cxx ($flags + @("-Itests", "tests/test_link.cpp", "src/UIAPIRProtocol.cpp", "-o", $linkTests))
    Invoke-Checked $linkTests @()

    # Protocol selection. Every UIAPIR_ENABLE_* combination has to compile
    # clean, which is more than a formality: -Werror turns a helper left behind
    # by a switched-off decoder into a build failure, and that is exactly the
    # mistake these guards invite. RAW-only (all three off) is a legitimate
    # build. Kept in step with tests/run.sh.
    $selections = @(
        @("-DUIAPIR_ENABLE_NEC=0"),
        @("-DUIAPIR_ENABLE_AEHA=0"),
        @("-DUIAPIR_ENABLE_SONY=0"),
        @("-DUIAPIR_ENABLE_AEHA=0", "-DUIAPIR_ENABLE_SONY=0"),
        @("-DUIAPIR_ENABLE_NEC=0", "-DUIAPIR_ENABLE_SONY=0"),
        @("-DUIAPIR_ENABLE_NEC=0", "-DUIAPIR_ENABLE_AEHA=0"),
        @("-DUIAPIR_ENABLE_NEC=0", "-DUIAPIR_ENABLE_AEHA=0", "-DUIAPIR_ENABLE_SONY=0")
    )
    $protocolSelObject = Join-Path $OutDir "protocol_sel.o"
    $apiSelObject = Join-Path $OutDir "api_sel.o"
    foreach ($selection in $selections) {
        Invoke-Checked $Cxx ($flags + $selection + @("-Itests", "tests/test_link.cpp", "src/UIAPIRProtocol.cpp", "-o", $linkTests))
        Invoke-Checked $linkTests @()
        Invoke-Checked $Cxx ($flags + $selection + @(
            "-c", "src/UIAPIRProtocol.cpp",
            "-o", $protocolSelObject
        ))
        Invoke-Checked $Cxx ($flags + $selection + @(
            "-Itests",
            "-c", "tests/test_api.cpp",
            "-o", $apiSelObject
        ))
    }
    Write-Host "UIAPIR protocol selection compile checks passed"
    $linkConfigs = @(
        @("-DUIAPIR_ENABLE_NEC=0", "-DUIAPIR_ENABLE_SONY=0", "-DUIAPIR_MAX_AEHA_BYTES=3", "-DUIAPIR_RAW_BUFFER_SIZE=67"),
        @("-DUIAPIR_ENABLE_NEC=0", "-DUIAPIR_ENABLE_SONY=0", "-DUIAPIR_MAX_AEHA_BYTES=32", "-DUIAPIR_RAW_BUFFER_SIZE=515"),
        @("-DUIAPIR_RAW_TICK_US=40")
    )
    foreach ($config in $linkConfigs) {
        Invoke-Checked $Cxx ($flags + $config + @("-Itests", "tests/test_link.cpp", "src/UIAPIRProtocol.cpp", "-o", $linkTests))
        Invoke-Checked $linkTests @()
    }
}
finally {
    Pop-Location
}
