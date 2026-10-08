[CmdletBinding()]
param(
    [string]$VenvDir,
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$projectDir = Join-Path $repoRoot "host/python"
$lockFile = Join-Path $projectDir "requirements-lock.txt"

if ([string]::IsNullOrWhiteSpace($VenvDir)) {
    $VenvDir = Join-Path $projectDir ".venv"
} elseif ($VenvDir -eq "~") {
    $VenvDir = $HOME
} elseif ($VenvDir.StartsWith("~/") -or $VenvDir.StartsWith("~\")) {
    $VenvDir = Join-Path $HOME $VenvDir.Substring(2)
}
if (-not [System.IO.Path]::IsPathRooted($VenvDir)) {
    $VenvDir = Join-Path (Get-Location).Path $VenvDir
}
$VenvDir = [System.IO.Path]::GetFullPath($VenvDir)

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable exited with status $LASTEXITCODE"
    }
}

try {
    Invoke-Checked -Executable $Python -Arguments @(
        "-c", "import sys; raise SystemExit(sys.version_info < (3, 10))"
    )
    Invoke-Checked -Executable $Python -Arguments @("-m", "venv", $VenvDir)

    $venvPython = Join-Path $VenvDir "Scripts/python.exe"
    Invoke-Checked -Executable $venvPython -Arguments @(
        "-m", "pip", "install", "--require-hashes", "-r", $lockFile
    )
    Invoke-Checked -Executable $venvPython -Arguments @(
        "-m", "pip", "install", "--no-deps", "--no-build-isolation", "-e", $projectDir
    )
} catch {
    Write-Error $_
    exit 1
}

$powershellActivate = Join-Path $VenvDir "Scripts/Activate.ps1"
$commandPromptActivate = Join-Path $VenvDir "Scripts/activate.bat"
Write-Host "Environment ready: $VenvDir"
Write-Host "PowerShell: & `"$powershellActivate`""
Write-Host "Command Prompt: `"$commandPromptActivate`""
Write-Host "Check the CLI with: `"$venvPython`" -m pico_uart --help"