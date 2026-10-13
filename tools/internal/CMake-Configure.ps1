$script:SouiWindowsSdkCheck=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../cmake/SouiWindowsSdk.cmake'))

function Assert-SouiCMakePath([string]$Path) {
  $cursor=[IO.Path]::GetFullPath($Path)
  while($cursor){
    $item=Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
    if($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){
      throw "CMake paths cannot traverse a link or reparse point: $cursor"
    }
    $parent=[IO.Path]::GetDirectoryName($cursor)
    if($parent -eq $cursor){break}
    $cursor=$parent
  }
}

function Assert-SouiCMakeCacheTree([string]$Directory) {
  Assert-SouiCMakePath $Directory
  if(-not (Test-Path -LiteralPath $Directory)){return}
  if(-not (Test-Path -LiteralPath $Directory -PathType Container)){throw "CMakeFiles is not a generated cache directory: $Directory"}
  $pending=[Collections.Generic.Stack[string]]::new()
  $pending.Push($Directory)
  while($pending.Count){
    $current=$pending.Pop()
    Assert-SouiCMakePath $current
    foreach($entry in @(Get-ChildItem -LiteralPath $current -Force -ErrorAction Stop)){
      if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){
        throw "CMake cache cannot contain a link or reparse point: $($entry.FullName)"
      }
      if($entry.PSIsContainer){$pending.Push($entry.FullName)}
    }
  }
}

