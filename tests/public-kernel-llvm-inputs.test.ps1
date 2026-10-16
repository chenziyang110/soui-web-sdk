[CmdletBinding()]
param([Parameter(Mandatory)][string]$CandidateSource,[Parameter(Mandatory)][string]$DependencySource)
$ErrorActionPreference='Stop'
# Data fixtures only; no executable is launched, including synthetic PE files.
. (Join-Path $DependencySource 'tools/internal/Find-LlvmToolchain.ps1')
. (Join-Path $CandidateSource 'tools/internal/Kernel-LlvmInputs.ps1')
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('soui-kernel-llvm-data-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
$caseCount=0
function Check([bool]$Value,[string]$Why){if(-not $Value){throw $Why}}
function Reject([scriptblock]$Action,[string]$Why){$rejected=$false;try{& $Action|Out-Null}catch{$rejected=$true};Check $rejected $Why}
function Case([string]$Name,[scriptblock]$Action){& $Action;$script:caseCount++;Write-Output ('ok '+$script:caseCount+' - '+$Name)}
function Put([string]$Path,[string]$Content){[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path));[IO.File]::WriteAllText($Path,$Content,$utf8)}
function PeFixture([string]$Path,[bool]$Amd64){
  [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
  $bytes=[byte[]]::new(128);$stream=[IO.MemoryStream]::new($bytes,$true);$writer=[IO.BinaryWriter]::new($stream)
  try{$writer.Write([uint16]0x5a4d);$stream.Position=60;$writer.Write([uint32]64);$stream.Position=64;$writer.Write([uint32]0x4550);$writer.Write([uint16]$(if($Amd64){0x8664}else{0x14c}));$stream.Position=84;$writer.Write([uint16]2);$writer.Write([uint16]2);$writer.Write([uint16]$(if($Amd64){0x20b}else{0x10b}))}finally{$writer.Dispose();$stream.Dispose()}
  [IO.File]::WriteAllBytes($Path,$bytes)
}
try{
Case 'spaces and parentheses remain valid local path data' {
  $actual=Assert-SouiKernelPathText ([IO.Path]::GetPathRoot($testRoot)+'Program Files (x86)/llvm')
  Check ($actual.EndsWith('/Program Files (x86)/llvm')) 'Ordinary Windows installation paths were rejected.'
}
Case 'cmd expansion, property expressions, lists and ADS are rejected' {
  foreach($suffix in @('&x','|x','<x','>x','^x','%TEMP%','!x',';x',':stream',"'x",'$()','@()',[string][char]96)){
    Reject {Assert-SouiKernelPathText ($testRoot+'/'+$suffix)} ('Unsafe input accepted: '+$suffix)
  }
  Reject {Assert-SouiKernelPathText '../relative'} 'Relative input accepted.'
  Reject {Assert-SouiKernelPathText '//host/share'} 'UNC input accepted.'
}
Case 'fresh output is disjoint and refuses existing empty directories' {
  $source=Join-Path $testRoot 'source';[void][IO.Directory]::CreateDirectory($source)
  $external=Join-Path $testRoot 'new-output'
  Check ((Assert-SouiKernelFreshOutput $external @($source)).EndsWith('/new-output')) 'External absent output rejected.'
  Reject {Assert-SouiKernelFreshOutput (Join-Path $source 'out') @($source)} 'Source child output accepted.'
  Reject {Assert-SouiKernelFreshOutput $testRoot @($source)} 'Existing or ancestor output accepted.'
  [void][IO.Directory]::CreateDirectory($external)
  Reject {Assert-SouiKernelFreshOutput $external @($source)} 'Existing empty output accepted.'
}
Case 'complete source file content mutation is rejected' {
  $source=Join-Path $testRoot 'inventory';Put (Join-Path $source 'a.cpp') 'abc'
  $before=Get-SouiKernelSourceSnapshot $source
  Put (Join-Path $source 'a.cpp') 'abd'
  $after=Get-SouiKernelSourceSnapshot $source
  Reject {Assert-SouiKernelSnapshotEqual $before $after} 'Same-size source mutation accepted.'
}
Case 'complete source additions and empty directory additions are rejected' {
  $source=Join-Path $testRoot 'inventory-add';Put (Join-Path $source 'a.cpp') 'abc'
  $before=Get-SouiKernelSourceSnapshot $source
  Put (Join-Path $source 'new.cpp') 'new'
  $after=Get-SouiKernelSourceSnapshot $source
  Reject {Assert-SouiKernelSnapshotEqual $before $after} 'New source file accepted.'
  $before=$after;[void][IO.Directory]::CreateDirectory((Join-Path $source 'empty'))
  $after=Get-SouiKernelSourceSnapshot $source
  Reject {Assert-SouiKernelSnapshotEqual $before $after} 'New empty directory accepted.'
}
Case 'current pins reject same-size changed input and missing input' {
  $file=Join-Path $testRoot 'recipe.txt';Put $file 'abc';$pin=Get-SouiKernelPin $file
  Assert-SouiKernelPins @($pin)
  Put $file 'abd';Reject {Assert-SouiKernelPins @($pin)} 'Changed tool/recipe data accepted.'
  $missing=[ordered]@{path=(Join-Path $testRoot 'missing');bytes=3;sha256=$pin.sha256}
  Reject {Assert-SouiKernelPins @($missing)} 'Missing input accepted.'
}
Case 'Win32 V8 complete original forced union is admitted' {
  $source=Join-Path $testRoot 'forced'
  foreach($name in @('v8_5_7','v8_5_7_1')){
    $xml='<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemDefinitionGroup Condition="Release|Win32"><Lib><ForceSymbolReferences>__chkstk</ForceSymbolReferences></Lib></ItemDefinitionGroup></Project>'
    Put (Join-Path $source ('build/v8_5_7/'+$name+'.vcxproj')) $xml
  }
  $forced=Get-SouiKernelV8ForcedSymbols $source
  Check ($forced.mainUnion.Count -eq 1 -and $forced.mainUnion[0] -ceq '__chkstk') 'Original union lost.'
}
Case 'additional V8 forced symbols are rejected rather than dropped' {
  $source=Join-Path $testRoot 'forced'
  $path=Join-Path $source 'build/v8_5_7/v8_5_7_1.vcxproj'
  $text=[IO.File]::ReadAllText($path).Replace('__chkstk','__chkstk;extra_symbol');Put $path $text
  Reject {Get-SouiKernelV8ForcedSymbols $source} 'Additional required symbol silently dropped.'
}
Case 'VS roles retain independent owner pins and AMD64 host' {
  $vs=Join-Path $testRoot 'VS';$msvc=Join-Path $vs 'VC/Tools/MSVC/14.29.30133'
  PeFixture (Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe') $true
  Put (Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat') '@rem data fixture'
  PeFixture (Join-Path $msvc 'bin/Hostx64/x86/ml.exe') $true
  PeFixture (Join-Path $msvc 'bin/Hostx64/x64/ml64.exe') $true
  $roles=Get-SouiKernelVsRoles $vs $msvc x64
  Check ($roles.roles.MSBUILD.hostMachine -eq 0x8664 -and $roles.roles.MASM.hostMachine -eq 0x8664 -and -not $roles.wholeVsInventoryVerified) 'Role identity overstated or wrong host.'
  $x86=Get-SouiKernelVsRoles $vs $msvc x86
  Check ($x86.targetArchitecture -ceq 'x86' -and $x86.roles.MASM.path.EndsWith('/bin/Hostx64/x86/ml.exe')) 'Win32 MASM owner lost.'
}
Case 'foreign MSVC owner and non-AMD64 MSBuild are rejected' {
  $vs=Join-Path $testRoot 'VS';$msvc=Join-Path $vs 'VC/Tools/MSVC/14.29.30133'
  $other=Join-Path $testRoot 'foreign/14.29.30133';[void][IO.Directory]::CreateDirectory($other)
  Reject {Get-SouiKernelVsRoles $vs $other x64} 'Foreign MSVC root accepted.'
  PeFixture (Join-Path $vs 'MSBuild/Current/Bin/amd64/MSBuild.exe') $false
  Reject {Get-SouiKernelVsRoles $vs $msvc x64} '32-bit role accepted as x64 host.'
}
Case 'compiler version response is checked without treating it as compile proof' {
  Check ((Get-SouiKernelClangVersion "clang version 22.1.3 (data fixture)\nTarget: x86_64-pc-windows-msvc") -ceq '22.1.3') 'Valid compiler version text rejected.'
  Reject {Get-SouiKernelClangVersion -Text 'clang version 22.1.3' -ExitCode 1} 'Failed compiler preflight response accepted.'
  foreach($text in @('clang version 23.0.0','clang version 22.1.4','clang version 22.1.3.4','unrelated version 22.1.3','',('x'*8193))){
    Reject {Get-SouiKernelClangVersion $text} 'Unsupported or absent compiler version accepted.'
  }
}
Case 'internal cmd extended argv uses SHIFT and preserves default body' {
  $psPath=Join-Path $CandidateSource 'tools/Build-Dependencies.ps1'
  $tokens=$null;$errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile($psPath,[ref]$tokens,[ref]$errors)
  $assignment=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -ceq '$llvmCompilerEnvironment'},$true))
  Check ($assignment.Count -eq 1) 'Compiler environment list is not an actual assignment.'
  Check (@($ast.FindAll({param($node)$node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -like '\*'},$true)).Count -eq 0) 'Malformed literal command admitted by syntax-only check.'

  foreach($item in @(@{name='Build-KernelLibraryArch.cmd';prefix=5},@{name='Build-KernelMainArch.cmd';prefix=6})){
    $actual=[IO.File]::ReadAllText((Join-Path $CandidateSource ('tools/internal/'+$item.name)))
    $basis=[IO.File]::ReadAllText((Join-Path $DependencySource ('tools/internal/'+$item.name)))
    $branch='if /I "%~'+($item.prefix+1)+'"=="LLVM22MSVC142" goto llvm'
    $guard='if not "%~'+($item.prefix+1)+'"=="" exit /b 64'
    $withoutBranch=$actual.Replace($branch+[Environment]::NewLine,'').Replace($guard+[Environment]::NewLine,'')
    $legacy=$withoutBranch.Substring(0,$withoutBranch.IndexOf(':llvm')).TrimEnd([char[]]@([char]13,[char]10))
    Check ($legacy -ceq $basis.TrimEnd([char[]]@([char]13,[char]10))) 'Default command body changed.'
    $tail=$actual.Substring($actual.IndexOf(':llvm'))
    Check ([regex]::Matches($tail,'(?m)^shift\r?$').Count -eq $item.prefix) 'Wrong extended argv offset.'
    Check ($actual -notmatch '%~?1[0-9]') 'Invalid cmd argument above nine.'
  }
}
Case 'portable targets preserve real CRT ISA forced-symbol and diagnostics rules' {
  $text=[IO.File]::ReadAllText((Join-Path $CandidateSource 'tools/internal/kernel-llvm-output.targets'))
  $xml=[xml]$text;$ns=[Xml.XmlNamespaceManager]::new($xml.NameTable);$ns.AddNamespace('m','http://schemas.microsoft.com/developer/msbuild/2003')
  Check ($xml.SelectSingleNode('//m:ClCompile[@Update]/m:RuntimeLibrary',$ns).InnerText -ceq 'MultiThreaded') 'Per-TU CRT omitted.'
  $sse=$xml.SelectSingleNode('//m:ClCompile[@Update]/m:EnableEnhancedInstructionSet',$ns)
  Check ($sse.InnerText -ceq 'StreamingSIMDExtensions2' -and $sse.Condition.Contains("MSBuildProjectName") -and $sse.Condition.Contains('skia') -and $sse.Condition.Contains('Win32') -and $sse.Condition.Contains('Release')) 'SSE2 scope widened or masked.'
  Check ($text.Contains('SOUI_WORKER_SHUTDOWN_DIAGNOSTICS=1') -and $text.Contains('__chkstk;%(ForceSymbolReferences)')) 'Required diagnostics/forced symbols omitted.'
  Check ($text.Contains('/X /imsvc') -and $text.Contains('fms-compatibility-version=19.29.30159') -and $text.Contains('AssertElevenFreshLibraries')) 'Compiler/current library assertions missing.'
  Check ($text -notmatch '[CF]:[/\\]|RootExpected|DumpFinal|nonce') 'Private paths or audit dump copied.'
}
Write-Output ('1..'+$caseCount)
}finally{
  $resolved=[IO.Path]::GetFullPath($testRoot)
  $temp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')
  if(-not $resolved.StartsWith($temp+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($resolved) -notmatch '^soui-kernel-llvm-data-[0-9a-f]{32}$'){throw 'Refuse cleanup outside this test-owned temporary directory.'}
  Remove-Item -LiteralPath $resolved -Recurse -Force
}
