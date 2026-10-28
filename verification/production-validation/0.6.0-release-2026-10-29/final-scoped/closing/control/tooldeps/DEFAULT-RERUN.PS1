[CmdletBinding()]
param()
$ErrorActionPreference='Stop';$controllerExit=1;$exportExit=$null;$evidence=$null
try{
 foreach($p in @('C:\sr06','C:\release06-tools','C:\release06-control')){if(-not(Test-Path -LiteralPath $p -PathType Container)){throw 'Original ordinary roots must be published'}}
 $node='C:\release06-tools\node\node.exe';$controller='C:\release06-control\Guest-Owned-Smokes.cjs'
 if((Get-FileHash -LiteralPath $node -Algorithm SHA256).Hash.ToLowerInvariant()-cne'63c259c81e5d472b5f11c8d506070130cb04a1ecf84b80377a34ed6ec9048088'){throw 'Original pinned Node64 required'}
 if((Get-FileHash -LiteralPath $controller -Algorithm SHA256).Hash.ToLowerInvariant()-cne'1d96606d39ef755482bd1a1b23dbdaf2b3297811bac8d3405aa7487338ff4c61'){throw 'Original controller changed'}
 $env:PATH='C:\release06-tools\node;'+$env:PATH
 $evidence='C:\release06-control\evidence\owned-'+[Guid]::NewGuid().ToString('N')
 Write-Output ('EVIDENCE='+$evidence)
 & $node $controller 'C:\sr06' $evidence
 $controllerExit=$LASTEXITCODE;if($null-eq$controllerExit){$controllerExit=1}
}catch{Write-Error -ErrorRecord $_ -ErrorAction Continue;$controllerExit=1}
finally{
 if($evidence-and(Test-Path -LiteralPath (Join-Path $evidence 'ExportFiles.actual.json'))){
  try{& "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'EXPORT.PS1') -EvidenceDirectory $evidence;$exportExit=$LASTEXITCODE}
  catch{$exportExit=1;Write-Error -ErrorRecord $_ -ErrorAction Continue}
 }
 Write-Output ('CONTROLLER_EXIT='+$controllerExit+' EXPORT_EXIT='+$exportExit+' EVIDENCE='+$evidence)
}
exit $controllerExit
