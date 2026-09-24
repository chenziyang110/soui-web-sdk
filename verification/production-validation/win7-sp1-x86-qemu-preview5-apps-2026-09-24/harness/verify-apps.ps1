$ErrorActionPreference='Stop'
try {
  $target='C:\soui-app-validation-5'
  foreach($pair in @(@('SouiStarter',68),@('SouiWorkbench',473))){
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
  exit 0
} catch {
  Write-Output ('Verification failed: '+$_.ToString())
  exit 1
}