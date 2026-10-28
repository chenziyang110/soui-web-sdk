[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
try{
 $node='C:\release06-tools\node\node.exe'
 if((Get-FileHash -LiteralPath $node -Algorithm SHA256).Hash.ToLowerInvariant()-cne'63c259c81e5d472b5f11c8d506070130cb04a1ecf84b80377a34ed6ec9048088'){throw 'Pinned existing Node64 required'}
 & $node (Join-Path $PSScriptRoot 'install-deps.cjs') (Join-Path $PSScriptRoot 'PLAN.JSON')
 $code=$LASTEXITCODE;if($null-eq$code){$code=1};exit $code
}catch{Write-Error -ErrorRecord $_ -ErrorAction Continue;exit 1}
