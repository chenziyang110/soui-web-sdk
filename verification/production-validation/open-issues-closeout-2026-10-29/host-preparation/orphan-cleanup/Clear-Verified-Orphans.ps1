$ErrorActionPreference='Stop'
$targets=@(103652,100872,56540,81964)
$active=@(Get-Process -Name cl,link,lib,cmake,ninja,clang-cl -ErrorAction SilentlyContinue)
if($active.Count){throw 'Active compilation prevents orphan cleanup'}
$rows=@();$held=@()
foreach($targetId in $targets){
 $p=Get-Process -Id $targetId -ErrorAction Stop
 [void]$p.Handle
 $c=Get-CimInstance Win32_Process -Filter ('ProcessId='+$targetId)
 if($p.ProcessName-notin@('MSBuild','vctip')){throw 'Unexpected helper kind'}
 $expected=if($p.ProcessName-eq'MSBuild'){'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'}else{'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\HostX64\x86\VCTIP.EXE'}
 if($p.Path-ine$expected){throw 'Helper image path differs'}
 if($p.ProcessName-eq'MSBuild'-and$c.CommandLine-notlike'*/nodemode:1*'){throw 'Not a reusable auxiliary node'}
 if($p.StartTime.ToString('yyyy-MM-dd HH:mm')-ne$(if($p.ProcessName-eq'vctip'){'2026-10-29 14:23'}else{'2026-10-29 14:54'})){throw 'Birth time differs from observed batch'}
 $parent=Get-Process -Id $c.ParentProcessId -ErrorAction SilentlyContinue
 if($parent-and$parent.StartTime-le$p.StartTime){throw 'Original parent is still present'}
 $rows+=@{pid=$targetId;name=$p.ProcessName;path=$p.Path;birthFileTime=$p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString();parentId=$c.ParentProcessId;originalParentExited=$true;cpuBefore=$p.TotalProcessorTime.Ticks}
 $held+=,$p
}
Start-Sleep -Milliseconds 1500
if(@(Get-Process -Name cl,link,lib,cmake,ninja,clang-cl -ErrorAction SilentlyContinue).Count){throw 'Compilation began; cleanup refused'}
$current=@(Get-Process -Name MSBuild,vctip -ErrorAction SilentlyContinue)
if(@($current|Where-Object{$_.Id-notin$targets}).Count){throw 'Unobserved helpers appeared; cleanup refused'}
for($i=0;$i-lt$held.Count;$i++){
 $p=$held[$i];$p.Refresh();$row=$rows[$i]
 if($p.HasExited-or$p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()-cne$row.birthFileTime-or$p.Path-ine$row.path){throw 'Held identity changed'}
 $row.cpuAfter=$p.TotalProcessorTime.Ticks
 if($row.cpuAfter-ne$row.cpuBefore){throw 'Helper is not idle'}
 $parent=Get-Process -Id $row.parentId -ErrorAction SilentlyContinue
 if($parent-and$parent.StartTime-le$p.StartTime){throw 'Original parent reappeared'}
}
$directory=Join-Path $PSScriptRoot ('orphan-cleanup-'+[Guid]::NewGuid().ToString());[void](New-Item -ItemType Directory -Path $directory)
[IO.File]::WriteAllText((Join-Path $directory 'before.json'),($rows|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
foreach($p in $held){$p.Kill();if(-not$p.WaitForExit(5000)){throw 'Held helper did not exit'};$p.Dispose()}
[IO.File]::WriteAllText((Join-Path $directory 'result.json'),(@{authorizedRule='Prior user authorization: no active build, original parent exited, stable birth/image and unchanged CPU';separateFromOwnedShortCase=$true;cleanedPids=$targets;forcedNaturalClaim=$false}|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
Write-Output $directory
