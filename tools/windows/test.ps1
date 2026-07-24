param(
    [string]$BuildDir = "build/firmware",
    [string]$Generator,
    [string]$PicoSdkPath,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$ctestFile = Join-Path $buildDirPath "CTestTestfile.cmake"

if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
    $PicoSdkPath = Join-Path $repoRoot ".pico-sdk"
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Generator $Generator -PicoSdkPath $PicoSdkPath
}

if (Test-Path $ctestFile) {
    ctest --test-dir $buildDirPath --output-on-failure
} else {
    Write-Host "No CMake tests are configured for this repository."
}