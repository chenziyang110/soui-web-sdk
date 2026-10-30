[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
try{
 foreach($p in @('C:\sr06','C:\release06-tools','C:\release06-control')){if(Test-Path -LiteralPath $p){throw 'Original target roots must remain absent'}}
 $stages=@(Get-ChildItem -LiteralPath 'C:\' -Directory -Filter 's06-stage-*' -Force)
 if($stages.Count-ne1-or($stages[0].Attributes-band[IO.FileAttributes]::ReparsePoint)){throw 'One plain unique retained stage required'}
 $node=Join-Path $stages[0].FullName 'release06-tools/node/node.exe'
 if((Get-FileHash -LiteralPath $node -Algorithm SHA256).Hash.ToLowerInvariant()-cne'63c259c81e5d472b5f11c8d506070130cb04a1ecf84b80377a34ed6ec9048088'){throw 'Original pinned Node64 required'}
 & $node (Join-Path $PSScriptRoot 'namefix.cjs') $stages[0].FullName (Join-Path $PSScriptRoot 'DEFAULT-CARRIER.JSON') (Join-Path $PSScriptRoot 'MAP.JSON')
 $repairExit=$LASTEXITCODE;if($repairExit-ne0){exit $repairExit}
 & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'DEFAULT-RESUME.PS1') -Stage $stages[0].FullName
 $originalExit=$LASTEXITCODE;if($null-eq$originalExit){$originalExit=1};exit $originalExit
}catch{Write-Error -ErrorRecord $_ -ErrorAction Continue;exit 1}
