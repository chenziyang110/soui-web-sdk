param([string]$Target='C:\soui-app-validation-8')
$ErrorActionPreference='Stop'
try {
  $target=$Target
  foreach($pair in @(@('SouiStarter',71),@('SouiWorkbench',475))){
    $path=Join-Path $target $pair[0]
    $count=@(Get-ChildItem -LiteralPath $path -Recurse | Where-Object { -not $_.PSIsContainer }).Count
    if($count -ne [int]$pair[1]){throw ($pair[0]+' count '+$count+' expected '+$pair[1])}
    Write-Output ($pair[0]+'='+$count)
  }
  $report=([string][char]0x62A5)+([string][char]0x544A)+'.txt'
  $config=([string][char]0x914D)+([string][char]0x7F6E)+'.ini'
  if(-not(Test-Path -LiteralPath (Join-Path $target ('SouiWorkbench\resources\fixtures\fileinsight-fixture\nested\'+$report)))){throw 'Chinese report fixture missing'}
  if(-not(Test-Path -LiteralPath (Join-Path $target ('SouiWorkbench\resources\fixtures\fileinsight-fixture-alt\nested\'+$config)))){throw 'Chinese config fixture missing'}
  Write-Output 'Chinese fixture names preserved'
  $expected=@{
    'THIRD_PARTY_NOTICES.md'='ADA599CB08DC27A0F7EC864902D95A1E3B765ACE80515F672A22701EE09CA662'
    'third_party\licenses\element-plus-2.11.1-LICENSE.txt'='0790118BB4D66681DB1D63181F72EF68E632D632F6DB0373EF87CF328561AF27'
    'third_party\licenses\ijg-jpeg-9a-README.txt'='C791DA525733040E622ED257C7B096B8F5191B332604EAD0C6BC2B54CBD8E0D1'
    'third_party\licenses\libcurl-7.61.1-COPYING.txt'='5F3849EC38DDB927E79F514BF948890C41B8D1407286A49609B8FB1585931095'
    'third_party\licenses\ots-5.0.0-LICENSE.txt'='984523EE987F4E8B72D61DF37D8F1189A7077CD4B77E41A397E35593B297A29D'
  }
  foreach($app in @('SouiStarter','SouiWorkbench')){
    foreach($name in $expected.Keys){
      $file=Join-Path (Join-Path $target $app) (Join-Path 'resources\legal' $name)
      $bytes=[IO.File]::ReadAllBytes($file)
      $hasher=[Security.Cryptography.SHA256]::Create()
      try {$actual=[BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','')}
      finally {$hasher.Clear()}
      if($actual -cne $expected[$name]){throw ($app+' legal file mismatch: '+$name)}
    }
  }
  Write-Output 'Legal files=5x2,hashes=matched'
  exit 0
} catch {
  Write-Output ('Verification failed: '+$_.ToString())
  exit 1
}
