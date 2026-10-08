param(
  [Parameter(Mandatory=$true)][ValidateSet('x86','x64')][string]$Architecture,
  [Parameter(Mandatory=$true)][ValidateSet(30000,2100000,28800000)][int]$DurationMs,
  [Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9][a-z0-9-]{0,63}$')][string]$AttemptName,
  [string]$NodePath = (Get-Command node.exe -ErrorAction Stop).Source
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$fixtureRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$runsRoot = Join-Path (Split-Path -Parent $fixtureRoot) 'preview15-real-renewal-runs-20260927'
$runRoot = Join-Path $runsRoot $AttemptName
if (Test-Path -LiteralPath $runRoot) { throw 'Run directory exists; preserve it and choose a new AttemptName.' }
[IO.Directory]::CreateDirectory($runsRoot) | Out-Null
New-Item -ItemType Directory -Path $runRoot -ErrorAction Stop | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$freezePath = Join-Path $fixtureRoot 'freeze.json'
$reportPath = Join-Path $runRoot 'native.ndjson'
$supervisorPath = Join-Path $runRoot 'supervisor.json'
$lease = $null
$leaseAcquired = $false
$native = $null; $nativeStarted = $false; $nativeExited = $false
$stdoutFile = $null; $stderrFile = $null; $stdoutCopy = $null; $stderrCopy = $null
$before = $null; $after = $null; $runtimePath = $null; $freezeSha256 = $null
$nativePid = $null; $exitCode = $null; $timedOut = $false; $externalLoadDetected = $false
$errorMessage = $null; $startedAt = $null; $finishedAt = $null; $removedEnvironmentKeys = @()
$beforeGate = @(); $remaining = @(); $postcheckExitCode = $null
$success=$false; $scriptSha256=$null; $reportSha256=$null
$payloadBefore=@(); $payloadAfter=@(); $diagnosticsBefore=@(); $diagnosticsAfter=@(); $provenance=$null
$unchanged=$false; $inputsUnchanged=$false
$timeoutMs = [long]$DurationMs + 180000L
function Write-NewJson([string]$Destination, $Value) {
  $stream = [IO.File]::Open($Destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  try { $bytes=$utf8.GetBytes(($Value | ConvertTo-Json -Depth 40) + "`n"); $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) }
  finally { $stream.Dispose() }
}
function Quote-Argument([string]$Value) {
  if ($Value -notmatch '[\s"]' -and $Value.Length -gt 0) { return $Value }
  return '"' + ([regex]::Replace([regex]::Replace($Value,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')) + '"'
}
function File-Sha256([string]$File) { return (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() }
function Latch-Failure([ref]$Failure,[string]$Message) { if (-not $Failure.Value) { $Failure.Value=$Message } }
function Close-LogFile($Stream,[ref]$Failure) {
  if (-not $Stream) { return }
  try { $Stream.Flush($true) } catch { Latch-Failure $Failure $_.Exception.Message }
  finally { try { $Stream.Dispose() } catch { Latch-Failure $Failure $_.Exception.Message } }
}
function Stop-OwnedProcess($Process,[ref]$Failure) {
  try {
    if (-not $Process.HasExited) {
      $Process.Kill()
      if (-not $Process.WaitForExit(10000)) { Latch-Failure $Failure 'Own supervised process did not exit' }
    }
  } catch { Latch-Failure $Failure $_.Exception.Message }
}
function Desktop-Loads([int]$ExcludePid = 0) {
  # Fail closed if CIM cannot enumerate. Include arbitrary generated apps whose
  # executable lives in this validation tree or the SDK's output tree.
  return @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
    $_.ProcessId -ne $ExcludePid -and (
      $_.Name -match '^(qemu-system-.+|Soui.*|soui_.*|RenewalStarter|InstalledRenewalProbe|UpgradeLedger|MSBuild|cl|link|cdb)\.exe$' -or
      ($_.ExecutablePath -and ($_.ExecutablePath -like 'C:\SOUI-SDK-production-validation\*.exe' -or
        $_.ExecutablePath -like 'C:\SOUI-SDK-development\preview14-scheduler\out\*.exe' -or $_.ExecutablePath -like 'C:\SOUI-SDK-source-validation\*.exe')))
  } | Select-Object Name,ProcessId,ParentProcessId,CreationDate,ExecutablePath,CommandLine)
}
function Capture-Command([string]$Name,[string]$Program,[string[]]$Arguments,[int]$LimitMs) {
  $info=New-Object Diagnostics.ProcessStartInfo
  $info.FileName=$Program; $info.Arguments=(($Arguments | ForEach-Object { Quote-Argument $_ }) -join ' ')
  $info.WorkingDirectory=$runRoot; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
  $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
  $process=New-Object Diagnostics.Process; $process.StartInfo=$info
  $outFile=$null; $errFile=$null
  $copyOut=$null; $copyErr=$null; $code=$null; $pidValue=$null; $timeout=$false; $failure=$null
  $begin=[DateTime]::UtcNow.ToString('o')
  try {
    $outFile=[IO.File]::Open((Join-Path $runRoot ($Name+'.stdout.log')),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $errFile=[IO.File]::Open((Join-Path $runRoot ($Name+'.stderr.log')),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    if (-not $process.Start()) { throw 'Helper did not start' }; $pidValue=$process.Id
    $copyOut=$process.StandardOutput.BaseStream.CopyToAsync($outFile)
    $copyErr=$process.StandardError.BaseStream.CopyToAsync($errFile)
    if (-not $process.WaitForExit($LimitMs)) {
      $timeout=$true; $process.Kill()
      if (-not $process.WaitForExit(10000)) { throw 'Timed out helper did not exit' }
    }
    $code=$process.ExitCode
    if (-not $copyOut.Wait(10000) -or -not $copyErr.Wait(10000)) { throw 'Helper output did not close' }
  } catch { $failure=$_.Exception.Message }
  finally {
    try {
      if ($pidValue) {
        Stop-OwnedProcess $process ([ref]$failure)
        try { if ($process.HasExited) { $code=$process.ExitCode } } catch { Latch-Failure ([ref]$failure) $_.Exception.Message }
        foreach ($which in @('StandardOutput','StandardError')) {
          try { $process.$which.Dispose() } catch { Latch-Failure ([ref]$failure) $_.Exception.Message }
        }
      }
      foreach ($copy in @($copyOut,$copyErr)) {
        if ($copy) {
          try { if (-not $copy.Wait(1000)) { Latch-Failure ([ref]$failure) 'Helper output copy did not stop' } }
          catch { Latch-Failure ([ref]$failure) $_.Exception.Message }
        }
      }
      Close-LogFile $outFile ([ref]$failure); Close-LogFile $errFile ([ref]$failure)
      try {
        Write-NewJson (Join-Path $runRoot ($Name+'.result.json')) ([ordered]@{program=$Program;arguments=$Arguments;processId=$pidValue;
          startedAt=$begin;finishedAt=[DateTime]::UtcNow.ToString('o');exitCode=$code;timedOut=$timeout;error=$failure})
      } catch { Latch-Failure ([ref]$failure) $_.Exception.Message; [Console]::Error.WriteLine('Cannot persist helper result: '+$_.Exception.Message) }
    } finally { try { $process.Dispose() } catch { Latch-Failure ([ref]$failure) $_.Exception.Message } }
  }
  if ($failure -or $timeout -or $null -eq $code -or $code -ne 0) { throw "Helper failed: $Name" }
  return $code
}

# The generated checker is retained in this run as reproducible evidence. It
# only reads/fingerprints inputs and copies the one known runtime diagnostic.
$checkerSource = @'
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const [stage,fixture,run,arch,nodePath]=process.argv.slice(2);
const SOURCE='b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07';
const VERSION='0.3.0-preview.15';
const PARENT_FREEZE='b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef';
const assert=(ok,message)=>{if(!ok)throw Error(message);};
const encoded=value=>JSON.stringify(value,null,2)+'\n';
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>fs.writeFileSync(file,encoded(value),{flag:'wx'});
function regular(file){const s=fs.lstatSync(file);assert(s.isFile()&&!s.isSymbolicLink(),'Expected regular file: '+file);return fs.readFileSync(file);}
function real(directory){assert(path.isAbsolute(directory),'Expected absolute path');let p=directory;for(;;){assert(!fs.lstatSync(p).isSymbolicLink(),'Linked path: '+p);const parent=path.dirname(p);if(parent===p)break;p=parent;}return fs.realpathSync(directory);}
function relative(root,name){assert(typeof name==='string'&&name&&!name.includes('\\')&&!name.includes(':')&&!name.startsWith('/')&&name.split('/').every(p=>p&&p!=='.'&&p!=='..'),'Unsafe manifest path');return path.join(root,name);}
function walk(root,part=''){const full=part?relative(root,part):root;const s=fs.lstatSync(full);assert(!s.isSymbolicLink(),'Linked manifest input');if(s.isFile())return [part];assert(s.isDirectory(),'Unsupported input');return fs.readdirSync(full).sort().flatMap(p=>walk(root,part?part+'/'+p:p));}
function checked(root,manifest,exact=true){
  real(root);assert(manifest&&Array.isArray(manifest.files)&&manifest.files.length&&hash(encoded(manifest.files))===manifest.sha256,'Manifest digest invalid');
  const expected=manifest.files.map(f=>f.path), seen=new Set();
  assert(JSON.stringify([...expected].sort())===JSON.stringify(expected),'Manifest not sorted');
  const actual=(exact?walk(root):[...expected]).sort();
  assert(JSON.stringify(actual)===JSON.stringify(expected),'Manifest file set changed: '+root);
  let total=0;
  for(const f of manifest.files){assert(!seen.has(f.path.toLowerCase()),'Case-fold duplicate');seen.add(f.path.toLowerCase());let data=regular(relative(root,f.path));
    if(f.normalization!==undefined){assert(f.normalization==='none'||f.normalization==='lf','Invalid normalization');if(f.normalization==='lf')data=Buffer.from(data.toString('latin1').replace(/\r\n/g,'\n'),'latin1');}
    assert(Number.isSafeInteger(f.bytes)&&f.bytes>=0&&/^[a-f0-9]{64}$/.test(f.sha256)&&data.length===f.bytes&&hash(data)===f.sha256,'Input differs: '+f.path);total+=f.bytes;}
  assert(total===manifest.bytes,'Manifest total differs');return manifest.sha256;
}
function payload(runtime){const names=walk(runtime).filter(n=>n!=='source-navigation.log').sort();return names.map(p=>{const b=regular(relative(runtime,p));return {path:p,bytes:b.length,sha256:hash(b)};});}
function outside(root,file){const p=path.relative(root,file);return p!==''&&(p==='..'||p.startsWith('..'+path.sep)||path.isAbsolute(p));}
function archive(runtime,freezeSha){
  const file=path.join(runtime,'source-navigation.log');if(!fs.existsSync(file))return {items:[],provenance:null};
  const bytes=regular(file), digest=hash(bytes);let provenance=null;
  if(stage==='before'){
    const runs=path.dirname(run);
    for(const item of fs.readdirSync(runs,{withFileTypes:true})){
      if(!item.isDirectory()||item.name===path.basename(run))continue;
      const prior=path.join(runs,item.name,'supervisor.json');if(!fs.existsSync(prior))continue;
      try{const record=read(prior);
        if(record.schema!=='soui.real-renewal-supervisor/v1'||record.architecture!==arch||record.freezeSha256!==freezeSha||real(record.runtimePath).toLowerCase()!==runtime.toLowerCase())continue;
        const match=(record.runtimeDiagnostics||[]).find(d=>d.path==='source-navigation.log'&&d.bytes===bytes.length&&d.sha256===digest);
        if(!match||!path.isAbsolute(match.archivePath)||outside(path.dirname(prior),match.archivePath)||!regular(match.archivePath).equals(bytes))continue;
        provenance={supervisorPath:prior,supervisorSha256:hash(regular(prior)),archivePath:match.archivePath};break;
      }catch{}
    }
    assert(provenance,'Existing source-navigation.log has no matching prior run archive; preserve it and investigate');
  }
  const destination=path.join(run,'diagnostics-'+stage);fs.mkdirSync(destination);
  const archivePath=path.join(destination,'source-navigation.log');
  assert(outside(runtime,archivePath),'Diagnostic must be outside runtime');fs.writeFileSync(archivePath,bytes,{flag:'wx'});
  assert(regular(file).equals(bytes)&&regular(archivePath).equals(bytes),'Diagnostic changed during archive');
  return {items:[{path:'source-navigation.log',bytes:bytes.length,sha256:digest,archivePath}],provenance};
}
assert((stage==='before'||stage==='after')&&['x86','x64'].includes(arch),'Invalid checker mode');
real(fixture);real(run);assert(outside(fixture,run),'Run output must be outside preparation root');
const freezePath=path.join(fixture,'freeze.json'),freezeBytes=regular(freezePath),freeze=read(freezePath),freezeSha256=hash(freezeBytes);
assert(freeze.schema==='soui.real-renewal-preparation/v1'&&freeze.status==='compiled-inputs-frozen'&&freeze.version===VERSION&&freeze.sourceInputSha256===SOURCE,'Wrong compiled freeze');
assert(freeze.fixtureRevision===2&&freeze.parent?.freezeSha256===PARENT_FREEZE,'Wrong v2 fixture revision or parent freeze');
assert(freeze.guiExecuted===false&&freeze.realRenewalVerified===false&&freeze.releaseReady===false,'Preparation claims invalid');
const bindings={source:checked(freeze.source.root,freeze.source),inputs:checked(freeze.inputs.root,freeze.inputs),toolchain:{},consumer:{},builds:{}};
assert(freeze.source.sha256===SOURCE&&freeze.source.files.length===717,'Wrong source manifest');
for(const architecture of ['x86','x64']){
  bindings.toolchain[architecture]=checked(freeze.toolchain[architecture].root,freeze.toolchain[architecture]);
  bindings.consumer[architecture]=checked(freeze.consumer[architecture].root,freeze.consumer[architecture]);
  const r=freeze.runtimes[architecture], bbytes=regular(r.buildRecordPath), b=read(r.buildRecordPath);
  assert(hash(bbytes)===r.buildRecordSha256&&b.schema==='soui.real-renewal-build/v1'&&b.architecture===architecture&&b.sourceSha256===SOURCE&&b.buildId===r.buildId&&b.sdkBuildId===r.sdkBuildId&&b.nativeBuild==='passed'&&b.webBuild==='passed'&&b.selfCheck==='passed'&&b.sourceUnchanged===true&&b.guiExecuted===false&&b.realRenewalVerified===false&&b.releaseReady===false,'Wrong build record');
  assert(b.fixtureRevision===2&&b.parent?.freezeSha256===PARENT_FREEZE,'Wrong v2 build revision or parent freeze');
  const base=path.dirname(r.buildRecordPath);
  checked(path.join(base,'drivers'),b.drivers);checked(path.join(base,'protocol'),b.protocol);checked(base,b.evidence,false);
  assert(b.payload.sha256===r.payloadSha256&&hash(encoded(r.payload))===r.payloadSha256&&JSON.stringify(b.payload.files)===JSON.stringify(r.payload),'Build/runtime payload mismatch');
  assert(!r.payload.some(f=>f.path.toLowerCase()==='source-navigation.log'),'Diagnostic cannot be an immutable payload file');
  bindings.builds[architecture]=r.buildRecordSha256;
}
const selected=freeze.runtimes[arch],runtimePath=real(selected.runtimePath);
const selectedBuild=read(selected.buildRecordPath),driverRoot=path.join(path.dirname(selected.buildRecordPath),'drivers');
const runnerEntry=selectedBuild.drivers.files.find(f=>f.path==='Run.ps1');
const analyzerEntry=selectedBuild.drivers.files.find(f=>f.path==='Verify-Run.cjs');
assert(runnerEntry&&analyzerEntry,'Runner/analyzer are not included in frozen build drivers');
const archivedRunner=regular(path.join(run,'Run.ps1'));
assert(archivedRunner.length===runnerEntry.bytes&&hash(archivedRunner)===runnerEntry.sha256&&regular(path.join(fixture,'Run.ps1')).equals(archivedRunner),'Invoked runner differs from selected build');
assert(outside(runtimePath,run),'Output lies inside runtime');
const expectedNode=freeze.toolchain[arch].tools.node;
assert(hash(regular(nodePath))===expectedNode.sha256,'Node executable differs from frozen build tool');
const files=payload(runtimePath);
assert(hash(encoded(files))===selected.payloadSha256&&JSON.stringify(files)===JSON.stringify(selected.payload),'Runtime payload changed');
const executable=regular(path.join(runtimePath,'InstalledRenewalProbe.exe')),pe=executable.readUInt32LE(0x3c);
assert(executable.readUInt16LE(0)===0x5a4d&&executable.readUInt32LE(pe)===0x4550&&executable.readUInt16LE(pe+4)===(arch==='x86'?0x14c:0x8664),'Wrong native PE architecture');
if(stage==='after'){
  const before=read(path.join(run,'identity-before.json'));
  assert(before.freezeSha256===freezeSha256&&before.runtimePath===runtimePath&&JSON.stringify(before.bindings)===JSON.stringify(bindings)&&JSON.stringify(before.payload)===JSON.stringify(files),'Frozen identity changed during execution');
}
const diagnostic=archive(runtimePath,freezeSha256);
const frozenAnalyzer=path.join(driverRoot,'Verify-Run.cjs'),analyzer=path.join(run,'Verify-Run.cjs');
const analyzerBytes=regular(frozenAnalyzer);
assert(analyzerBytes.length===analyzerEntry.bytes&&hash(analyzerBytes)===analyzerEntry.sha256,'Frozen analyzer differs');
if(stage==='before')fs.writeFileSync(analyzer,analyzerBytes,{flag:'wx'});
assert(regular(analyzer).equals(analyzerBytes),'Run analyzer copy changed');
const record={schema:'soui.real-renewal-run-input-check/v1',stage,checkedAt:new Date().toISOString(),architecture:arch,
  freezePath,freezeSha256,runtimePath,payload:files,payloadSha256:selected.payloadSha256,bindings,
  runtimeDiagnostics:diagnostic.items,diagnosticProvenance:diagnostic.provenance,
  analyzerPath:analyzer,analyzerSha256:hash(analyzerBytes),frozenAnalyzerPath:frozenAnalyzer,
  runnerSha256:runnerEntry.sha256,inputsUnchanged:true};
if(stage==='after'){const report=path.join(run,'native.ndjson');record.reportSha256=fs.existsSync(report)?hash(regular(report)):null;}
assert(hash(regular(freezePath))===freezeSha256,'Freeze changed during checks');
write(path.join(run,'identity-'+stage+'.json'),record);
console.log(encoded({status:'identity-verified',stage,architecture:arch,payloadFiles:files.length,diagnosticFiles:diagnostic.items.length,inputsUnchanged:true}));
'@
$checkerPath=Join-Path $runRoot 'check-inputs.cjs'
try {
  $lease=New-Object Threading.Mutex($false,'Local\SouiDevKit.DesktopValidation')
  [IO.File]::WriteAllText($checkerPath,$checkerSource,$utf8)
  [IO.File]::Copy($PSCommandPath,(Join-Path $runRoot 'Run.ps1'),$false)
  $scriptSha256=File-Sha256 (Join-Path $runRoot 'Run.ps1')
  try { $leaseAcquired=$lease.WaitOne(0) } catch [Threading.AbandonedMutexException] { $leaseAcquired=$true }
  if (-not $leaseAcquired) { throw 'Another desktop validation holds the SDK validation mutex' }
  $beforeGate=@(Desktop-Loads)
  Write-NewJson (Join-Path $runRoot 'gate-initial.json') ([ordered]@{checkedAt=[DateTime]::UtcNow.ToString('o');processes=$beforeGate})
  if ($beforeGate.Count) { throw 'Desktop/QEMU workload exists; no application was started' }
  $NodePath=[IO.Path]::GetFullPath($NodePath)
  [void](Capture-Command 'identity-before' $NodePath @($checkerPath,'before',$fixtureRoot,$runRoot,$Architecture,$NodePath) 180000)
  $before=Get-Content -Raw -LiteralPath (Join-Path $runRoot 'identity-before.json') | ConvertFrom-Json
  $runtimePath=$before.runtimePath; $freezeSha256=$before.freezeSha256
  if (Test-Path -LiteralPath $reportPath) { throw 'Native report already exists' }
  $lastGate=@(Desktop-Loads)
  Write-NewJson (Join-Path $runRoot 'gate-before-launch.json') ([ordered]@{checkedAt=[DateTime]::UtcNow.ToString('o');processes=$lastGate})
  if ($lastGate.Count) { throw 'Desktop/QEMU became occupied; no application was started' }
  $info=New-Object Diagnostics.ProcessStartInfo
  $info.FileName=Join-Path $runtimePath 'InstalledRenewalProbe.exe'
  $nativeArguments=@('--runtime',$runtimePath,'--report',$reportPath,'--duration-ms',[string]$DurationMs)
  $info.Arguments=(($nativeArguments | ForEach-Object { Quote-Argument $_ }) -join ' ')
  $info.WorkingDirectory=$runtimePath; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
  $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
  # This fixture uses the SDK's normal profile. Do not inherit unrelated task
  # experiments; record names only, never possible secret environment values.
  $removedEnvironmentKeys=@($info.EnvironmentVariables.Keys | Where-Object { $_ -like 'SOUI_*' })
  foreach ($key in $removedEnvironmentKeys) { $info.EnvironmentVariables.Remove($key) }
  $stdoutFile=[IO.File]::Open((Join-Path $runRoot 'native.stdout.log'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  $stderrFile=[IO.File]::Open((Join-Path $runRoot 'native.stderr.log'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
  $native=New-Object Diagnostics.Process; $native.StartInfo=$info
  $startedAt=[DateTime]::UtcNow.ToString('o')
  if (-not $native.Start()) { throw 'Native probe did not start' }
  $nativeStarted=$true; $nativePid=$native.Id
  $stdoutCopy=$native.StandardOutput.BaseStream.CopyToAsync($stdoutFile)
  $stderrCopy=$native.StandardError.BaseStream.CopyToAsync($stderrFile)
  $clock=[Diagnostics.Stopwatch]::StartNew()
  $nextGate=0L
  while (-not $native.WaitForExit(250)) {
    if ($clock.ElapsedMilliseconds -ge $timeoutMs) {
      $timedOut=$true; $native.Kill()
      if (-not $native.WaitForExit(10000)) { throw 'Own timed out probe did not exit' }
      break
    }
    if ($clock.ElapsedMilliseconds -ge $nextGate) {
      $loads=@(Desktop-Loads $nativePid); $nextGate=$clock.ElapsedMilliseconds+5000L
      if ($loads.Count) {
        $externalLoadDetected=$true
        Write-NewJson (Join-Path $runRoot 'gate-interference.json') ([ordered]@{checkedAt=[DateTime]::UtcNow.ToString('o');processes=$loads})
        $native.Kill()
        if (-not $native.WaitForExit(10000)) { throw 'Own probe did not exit after external load appeared' }
        throw 'External desktop/QEMU load appeared; stopped only this probe'
      }
    }
  }
  $nativeExited=$native.HasExited; $exitCode=$native.ExitCode
  if (-not $stdoutCopy.Wait(10000) -or -not $stderrCopy.Wait(10000)) { throw 'Native stdout/stderr did not close' }
  if ($timedOut) { throw 'Native probe exceeded duration plus 180 seconds' }
  if ($exitCode -ne 0) { throw "Native probe failed with exit code $exitCode" }
} catch { $errorMessage=$_.Exception.Message }
finally {
  try {
  if ($nativeStarted) {
    Stop-OwnedProcess $native ([ref]$errorMessage)
    try {
      $nativeExited=$native.HasExited
      if ($nativeExited) { $exitCode=$native.ExitCode }
    } catch { Latch-Failure ([ref]$errorMessage) $_.Exception.Message }
    foreach ($which in @('StandardOutput','StandardError')) {
      try { $native.$which.Dispose() } catch { Latch-Failure ([ref]$errorMessage) $_.Exception.Message }
    }
  }
  foreach ($copy in @($stdoutCopy,$stderrCopy)) {
    if ($copy) {
      try { if (-not $copy.Wait(1000) -and -not $errorMessage) { $errorMessage='Native output copy did not finish' } }
      catch { if (-not $errorMessage) { $errorMessage=$_.Exception.Message } }
    }
  }
  Close-LogFile $stdoutFile ([ref]$errorMessage)
  Close-LogFile $stderrFile ([ref]$errorMessage)
  $finishedAt=[DateTime]::UtcNow.ToString('o')
  try {
    $remaining=@(Desktop-Loads)
    Write-NewJson (Join-Path $runRoot 'gate-after.json') ([ordered]@{checkedAt=[DateTime]::UtcNow.ToString('o');processes=$remaining})
    if ($remaining.Count) { $externalLoadDetected=$true; if (-not $errorMessage) { $errorMessage='Desktop load remains after run' } }
  } catch { if (-not $errorMessage) { $errorMessage=$_.Exception.Message } }
  if ($before -and $nativeExited) {
    try {
      $postcheckExitCode=Capture-Command 'identity-after' $NodePath @($checkerPath,'after',$fixtureRoot,$runRoot,$Architecture,$NodePath) 180000
      $after=Get-Content -Raw -LiteralPath (Join-Path $runRoot 'identity-after.json') | ConvertFrom-Json
    } catch { if (-not $errorMessage) { $errorMessage=$_.Exception.Message } }
  }
  $reportSha256=$null
  if (Test-Path -LiteralPath $reportPath -PathType Leaf) { $reportSha256=File-Sha256 $reportPath }
  $payloadBefore=@(); $payloadAfter=@(); $diagnosticsBefore=@(); $diagnosticsAfter=@(); $provenance=$null
  if ($before) { $payloadBefore=@($before.payload); $diagnosticsBefore=@($before.runtimeDiagnostics); $provenance=$before.diagnosticProvenance }
  if ($after) { $payloadAfter=@($after.payload); $diagnosticsAfter=@($after.runtimeDiagnostics) }
  elseif ($runtimePath -and $nativeExited) {
    # Preserve the diagnostic even when a different identity check rejected the
    # run. This is evidence retention, never a substitute for a passing check.
    try {
      $diagnostic=Join-Path $runtimePath 'source-navigation.log'
      if (Test-Path -LiteralPath $diagnostic -PathType Leaf) {
        $item=Get-Item -LiteralPath $diagnostic
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Diagnostic is a link' }
        $diagnosticRoot=Join-Path $runRoot 'diagnostics-after-failed'
        New-Item -ItemType Directory -Path $diagnosticRoot -ErrorAction Stop | Out-Null
        $archive=Join-Path $diagnosticRoot 'source-navigation.log'
        [IO.File]::Copy($diagnostic,$archive,$false)
        $digest=File-Sha256 $archive
        if ((File-Sha256 $diagnostic) -ne $digest) { throw 'Diagnostic changed during failed-run archive' }
        $diagnosticsAfter=@([ordered]@{path='source-navigation.log';bytes=(Get-Item -LiteralPath $archive).Length;sha256=$digest;archivePath=$archive})
      }
    } catch { if (-not $errorMessage) { $errorMessage=$_.Exception.Message } }
  }
  $unchanged=($null -ne $before -and $null -ne $after -and $before.payloadSha256 -eq $after.payloadSha256)
  $inputsUnchanged=($null -ne $before -and $null -ne $after -and $after.inputsUnchanged -eq $true)
  } catch { Latch-Failure ([ref]$errorMessage) $_.Exception.Message }
  finally {
    # Evidence/hash/flush failures may never bypass these ownership releases.
    if ($native) { try { $native.Dispose() } catch { Latch-Failure ([ref]$errorMessage) $_.Exception.Message } }
    if ($leaseAcquired -and $lease) {
      try { $lease.ReleaseMutex() } catch { Latch-Failure ([ref]$errorMessage) $_.Exception.Message }
    }
    if ($lease) { try { $lease.Dispose() } catch { Latch-Failure ([ref]$errorMessage) $_.Exception.Message } }
  }
  $success=($nativeStarted -and $nativeExited -and $null -ne $exitCode -and $exitCode -eq 0 -and -not $timedOut -and
    -not $errorMessage -and -not $externalLoadDetected -and $remaining.Count -eq 0 -and $unchanged -and $inputsUnchanged)
  try {
  Write-NewJson $supervisorPath ([ordered]@{schema='soui.real-renewal-supervisor/v1';architecture=$Architecture;durationMs=$DurationMs;
    program=if($runtimePath){Join-Path $runtimePath 'InstalledRenewalProbe.exe'}else{$null};pid=$nativePid;startedAt=$startedAt;finishedAt=$finishedAt;
    exitCode=$exitCode;timedOut=$timedOut;timeoutMs=$timeoutMs;nativeStarted=$nativeStarted;nativeExited=$nativeExited;
    supervisionSucceeded=$success;supervisionError=$errorMessage;externalLoadDetected=$externalLoadDetected;remainingDesktopProcesses=$remaining;
    reportPath=$reportPath;reportSha256=$reportSha256;runtimePath=$runtimePath;freezePath=$freezePath;freezeSha256=$freezeSha256;
    payloadBefore=$payloadBefore;payloadAfter=$payloadAfter;payloadUnchanged=$unchanged;inputsUnchanged=$inputsUnchanged;
    runtimeDiagnostics=$diagnosticsAfter;runtimeDiagnosticsBefore=$diagnosticsBefore;existingDiagnosticProvenance=$provenance;
    removedEnvironmentKeys=$removedEnvironmentKeys;postcheckExitCode=$postcheckExitCode;runnerSha256=$scriptSha256;
    analyzerPath=if($before){$before.analyzerPath}else{$null};analyzerSha256=if($before){$before.analyzerSha256}else{$null};
    reusedRuntime=$true;historicalLiveReverificationLimitedByAppendedDiagnostic=$true;realRenewalVerified=$false;releaseReady=$false})
  } catch {
    $success=$false; Latch-Failure ([ref]$errorMessage) $_.Exception.Message
    [Console]::Error.WriteLine('Cannot persist supervisor record: '+$_.Exception.Message)
  }
}
if (-not $success) { Write-Error ("Run rejected; raw evidence retained at " + $runRoot + ': ' + $errorMessage) -ErrorAction Continue; exit 1 }
Write-Output "Native run completed; frozen inputs and payload remain unchanged: $runRoot"
Write-Output 'No renewal verdict is inferred from exit 0. Run the frozen analyzer against native.ndjson and supervisor.json.'
Write-Output ("Analyzer: " + $before.analyzerPath)
exit 0
