# The pinned Microsoft VCTIP help describes -timeout:<seconds>. This controls
# its idle server lifetime; it is not a compiler flag or a supervision budget.
# Both phases and all waits remain inside the caller's original owned Job.
function Open-SouiCompilerPhaseFile([string]$Path,[string]$ExpectedSha256) {
  Assert-SouiCMakePath $Path
  $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  try {
    $hasher=[Security.Cryptography.SHA256]::Create()
    try {$digest=[BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
    finally {$hasher.Dispose()}
    if($ExpectedSha256-and$digest-cne$ExpectedSha256){throw 'MSVC compiler lifecycle image differs from the reviewed tool.'}
    return [pscustomobject]@{stream=$stream;path=$Path;bytes=$stream.Length;sha256=$digest}
  }catch{$stream.Dispose();throw}
}

function Assert-SouiCompilerPhaseIdle([string]$ReadyName) {
  $existing=$null
  try {$existing=[Threading.EventWaitHandle]::OpenExisting($ReadyName)}
  catch [Threading.WaitHandleCannotBeOpenedException] {}
  if($null-ne$existing){$existing.Dispose();throw 'A preexisting VCTIP ready event prevents an isolated compiler phase.'}
  $busy=@(Get-Process -Name vctip,cl,link,lib,MSBuild,clang-cl,ninja,cmake -ErrorAction SilentlyContinue)
  if($busy.Count){throw 'A preexisting compiler or VCTIP service prevents an isolated compiler phase; no process was stopped.'}
}

function Wait-SouiCompilerPhaseReady($Process,[string]$ReadyName) {
  $ready=$null;$timer=[Diagnostics.Stopwatch]::StartNew()
  try {
    while($timer.ElapsedMilliseconds-lt3000){
      if($Process.HasExited){throw 'Owned VCTIP exited before readiness.'}
      try {$ready=[Threading.EventWaitHandle]::OpenExisting($ReadyName);break}
      catch [Threading.WaitHandleCannotBeOpenedException] {[Threading.Thread]::Sleep(20)}
    }
    if($null-eq$ready-or-not$ready.WaitOne([Math]::Max(0,3000-[int]$timer.ElapsedMilliseconds))-or$Process.HasExited){
      throw 'Owned VCTIP did not become ready within the compiler phase.'
    }
  }finally{if($null-ne$ready){$ready.Dispose()}}
}

function Write-SouiCompilerPhaseRecord($State,[string]$Name,$Record) {
  $path=Join-Path $State.directory $Name
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Record|ConvertTo-Json -Depth 8)+[char]10)
  $stream=[IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
}

function Start-SouiMsvcCompilerPhase {
  [CmdletBinding()]
  param([Parameter(Mandatory=$true)]$Toolchain,
        [Parameter(Mandatory=$true)][ValidateSet('x86','x64')][string]$Architecture,
        [Parameter(Mandatory=$true)][string]$EvidenceRoot,
        [Parameter(Mandatory=$true)][ValidateSet('configure','build')][string]$Phase)
  if($Toolchain.MsvcVersion-cne'14.44.35207'-or$Toolchain.HostArchitecture-cne'x64'-or$Toolchain.Toolset-cne'v143'){
    throw 'Bounded compiler lifecycle requires the reviewed MSVC v143 Hostx64 toolset.'
  }
  $pins=@{x86='1c1ca5f9c04171c02caca6863e29eca3550333219486d23e65bf01fb02951311';
    x64='651345513aae26090c813b1ded859a5680c3823ca716a80e1d337bac0ca797ef'}
  $directory=[IO.Path]::GetFullPath((Join-Path $Toolchain.MsvcRoot ('bin/Hostx64/'+$Architecture)))
  $state=[pscustomobject]@{process=$null;mutex=$null;mutexHeld=$false;files=[Collections.Generic.List[object]]::new();
    directory=$null;record=$null;closed=$false}
  try {
    # One session-wide lease across workspaces and architectures. Contention
    # fails immediately; it never consumes a new wait or extends a deadline.
    $state.mutex=[Threading.Mutex]::new($false,'Local\SouiDevKit.MsvcCompilerLifecycle.14.44.35226.0')
    try {$state.mutexHeld=$state.mutex.WaitOne(0)}
    catch [Threading.AbandonedMutexException] {$state.mutexHeld=$true;throw 'The compiler lifecycle lease was abandoned; inspect the previous failure.'}
    if(-not$state.mutexHeld){throw 'Another owned MSVC compiler phase is active.'}
    $readyName='VCTIP_14.44.35226.0_READY_EVENT'
    Assert-SouiCompilerPhaseIdle $readyName
    $compiler=Open-SouiCompilerPhaseFile (Join-Path $directory 'cl.exe') ''
    $state.files.Add($compiler)
    $server=Open-SouiCompilerPhaseFile (Join-Path $directory 'vctip.exe') $pins[$Architecture]
    $state.files.Add($server)
    $config=Open-SouiCompilerPhaseFile ($server.path+'.config') 'b58b1a18fc3e3c9c760f46d3d70684bbe73edab671b2d0d26a203e830ccef49d'
    $state.files.Add($config)
    Assert-SouiCMakePath $EvidenceRoot
    $state.directory=Join-Path $EvidenceRoot ('compiler-lifecycle-'+$Phase+'-'+[Guid]::NewGuid().ToString())
    [void](New-Item -ItemType Directory -Path $state.directory -ErrorAction Stop)
    $state.record=[ordered]@{schema='soui.msvc-compiler-lifecycle/v1';phase=$Phase;architecture=$Architecture;
      beganUtc=[DateTime]::UtcNow.ToString('o');compiler=@{path=$compiler.path;sha256=$compiler.sha256};
      image=@{path=$server.path;bytes=$server.bytes;sha256=$server.sha256;configSha256=$config.sha256};
      arguments=@('-timeout:20');readyEvent=$readyName;readinessIsOwnershipProof=$false;
      preexistingServiceRejected=$true;compilerHelperSuppressionConfigured=$false;
      originalOwnedJobClosureRequired=$true;naturalWaitLimitMs=30000;forcedTerminationUsed=$false}
    Write-SouiCompilerPhaseRecord $state 'started.json' $state.record
    $state.process=Start-Process -FilePath $server.path -ArgumentList @('-timeout:20') -WorkingDirectory $EvidenceRoot `
      -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $state.directory 'stdout.bin') `
      -RedirectStandardError (Join-Path $state.directory 'stderr.bin')
    [void]$state.process.Handle
    $state.record.pid=$state.process.Id
    $state.record.creationFileTime=$state.process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()
    Wait-SouiCompilerPhaseReady $state.process $readyName
    $state.record.readinessSignaled=$true
    return $state
  }catch{
    $original=$_
    if($null-ne$state.record){$state.record.startupFailure=$original.Exception.Message}
    # Startup failure may still own a live server. Retire only by its normal
    # timeout; the outer supervisor remains responsible if it cannot retire.
    try{Close-SouiMsvcCompilerPhase $state}catch{Write-Warning $_.Exception.Message}
    throw $original
  }
}

function Close-SouiMsvcCompilerPhase($State) {
  if($null-eq$State-or$State.closed){return}
  $failure=$null
  try {
    if($null-ne$State.process){
      if(-not$State.process.WaitForExit(30000)){throw 'Owned VCTIP did not retire naturally within the compiler phase; outer Job closure is still required.'}
      $State.record.exitCode=$State.process.ExitCode
      if($State.process.ExitCode-ne0){throw 'Owned VCTIP returned a nonzero exit code.'}
      # A quiet, long-running compiler can outlive the short idle server and
      # launch a replacement with its default lifetime. Do not call that phase
      # closed merely because the originally held server returned successfully.
      $remaining=@(Get-Process -Name vctip -ErrorAction SilentlyContinue)
      if($remaining.Count){throw 'A VCTIP service remains after the owned server returned; compiler phase closure is unverified.'}
    }
  }catch{$failure=$_}
  finally{
    try {
      if($null-ne$State.record){
        $State.record.returnedUtc=[DateTime]::UtcNow.ToString('o')
        $State.record.serverNaturallyExited=($null-ne$State.process-and$null-eq$failure)
        $State.record.failure=if($failure){$failure.Exception.Message}else{$null}
        Write-SouiCompilerPhaseRecord $State 'returned.json' $State.record
      }
    }finally{
      if($null-ne$State.process){$State.process.Dispose()}
      foreach($file in $State.files){$file.stream.Dispose()}
      if($State.mutexHeld){$State.mutex.ReleaseMutex()}
      if($null-ne$State.mutex){$State.mutex.Dispose()}
      $State.closed=$true
    }
  }
  if($failure){throw $failure}
}

function Complete-SouiMsvcCompilerPhase($State,[int]$NativeExitCode,[string]$FailureMessage) {
  if($null-ne$State-and$null-ne$State.record){$State.record.nativeExitCode=$NativeExitCode}
  $secondary=$null
  try{Close-SouiMsvcCompilerPhase $State}catch{$secondary=$_}
  if($NativeExitCode-ne0){
    $failure=[InvalidOperationException]::new($FailureMessage)
    $failure.Data['nativeExitCode']=$NativeExitCode
    if($secondary){
      $failure.Data['compilerLifecycleFailure']=$secondary.Exception.Message
      Write-Warning ('Secondary compiler lifecycle failure: '+$secondary.Exception.Message)
    }
    throw $failure
  }
  if($secondary){throw $secondary}
}
