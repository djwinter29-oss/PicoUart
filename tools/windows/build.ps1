param(
    [string]$BuildDir = "build/firmware",
    [string]$Board,
    [string]$Generator,
    [string]$PicoSdkPath,
    [string]$FirmwareVersion = $env:PICO_UART_VERSION,
    [string]$SystemClockKhz
)

$ErrorActionPreference = "Stop"

if (-not [string]::IsNullOrWhiteSpace($SystemClockKhz)) {
    [int]$parsedSystemClockKhz = 0
    if (-not [int]::TryParse($SystemClockKhz, [ref]$parsedSystemClockKhz) -or
        $parsedSystemClockKhz -le 0 -or $parsedSystemClockKhz -gt 400000) {
        throw "System clock must be a positive integer no greater than 400000 kHz."
    }
}

if ([string]::IsNullOrWhiteSpace($Board)) {
    $Board = $env:PICO_BOARD
}

if (-not [string]::IsNullOrWhiteSpace($Board)) {
    if ($BuildDir -eq "build/firmware") {
        $BuildDir = "build/firmware-$Board"
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sourceDir = Join-Path $repoRoot "firmware"
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))

if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
    $PicoSdkPath = Join-Path $repoRoot ".pico-sdk"
}

if (-not (Test-Path (Join-Path $PicoSdkPath "external\pico_sdk_import.cmake"))) {
    throw "Pico SDK is not available at $PicoSdkPath. Run . .\tools\windows\setup-sdk-env.ps1 first."
}

if ([string]::IsNullOrWhiteSpace($Generator)) {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $Generator = "Ninja"
    } else {
        $Generator = "MinGW Makefiles"
    }
}

$cmakeArgs = @(
    "-S", $sourceDir,
    "-B", $buildDirPath,
    "-G", $Generator,
    "-DPICO_SDK_PATH=$PicoSdkPath"
)

if (-not [string]::IsNullOrWhiteSpace($FirmwareVersion)) {
    $cmakeArgs += "-DPICO_UART_VERSION=$FirmwareVersion"
}

if (-not [string]::IsNullOrWhiteSpace($SystemClockKhz)) {
    $cmakeArgs += "-DPICO_UART_SYSTEM_CLOCK_KHZ=$parsedSystemClockKhz"
}

if (-not [string]::IsNullOrWhiteSpace($Board)) {
    $cmakeArgs += "-DPICO_BOARD=$Board"
}

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $buildDirPath --parallel
if ($LASTEXITCODE -ne 0) { throw "firmware build failed" }
