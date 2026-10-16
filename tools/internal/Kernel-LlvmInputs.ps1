# Fixed data-only checks for the public Miniblink LLVM lane. No native launch.
function Assert-SouiKernelPathText {
  param([Parameter(Mandatory)][string]$Path)
  # These paths also become cmd arguments and MSBuild properties. In particular,
  # %/! expansion and $()/@() expressions must not cross this boundary.
  if($Path -notmatch '^[A-Za-z]:[\\/]' -or $Path.Substring(2) -match '[;:&|<>^%!"''$@\x00-\x1f*?\x60]'){
    throw 'Kernel LLVM paths require absolute local paths without cmd or MSBuild metacharacters.'
  }
  $full=[IO.Path]::GetFullPath($Path).Replace('\','/')
  if($full.Length -gt 3){$full=$full.TrimEnd('/')}
  return $full
}
function Test-SouiKernelWithin {
  param([string]$Path,[string]$Root)
  $a=$Path.Replace('\','/').TrimEnd('/')
  $b=$Root.Replace('\','/').TrimEnd('/')
  return $a.Equals($b,[StringComparison]::OrdinalIgnoreCase) -or $a.StartsWith($b+'/',[StringComparison]::OrdinalIgnoreCase)
}
function Resolve-SouiKernelPath {
  param([string]$Path,[ValidateSet('Directory','File')][string]$Kind)
  $safe=Assert-SouiKernelPathText $Path
  return Resolve-SouiLlvmPath -Path $safe -Kind $Kind
}
function Assert-SouiKernelFreshOutput {
  param([string]$Output,[string[]]$ProtectedRoots)
  $safe=Assert-SouiKernelPathText $Output
  if(Test-Path -LiteralPath $safe){throw 'LLVM OutputRoot must be absent; do not reuse even an empty build directory.'}
  foreach($root in $ProtectedRoots){
    if((Test-SouiKernelWithin $safe $root) -or (Test-SouiKernelWithin $root $safe)){
      throw 'LLVM OutputRoot must be disjoint from all source and selected tool roots.'
    }
  }
  $parent=[IO.Path]::GetDirectoryName($safe)
  while(-not(Test-Path -LiteralPath $parent)){
    $parent=[IO.Path]::GetDirectoryName($parent)
    if(-not $parent){throw 'LLVM OutputRoot has no existing local parent.'}
  }
  [void](Resolve-SouiKernelPath $parent Directory)
  return $safe
}
function Get-SouiKernelPin {
  param([string]$Path)
  $file=Get-Item -LiteralPath $Path -Force -ErrorAction Stop
  if($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Expected a regular input file.'}
  return [ordered]@{path=$file.FullName.Replace('\','/');bytes=[long]$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
}
function Assert-SouiKernelPins {
  param([object[]]$Pins)
  foreach($pin in $Pins){
    $current=Get-SouiKernelPin $pin.path
    if($current.bytes -ne $pin.bytes -or $current.sha256 -cne $pin.sha256){throw "Kernel LLVM input changed: $($pin.path)"}
    [void](Resolve-SouiLlvmPath -Path $pin.path -Kind File)
  }
}
function Get-SouiKernelVsRoles {
  param([string]$VsInstallDir,[string]$MsvcRoot,[ValidateSet('x86','x64')][string]$Architecture)
  $vs=Resolve-SouiKernelPath $VsInstallDir Directory
  $msvc=Resolve-SouiKernelPath $MsvcRoot Directory
  $expected=Resolve-SouiKernelPath ($vs+'/VC/Tools/MSVC/14.29.30133') Directory
  if(-not $msvc.Equals($expected,[StringComparison]::OrdinalIgnoreCase)){throw 'MSVC root must belong to the explicitly selected VS installation.'}
  $paths=[ordered]@{
    MSBUILD=($vs+'/MSBuild/Current/Bin/amd64/MSBuild.exe')
    VCVARSALL=($vs+'/VC/Auxiliary/Build/vcvarsall.bat')
    MASM=($msvc+'/bin/Hostx64/'+$Architecture+'/'+$(if($Architecture -eq 'x64'){'ml64.exe'}else{'ml.exe'}))
  }
  $pins=[ordered]@{}
  foreach($role in $paths.Keys){
    [void](Resolve-SouiKernelPath $paths[$role] File)
    $pins[$role]=Get-SouiLlvmFileIdentity -Path $paths[$role] -HostExecutable:($role -ne 'VCVARSALL')
  }
  return [ordered]@{vsInstallDir=$vs;msvcRoot=$msvc;hostArchitecture='x64';targetArchitecture=$Architecture;roles=$pins;wholeVsInventoryVerified=$false}
}
function Get-SouiKernelV8ForcedSymbols {
  param([string]$SourceRoot)
  $rows=@()
  foreach($name in @('v8_5_7','v8_5_7_1')){
    $path=Join-Path $SourceRoot ('build/v8_5_7/'+$name+'.vcxproj')
    $settings=[Xml.XmlReaderSettings]::new();$settings.DtdProcessing=[Xml.DtdProcessing]::Prohibit;$settings.XmlResolver=$null
    $reader=[Xml.XmlReader]::Create($path,$settings)
    try{$doc=[Xml.XmlDocument]::new();$doc.XmlResolver=$null;$doc.Load($reader)}finally{$reader.Dispose()}
    $ns=[Xml.XmlNamespaceManager]::new($doc.NameTable);$ns.AddNamespace('m','http://schemas.microsoft.com/developer/msbuild/2003')
    $symbols=@()
    foreach($node in $doc.SelectNodes('//m:ItemDefinitionGroup/m:Lib/m:ForceSymbolReferences',$ns)){
      if($node.ParentNode.ParentNode.GetAttribute('Condition') -match 'Release\|Win32'){
        $symbols+=@($node.InnerText.Split(';')|Where-Object{$_}|ForEach-Object{$_.Trim()})
      }
    }
    if($symbols.Count -ne 1 -or $symbols[0] -cne '__chkstk'){throw "Unsupported Win32 V8 forced-symbol set in $name; preserve and review the complete union before building."}
    $rows+=[ordered]@{project=$name;projectPin=(Get-SouiKernelPin $path);symbols=$symbols}
  }
  return [ordered]@{scope='Release|Win32';projects=$rows;mainUnion=@('__chkstk')}
}
function Get-SouiKernelSourceSnapshot {
  param([string]$SourceRoot)
  $root=Resolve-SouiKernelPath $SourceRoot Directory
  $pending=[Collections.Generic.Stack[IO.DirectoryInfo]]::new()
  $pending.Push([IO.DirectoryInfo]::new($root))
  $paths=[Collections.Generic.List[string]]::new();$directories=[Collections.Generic.List[string]]::new()
  while($pending.Count){
    $dir=$pending.Pop()
    foreach($item in $dir.EnumerateFileSystemInfos()){
      if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Source inventory refuses reparse point: $($item.FullName)"}
      $relative=$item.FullName.Substring($root.Length+1).Replace('\','/')
      if($item -is [IO.DirectoryInfo]){$directories.Add($relative);$pending.Push($item)}
      else{$paths.Add($relative)}
    }
  }
  $files=$paths.ToArray();$dirs=$directories.ToArray()
  [Array]::Sort($files,[StringComparer]::Ordinal);[Array]::Sort($dirs,[StringComparer]::Ordinal)
  $rows=[Collections.Generic.List[object]]::new();$lines=[Collections.Generic.List[string]]::new();$total=[long]0
  foreach($dir in $dirs){$lines.Add('D|'+$dir)}
  foreach($relative in $files){
    $pin=Get-SouiKernelPin ($root+'/'+$relative);$total+=$pin.bytes
    $rows.Add([ordered]@{path=$relative;bytes=$pin.bytes;sha256=$pin.sha256})
    $lines.Add('F|'+$relative+'|'+$pin.bytes.ToString([Globalization.CultureInfo]::InvariantCulture)+'|'+$pin.sha256)
  }
  $hasher=[Security.Cryptography.SHA256]::Create()
  try{$identity=[BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes([string]::Join([string][char]10,$lines)))).Replace('-','').ToLowerInvariant()}finally{$hasher.Dispose()}
  return [ordered]@{schema='soui.kernel-source-files/v1';root=$root;files=$rows.ToArray();directories=$dirs;fileCount=$files.Count;directoryCount=$dirs.Count;totalBytes=$total;identity=$identity}
}
function Assert-SouiKernelSnapshotEqual {
  param($Before,$After)
  if($Before.root -cne $After.root -or $Before.identity -cne $After.identity -or $Before.fileCount -ne $After.fileCount -or $Before.directoryCount -ne $After.directoryCount -or $Before.totalBytes -ne $After.totalBytes){
    throw 'Kernel source roster or bytes changed during the public build.'
  }
}
function Write-SouiKernelData {
  param([string]$Path,$Data)
  [IO.File]::WriteAllText($Path,($Data|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
  return Get-SouiKernelPin $Path
}
function Get-SouiKernelClangVersion {
  param([string]$Text,[int]$ExitCode=0)
  if($ExitCode -ne 0){throw 'The selected compiler version preflight did not exit successfully.'}
  if([string]::IsNullOrWhiteSpace($Text) -or $Text.Length -gt 8192){throw 'Missing or oversized clang version response.'}
  $match=[regex]::Match($Text,'(?m)^clang version (?<version>[0-9]+\.[0-9]+\.[0-9]+)(?:[\s(]|$)')
  if(-not $match.Success -or $match.Groups['version'].Value -cne '22.1.3'){throw 'The selected compiler must report clang version 22.1.3.'}
  return $match.Groups['version'].Value
}
