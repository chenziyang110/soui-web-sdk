# Host-side helper test only. Never executes the guest harness body or applications.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$Transfer=Join-Path $root 'volume\soui-input'
$Results=Join-Path $root 'host-transfer-diagnostics'
if(Test-Path -LiteralPath $Results){throw 'Refuse to overwrite prior host diagnostic helper test'}
[IO.Directory]::CreateDirectory($Results) | Out-Null
$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'harness\run-apps.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Guest harness syntax invalid'}
foreach($name in @('Write-New','Get-Sha256','To-Base64','Write-TransferDiagnostics')) {
  $matches=@($ast.FindAll({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object {$_.Name -eq $name})
  if($matches.Count -ne 1){throw ('Ambiguous diagnostic helper: '+$name)}
  . ([ScriptBlock]::Create($matches[0].Extent.Text))
}
$transferLines=@(Get-Content -LiteralPath (Join-Path $Transfer 'transfer-files.tsv') -Encoding UTF8 | Where-Object {$_ -ne ''})
$files=@(Get-ChildItem -LiteralPath $Transfer -Force)
Write-TransferDiagnostics $files $transferLines
if(@($files | Where-Object {$_.PSIsContainer}).Count -ne 0 -or $files.Count -ne $transferLines.Count+1){throw 'Host transfer file set differs'}
$original=[IO.File]::ReadAllBytes((Join-Path $Transfer 'transfer-files.tsv'))
$copied=[IO.File]::ReadAllBytes((Join-Path $Results 'transfer-manifest-raw.tsv'))
if([Convert]::ToBase64String($original) -cne [Convert]::ToBase64String($copied)){throw 'Diagnostic raw manifest changed'}
if(@(Get-ChildItem -LiteralPath $Results -File).Count -ne 4){throw 'Diagnostic output set differs'}
$guard='if(@($files | Where-Object {$_.PSIsContainer}).Count -ne 0 -or $files.Count -ne $transferLines.Count+1){throw ''Transfer file set differs''}'
if(-not ([IO.File]::ReadAllText((Join-Path $root 'harness\run-apps.ps1')).Contains($guard))){throw 'Original guard differs'}
$baseFiles=$files
foreach($extra in @((Get-Item -LiteralPath $Results),(Get-Item -LiteralPath (Join-Path $Results 'transfer-manifest-raw.tsv')))) {
  $files=@($baseFiles)+@($extra)
  $rejected=$false
  try {. ([ScriptBlock]::Create($guard))} catch {if($_.Exception.Message -ne 'Transfer file set differs'){throw};$rejected=$true}
  if(-not $rejected){throw 'Original guard accepted extra entry'}
}
Write-Output 'host-diagnostic-helper-self-test-passed;extraDirectoryRejected=true;extraFileRejected=true;guestExecuted=false;applicationsStarted=false'
