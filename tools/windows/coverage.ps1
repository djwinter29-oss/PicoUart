param(
    [string]$BuildDir = "build/host-coverage",
    [string]$OutputDir = "build/host-coverage/report",
    [string]$Generator,
    [switch]$SkipBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command gcovr -ErrorAction SilentlyContinue)) {
    throw "gcovr is not installed or not on PATH."
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sourceDir = Join-Path $repoRoot "firmware\tests"
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
    $gcc = Get-Command gcc -ErrorAction SilentlyContinue
    if (-not $gcc) {
        throw "GCC is required for gcov coverage. Install MinGW GCC and ensure gcc is on PATH."
    }
    $cmakeArgs = @(
        "-S", $sourceDir,
        "-B", $buildDirPath,
        "-G", $Generator,
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_C_COMPILER=$($gcc.Source)",
        "-DCMAKE_C_FLAGS=--coverage -O0 -g",
        "-DCMAKE_EXE_LINKER_FLAGS=--coverage"
    )

    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "coverage configure failed" }
    & cmake --build $buildDirPath --parallel
    if ($LASTEXITCODE -ne 0) { throw "coverage build failed" }
}

if (-not (Test-Path $ctestFile)) {
    throw "Host Unity/CTest targets were not generated in $buildDirPath."
}

if (-not $SkipTests) {
    ctest --test-dir $buildDirPath --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "coverage tests failed" }
}

New-Item -ItemType Directory -Path $outputDirPath -Force | Out-Null

$gcovrArgs = @(
    "--root", $repoRoot,
    "--object-directory", $buildDirPath,
    "--filter", "firmware/src/uart/ring_buffer",
    "--filter", "firmware/src/uart/line_coding.c",
    "--filter", "firmware/src/uart/dma_progress_math.h",
    "--filter", "firmware/src/uart/control_pending.h",
    "--filter", "firmware/src/uart/backend_policy.h",
    "--filter", "firmware/src/uart/topology.c",
    "--filter", "firmware/src/uart/pio/txstall_wait.h",
    "--filter", "firmware/src/usb/cdc_soft_pending.h",
    "--exclude", ".*CMakeFiles/.*",
    "--exclude-unreachable-branches",
    "--exclude-throw-branches",
    "--gcov-ignore-errors=no_working_dir_found"
)

& gcovr @gcovrArgs --print-summary
if ($LASTEXITCODE -ne 0) { throw "gcovr summary failed" }
& gcovr @gcovrArgs --html-details $htmlReport
if ($LASTEXITCODE -ne 0) { throw "gcovr HTML report failed" }
& gcovr @gcovrArgs --xml-pretty -o $xmlReport
if ($LASTEXITCODE -ne 0) { throw "gcovr XML report failed" }

Write-Host "Coverage reports written to: $outputDirPath"
