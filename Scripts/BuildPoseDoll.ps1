param([string]$EngineRoot = 'E:\UnrealEngine\UE_5.8', [switch]$VerifyUnity)
$ErrorActionPreference = 'Stop'
$taskProject = Split-Path $PSScriptRoot -Parent
$taskReports = Join-Path $taskProject 'reports'
New-Item -ItemType Directory -Force -Path $taskReports | Out-Null
# Invalidate directory discovery when this project adds module Public/Private source directories.
$taskRules = Join-Path $taskProject 'Plugins\PoseDoll\Source\PoseDollEditor\PoseDollEditor.Build.cs'
(Get-Item -LiteralPath $taskRules).LastWriteTime = Get-Date
# Exercise committed-source unity builds even when Git marks edited files as adaptive exclusions.
$taskUnityArgs = @()
if ($VerifyUnity) { $taskUnityArgs = @('-ForceUnity', '-DisableAdaptiveUnity') }
& (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') DollSimulationEditor Win64 Development "-Project=$taskProject\DollSimulation.uproject" -WaitMutex -NoHotReloadFromIDE -NoLiveCoding -NoUBTMakefiles @taskUnityArgs 2>&1 | Tee-Object (Join-Path $taskReports 'build_latest.log')
exit $LASTEXITCODE
