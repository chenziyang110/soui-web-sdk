[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
function Need($v,$m){if(-not$v){throw $m}}
function Digest([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
try{
 foreach($root in @('C:\soui-release06-compat-diag','C:\soui-release06-modern-diag')){Need (-not(Test-Path -LiteralPath $root)) 'Two final diagnostic roots must remain absent'}
 $cache='C:\release06-control\diag-media';$catalog=Join-Path $cache 'CARRIER.JSON';$zip=Join-Path $cache 'PAYLOAD.ZIP'
 Need ((Digest $catalog)-ceq'e52ba6a8bdcd0f5eed8e1620ba0f8a232c7907a5aa8611b295f3563990911eb8') 'Original cached diagnostic catalog required'
 $c=Get-Content -LiteralPath $catalog -Encoding UTF8 -Raw|ConvertFrom-Json
 Need ((Get-Item -LiteralPath $zip).Length-eq$c.zip.bytes-and(Digest $zip)-ceq$c.zip.sha256) 'Original cached diagnostic ZIP changed'
 $seven=Join-Path $PSScriptRoot '7z.exe';Need ((Digest $seven)-ceq'4cd7d776c686427226a151789d2d61f0b2ed2c392148cc4e69c0238362fafecf') 'Pinned official7z required'
 Need ((Digest (Join-Path $PSScriptRoot '7z.dll'))-ceq'5bd20fb38499d95c39594f41d4781b6181b3304b7f1f4d06b0182f514e7eaa74') 'Pinned companion7z.dll required'
 $node='C:\release06-tools\node\node.exe';Need ((Digest $node)-ceq'63c259c81e5d472b5f11c8d506070130cb04a1ecf84b80377a34ed6ec9048088') 'Original Node64 required'
 $stage='C:\z06-'+[Guid]::NewGuid().ToString('N').Substring(0,8);Need (-not(Test-Path -LiteralPath $stage)) 'Fresh short stage required'
 & $seven t '-sccUTF-8' $zip;if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
 & $seven x '-sccUTF-8' '-y' ('-o'+$stage) $zip;if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
 & $node (Join-Path $PSScriptRoot 'publish-diag.cjs') $stage $catalog
 $code=$LASTEXITCODE;if($null-eq$code){$code=1};exit $code
}catch{Write-Error -ErrorRecord $_ -ErrorAction Continue;exit 1}
