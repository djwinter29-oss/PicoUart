param(
    [string]$BuildDir = "build/firmware",
    [string]$HostTestBuildDir = "build/host-tests",
    [string]$Generator,
    [string]$PicoSdkPath,
    [switch]$SkipBuild,
    [switch]$SkipHost,
    [switch]$SkipC,
    [switch]$SkipPython
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$hostTestBuildPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $HostTestBuildDir))
$ctestFile = Join-Path $buildDirPath "CTestTestfile.cmake"

if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
    $PicoSdkPath = Join-Path $repoRoot ".pico-sdk"
}

if ([string]::IsNullOrWhiteSpace($Generator)) {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $Generator = "Ninja"
    } else {
        $Generator = "MinGW Makefiles"
    }
}

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Generator $Generator -PicoSdkPath $PicoSdkPath
}

if (-not $SkipHost) {
    if (-not $SkipC) {
        Write-Host "=== Host C unit tests (Unity / CTest) ==="
        cmake -S (Join-Path $repoRoot "tests\c") -B $hostTestBuildPath -G $Generator
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
        cmake --build $hostTestBuildPath --parallel
        if ($LASTEXITCODE -ne 0) { throw "host C test build failed" }
        ctest --test-dir $hostTestBuildPath --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "host C tests failed" }
    }

    if (-not $SkipPython) {
        Write-Host "=== Host Python tests (pytest) ==="
        python -m pip install -q -r (Join-Path $repoRoot "host\python\requirements-dev.txt")
        Push-Location $repoRoot
        try {
            python -m pytest
            if ($LASTEXITCODE -ne 0) { throw "pytest failed" }
        } finally {
            Pop-Location
        }
    }
}

if (Test-Path $ctestFile) {
    ctest --test-dir $buildDirPath --output-on-failure
}
