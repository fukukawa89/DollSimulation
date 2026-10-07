param([string]$EngineRoot='E:\UnrealEngine\UE_5.8')
$ErrorActionPreference='Stop'
$taskProject=Split-Path $PSScriptRoot -Parent
foreach($taskSuite in @('hand_presets','hand_presets_reopen')) {
    $taskReport=Join-Path $taskProject ('reports/'+$taskSuite+'.json')
    $taskStarted=Get-Date
    & (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') (Join-Path $taskProject 'DollSimulation.uproject') "-ExecutePythonScript=$PSScriptRoot/test_$taskSuite.py" -NullRHI -Unattended -NoSound -NoSaveConfig '-DisablePlugins=ModelContextProtocol,AllToolsets,MCPClientToolset,PoseDollAutomation' "-abslog=$taskProject/reports/$taskSuite.log"
    if($LASTEXITCODE -ne 0){throw "$taskSuite editor failed: $LASTEXITCODE"}
    if(!(Test-Path -LiteralPath $taskReport) -or (Get-Item -LiteralPath $taskReport).LastWriteTime -lt $taskStarted){throw "$taskSuite missing fresh report"}
    $taskResult=Get-Content -LiteralPath $taskReport -Raw | ConvertFrom-Json
    if(!$taskResult.passed){throw "$taskSuite failed: $($taskResult.exception)"}
    Write-Output "$taskSuite passed"
}