function Get-SouiCMakeConfigureArguments {
  [CmdletBinding()]
  param(
    [Parameter(Mandatory=$true)][string]$CMake,
    [Parameter(Mandatory=$true)][string]$WorkspaceRoot,
    [Parameter(Mandatory=$true)][string]$SourceDirectory,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [ValidateSet('x86','x64')][string]$Architecture='x64',
    [switch]$FreshConfigure,
    [ValidateSet('MSVC143','LLVM22MSVC142')][string]$NativeToolchain='MSVC143',
    [Collections.IDictionary]$LlvmToolchain
  )
  $workspace=[IO.Path]::GetFullPath($WorkspaceRoot).TrimEnd('\','/')
  $source=[IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\','/')
  $build=[IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\','/')
  $out=Join-Path $workspace 'out'
  $comparison=[StringComparison]::OrdinalIgnoreCase
  if(-not $build.StartsWith($out+'\',$comparison)){
    throw 'CMake build directory must be a generated directory inside this workspace out/; out itself is not allowed.'
  }
  foreach($segment in $build.Substring($out.Length+1).Split([char[]]@('\','/'))){
    if(-not $segment -or $segment -match '[<>:"|?*]' -or $segment -match '[. ]$' -or
        $segment -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)'){
      throw 'CMake build directory must be a safe generated path inside this workspace out/.'
    }
  }
  if($source -ne $workspace -and -not $source.StartsWith($workspace+'\',$comparison)){
    throw 'CMake source directory must belong to this workspace.'
  }
  foreach($directory in @($workspace,$source)){
    if(-not (Test-Path -LiteralPath $directory -PathType Container)){throw "CMake source/workspace directory is missing: $directory"}
  }
  Assert-SouiCMakePath $build
  Assert-SouiCMakePath $source
  if((Test-Path -LiteralPath $build) -and -not (Test-Path -LiteralPath $build -PathType Container)){
    throw "CMake build path is not a generated directory: $build"
  }
  if(-not (Test-Path -LiteralPath $script:SouiWindowsSdkCheck -PathType Leaf)){
    throw 'SDK selection check is missing; restore cmake/SouiWindowsSdk.cmake from the reviewed SDK.'
  }
  $versionOutput=@(& $CMake --version)
  if($LASTEXITCODE -ne 0){throw 'Could not read CMake version.'}
  $versionLine=$versionOutput | Select-Object -First 1
  if($versionLine -notmatch '^cmake version (\d+\.\d+(?:\.\d+)?)(?:[-\s].*)?$'){
    throw 'Could not recognize CMake version; CMake 3.24 or newer is required.'
  }
  $cmakeVersion=[version]$Matches[1]
  if($cmakeVersion -lt [version]'3.24.0'){throw 'CMake 3.24 or newer is required.'}
  $platform=if($Architecture -eq 'x86'){'Win32'}else{'x64'}
  if($cmakeVersion -ge [version]'3.27.0'){$platform+=',version=10.0.19041.0'}
  $toolset='v143,host=x64,version=14.44.35207'

  $cacheFile=Join-Path $build 'CMakeCache.txt'
  $cacheDirectory=Join-Path $build 'CMakeFiles'
  Assert-SouiCMakePath $cacheFile
  Assert-SouiCMakeCacheTree $cacheDirectory
  $cache=@{}
  if(Test-Path -LiteralPath $cacheFile){
    foreach($line in [IO.File]::ReadAllLines($cacheFile)){
      if($line -match '^([^#/:][^:]*):[^=]+=(.*)$'){
        $key=$Matches[1]
        if($cache.ContainsKey($key)){throw "Duplicate CMake cache entry: $key"}
        $cache[$key]=$Matches[2]
      }
    }
    # Ownership checks apply even to an explicitly requested refresh. CMake
    # --fresh must never clear a copied or foreign cache on our behalf.
    foreach($ownership in @(@('CMAKE_HOME_DIRECTORY',$source),@('CMAKE_CACHEFILE_DIR',$build))){
      $key=$ownership[0]
      if(-not $cache.ContainsKey($key) -or
          -not [string]::Equals([IO.Path]::GetFullPath($cache[$key]).TrimEnd('\','/'),$ownership[1],$comparison)){
        throw "CMake cache ownership/source differs for $key in $build; use a new generated directory."
      }
    }
    if(-not $FreshConfigure -and $NativeToolchain -eq 'MSVC143'){
      foreach($expected in @(@('CMAKE_GENERATOR','Visual Studio 17 2022'),@('CMAKE_GENERATOR_PLATFORM',$platform),@('CMAKE_GENERATOR_TOOLSET',$toolset))){
        if(-not $cache.ContainsKey($expected[0]) -or $cache[$expected[0]] -cne $expected[1]){
          throw "CMake cache $($expected[0]) conflicts in $build. Inspect this directory and explicitly use -FreshConfigure to reconfigure."
        }
      }
      if($cache.ContainsKey('CMAKE_SYSTEM_VERSION') -and $cache['CMAKE_SYSTEM_VERSION'] -ne '10.0.19041.0'){
        throw "CMake cache system version conflicts in $build. Inspect this directory and explicitly use -FreshConfigure to reconfigure."
      }
      if(Test-Path -LiteralPath $cacheDirectory){
        foreach($entry in @(Get-ChildItem -LiteralPath $cacheDirectory -Directory -Force)){
          $systemFile=Join-Path $entry.FullName 'CMakeSystem.cmake'
          if(Test-Path -LiteralPath $systemFile){
            $systemText=[IO.File]::ReadAllText($systemFile)
            if($systemText -notmatch '(?m)^set\(CMAKE_SYSTEM_VERSION\s+"([^"]*)"\)' -or $Matches[1] -ne '10.0.19041.0'){
              throw "CMake persisted system version conflicts in $systemFile. Inspect this directory and explicitly use -FreshConfigure to reconfigure."
            }
          }
        }
      }
    }
  } elseif($FreshConfigure -and (Test-Path -LiteralPath $cacheDirectory)){
    throw "Cannot establish cache ownership without CMakeCache.txt in $build; use a new generated directory."
  }
  if($NativeToolchain -eq 'LLVM22MSVC142'){
    if($cmakeVersion -lt [version]'3.29.0'){throw 'LLVM22MSVC142 requires CMake 3.29 or newer.'}
    if(-not $LlvmToolchain -or $LlvmToolchain.Profile -cne 'LLVM22MSVC142' -or $LlvmToolchain.Architecture -cne $Architecture){throw 'The explicit LLVM discovery profile/architecture differs.'}
    $definitionNames=@('SOUI_NATIVE_TOOLCHAIN','SOUI_LLVM_ARCHITECTURE','SOUI_LLVM_ROOT','SOUI_MSVC142_ROOT',
      'SOUI_WINDOWS_SDK_ROOT','SOUI_NINJA_EXECUTABLE','SOUI_WINDOWS_CMD_EXECUTABLE','SOUI_LLVM_INPUT_IDENTITY',
      'SOUI_TOOL_IDENTITY_POWERSHELL_EXECUTABLE','SOUI_TOOL_IDENTITY_BRIDGE','SOUI_TOOL_IDENTITY_HELPER','SOUI_LLVM_BRIDGE_IDENTITY')
    $definitions=$LlvmToolchain.CMakeDefinitions
    if($definitions.Count-ne12-or($definitions.Keys-join"`n")-cne($definitionNames-join"`n")){throw 'The LLVM discovery definitions differ from the fixed twelve-key contract.'}
    $toolchainFile=[IO.Path]::GetFullPath((Join-Path $workspace 'cmake/toolchains/SouiLLVM22.cmake')).Replace('\','/')
    Assert-SouiCMakePath $toolchainFile
    if(-not(Test-Path -LiteralPath $toolchainFile -PathType Leaf)){throw 'The product LLVM toolchain is missing.'}
    $fixedBridge=[IO.Path]::GetFullPath((Join-Path $workspace 'tools/internal/Invoke-ToolFileIdentity.ps1')).Replace('\','/')
    $fixedHelper=[IO.Path]::GetFullPath((Join-Path $workspace 'tools/internal/Tool-FileIdentity.ps1')).Replace('\','/')
    if($definitions.SOUI_TOOL_IDENTITY_BRIDGE-cne$fixedBridge-or$definitions.SOUI_TOOL_IDENTITY_HELPER-cne$fixedHelper){throw 'LLVM must use the fixed product identity bridge and helper.'}
    $check=$script:SouiWindowsSdkCheck.Replace('\','/')
    if($cache.Count-and-not$FreshConfigure){
      $expectedCache=[ordered]@{CMAKE_GENERATOR='Ninja Multi-Config';CMAKE_GENERATOR_PLATFORM='';CMAKE_GENERATOR_TOOLSET='';
        CMAKE_CONFIGURATION_TYPES='Debug;Release;RelWithDebInfo';CMAKE_SYSTEM_VERSION='10.0.19041.0';CMAKE_EXPORT_COMPILE_COMMANDS='ON';
        CMAKE_TOOLCHAIN_FILE=$toolchainFile;CMAKE_PROJECT_TOP_LEVEL_INCLUDES=$check}
      foreach($name in $definitionNames){$expectedCache[$name]=[string]$definitions[$name]}
      $toolchainSha=(Get-FileHash -LiteralPath $toolchainFile -Algorithm SHA256).Hash.ToLowerInvariant()
      $cacheLines=$definitions.SOUI_LLVM_INPUT_IDENTITY+"`n"+$toolchainSha+"`n"+$definitions.SOUI_LLVM_BRIDGE_IDENTITY+"`nNinja Multi-Config`nDebug;Release;RelWithDebInfo"
      $hasher=[Security.Cryptography.SHA256]::Create()
      try{$expectedCache.SOUI_LLVM_CACHE_IDENTITY=[BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($cacheLines))).Replace('-','').ToLowerInvariant()}finally{$hasher.Dispose()}
      foreach($name in $expectedCache.Keys){
        if(-not$cache.ContainsKey($name)-or$cache[$name]-cne$expectedCache[$name]){throw "LLVM cache $name conflicts in $build; inspect it and explicitly use FreshConfigure."}
      }
    }
    $arguments=@('-S',$source,'-B',$build,'-G','Ninja Multi-Config',"-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$toolchainFile",
      "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES:STRING=$check",'-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON')
    foreach($name in $definitionNames){$value=[string]$definitions[$name];if([string]::IsNullOrWhiteSpace($value)-or$value-match'[;"\x00-\x1f]'){throw "Unsafe LLVM definition: $name"};$arguments+=('-D'+$name+':STRING='+$value)}
    if($FreshConfigure){$arguments+='--fresh'}
    return $arguments
  }
  if($LlvmToolchain){throw 'LLVM discovery cannot be supplied to the default MSVC143 profile.'}
  $check=$script:SouiWindowsSdkCheck.Replace('\','/').Replace(';','\;')
  $includes=$check
  if(-not $FreshConfigure -and $cache.ContainsKey('CMAKE_PROJECT_TOP_LEVEL_INCLUDES') -and $cache['CMAKE_PROJECT_TOP_LEVEL_INCLUDES']){
    $previous=[string]$cache['CMAKE_PROJECT_TOP_LEVEL_INCLUDES']
    if(-not ($previous.Split(';') -contains $check)){$includes=$previous+';'+$check}else{$includes=$previous}
  }
  $arguments=@('-S',$source,'-B',$build,'-G','Visual Studio 17 2022','-A',$platform,'-T',$toolset,
    '-DCMAKE_SYSTEM_VERSION=10.0.19041.0',"-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES:STRING=$includes")
  if($FreshConfigure){$arguments+='--fresh'}
  return $arguments
}
