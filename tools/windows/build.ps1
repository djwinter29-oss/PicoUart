param(
    [string]$BuildDir = "firmware/build",
    [string]$Board,
    [string]$Generator,
    [string]$PicoSdkPath = $env:PICO_SDK_PATH,
    [string]$FirmwareVersion = $env:PICO_UART_VERSION
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
    throw "PICO_SDK_PATH is not set."
}

if ([string]::IsNullOrWhiteSpace($Board)) {
    $Board = $env:PICO_BOARD
}

if (-not [string]::IsNullOrWhiteSpace($Board)) {
    if (($Board -ne "pico") -and ($Board -ne "pico2")) {
        throw "Unsupported board '$Board'. Use 'pico' or 'pico2'."
    }

    if ($BuildDir -eq "firmware/build") {
        $BuildDir = "firmware/build-$Board"
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sourceDir = Join-Path $repoRoot "firmware"
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))

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

if (-not [string]::IsNullOrWhiteSpace($Board)) {
    $cmakeArgs += "-DPICO_BOARD=$Board"
}

& cmake @cmakeArgs
cmake --build $buildDirPath --parallel