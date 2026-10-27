param([Parameter(Mandatory=$true)][string]$EngineRoot,[switch]$EditorOnly)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$project=Join-Path $root 'LongNight.uproject'
& (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') LongNightEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
if($LASTEXITCODE -ne 0){throw "Editor build failed: $LASTEXITCODE"}
if($EditorOnly){return}
& (Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat') BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -nodebuginfo "-archivedirectory=$root/Builds/NineStages" '-AdditionalCookerOptions=-skipzenstore' -utf8output
if($LASTEXITCODE -ne 0){throw "Packaging failed: $LASTEXITCODE"}
