[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$MediaRoot,[Parameter(Mandatory=$true)][string]$ManifestSha256,
 [Parameter(Mandatory=$true)][string]$ExpectedMachine,[string]$DriveBacking='C:\SOUI_Issue77_Drive',[string]$TrayDirectory='C:\issues8')
$ErrorActionPreference='Stop'
$media=[IO.Path]::GetFullPath($MediaRoot)
$manifest=Join-Path $media 'MessagePort.manifest.json'
if((Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()-cne$ManifestSha256){throw 'Exact reviewed carrier manifest required'}
$cfg=[IO.File]::ReadAllText($manifest,[Text.Encoding]::UTF8)|ConvertFrom-Json
if($env:COMPUTERNAME-ieq$cfg.preparationHost-or$env:COMPUTERNAME-ine$ExpectedMachine){throw 'Copy is authorized only inside the identified task VM'}
if([IO.Path]::GetFullPath($DriveBacking)-notmatch'^C:\\SOUI_Issue77_Drive(?:-[a-f0-9]{8,32})?$'-or[IO.Path]::GetFullPath($TrayDirectory)-notmatch'^C:\\issues8(?:-[a-f0-9]{8,32})?$'){throw 'Use fresh isolated task roots with the reviewed prefix'}
if((Get-PSDrive -Name F -ErrorAction SilentlyContinue)-or(Test-Path -LiteralPath $DriveBacking)-or(Test-Path -LiteralPath $TrayDirectory)){throw 'F: and both task directories must be absent; no overwrite or removal'}
$output='C:\issue-closeout-evidence\mp77-copy-'+[Guid]::NewGuid().ToString('N')
New-Item -ItemType Directory -Path $output|Out-Null
$copy=[IO.Path]::Combine($env:SystemRoot,'System32','robocopy.exe')
foreach($item in @(@{name='F';source=(Join-Path $media 'F');target=$DriveBacking},@{name='issues8';source=(Join-Path $media 'issues8');target=$TrayDirectory})){
 & $copy $item.source $item.target /E /COPY:DAT /DCOPY:T /R:0 /W:0 /XJ 1>(Join-Path $output ($item.name+'.stdout.log')) 2>(Join-Path $output ($item.name+'.stderr.log'))
 $code=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $output ($item.name+'.actual.json')),([ordered]@{source=$item.source;target=$item.target;exitCode=$code;freshCopy=$true;retryCount=0;waitSeconds=0;sourceVersion='0.5.1-preview.1';productExecuted=$false;releaseReady=$false}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
 if($code-ge8){throw 'Native fresh exact task copy failed; preserve partial copy and logs'}
}
& ([IO.Path]::Combine($env:SystemRoot,'System32','subst.exe')) 'F:' $DriveBacking
if($LASTEXITCODE-ne0){throw 'Explicit guest-only F: mapping failed'}
$copied=Join-Path $cfg.guestControlRoot 'Carrier.manifest.json'
if((Get-FileHash -LiteralPath $copied -Algorithm SHA256).Hash.ToLowerInvariant()-cne$ManifestSha256){throw 'Copied carrier manifest differs'}
[IO.File]::WriteAllText((Join-Path $output 'Copy.actual.json'),([ordered]@{manifestSha256=$ManifestSha256;machine=$env:COMPUTERNAME;drive='F:';backing=$DriveBacking;trayDirectory=$TrayDirectory;mappingCreated=$true;bytePreflightStillRequired=$true;productExecuted=$false;releaseReady=$false}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
Write-Output ('Copied task data; next run metadata preflight from '+$cfg.guestControlRoot)
Write-Output ('Manifest='+$copied+' SHA256='+$ManifestSha256)
