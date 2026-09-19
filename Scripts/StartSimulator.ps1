param([switch]$Headless, [int]$Port = 39177)
$ErrorActionPreference = 'Stop'
$taskSimulator = Join-Path (Split-Path $PSScriptRoot -Parent) 'Tools\PoseDollSimulator'
$env:PYTHONPATH = Join-Path $taskSimulator 'src'
$taskPython = Join-Path $taskSimulator '.venv\Scripts\python.exe'
if ($Headless) { & $taskPython -X utf8 -m posedoll_sim --headless --port $Port }
else { & $taskPython -X utf8 -m posedoll_sim --port $Port }
exit $LASTEXITCODE
