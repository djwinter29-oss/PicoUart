param(
    [string]$BuildDir = "build/firmware",
    [string]$Board,
    [string]$ElfPath,
    [string]$Generator,
    [string]$PicoSdkPath,
    [string]$SystemClockKhz,
    [string]$OpenOcdExe = $env:OPENOCD_EXE,
    [string]$OpenOcdTarget = $env:PICO_OPENOCD_TARGET,
    [int]$AdapterSpeedKhz = 5000,
    [string]$DebugProbeVid = $(if ($env:PICO_DEBUG_PROBE_VID) { $env:PICO_DEBUG_PROBE_VID } else { "0x2e8a" }),
    [string]$DebugProbePid = $(if ($env:PICO_DEBUG_PROBE_PID) { $env:PICO_DEBUG_PROBE_PID } else { "0x000c" }),
    [string]$DebugProbeSerial = $env:PICO_DEBUG_PROBE_SERIAL,
    [switch]$SkipBuild,
    [switch]$UnsafeOverclock
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Board)) {
    $Board = $env:PICO_BOARD
}

if (-not [string]::IsNullOrWhiteSpace($Board)) {
    if ($BuildDir -eq "build/firmware") {
        $BuildDir = "build/firmware-$Board"
    }
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))

if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
    $PicoSdkPath = Join-Path $repoRoot ".pico-sdk"
}

if (-not $SkipBuild) {
    $buildArguments = @{
        BuildDir = $BuildDir
        Board = $Board
        Generator = $Generator
        PicoSdkPath = $PicoSdkPath
    }
    if (-not [string]::IsNullOrWhiteSpace($SystemClockKhz)) {
        $buildArguments.SystemClockKhz = $SystemClockKhz
    }
    if ($UnsafeOverclock) {
        $buildArguments.UnsafeOverclock = $true
    }
    & (Join-Path $PSScriptRoot "build.ps1") @buildArguments
}

if ([string]::IsNullOrWhiteSpace($ElfPath)) {
    $ElfPath = Join-Path $buildDirPath "pico_uart.elf"
}

if (-not (Test-Path $ElfPath)) {
    throw "ELF file not found: $ElfPath"
}

if ([string]::IsNullOrWhiteSpace($OpenOcdExe)) {
    $OpenOcdExe = "openocd"
}

if ([string]::IsNullOrWhiteSpace($OpenOcdTarget)) {
    $targetBoard = if ([string]::IsNullOrWhiteSpace($Board)) { "pico" } else { $Board }

    switch -Wildcard ($targetBoard) {
        "pico" { $OpenOcdTarget = "target/rp2040.cfg"; break }
        "pico_w" { $OpenOcdTarget = "target/rp2040.cfg"; break }
        "rp2040*" { $OpenOcdTarget = "target/rp2040.cfg"; break }
        "pico2" { $OpenOcdTarget = "target/rp2350.cfg"; break }
        "pico2_w" { $OpenOcdTarget = "target/rp2350.cfg"; break }
        "rp2350*" { $OpenOcdTarget = "target/rp2350.cfg"; break }
        default { throw "No default OpenOCD target for board '$targetBoard'. Use -OpenOcdTarget." }
    }
}

if (-not (Get-Command $OpenOcdExe -ErrorAction SilentlyContinue)) {
    throw "OpenOCD executable not found: $OpenOcdExe"
}

& $OpenOcdExe `
    -f interface/cmsis-dap.cfg `
    -f $OpenOcdTarget `
    -c "adapter speed $AdapterSpeedKhz" `
    $(if ([string]::IsNullOrWhiteSpace($DebugProbeSerial)) { @() } else { @("-c", "adapter serial $DebugProbeSerial") }) `
    -c "program $ElfPath verify reset exit"

if ($LASTEXITCODE -ne 0) {
    throw "OpenOCD failed to program $ElfPath"
}
