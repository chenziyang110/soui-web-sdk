[CmdletBinding()]
param([ValidateSet('x86','x64')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$version=([IO.File]::ReadAllText((Join-Path $root 'VERSION'))).Trim()
$report=Get-Content -Raw -LiteralPath (Join-Path $root "out/$Architecture/smoke-Release.json") | ConvertFrom-Json
if(-not $report.passed){throw 'Release native smoke must pass before application packaging.'}
$bin=Join-Path $root "out/$Architecture/bin/Release"
foreach($run in $report.runs){
 $app=Join-Path $bin $run.target
 if((Get-FileHash -LiteralPath (Join-Path $app "$($run.target).exe")).Hash -ne $run.executableSha256 -or
    (Get-FileHash -LiteralPath (Join-Path $app 'node.dll')).Hash -ne $run.kernelSha256){throw 'Application changed since native verification.'}
}
$archive=Join-Path $root "dist/soui-sdk-$version-windows-$Architecture.zip"
if(Test-Path -LiteralPath $archive){throw 'Archive already exists.'}
$zip=Get-Command 7z.exe -ErrorAction SilentlyContinue
$zipPath=if($zip){$zip.Source}else{Join-Path $env:ProgramFiles '7-Zip/7z.exe'}
if(-not(Test-Path -LiteralPath $zipPath)){throw 'Install 7-Zip or put 7z.exe on PATH.'}
New-Item -ItemType Directory -Force -Path (Join-Path $root 'out/package'),(Join-Path $root 'dist') | Out-Null
Push-Location $bin
try{
 & $zipPath a -tzip -mx=5 -mmt=2 $archive SouiStarter SouiWorkbench *> (Join-Path $root "out/package/apps-$Architecture.log")
 if($LASTEXITCODE -ne 0){throw 'Application archive failed'}
 & $zipPath t $archive *> (Join-Path $root "out/package/apps-$Architecture-test.log")
 if($LASTEXITCODE -ne 0){throw 'Application archive integrity check failed'}
}finally{Pop-Location}
$hash=(Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archive+'.sha256'),$hash+'  '+[IO.Path]::GetFileName($archive)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
Write-Output "Application archive verified: $archive"
