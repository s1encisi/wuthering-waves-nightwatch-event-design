<# Real-RHI delayed menu navigation through UMG delegates; no desktop input. #>
param(
    [string]$PackageRoot=(Join-Path (Split-Path -Parent $PSScriptRoot) 'Builds\NineStages\Windows'),
    [ValidateRange(640,3840)][int]$Width=1440,
    [ValidateRange(480,2160)][int]$Height=900,
    [ValidateSet('Offscreen','Windowed')][string]$RenderMode='Offscreen',
    [switch]$AllowFailure,
    [ValidateRange(30,900)][int]$TimeoutSeconds=600
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot)
$packageProject=Join-Path $PackageRoot 'LongNight'
$executable=Join-Path $packageProject 'Binaries\Win64\LongNight.exe'
if(-not(Test-Path -LiteralPath $executable -PathType Leaf)){throw "Actual packaged executable is missing: $executable"}
$runId=[DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')+'_'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$savedSuffix='CampaignScene_'+$runId
$savedRoot=Join-Path $packageProject ('Saved_'+$savedSuffix)
$sceneRoot=Join-Path $savedRoot 'SceneChecks'
$archive=Join-Path $projectRoot ('Saved\Validation\CampaignScene\'+$runId+'_'+$Width+'x'+$Height+'_'+$RenderMode)
New-Item -ItemType Directory -Path $archive | Out-Null
$log=Join-Path $archive 'process.log'
$started=[DateTime]::UtcNow
$beforeHash=(Get-FileHash -LiteralPath $executable).Hash.ToLowerInvariant()
$arguments=@('/Game/Maps/L_Campaign','-LNCampaignSceneChecks','-windowed',('-ResX='+$Width),('-ResY='+$Height),'-ForceRes','-NoVSync','-unattended','-nop4','-nosplash',('-UserDir="'+$packageProject+'"'),('-saveddirsuffix='+$savedSuffix),('-abslog="'+$log+'"'),'-ExecCmds="t.MaxFPS 60,t.IdleWhenNotForeground 0"')
if($RenderMode -eq 'Offscreen'){$arguments+='-RenderOffscreen'}
$manifest=[ordered]@{status='running';run_id=$runId;executable=$executable;executable_sha256=$beforeHash;started_utc=$started.ToString('o');width=$Width;height=$Height;render_mode=$RenderMode;arguments=$arguments;saved_root=$savedRoot;log=$log;scope='Real RHI and delayed UMG delegates after World.HasBegunPlay; no OS input; isolated automation save.';files=@()}
try{
    $process=Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $PackageRoot -WindowStyle Hidden -PassThru
    $manifest.process_id=$process.Id
    if(-not $process.WaitForExit($TimeoutSeconds*1000)){Stop-Process -Id $process.Id;throw "Owned scene-check process timed out: $log"}
    $process.WaitForExit();$manifest.exit_code=$process.ExitCode
    $jsonPath=Join-Path $sceneRoot 'scene-checks.json'
    $textPath=Join-Path $sceneRoot 'scene-checks.txt'
    foreach($path in @($jsonPath,$textPath)){
        if(-not(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).LastWriteTimeUtc -lt $started){throw "Missing/stale scene report: $path"}
    }
    $report=Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
    $manifest.checks=$report.checks;$manifest.failures=$report.failures;$manifest.snapshots=$report.snapshots.Count
    if($report.width -ne $Width -or $report.height -ne $Height){throw "Unexpected actual viewport: $($report.width)x$($report.height)"}
    if($report.snapshots.Count -ne 14){throw "Expected all fourteen delayed scene snapshots; got $($report.snapshots.Count)"}
    $paths=@($jsonPath,$textPath)
    foreach($snapshot in $report.snapshots){
        $screenshot=[IO.Path]::GetFullPath($snapshot.screenshot)
        if([IO.Path]::GetDirectoryName($screenshot) -ne $sceneRoot){throw 'Screenshot escaped this isolated test directory'}
        if(-not(Test-Path -LiteralPath $screenshot -PathType Leaf) -or (Get-Item -LiteralPath $screenshot).Length -lt 1000 -or (Get-Item -LiteralPath $screenshot).LastWriteTimeUtc -lt $started){throw "Missing/stale scene screenshot: $screenshot"}
        $paths+=$screenshot
    }
    foreach($path in $paths){
        $destination=Join-Path $archive ([IO.Path]::GetFileName($path));Copy-Item -LiteralPath $path -Destination $destination
        $manifest.files+=@{source=$path;archive=$destination;sha256=(Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant()}
    }
    $logText=Get-Content -LiteralPath $log -Raw
    $manifest.geometry_warning_count=@(Select-String -LiteralPath $log -Pattern 'Calling SetStaticMesh|Mobility.*Static|SetStaticMesh.*not allowed').Count
    if($process.ExitCode -notin @(0,1) -or $logText -match 'Fatal error:|Unhandled Exception|Assertion failed:'){throw "Scene-check process crashed; exit=$($process.ExitCode)"}
    if($logText -notmatch 'CAMPAIGN_SCENE_CHECKS.*saved=1'){throw 'No confirmed final scene report marker'}
    if((Get-FileHash -LiteralPath $executable).Hash.ToLowerInvariant() -ne $beforeHash){throw 'Executable changed during scene verification'}
    $manifest.status=if($report.status -eq 'pass' -and $report.failures -eq 0 -and $manifest.geometry_warning_count -eq 0){'pass'}else{'fail'}
    Write-Output ("SCENE "+$manifest.status+"; checks="+$report.checks+"; failures="+$report.failures+"; screenshots="+$report.snapshots.Count+"; archive="+$archive)
    if($manifest.status -ne 'pass' -and -not $AllowFailure){throw 'Scene checks failed; screenshots and raw results were retained'}
}catch{
    if($manifest.status -eq 'running'){$manifest.status='error'}
    $manifest.error=$_.Exception.Message
    throw
}finally{
    $manifest.finished_utc=[DateTime]::UtcNow.ToString('o')
    $manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $archive 'manifest.json') -Encoding utf8
}
