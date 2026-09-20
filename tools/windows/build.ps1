param(
    [string]$BuildDir = "build/firmware",
    [string]$Board,
    [string]$Generator,
    [string]$PicoSdkPath,
    [string]$FirmwareVersion = $env:PICO_UART_VERSION,
    [string]$SystemClockKhz,
    [switch]$AllowHidReset,
    [switch]$UnsafeOverclock
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
if ([string]::IsNullOrWhiteSpace($Board)) {
    $Board = "pico"
}

if (-not [string]::IsNullOrWhiteSpace($Board)) {
    if ($BuildDir -eq "build/firmware") {
        $BuildDir = "build/firmware-$Board"
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sourceDir = Join-Path $repoRoot "firmware"
$buildDirPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    [System.IO.Path]::GetFullPath($BuildDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
}

if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
    $PicoSdkPath = $env:PICO_SDK_PATH
    if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
        $PicoSdkPath = Join-Path $repoRoot ".pico-sdk"
    }
}
$PicoSdkPath = [System.IO.Path]::GetFullPath($PicoSdkPath)

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

if ([string]::IsNullOrWhiteSpace($SystemClockKhz)) {
    if ($Board -match "^(pico2|pico2_w|rp2350)") {
        $SystemClockKhz = "150000"
    } else {
        $SystemClockKhz = "125000"
    }
    $parsedSystemClockKhz = [int]$SystemClockKhz
}

$cachePath = Join-Path $buildDirPath "CMakeCache.txt"
if (Test-Path $cachePath) {
    $cache = Get-Content $cachePath -Raw
    $cachedSdkPathMatch = [regex]::Match(
        $cache,
        "(?m)^PICO_SDK_PATH:(?:PATH|UNINITIALIZED)=(.+)`r?$"
    )
    $sdkPathMatches = $false
    if ($cachedSdkPathMatch.Success) {
        $cachedSdkPath = [System.IO.Path]::GetFullPath(
            $cachedSdkPathMatch.Groups[1].Value
        ).TrimEnd([char[]]"\/")
        $expectedSdkPath = $PicoSdkPath.TrimEnd([char[]]"\/")
        $sdkPathMatches = [string]::Equals(
            $cachedSdkPath,
            $expectedSdkPath,
            [System.StringComparison]::OrdinalIgnoreCase
        )
    }
    if ($cache -notmatch [regex]::Escape("CMAKE_GENERATOR:INTERNAL=$Generator") -or
        $cache -notmatch [regex]::Escape("PICO_BOARD:STRING=$Board") -or
        -not $sdkPathMatches) {
        Write-Host "Build configuration changed; resetting generated CMake state in $buildDirPath"
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue `
            (Join-Path $buildDirPath "CMakeCache.txt"),
            (Join-Path $buildDirPath "CMakeFiles"),
            (Join-Path $buildDirPath "build.ninja"),
            (Join-Path $buildDirPath "cmake_install.cmake"),
            (Join-Path $buildDirPath "Makefile"),
            (Join-Path $buildDirPath "_deps"),
            (Join-Path $buildDirPath "pico-sdk"),
            (Join-Path $buildDirPath "pioasm"),
            (Join-Path $buildDirPath "pioasm-install")
    }
}

if ([string]::IsNullOrWhiteSpace($FirmwareVersion)) {
    $FirmwareVersion = "0.0.0-dev"
}

$cmakeArgs = @(
    "-S", $sourceDir,
    "-B", $buildDirPath,
    "-G", $Generator,
    "-DPICO_SDK_PATH=$PicoSdkPath",
    "-DPICO_BOARD=$Board",
    "-DPICO_UART_VERSION=$FirmwareVersion",
    "-DPICO_UART_SYSTEM_CLOCK_KHZ=$parsedSystemClockKhz",
    "-DPICO_UART_ALLOW_HID_RESET=$($AllowHidReset.IsPresent)",
    "-DPICO_UART_ALLOW_UNSAFE_OVERCLOCK=$($UnsafeOverclock.IsPresent)"
)

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
cmake --build $buildDirPath --parallel
if ($LASTEXITCODE -ne 0) { throw "firmware build failed" }
