param(
    [string]$SdkVersion = "2.3.0",
    [string]$SdkRevision = "98a542c1a62fb549ffb5d66a3e5892b06276b670"
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
} else {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "git is required to verify the Pico SDK checkout."
    }
    & git -C $sdkPath rev-parse --git-dir *> $null
    if ($LASTEXITCODE -ne 0) { throw "Pico SDK is not a git checkout: $sdkPath" }
    & git -C $sdkPath fetch --depth 1 --force origin "refs/tags/${SdkVersion}:refs/tags/${SdkVersion}"
    if ($LASTEXITCODE -ne 0) { throw "Pico SDK $SdkVersion update check failed." }
    $tagRevision = (& git -C $sdkPath rev-parse "${SdkVersion}^{commit}").Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($tagRevision)) {
        throw "Unable to resolve Pico SDK $SdkVersion."
    }
    if ($tagRevision -ne $SdkRevision) {
        throw "Pico SDK tag $SdkVersion resolved to $tagRevision, expected $SdkRevision."
    }
    $currentRevision = (& git -C $sdkPath rev-parse HEAD).Trim()
    if ($currentRevision -ne $SdkRevision) {
        Write-Host "Updating Pico SDK checkout to $SdkRevision ($SdkVersion)"
        & git -C $sdkPath checkout --detach $SdkRevision
        if ($LASTEXITCODE -ne 0) { throw "Pico SDK checkout update failed." }
    }
    & git -C $sdkPath submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "Pico SDK submodule update failed." }
}

if (-not (Test-Path $sdkImport)) {
    throw "Pico SDK is incomplete: $sdkPath"
}

if (-not (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue)) {
    throw "arm-none-eabi-gcc is not available on PATH."
}

$currentRevision = (& git -C $sdkPath rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $currentRevision -ne $SdkRevision) {
    throw "Pico SDK revision mismatch: expected $SdkRevision, got $currentRevision"
}
$dirtyState = & git -C $sdkPath status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $dirtyState) {
    throw "Pico SDK checkout or submodules contain tracked modifications."
}
$env:PICO_SDK_PATH = $sdkPath
Write-Host "PICO_SDK_PATH=$env:PICO_SDK_PATH"
Write-Host "Pico SDK $SdkVersion revision: $currentRevision"
