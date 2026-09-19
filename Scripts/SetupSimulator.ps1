param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$taskSimulator = Join-Path (Split-Path $PSScriptRoot -Parent) 'Tools\PoseDollSimulator'
$taskVenvPython = Join-Path $taskSimulator '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $taskVenvPython)) {
    & $Python -m venv (Join-Path $taskSimulator '.venv')
    if ($LASTEXITCODE -ne 0) { throw 'Creating the local Python virtual environment failed.' }
}
& $taskVenvPython -X utf8 -m pip install -r (Join-Path $taskSimulator 'requirements.lock')
if ($LASTEXITCODE -ne 0) { throw 'Installing the locked local dependencies failed.' }
Write-Output "Local simulator environment ready: $taskVenvPython"
