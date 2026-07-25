param(
    [string]$BuildDir = "build/firmware",
    [string]$Generator,
    [string]$PicoSdkPath,
    [switch]$SkipBuild,
    [switch]$SkipHost
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$ctestFile = Join-Path $buildDirPath "CTestTestfile.cmake"
$hostTests = Join-Path $repoRoot "host\python\tests\test_hid_reports.py"

if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
    $PicoSdkPath = Join-Path $repoRoot ".pico-sdk"
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Generator $Generator -PicoSdkPath $PicoSdkPath
}

if (-not $SkipHost) {
    Write-Host "Running host HID golden tests..."
    python $hostTests
    if ($LASTEXITCODE -ne 0) {
        throw "Host HID golden tests failed with exit code $LASTEXITCODE"
    }
}

if (Test-Path $ctestFile) {
    ctest --test-dir $buildDirPath --output-on-failure
} else {
    Write-Host "No firmware CMake/CTest targets are configured yet; host golden tests are the default automated suite."
}
