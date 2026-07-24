param(
    [string]$SdkVersion = "2.2.0"
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sdkPath = Join-Path $repoRoot ".pico-sdk"
$sdkImport = Join-Path $sdkPath "external\pico_sdk_import.cmake"

if (-not (Test-Path $sdkPath)) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "git is required to download the Pico SDK."
    }

    Write-Host "Downloading Pico SDK $SdkVersion into $sdkPath"
    & git clone --branch $SdkVersion --depth 1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git $sdkPath
    if ($LASTEXITCODE -ne 0) {
        throw "Pico SDK download failed."
    }
}

if (-not (Test-Path $sdkImport)) {
    throw "Pico SDK is incomplete: $sdkPath"
}

if (-not (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue)) {
    throw "arm-none-eabi-gcc is not available on PATH."
}

$env:PICO_SDK_PATH = $sdkPath
Write-Host "PICO_SDK_PATH=$env:PICO_SDK_PATH"