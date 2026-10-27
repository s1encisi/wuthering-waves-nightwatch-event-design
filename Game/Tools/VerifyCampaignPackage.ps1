<#
Run the actual Development package, never UnrealEditor or the root bootstrap EXE.
Each invocation uses a new package-local Saved suffix. Save mode shares that
suffix across its write/read processes; normal and review saves are untouched.
UI checks broadcast actual UMG delegates in an offscreen RHI viewport; they do
not represent OS mouse/keyboard testing. No Computer Use dependency exists.
#>
param(
    [string]$PackageRoot=(Join-Path (Split-Path -Parent $PSScriptRoot) 'Builds\NineStages\Windows'),
    [ValidateSet('State','Save','All','Stage','Variants','UI')][string]$Mode='State',
    [ValidateRange(0,8)][int]$Stage=0,
    [ValidateRange(0,1)][int]$Variant=0,
    [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$FormationPlan='',
    [ValidateRange(30,3600)][int]$TimeoutSeconds=900
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$PackageRoot=[IO.Path]::GetFullPath($PackageRoot)
$packageProject=Join-Path $PackageRoot 'LongNight'
$gameExe=Join-Path $packageProject 'Binaries\Win64\LongNight.exe'
if(-not (Test-Path -LiteralPath $gameExe -PathType Leaf)){throw "Actual packaged game is missing: $gameExe"}
if($FormationPlan -and $Mode -notin @('All','Stage','Variants')){throw 'FormationPlan applies only to gameplay verification modes'}
if($Mode -eq 'Stage' -and $Stage -lt 6 -and $Variant -ne 0){throw 'Only hazard stages have variant 1'}

$runId=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
$savedSuffix='CampaignVerify_'+$runId.Replace('-','')
$savedRoot=Join-Path $packageProject ('Saved_'+$savedSuffix)
$resultRoot=Join-Path $savedRoot 'Telemetry\Campaign'
$archive=Join-Path $projectRoot ('Saved\Validation\CampaignPackage\'+$runId+'-'+$Mode.ToLower())
if((Test-Path -LiteralPath $savedRoot) -or (Test-Path -LiteralPath $archive)){throw 'Refusing to reuse a verification directory'}
New-Item -ItemType Directory -Path $archive | Out-Null
$sourceSnapshot=Join-Path $archive 'source-snapshot'
New-Item -ItemType Directory -Path $sourceSnapshot | Out-Null
$sourceRecords=@()
foreach($name in @('Campaign.json','ValidationPlans.json')){
    $source=Join-Path $projectRoot ('Data\Campaign\'+$name)
    $destination=Join-Path $sourceSnapshot $name
    Copy-Item -LiteralPath $source -Destination $destination
    $sourceRecords+=@{source=$source;archive=$destination;sha256=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$campaign=Get-Content -LiteralPath (Join-Path $sourceSnapshot 'Campaign.json') -Raw | ConvertFrom-Json
$exeHash=(Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest=[ordered]@{
    schema=1;status='running';mode=$Mode;run_id=$runId;started_utc=[DateTime]::UtcNow.ToString('o')
    package_root=$PackageRoot;executable=$gameExe;executable_sha256=$exeHash
    saved_root=$savedRoot;source_snapshots=$sourceRecords;formation_plan=$FormationPlan
    scope='Actual packaged process; isolated automation save. UI mode checks UMG delegates, not physical input.'
    source_snapshot_scope='Current source configuration copied before launch; this hash alone does not prove the bytes inside cooked package containers.'
    checks=[System.Collections.Generic.List[object]]::new()
}

function Add-ArchivedFile([string]$Path,[string]$Destination,[System.Collections.Generic.List[object]]$Records){
    if(-not (Test-Path -LiteralPath $Path -PathType Leaf)){throw "Missing evidence: $Path"}
    $copy=Join-Path $Destination ([IO.Path]::GetFileName($Path))
    if(Test-Path -LiteralPath $copy){throw "Refusing to overwrite archived evidence: $copy"}
    Copy-Item -LiteralPath $Path -Destination $copy
    $sourceHash=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $copyHash=(Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash.ToLowerInvariant()
    if($sourceHash -ne $copyHash){throw "Evidence copy mismatch: $Path"}
    $Records.Add(@{source=$Path;archive=$copy;sha256=$copyHash;bytes=(Get-Item -LiteralPath $copy).Length})
}

$checks=@()
switch($Mode){
    'State' {$checks+=@{Name='state';Flags=@('-LNCampaignStateChecks');Files=@('state-checks.txt');Kind='text'}}
    'Save' {
        $checks+=@{Name='save-write';Flags=@('-LNCampaignSaveWrite');Files=@('save_write.txt');Kind='text'}
        $checks+=@{Name='save-read';Flags=@('-LNCampaignSaveRead');Files=@('save_read.txt');Kind='text'}
    }
    'All' {$checks+=@{Name='all';Flags=@('-LNCampaignAll');Files=@($campaign.stages | ForEach-Object {$_.id+'-all-v0.json'});Kind='game';Variant=0;ReportMode='all'}}
    'Stage' {
        $id=$campaign.stages[$Stage].id
        $checks+=@{Name=('stage-'+$Stage+'-v'+$Variant);Flags=@('-LNCampaignAuto',('-LNCampaignStage='+$Stage),('-LNVariant='+$Variant));Files=@($id+'-play-v'+$Variant+'.json');Kind='game';Variant=$Variant;ReportMode='play'}
    }
    'Variants' {
        foreach($index in 6..8){$checks+=@{Name=('variant-'+$index);Flags=@('-LNCampaignAuto',('-LNCampaignStage='+$index),'-LNVariant=1');Files=@($campaign.stages[$index].id+'-play-v1.json');Kind='game';Variant=1;ReportMode='play'}}
    }
    'UI' {$checks+=@{Name='ui';Flags=@('-LNCampaignUIChecks');Files=@('ui-binding-checks.txt');Kind='text'}}
}

try{
    foreach($check in $checks){
        $checkArchive=Join-Path $archive $check.Name
        New-Item -ItemType Directory -Path $checkArchive | Out-Null
        $log=Join-Path $checkArchive 'process.log'
        $arguments=@('/Game/Maps/L_Campaign')+$check.Flags+@(
            ('-UserDir="'+$packageProject+'"'),('-saveddirsuffix='+$savedSuffix),
            '-unattended','-nop4','-nosplash',('-abslog="'+$log+'"'))
        if($Mode -eq 'UI'){$arguments+=@('-RenderOffscreen','-windowed','-ResX=1440','-ResY=900','-ForceRes','-NoVSync')}
        else{$arguments+=@('-nullrhi','-nosound','-benchmark','-fps=60')}
        $expectedFiles=@($check.Files)
        if($FormationPlan){
            $arguments+=('-LNFormationPlan='+$FormationPlan)
            $expectedFiles=@($expectedFiles | ForEach-Object {$_.Replace('.json',('-'+$FormationPlan+'.json'))})
        }
        $started=[DateTime]::UtcNow
        $record=[ordered]@{name=$check.Name;status='running';started_utc=$started.ToString('o');arguments=$arguments;log=$log;files=[System.Collections.Generic.List[object]]::new()}
        $manifest.checks.Add($record)
        $process=Start-Process -FilePath $gameExe -ArgumentList $arguments -WorkingDirectory $PackageRoot -WindowStyle Hidden -PassThru
        $record.process_id=$process.Id
        if(-not $process.WaitForExit($TimeoutSeconds*1000)){
            Stop-Process -Id $process.Id -ErrorAction Stop
            throw "$($check.Name) timed out after $TimeoutSeconds seconds; stopped only process $($process.Id); log=$log"
        }
        $process.WaitForExit()
        $record.exit_code=$process.ExitCode
        $record.finished_utc=[DateTime]::UtcNow.ToString('o')
        if($process.ExitCode -ne 0){throw "$($check.Name) process exited $($process.ExitCode); log=$log"}
        if(-not (Test-Path -LiteralPath $log -PathType Leaf)){throw "Package produced no requested log: $log"}
        $logText=Get-Content -LiteralPath $log -Raw
        if($logText -match 'Calling SetStaticMesh[^\r\n]*Mobility is Static'){
            throw "$($check.Name) failed to create runtime scenery meshes; inspect $log"
        }
        if($logText -match '(?im)Fatal error:|Assertion failed:|Unhandled Exception|^.*\bLog[^:\r\n]+:\s*Error:'){
            throw "$($check.Name) log contains an engine error; inspect $log"
        }
        $eventPaths=[System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach($filename in $expectedFiles){
            $path=Join-Path $resultRoot $filename
            if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "No fresh package report: $path; log=$log"}
            $file=Get-Item -LiteralPath $path
            if($file.LastWriteTimeUtc -lt $started -or $file.Length -eq 0){throw "Stale or empty package report: $path"}
            Add-ArchivedFile $path $checkArchive $record.files
            $raw=Get-Content -LiteralPath $path -Raw
            if($check.Kind -eq 'text'){
                if($raw -notmatch '(?m)^FAILURES=0\s*$' -or $raw -match '(?m)^FAIL\b' -or $raw -notmatch '(?m)^PASS\s'){
                    throw "Failed package checks: $path"
                }
                if($Mode -eq 'State' -and $raw -notmatch 'CONFIGURATION_COVERAGE skills=52/52 synergies=52/52; checks=\d+ failures=0 missing=0'){
                    throw "Incomplete skill/synergy coverage: $path"
                }
                if($Mode -eq 'UI' -and $logText -notmatch 'CAMPAIGN_UI_CHECKS checks=[1-9]\d* failures=0 saved=1'){
                    throw "UI integration did not confirm a saved successful result: $log"
                }
            }else{
                $report=$raw | ConvertFrom-Json
                $stageDefinition=@($campaign.stages | Where-Object id -eq $report.stage)
                if($stageDefinition.Count -ne 1){throw "Unknown stage in report: $path"}
                $expectedName=$report.stage+'-'+$check.ReportMode+'-v'+$check.Variant
                if($FormationPlan){$expectedName+='-'+$FormationPlan}
                if($filename -ne $expectedName+'.json' -or $report.result -ne 'VICTORY' -or $report.mode -ne $check.ReportMode -or $report.variant -ne $check.Variant -or $report.formation_plan -ne $FormationPlan){throw "Wrong or unsuccessful gameplay report: $path"}
                $expectedEnemies=($stageDefinition[0].waves | ForEach-Object {$_.batches} | Measure-Object count -Sum).Sum
                if($report.waves.Count -ne $stageDefinition[0].waves.Count -or $report.spawned -ne $expectedEnemies -or ($report.killed+$report.leaked) -ne $report.spawned -or @($report.waves | Where-Object {-not $_.complete}).Count -gt 0){throw "Incomplete stage totals: $path"}
                if($report.enemy_health_scale -ne 1 -or $report.batch_time_offset -ne 0){throw "Unexpected numeric test override: $path"}
                if([string]::IsNullOrWhiteSpace($report.events)){throw "Gameplay report has no event path: $path"}
                $events=[IO.Path]::GetFullPath($report.events)
                if(-not [string]::Equals([IO.Path]::GetDirectoryName($events),$resultRoot,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($events) -notlike ('campaign-*-'+$process.Id+'.tsv')){
                    throw "Report references events outside this package process: $events"
                }
                [void]$eventPaths.Add($events)
            }
        }
        if($check.Kind -eq 'game' -and $eventPaths.Count -ne 1){throw 'Expected one event stream for each actual game process'}
        if($check.Kind -eq 'text'){
            foreach($events in @(Get-ChildItem -LiteralPath $resultRoot -File -Filter ('campaign-*-'+$process.Id+'.tsv'))){[void]$eventPaths.Add($events.FullName)}
        }
        foreach($events in $eventPaths){
            if(-not (Test-Path -LiteralPath $events -PathType Leaf) -or (Get-Item -LiteralPath $events).LastWriteTimeUtc -lt $started){throw "Missing/stale process event log: $events"}
            Add-ArchivedFile $events $checkArchive $record.files
        }
        $record.log_sha256=(Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash.ToLowerInvariant()
        $record.status='pass'
        Write-Output ($check.Name+': PASS; evidence='+$checkArchive)
    }
    if((Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $exeHash){throw 'Packaged executable changed during verification'}
    $manifest.status='pass'
}catch{
    $manifest.status='fail'
    $manifest.error=$_.Exception.Message
    if($record -and $record.status -eq 'running'){$record.status='fail';$record.error=$_.Exception.Message}
    throw
}finally{
    $manifest.finished_utc=[DateTime]::UtcNow.ToString('o')
    $manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath (Join-Path $archive 'manifest.json') -Encoding UTF8
    Write-Output ('Package verification manifest: '+(Join-Path $archive 'manifest.json'))
}
