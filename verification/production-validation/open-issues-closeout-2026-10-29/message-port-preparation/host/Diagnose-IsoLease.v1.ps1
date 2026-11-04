param([Parameter(Mandatory=$true)][string]$Plan)
$ErrorActionPreference='Stop'
$helper='F:/work/SOUI_WEB/out/release-v0.6.0-20261028/vm-short-prep/media-tooldeps-4/BoundedIsoIO.cs'
if((Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash.ToLowerInvariant()-cne'fbedad4f3f1dd8357df8615f15917572e191681c676eba56d2b23507bf24dc72'){throw 'Original helper differs'}
Add-Type -TypeDefinition ([IO.File]::ReadAllText($helper)) -Language CSharp
$p=[IO.File]::ReadAllText($Plan)|ConvertFrom-Json
foreach($name in @($p.sourceRoot)+@($p.directories|ForEach-Object{Join-Path $p.sourceRoot $_})){
 $native=[IO.Path]::GetFullPath($name);$lease=$null
 try{$lease=[SouiDataIso4ddce37c.ReadLease]::new($native,$true)}catch{
  $chain=@();for($error=$_.Exception;$null-ne$error;$error=$error.InnerException){$chain+=@([ordered]@{type=$error.GetType().FullName;message=$error.Message;hresult=$error.HResult;nativeError=if($error-is[ComponentModel.Win32Exception]){$error.NativeErrorCode}else{$null}})}
  [ordered]@{path=$native;pathLength=$native.Length;directory=$true;errors=$chain}|ConvertTo-Json -Depth 8 -Compress
  exit 0
 }finally{if($lease){$lease.Dispose()}}
}
throw 'Directory leases opened separately; inspect simultaneous holds or file rows next'
