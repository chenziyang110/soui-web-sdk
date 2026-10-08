$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$results=@()
foreach($relative in @('Run-V10.ps1','Launch-V10.ps1','win7-v10-source/run-visibility-diagnostic.ps1')) {
  $file=Join-Path $root $relative
  $tokens=$null
  $errors=$null
  $ast=[System.Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
  $actionCalls=@($ast.FindAll({param($node)
    $node -is [System.Management.Automation.Language.CommandAst] -and
      $node.GetCommandName() -in @('Run-Action','Wait-AppDelay')
  },$true))
  if($errors.Count -ne 0 -or $actionCalls.Count -ne 0){throw "V10 parser or action-call check failed: $relative"}
  $results+=[ordered]@{
    file=$relative
    sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    parserErrors=$errors.Count
    nativeActionCalls=$actionCalls.Count
  }
}
$record=[ordered]@{schema='soui.win7-v02-v10-parser/v1';checkedAt=[DateTime]::UtcNow.ToString('o');hostPowerShellVersion=$PSVersionTable.PSVersion.ToString();files=$results;guestExecuted=$false;releaseReady=$false}
$destination=Join-Path $root 'v10-powershell-parser.json'
$bytes=[Text.Encoding]::UTF8.GetBytes(($record|ConvertTo-Json -Depth 5)+"`n")
$stream=New-Object IO.FileStream($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
$record|ConvertTo-Json -Depth 5 -Compress
