param([switch]$Headless, [switch]$Static, [int]$Port = 0)
$ErrorActionPreference = 'Stop'
$taskSimulator = Join-Path (Split-Path $PSScriptRoot -Parent) 'Tools\PoseDollSimulator'
$env:PYTHONPATH = Join-Path $taskSimulator 'src'
$taskPython = Join-Path $taskSimulator '.venv\Scripts\python.exe'
$taskArgs = @('-X', 'utf8', '-m', 'posedoll_sim')
if ($Static) { $taskArgs += '--static' }
if ($Headless) { $taskArgs += '--headless' }
if ($Port -gt 0) { $taskArgs += @('--port', $Port) }
& $taskPython @taskArgs
exit $LASTEXITCODE
