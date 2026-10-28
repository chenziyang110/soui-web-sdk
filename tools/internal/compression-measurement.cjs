'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const id = require('./delivery-identity.cjs');
const profiles = require('./compression-profiles.cjs');

const schema = 'soui.compression-measurement/v1';
const memoryCalibrationBytes = 32 * 1024 * 1024;
const need = (condition, reason) => { if (!condition) throw new Error(reason); };
function fileIdentity(file, allowHardlinks = false) {
  file = id.safePath(path.resolve(file));
  const before = fs.lstatSync(file);
  need(before.isFile() && (allowHardlinks ? before.nlink >= 1 : before.nlink === 1), 'Expected ordinary file');
  const hash = crypto.createHash('sha256'), buffer = Buffer.allocUnsafe(128 * 1024);
  const descriptor = fs.openSync(file, 'r');
  let bytes = 0, count;
  try {
    while ((count = fs.readSync(descriptor, buffer, 0, buffer.length, null))) { bytes += count; hash.update(buffer.subarray(0, count)); }
    const after = fs.fstatSync(descriptor), current = fs.lstatSync(id.safePath(file));
    need(bytes === before.size && after.size === before.size && after.mtimeMs === before.mtimeMs &&
      current.ino === before.ino && current.dev === before.dev && current.mtimeMs === before.mtimeMs && current.nlink === before.nlink,
    'File changed during measurement hashing');
  } finally { fs.closeSync(descriptor); }
  return {bytes, sha256: hash.digest('hex')};
}
function snapshot(directory) {
  directory = id.safePath(path.resolve(directory));
  need(fs.lstatSync(directory).isDirectory(), 'Expected input directory');
  const files = [], directories = [], pending = [''], seen = new Set();
  while (pending.length) {
    const parent = pending.pop();
    for (const entry of fs.readdirSync(id.safePath(directory, parent), {withFileTypes: true})) {
      const relative = parent ? parent + '/' + entry.name : entry.name;
      const full = id.safePath(directory, relative), stat = fs.lstatSync(full);
      need(!seen.has(relative.toLowerCase()), 'Case-colliding measurement path'); seen.add(relative.toLowerCase());
      need(seen.size <= 100000, 'Measurement input exceeds 100000 entries');
      if (stat.isDirectory()) { directories.push(relative); pending.push(relative); }
      else { need(stat.isFile() && stat.nlink === 1, 'Measurement inputs cannot contain links or special files'); files.push({path: relative, ...fileIdentity(full)}); }
    }
  }
  files.sort((a,b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0); directories.sort();
  return {sha256: id.hash(id.encoded({files, directories})), bytes: files.reduce((n,file) => n + file.bytes,0), fileCount: files.length, files, directories};
}
function same(actual, expected, reason) { need(JSON.stringify(actual) === JSON.stringify(expected), reason); }
function within(parent, child) { const relative = path.relative(parent, child); return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith('..' + path.sep)); }
function validateOptions(options) {
  need(process.platform === 'win32', 'Compression measurement requires Windows Job accounting');
  need(options && Array.isArray(options.inputs) && options.inputs.length > 0 && options.inputs.length <= 32,
    'Provide between 1 and 32 input directories');
  need(typeof options.output === 'string' && options.output, 'Output directory is required');
  const output = id.safePath(path.resolve(options.output));
  need(!fs.existsSync(output) && fs.statSync(path.dirname(output)).isDirectory(), 'Output must be a new directory with an existing parent');
  const names = new Set(), inputs = options.inputs.map(source => {
    need(typeof source === 'string' && source.length > 0, 'Input directory is required');
    source = id.safePath(path.resolve(source)); need(fs.statSync(source).isDirectory(), 'Input must be a directory');
    const name = path.basename(source); id.safePath(output, name);
    need(!names.has(name.toLowerCase()), 'Input names must be unique'); names.add(name.toLowerCase());
    need(!within(source,output), 'Output cannot be inside an input directory');
    return {name, source};
  });
  const selected = options.profiles ?? ['zip','7z-balanced','7z-small'];
  need(Array.isArray(selected) && selected.length > 0 && new Set(selected).size === selected.length, 'Provide unique compression profiles');
  const resolved = selected.map(profile => profiles.resolve(profile));
  const extractionRuns = options.extractionRuns ?? 3, timeoutMs = options.timeoutMs ?? 180000;
  need(Number.isSafeInteger(extractionRuns) && extractionRuns >= 1 && extractionRuns <= 10, 'Extraction runs must be 1-10');
  need(Number.isSafeInteger(timeoutMs) && timeoutMs >= 1000 && timeoutMs <= 3600000, 'Timeout must be 1000-3600000 milliseconds');
  need(typeof options.tool === 'string' && fs.statSync(id.safePath(path.resolve(options.tool))).isFile(), '7-Zip executable is required');
  return {output, inputs, profiles: resolved, extractionRuns, timeoutMs, tool: path.resolve(options.tool)};
}
function compileRunner(output) {
  const source = path.join(__dirname,'compression-process-runner.cs');
  const sourceIdentity = fileIdentity(source);
  const compiler = path.join(process.env.SystemRoot || 'C:/Windows', 'Microsoft.NET/Framework64/v4.0.30319/csc.exe');
  need(fs.existsSync(compiler), '.NET Framework 4 C# compiler is required');
  const executable = path.join(output,'compression-process-runner.exe');
  need(!fs.existsSync(executable), 'Measurement runner already exists');
  const arguments_ = ['/nologo','/optimize+','/platform:anycpu','/target:exe','/r:System.Web.Extensions.dll','/out:' + executable,source];
  const result = spawnSync(compiler,arguments_,{encoding:'utf8',windowsHide:true,timeout:60000});
  fs.writeFileSync(path.join(output,'compile-runner.log'),(result.stdout || '')+(result.stderr || '')+(result.error ? String(result.error) : ''));
  need(!result.error && result.status === 0, 'Native process supervisor compilation failed');
  same(fileIdentity(source),sourceIdentity,'Supervisor source changed during compilation');
  // Windows servicing legitimately hardlinks the system compiler into WinSxS.
  // User input and generated artifacts remain restricted to ordinary copies.
  return {executable, identity:fileIdentity(executable), sourceIdentity, compiler:{file:path.basename(compiler),...fileIdentity(compiler,true)},arguments:arguments_,compilationExcludedFromMeasurement:true};
}
function runOwned(runner, output, name, executable, arguments_, directory, timeoutMs) {
  const request = {Executable:executable,Arguments:arguments_,Directory:directory,Log:path.join(output,name+'.log'),TimeoutMs:timeoutMs};
  const requestFile = path.join(output,name+'.request.json');
  fs.writeFileSync(requestFile,id.encoded(request),{flag:'wx'});
  const result = spawnSync(runner,['run',requestFile],{encoding:'utf8',windowsHide:true,timeout:timeoutMs+15000,maxBuffer:1024*1024});
  fs.writeFileSync(path.join(output,name+'.supervisor.log'),(result.stdout || '')+(result.stderr || '')+(result.error ? String(result.error) : ''));
  if (result.error || result.status !== 0) return {schema:'soui.windows-process-measurement/v1',executable,arguments:arguments_,
    supervisorExitCode:result.status,supervisorError:result.error?.message || result.stderr || 'Supervisor failed',passed:false};
  try {
    const measured = validateRun(JSON.parse(result.stdout),request);
    return {...measured,supervisorExitCode:0,passed:measured.exitCode === 0 && measured.timedOut === false && measured.jobDrained === true};
  } catch (error) { return {supervisorExitCode:result.status,supervisorError:error.message,passed:false}; }
}
function validateRun(measured, request) {
  need(measured && measured.schema==='soui.windows-process-measurement/v1','Unsupported process measurement');
  for(const [key,field] of [['executable','Executable'],['arguments','Arguments'],['directory','Directory'],['log','Log']])
    same(measured[key],request[field],'Process measurement does not match its request: '+key);
  need(typeof measured.elapsedMs==='number' && Number.isFinite(measured.elapsedMs) && measured.elapsedMs>=0,'Invalid measured elapsed time');
  need(Number.isSafeInteger(measured.exitCode) && measured.exitCode>=0 && measured.exitCode<=0xffffffff &&
    typeof measured.timedOut==='boolean' && measured.jobDrained===true &&
    Number.isSafeInteger(measured.totalProcesses) && measured.totalProcesses>=1,'Invalid measured process outcome');
  need(measured.memory && measured.memory.metric==='private-commit' &&
    ['peakProcessMemoryBytes','peakJobMemoryBytes'].every(key=>Number.isSafeInteger(measured.memory[key]) && measured.memory[key]>0),'Invalid measured Job memory peaks');
  need(measured.memory.peakJobMemoryBytes>=measured.memory.peakProcessMemoryBytes,'Inconsistent measured Job memory peaks');
  need(Array.isArray(measured.observedProcesses) && measured.observedProcesses.length>0 &&
    measured.observedProcesses.every(process=>Number.isSafeInteger(process.processId) && process.processId>0 &&
      (typeof process.imagePath==='string' || process.imagePath===null) && Number.isSafeInteger(process.queryError)),
  'Missing measured Job process identity observations');
  return measured;
}
function requireRun(run, name) { need(run.passed === true, name + ' failed or timed out; inspect preserved raw measurement'); }
function calibrate(runner, output) {
  const baseline = runOwned(runner,output,'calibration-baseline',runner,['allocate','0','300'],output,10000);
  requireRun(baseline,'Memory calibration baseline');
  const allocation = runOwned(runner,output,'calibration-32m',runner,['allocate',String(memoryCalibrationBytes),'300'],output,10000);
  requireRun(allocation,'Memory calibration allocation');
  const delta = allocation.memory.peakJobMemoryBytes - baseline.memory.peakJobMemoryBytes;
  need(allocation.memory.peakJobMemoryBytes >= memoryCalibrationBytes && delta >= memoryCalibrationBytes - 2 * 1024 * 1024,
    'Windows private commit calibration did not observe the touched 32 MiB allocation');
  const timeout = runOwned(runner,output,'calibration-timeout',runner,['allocate','4096','2000'],output,100);
  need(timeout.supervisorExitCode === 0 && timeout.timedOut === true && timeout.exitCode !== 0 && timeout.jobDrained === true,
    'Owned process timeout calibration failed');
  return {allocatedAndTouchedBytes:memoryCalibrationBytes,observedJobPrivateCommitDeltaBytes:delta,baseline,allocation,
    timeout:{...timeout,expectedTimeout:true},passed:true};
}
function summarizeRuns(runs) {
  need(Array.isArray(runs) && runs.length > 0 && runs.every(run => run.passed === true), 'Cannot summarize incomplete or failed measurement runs');
  const stats = values => { const sorted=[...values].sort((a,b)=>a-b),middle=Math.floor(sorted.length/2); return {min:sorted[0],median:sorted.length%2 ? sorted[middle] : (sorted[middle-1]+sorted[middle])/2,max:sorted.at(-1)}; };
  return {count:runs.length,elapsedMs:stats(runs.map(run=>run.elapsedMs)),peakJobPrivateCommitBytes:stats(runs.map(run=>run.memory.peakJobMemoryBytes)),
    peakProcessPrivateCommitBytes:stats(runs.map(run=>run.memory.peakProcessMemoryBytes))};
}
function parseMethodListing(text) {
  const methods=[...new Set([...text.matchAll(/^Method = (.+)\r?$/gm)].map(match=>match[1].trim()))];
  const dictionaryDetails=[];
  for(const method of methods) for(const match of method.matchAll(/\bLZMA2:([a-z0-9.]+)/gi)) {
    const token=/^(\d+)([kmg])?$/i.exec(match[1]);
    if(!token) continue;
    const unit=token[2]?.toLowerCase(),value=Number(token[1]);
    const bytes=unit ? value*({k:1024,m:1024**2,g:1024**3}[unit]) : 2**value;
    if(!Number.isSafeInteger(bytes) || bytes<=0) continue;
    if(!dictionaryDetails.some(detail=>detail.token===match[0])) dictionaryDetails.push({token:match[0],bytes,
      notation:unit ? 'explicit-binary-unit' : 'base-two-exponent'});
  }
  const dictionaries=[...new Set(dictionaryDetails.map(detail=>detail.bytes))];
  return {methods,observedDictionaryBytes:dictionaries,dictionaryDetails,
    interpretation:'Archive-reported LZMA2:N uses exponent 2^N; explicit k/m/g suffixes use KiB/MiB/GiB. Unknown notation remains unknown; requested settings never replace archive observations.',
    types:[...new Set([...text.matchAll(/^Type = (.+)\r?$/gm)].map(match=>match[1].trim()))],solid:[...new Set([...text.matchAll(/^Solid = (.+)\r?$/gm)].map(match=>match[1].trim()))]};
}
function inputKind(directory, inventory) {
  const manifest=inventory.files.find(file=>file.path==='manifest.json');
  // A business directory may contain an unrelated or non-JSON manifest. Only
  // an explicitly recognized bundle schema enters the bundle validation path.
  if(!manifest || manifest.bytes>32*1024*1024) return 'directory';
  try { return id.readJson(path.join(directory,'manifest.json')).schema==='soui.runtime-bundle/v1' ? 'bundle' : 'directory'; }
  catch { return 'directory'; }
}
function runMeasurement(rawOptions) {
  const options = validateOptions(rawOptions), root = options.output;
  fs.mkdirSync(root); for (const name of ['runner','inputs','archives','runs','extractions','restored']) fs.mkdirSync(path.join(root,name));
  const report = {schema,outputDirectory:root,startedAt:new Date().toISOString(),passed:false,releaseReady:false,serial:true,coldCache:false,
    implementation:{measurementSource:fileIdentity(__filename),profileDefinitionsSource:fileIdentity(require.resolve('./compression-profiles.cjs'))},
    method:{elapsed:'ResumeThread through observed Job drain; excludes supervisor launch, CreateProcess suspended, Job assignment, compilation, copies, hashing and verification; includes process-observer queries and 1-5 ms polling granularity',
      memory:'Whole lifetime private commit peaks from Windows Job accounting. Primary process post-exit peak working set/private commit are supplementary only when available. Neither dictionary bytes nor private commit are physical resident memory.',
      cache:'No cache eviction or global cache changes; serial warm/unspecified-cache observations, not cold-cache claims',
      references:['https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_extended_limit_information','https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex']},
    requestedExtractionRuns:options.extractionRuns,timeoutMs:options.timeoutMs,tool:{executable:options.tool},inputs:[]};
  const save=()=>fs.writeFileSync(path.join(root,'measurement.json'),id.encoded(report)); save();
  try {
    report.tool.identity=profiles.toolIdentity(options.tool); save();
    report.runner=compileRunner(path.join(root,'runner')); save();
    report.calibration=calibrate(report.runner.executable,path.join(root,'runs')); save();
    report.processIdentityProbe=runOwned(report.runner.executable,path.join(root,'runs'),'tool-process-identities',options.tool,['i'],root,options.timeoutMs);
    save();requireRun(report.processIdentityProbe,'Archive tool process identity probe');
    const bundle=require('./runtime-bundle.cjs');
    for (const input of options.inputs) {
      const sourceSnapshot=snapshot(input.source), fixed=path.join(root,'inputs',input.name);
      need(sourceSnapshot.files.length>0,'Input directory cannot be empty'); fs.mkdirSync(fixed);
      for (const name of sourceSnapshot.directories) fs.mkdirSync(id.safePath(fixed,name),{recursive:true});
      for (const file of sourceSnapshot.files) fs.copyFileSync(id.safePath(input.source,file.path),id.safePath(fixed,file.path),fs.constants.COPYFILE_EXCL);
      same(snapshot(input.source),sourceSnapshot,'Input changed during fixed-copy creation');
      const fixedSnapshot=snapshot(fixed); same(fixedSnapshot,sourceSnapshot,'Fixed measurement source differs');
      const kind=inputKind(fixed,sourceSnapshot);
      const dataset={name:input.name,sourceDirectory:input.source,fixedDirectory:fixed,sourceSnapshot,fixedSnapshot,kind,profiles:[]};
      if (kind==='bundle') dataset.bundleVerification=bundle.verify(fixed);
      report.inputs.push(dataset); save();
    }
    // All copies are fixed before timings begin. No hashing or copying overlaps
    // any measured compression/decompression process, including another input.
    for (const dataset of report.inputs) for (const selected of options.profiles) {
      const stem=dataset.name+'-'+selected.profile, archive=path.join(root,'archives',stem+selected.extension);
      const entry={selection:selected,archive:{file:path.basename(archive)},extractionRuns:[],passed:false}; dataset.profiles.push(entry); save();
      entry.compressionRun=runOwned(report.runner.executable,path.join(root,'runs'),stem+'-compress',options.tool,
        ['a',...selected.arguments,archive,'*'],dataset.fixedDirectory,options.timeoutMs); save(); requireRun(entry.compressionRun,'Compression');
      entry.archive={...entry.archive,...fileIdentity(archive)};
      entry.integrity=runOwned(report.runner.executable,path.join(root,'runs'),stem+'-integrity',options.tool,['t',archive],root,options.timeoutMs); save(); requireRun(entry.integrity,'Archive integrity');
      entry.listing=runOwned(report.runner.executable,path.join(root,'runs'),stem+'-listing',options.tool,['l','-slt',archive],root,options.timeoutMs); save(); requireRun(entry.listing,'Archive method listing');
      entry.observedMethod=parseMethodListing(fs.readFileSync(entry.listing.log,'utf8')); save();
      for(let index=0;index<options.extractionRuns;index++) {
        const destination=path.join(root,'extractions',stem+'-'+(index+1)); need(!fs.existsSync(destination),'Extraction destination already exists');
        const measured={index:index+1,directory:destination,run:runOwned(report.runner.executable,path.join(root,'runs'),stem+'-extract-'+(index+1),
          options.tool,['x',archive,'-o'+destination,'-y'],root,options.timeoutMs),passed:false}; entry.extractionRuns.push(measured); save(); requireRun(measured.run,'Archive extraction');
        measured.snapshot=snapshot(destination); same(measured.snapshot,dataset.fixedSnapshot,'Extracted directory bytes differ'); measured.hashVerified=true;
        if(dataset.kind==='bundle') {
          measured.bundleVerification=bundle.verify(destination);
          const restored=path.join(root,'restored',stem+'-'+(index+1)); measured.bundleRestore=bundle.restore(destination,restored);
          const applications=id.readJson(path.join(destination,'manifest.json')).applications;
          for(const application of applications) {
            const restoredRoot=path.join(restored,application.name);
            same(id.verifyPayload(restoredRoot,application.report.target,application.report.payload,application.report.rendererBackend),application.report.payload,'Restored bundle payload differs');
          }
        }
        measured.passed=true; save();
      }
      entry.extractionSummary=summarizeRuns(entry.extractionRuns.map(extraction=>extraction.run));
      same(snapshot(dataset.fixedDirectory),dataset.fixedSnapshot,'Fixed source changed during measurement');
      same(snapshot(dataset.sourceDirectory),dataset.sourceSnapshot,'Original source changed during measurement');
      if(dataset.kind==='bundle') same(bundle.verify(dataset.fixedDirectory),dataset.bundleVerification,'Fixed bundle changed during measurement');
      same(fileIdentity(archive),{bytes:entry.archive.bytes,sha256:entry.archive.sha256},'Retained archive identity changed during measurement');
      entry.passed=true; save();
    }
    same(profiles.toolIdentity(options.tool),report.tool.identity,'Archive executable changed during measurement');
    same(fileIdentity(report.runner.executable),report.runner.identity,'Compiled supervisor changed during measurement');
    report.finishedAt=new Date().toISOString(); report.passed=true; save(); return report;
  } catch(error) { report.error=error.message; report.finishedAt=new Date().toISOString(); save(); throw error; }
}
function cli(argv) {
  const options={inputs:[],profiles:[]}, seen=new Set();
  while(argv.length) {
    const flag=argv.shift(),value=argv.shift(); need(typeof value==='string' && value.length>0,'Missing measurement argument');
    if(flag==='--input') options.inputs.push(value);
    else if(flag==='--profile') options.profiles.push(value);
    else {
      const key={'--output':'output','--tool':'tool','--extraction-runs':'extractionRuns','--timeout-ms':'timeoutMs'}[flag];
      need(key && !seen.has(key),'Invalid or duplicate measurement argument'); seen.add(key); options[key]=['extractionRuns','timeoutMs'].includes(key) ? Number(value) : value;
    }
  }
  if(!options.profiles.length) delete options.profiles;
  return runMeasurement(options);
}
module.exports={schema,fileIdentity,snapshot,validateOptions,compileRunner,runOwned,validateRun,calibrate,summarizeRuns,parseMethodListing,inputKind,runMeasurement,cli};
if(require.main===module) {
  try { const report=cli(process.argv.slice(2)); console.log(id.encoded({schema,passed:report.passed,report:path.join(report.outputDirectory,'measurement.json')})); }
  catch(error) { console.error(error.message); process.exitCode=1; }
}
