param(
    [string]$BuildDir = "firmware/build",
    [string]$Board,
    [string]$Uf2Path,
    [string]$MountPath,
    [string]$Generator,
    [string]$PicoSdkPath = $env:PICO_SDK_PATH,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

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
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1") -BuildDir $BuildDir -Board $Board -Generator $Generator -PicoSdkPath $PicoSdkPath
}

if ([string]::IsNullOrWhiteSpace($Uf2Path)) {
    $Uf2Path = Join-Path $buildDirPath "pico_uart.uf2"
}

if (-not (Test-Path $Uf2Path)) {
    throw "UF2 file not found: $Uf2Path"
}

if (Get-Command picotool -ErrorAction SilentlyContinue) {
    picotool load $Uf2Path -f
    picotool reboot
    exit 0
}

if ([string]::IsNullOrWhiteSpace($MountPath)) {
    $bootDrive = Get-Volume -FileSystemLabel "RPI-RP2" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $bootDrive) {
        $MountPath = ($bootDrive.DriveLetter + ":\")
    }
}

if ([string]::IsNullOrWhiteSpace($MountPath) -or -not (Test-Path $MountPath)) {
    throw "Could not find the RPI-RP2 boot volume. Use -MountPath or install picotool."
}

Copy-Item $Uf2Path (Join-Path $MountPath "pico_uart.uf2") -Force