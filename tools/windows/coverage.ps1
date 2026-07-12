param(
    [string]$BuildDir = "firmware/build-coverage",
    [string]$OutputDir = "firmware/build-coverage/coverage",
    [string]$Generator,
    [string]$PicoSdkPath = $env:PICO_SDK_PATH,
    [switch]$SkipBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
    throw "PICO_SDK_PATH is not set."
}

if (-not (Get-Command gcovr -ErrorAction SilentlyContinue)) {
    throw "gcovr is not installed or not on PATH."
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sourceDir = Join-Path $repoRoot "firmware"
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$outputDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDir))
$ctestFile = Join-Path $buildDirPath "CTestTestfile.cmake"
$htmlReport = Join-Path $outputDirPath "index.html"
$xmlReport = Join-Path $outputDirPath "coverage.xml"

if ([string]::IsNullOrWhiteSpace($Generator)) {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $Generator = "Ninja"
    } else {
        $Generator = "MinGW Makefiles"
    }
}

if (-not $SkipBuild) {
    $cmakeArgs = @(
        "-S", $sourceDir,
        "-B", $buildDirPath,
        "-G", $Generator,
        "-DPICO_SDK_PATH=$PicoSdkPath",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_C_FLAGS=--coverage -O0 -g",
        "-DCMAKE_CXX_FLAGS=--coverage -O0 -g",
        "-DCMAKE_EXE_LINKER_FLAGS=--coverage"
    )

    & cmake @cmakeArgs
    & cmake --build $buildDirPath --parallel
}

if (-not (Test-Path $ctestFile)) {
    throw "No CMake tests are configured for this repository, so no coverage report can be generated yet."
}

if (-not $SkipTests) {
    ctest --test-dir $buildDirPath --output-on-failure
}

New-Item -ItemType Directory -Path $outputDirPath -Force | Out-Null

$gcovrArgs = @(
    "--root", $repoRoot,
    "--object-directory", $buildDirPath,
    "--filter", "firmware/src",
    "--exclude-unreachable-branches",
    "--exclude-throw-branches"
)

& gcovr @gcovrArgs --print-summary
& gcovr @gcovrArgs --html-details $htmlReport
& gcovr @gcovrArgs --xml-pretty $xmlReport

Write-Host "Coverage reports written to: $outputDirPath"