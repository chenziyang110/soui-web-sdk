$ErrorActionPreference='Stop'
$repo='C:\SOUI-SDK-development\preview9-tray'
$evidence=$PSScriptRoot
Set-Location -LiteralPath $repo
function Run-Check([string]$name,[string[]]$arguments) {
  $log=Join-Path $evidence ($name+'.log')
  $result=Join-Path $evidence ($name+'.result.json')
  if((Test-Path -LiteralPath $log) -or (Test-Path -LiteralPath $result)){throw "Refuse overwrite $name"}
  $start=[DateTime]::UtcNow.ToString('o')
  & node @arguments *> $log
  $code=$LASTEXITCODE
  $record=[ordered]@{name=$name;program='node';arguments=$arguments;startedAt=$start;finishedAt=[DateTime]::UtcNow.ToString('o');exitCode=$code;rawFile=($name+'.log')}
  [IO.File]::WriteAllText($result,($record|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
  Write-Host "$name exit=$code"
  if($code -ne 0){throw "$name failed; raw sample preserved"}
  return $record
}
$null=Run-Check 'web-tests' @('--test','--test-reporter=tap','tests/web/sdk-client.test.mjs','tests/web/native-resolvers.test.mjs','tests/web/session-renewal.test.mjs')
$packageTests=@()
$packageTests+=Run-Check 'sdk-package-smoke' @('tests/sdk/sdk-package-smoke.cjs')
$packageTests+=Run-Check 'delivery-identity' @('--test','--test-reporter=tap','tests/sdk/delivery-identity.test.cjs')
[IO.File]::WriteAllText((Join-Path $evidence 'packaging-tests.result.json'),($packageTests|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
$package=Join-Path $evidence 'sdk-package-attempt1'
$null=Run-Check 'sdk-pack-attempt1' @('tools/sdk/soui-sdk.cjs','pack','--source',$repo,'--output',$package,'--version','0.3.0-preview.11')
$null=Run-Check 'sdk-verify' @('tools/sdk/soui-sdk.cjs','verify','--package',$package)
$null=Run-Check 'sdk-verify-source' @('tools/sdk/soui-sdk.cjs','verify-source','--package',$package,'--source',$repo)
