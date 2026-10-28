[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64',
  [ValidateSet('compatibility','modern','auto')][string]$Profile='compatibility',
  [string]$SizeBaselineDirectory='',
  [string]$SizeBudgetPath='',
  [switch]$Bundle,
  [ValidateSet('zip','7z-balanced','7z-small')][string]$CompressionProfile='zip')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version=([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
$evidence=Join-Path $root 'tools/internal/build-evidence.cjs'
$sizeTool=Join-Path $root 'tools/internal/size-report.cjs'
if($SizeBaselineDirectory -and -not(Test-Path -LiteralPath $SizeBaselineDirectory -PathType Container)){throw 'Size baseline directory does not exist.'}
if($SizeBudgetPath -and -not(Test-Path -LiteralPath $SizeBudgetPath -PathType Leaf)){throw 'Size budget file does not exist.'}
$zip=Get-Command 7z.exe -ErrorAction SilentlyContinue
$zipPath=if($zip){$zip.Source}else{Join-Path $env:ProgramFiles '7-Zip/7z.exe'}
if(-not(Test-Path -LiteralPath $zipPath)){throw 'Install 7-Zip or put 7z.exe on PATH.'}
$compressionTool=Join-Path $root 'tools/internal/compression-profiles.cjs'
$compressionText=& node $compressionTool describe --profile $CompressionProfile --tool $zipPath
if($LASTEXITCODE -ne 0){throw 'Archive compression profile or tool identity validation failed.'}
$compression=$compressionText | ConvertFrom-Json
function Get-ArchiveCompressionMetadata([string[]]$ExactArguments){
 $observedText=& node $compressionTool describe --profile $CompressionProfile --tool $zipPath
 if($LASTEXITCODE -ne 0){throw 'Archive tool identity recheck failed.'}
 $observed=$observedText | ConvertFrom-Json
 if($observed.toolIdentity.sha256 -ne $compression.toolIdentity.sha256 -or
    $observed.toolIdentity.bytes -ne $compression.toolIdentity.bytes -or
    $observed.toolIdentity.version -ne $compression.toolIdentity.version -or
    (ConvertTo-Json -InputObject @($observed.toolIdentity.runtimeFiles) -Compress -Depth 8) -cne
      (ConvertTo-Json -InputObject @($compression.toolIdentity.runtimeFiles) -Compress -Depth 8)){throw 'Archive tool changed during packaging.'}
 return [ordered]@{compression=$compression.compression;profile=$compression.profile;format=$compression.format;
  requestedDictionaryBytes=$compression.requestedDictionaryBytes;solid=$compression.solid;
  exactArguments=@($ExactArguments);toolIdentity=$compression.toolIdentity}
}
New-Item -ItemType Directory -Force -Path (Join-Path $root 'out/package'),(Join-Path $root 'dist') | Out-Null
$targets=if($Profile-eq'auto'){@('SouiAutoStarter')}elseif($Profile-eq'modern'){@('SouiModernStarter')}else{@('SouiStarter','SouiWorkbench')}
$bundleApplications=[Collections.Generic.List[object]]::new()
$bundleArchive=Join-Path $root "dist/SouiApplications-$version-windows-$Architecture-$Profile.bundle$($compression.suffix)$($compression.extension)"
if($Bundle -and (Test-Path -LiteralPath $bundleArchive)){throw "Archive already exists: $bundleArchive"}
foreach($target in $targets){
 $archive=Join-Path $root "dist/$target-$version-windows-$Architecture$($compression.suffix)$($compression.extension)"
 if(-not $Bundle -and (Test-Path -LiteralPath $archive)){throw "Archive already exists: $archive"}
 $stage=Join-Path $root ('out/package/'+$target+'-'+[Guid]::NewGuid().ToString('N'))
 $reportText=& node $evidence stage-app $Architecture Release $target $stage
 if($LASTEXITCODE -ne 0){throw 'Application evidence or staging failed; rebuild and rerun native tests.'}
 $report=$reportText | ConvertFrom-Json
 $sizeInput=$stage+'.size-input.json'
 [IO.File]::WriteAllText($sizeInput,($report|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
 $sizeArgs=@('--report',$sizeInput)
 if($SizeBaselineDirectory){
  $baseline=Join-Path $SizeBaselineDirectory ($target+'.json')
  if(-not(Test-Path -LiteralPath $baseline -PathType Leaf)){throw "Size baseline missing: $baseline"}
  $sizeArgs+=@('--baseline',$baseline)
 }
 if($SizeBudgetPath){$sizeArgs+=@('--budget',$SizeBudgetPath)}
 $sizeText=& node $sizeTool @sizeArgs
 $sizeExit=$LASTEXITCODE
 if($sizeText){[IO.File]::WriteAllText(($stage+'.size-analysis.json'),($sizeText -join [Environment]::NewLine),[Text.UTF8Encoding]::new($false))}
 if($sizeExit -ne 0){throw "Application size analysis failed or budget exceeded; see $stage.size-analysis.json"}
 $report | Add-Member -Force -NotePropertyName sizeAnalysis -NotePropertyValue ($sizeText | ConvertFrom-Json)
 if($Bundle){
  [IO.File]::WriteAllText($sizeInput,($report|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
  $bundleApplications.Add([ordered]@{name=$target;root=$stage;reportFile=$sizeInput})
  continue
 }
 Push-Location $stage
 $archiveArguments=@('a')+@($compression.arguments)+@($archive,'*')
 try{
  & $zipPath @archiveArguments *> (Join-Path $root "out/package/$target-$Architecture.log")
  if($LASTEXITCODE -ne 0){throw 'Application archive failed'}
  & $zipPath t $archive *> (Join-Path $root "out/package/$target-$Architecture-test.log")
  if($LASTEXITCODE -ne 0){throw 'Application archive integrity check failed'}
 }finally{Pop-Location}
 $compressionMetadata=Get-ArchiveCompressionMetadata -ExactArguments $archiveArguments
 $hash=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
 [IO.File]::WriteAllText(($archive+'.sha256'),$hash+'  '+[IO.Path]::GetFileName($archive)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
 $archiveReport=[ordered]@{file=[IO.Path]::GetFileName($archive);bytes=(Get-Item -LiteralPath $archive).Length;sha256=$hash;createdAt=(Get-Date).ToString('o')}
 foreach($entry in $compressionMetadata.GetEnumerator()){$archiveReport[$entry.Key]=$entry.Value}
 $report | Add-Member -NotePropertyName archive -NotePropertyValue $archiveReport
 [IO.File]::WriteAllText(($archive+'.json'),($report|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
 Write-Output "Application archive verified: $archive (uncompressed $($report.payload.bytes) bytes)"
}
if($Bundle){
 $bundleStage=Join-Path $root ('out/package/bundle-'+[Guid]::NewGuid().ToString('N'))
 $bundleInput=$bundleStage+'.applications.json'
 [IO.File]::WriteAllText($bundleInput,(ConvertTo-Json -InputObject @($bundleApplications.ToArray()) -Depth 30),[Text.UTF8Encoding]::new($false))
 $bundleTool=Join-Path $root 'tools/internal/runtime-bundle.cjs'
 & node $bundleTool pack --output $bundleStage --applications $bundleInput
 if($LASTEXITCODE -ne 0){throw 'Runtime bundle staging failed'}
 & node $bundleTool verify --bundle $bundleStage
 if($LASTEXITCODE -ne 0){throw 'Runtime bundle verification failed'}
 Push-Location $bundleStage
 $bundleArchiveArguments=@('a')+@($compression.arguments)+@($bundleArchive,'*')
 try{
  & $zipPath @bundleArchiveArguments *> ($bundleStage+'.archive.log')
  if($LASTEXITCODE -ne 0){throw 'Runtime bundle archive failed'}
  & $zipPath t $bundleArchive *> ($bundleStage+'.archive-test.log')
  if($LASTEXITCODE -ne 0){throw 'Runtime bundle archive integrity check failed'}
 }finally{Pop-Location}
 $bundleCompressionMetadata=Get-ArchiveCompressionMetadata -ExactArguments $bundleArchiveArguments
 $bundleHash=(Get-FileHash -LiteralPath $bundleArchive -Algorithm SHA256).Hash.ToLowerInvariant()
 [IO.File]::WriteAllText(($bundleArchive+'.sha256'),$bundleHash+'  '+[IO.Path]::GetFileName($bundleArchive)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
 $bundleManifest=Get-Content -Raw -LiteralPath (Join-Path $bundleStage 'manifest.json') | ConvertFrom-Json
 $bundleArchiveReport=[ordered]@{file=[IO.Path]::GetFileName($bundleArchive);bytes=(Get-Item -LiteralPath $bundleArchive).Length;sha256=$bundleHash;createdAt=(Get-Date).ToString('o')}
 foreach($entry in $bundleCompressionMetadata.GetEnumerator()){$bundleArchiveReport[$entry.Key]=$entry.Value}
 $bundleReport=[ordered]@{schema='soui.application-bundle-size/v1';version=$version;architecture=$Architecture;profile=$Profile;
  totals=$bundleManifest.totals;releaseReady=$false;
  archive=$bundleArchiveReport}
 [IO.File]::WriteAllText(($bundleArchive+'.json'),($bundleReport|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
 Write-Output "Runtime bundle verified: $bundleArchive"
}
