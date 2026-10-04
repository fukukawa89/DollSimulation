param(
    [string]$EngineRoot = 'E:\UnrealEngine\UE_5.8',
    [switch]$IncludeO22
)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path $PSScriptRoot -Parent
$taskEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$taskProjectFile = Join-Path $taskProject 'DollSimulation.uproject'
$taskReports = Join-Path $taskProject 'reports'
New-Item -ItemType Directory -Force -Path $taskReports | Out-Null
$taskSuites = @('pose_editing', 'pose_editing_reopen', 'pose_editing_static')
if ($IncludeO22) { $taskSuites += 'pose_editing_o22' }
foreach ($taskSuite in $taskSuites) {
    $taskScript = Join-Path $PSScriptRoot ('test_' + $taskSuite + '.py')
    $taskReport = Join-Path $taskReports ($taskSuite + '.json')
    $taskLog = Join-Path $taskReports ($taskSuite + '.log')
    $taskStarted = Get-Date
    & $taskEditor $taskProjectFile "-ExecutePythonScript=$taskScript" -NullRHI -Unattended -NoSound -NoSaveConfig '-DisablePlugins=ModelContextProtocol,AllToolsets,MCPClientToolset,PoseDollAutomation' "-abslog=$taskLog"
    if ($LASTEXITCODE -ne 0) { throw "${taskSuite}: editor exited with $LASTEXITCODE; see $taskLog" }
    if (!(Test-Path -LiteralPath $taskReport) -or (Get-Item -LiteralPath $taskReport).LastWriteTime -lt $taskStarted) {
        throw "$taskSuite did not produce a fresh report; see $taskLog"
    }
    $taskResult = Get-Content -Raw -LiteralPath $taskReport | ConvertFrom-Json
    if (!$taskResult.passed) { throw "$taskSuite failed: $($taskResult.exception)" }
    Write-Output "$taskSuite passed"
}
