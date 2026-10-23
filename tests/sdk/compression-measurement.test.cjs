'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {spawnSync}=require('node:child_process');
const id=require('../../tools/internal/delivery-identity.cjs');
const bundle=require('../../tools/internal/runtime-bundle.cjs');
const measurement=require('../../tools/internal/compression-measurement.cjs');
const tool=path.join(process.env.ProgramFiles || '', '7-Zip/7z.exe');
const windowsTools=process.platform==='win32' && fs.existsSync(tool);
function temporary(t) {
 const parent=fs.realpathSync(os.tmpdir()),root=fs.realpathSync(fs.mkdtempSync(path.join(parent,'soui-compression-measure-test-')));
 t.after(()=>{assert.equal(path.dirname(root),parent);assert.equal(id.safePath(root),root);assert.equal(fs.realpathSync(root),root);fs.rmSync(root,{recursive:true,force:true});});return root;
}
function put(root,name,bytes){const full=id.safePath(root,name);fs.mkdirSync(path.dirname(full),{recursive:true});fs.writeFileSync(full,bytes);}
test('method observations retain actual dictionary evidence without replacing it with requested settings',()=>{
 const actual=measurement.parseMethodListing('Type = 7z\r\nMethod = LZMA2:25\r\nSolid = +\r\nMethod = LZMA2:25\r\n');
 assert.deepEqual(actual.methods,['LZMA2:25']);assert.deepEqual(actual.observedDictionaryBytes,[32*1024*1024]);
 assert.deepEqual(actual.types,['7z']);assert.deepEqual(actual.solid,['+']);
 assert.deepEqual(measurement.parseMethodListing('Method = Deflate\n').observedDictionaryBytes,[]);
 assert.deepEqual(measurement.parseMethodListing('Method = LZMA2:12\n').observedDictionaryBytes,[4096]);
 assert.deepEqual(measurement.parseMethodListing('Method = LZMA2:48m LZMA:20 BCJ2\n').observedDictionaryBytes,[48*1024*1024]);
 assert.deepEqual(measurement.parseMethodListing('Method = LZMA2:192k\nMethod = LZMA2:2g\n').observedDictionaryBytes,[192*1024,2*1024**3]);
 assert.deepEqual(measurement.parseMethodListing('Method = LZMA2:1.5m\nMethod = LZMA2:99\n').observedDictionaryBytes,[]);
});
test('summary refuses failed rounds and reports observed median, range and private commit peaks',()=>{
 const runs=[3,1,2].map(value=>({passed:true,elapsedMs:value,memory:{peakJobMemoryBytes:value*20,peakProcessMemoryBytes:value*10}}));
 const summary=measurement.summarizeRuns(runs);assert.equal(summary.count,3);
 assert.deepEqual(summary.elapsedMs,{min:1,median:2,max:3});assert.deepEqual(summary.peakJobPrivateCommitBytes,{min:20,median:40,max:60});
 for(const input of [[],[...runs,{passed:false}],null]) assert.throws(()=>measurement.summarizeRuns(input));
});
test('process measurements require request identity and finite actual outcome and memory evidence',()=>{
 const request={Executable:'C:/tool.exe',Arguments:['a','space value'],Directory:'C:/input',Log:'C:/run.log'};
 const value={schema:'soui.windows-process-measurement/v1',executable:request.Executable,arguments:request.Arguments,
  directory:request.Directory,log:request.Log,elapsedMs:25,exitCode:0,timedOut:false,jobDrained:true,totalProcesses:1,
  observedProcesses:[{processId:123,imagePath:request.Executable,queryError:0}],
  memory:{metric:'private-commit',peakProcessMemoryBytes:1000,peakJobMemoryBytes:2000}};
 assert.equal(measurement.validateRun(value,request),value);
 const invalid=[{...value,executable:'C:/other.exe'},{...value,arguments:['wrong']},{...value,directory:'C:/other'},
  {...value,log:'C:/other.log'},{...value,elapsedMs:NaN},{...value,elapsedMs:Infinity},{...value,elapsedMs:-1},
  {...value,elapsedMs:'25'},{...value,totalProcesses:0},{...value,totalProcesses:'1'},{...value,exitCode:-1},
  {...value,timedOut:'false'},{...value,jobDrained:false},{...value,observedProcesses:[]},
  {...value,memory:{...value.memory,peakJobMemoryBytes:0}},{...value,memory:{...value.memory,peakProcessMemoryBytes:'1000'}},
  {...value,memory:{...value.memory,peakProcessMemoryBytes:Number.MAX_SAFE_INTEGER+1}},
  {...value,memory:{...value.memory,peakJobMemoryBytes:500}}];
 for(const malformed of invalid) assert.throws(()=>measurement.validateRun(malformed,request));
});
test('source snapshots bind all files and empty directories and reject linked inputs',t=>{
 const root=temporary(t);put(root,'original.bin',Buffer.from([0,1,2,255]));fs.mkdirSync(path.join(root,'empty'));
 const first=measurement.snapshot(root);assert.deepEqual(first.directories,['empty']);assert.equal(first.fileCount,1);
 put(root,'original.bin',Buffer.from([0,1,3,255]));assert.notEqual(measurement.snapshot(root).sha256,first.sha256);
 fs.linkSync(path.join(root,'original.bin'),path.join(root,'alias.bin'));assert.throws(()=>measurement.snapshot(root),/links|ordinary/);
});
test('ordinary business manifests remain generic inputs while a recognized bundle selects strict verification',t=>{
 const root=temporary(t);
 for(const content of ['not a JSON document','null',JSON.stringify({schema:'business/v1'})]) {
  put(root,'manifest.json',content);assert.equal(measurement.inputKind(root,measurement.snapshot(root)),'directory');
 }
 put(root,'manifest.json',JSON.stringify({schema:'soui.runtime-bundle/v1'}));
 assert.equal(measurement.inputKind(root,measurement.snapshot(root)),'bundle');
 assert.throws(()=>bundle.verify(root));
});
test('measurement options reject unsafe layouts, collisions, profiles and incomplete requests',{skip:!windowsTools},t=>{
 const root=temporary(t),input=path.join(root,'input');fs.mkdirSync(input);put(input,'file.txt','fixture');
 const valid={inputs:[input],output:path.join(root,'new-output'),tool};
 assert.equal(measurement.validateOptions(valid).extractionRuns,3);
 assert.deepEqual(measurement.validateOptions(valid).profiles.map(profile=>profile.profile),['zip','7z-balanced','7z-small']);
 for(const options of [{...valid,output:input},{...valid,output:path.join(input,'inside')},{...valid,inputs:[input,input]},
  {...valid,profiles:['unknown']},{...valid,profiles:['zip','zip']},{...valid,extractionRuns:0},{...valid,extractionRuns:1.5},
  {...valid,timeoutMs:0},{...valid,inputs:[]},{...valid,tool:path.join(root,'absent.exe')}]) assert.throws(()=>measurement.validateOptions(options));
 assert.throws(()=>measurement.cli(['--input']));assert.throws(()=>measurement.cli(['--unknown','value']));
});
test('Windows Job calibration and real archives bind all three profiles, repeated extraction and bundle restore',{skip:!windowsTools},t=>{
 const root=temporary(t),input=path.join(root,'输入 sample');put(input,'SampleApp.exe','SYNTHETIC EXE: NEVER RUN');
 put(input,'resources/index.html','<!doctype html><main>compressed fixture</main>'.repeat(32));
 const payload=id.payload(input,'SampleApp','webview2');
 const report={schema:'soui.application-size/v1',version:'0.5.0',target:'SampleApp',architecture:'x64',configuration:'Release',
  rendererBackend:'webview2',buildId:'inert-measurement-fixture',sourceSha256:id.hash('fixture'),releaseReady:false,payload};
 const bundled=path.join(root,'bundle');bundle.pack(bundled,[{name:'One',root:input,report},{name:'Two',root:input,report}]);
 const output=path.join(root,'测量 output');
 const actual=measurement.runMeasurement({inputs:[input,bundled],output,tool,extractionRuns:2});
 assert.equal(actual.schema,'soui.compression-measurement/v1');assert.equal(actual.passed,true);assert.equal(actual.coldCache,false);
 assert.equal(actual.calibration.passed,true);assert.equal(actual.calibration.allocatedAndTouchedBytes,32*1024*1024);
 assert.equal(actual.calibration.timeout.timedOut,true);assert.equal(actual.calibration.timeout.expectedTimeout,true);
 assert.ok(actual.runner.identity.sha256);assert.ok(actual.tool.identity.version);assert.equal(actual.inputs.length,2);
 for(const dataset of actual.inputs){
  assert.equal(dataset.sourceSnapshot.sha256,dataset.fixedSnapshot.sha256);assert.equal(dataset.profiles.length,3);
  for(const entry of dataset.profiles){
   assert.equal(entry.passed,true);assert.equal(entry.integrity.passed,true);assert.ok(entry.archive.bytes>0);assert.equal(entry.extractionSummary.count,2);
   assert.ok(entry.compressionRun.totalProcesses>=1);assert.ok(entry.compressionRun.memory.peakJobMemoryBytes>0);
   assert.deepEqual(entry.observedMethod.types,[entry.selection.format]);
   for(const round of entry.extractionRuns){assert.equal(round.passed,true);assert.equal(round.hashVerified,true);
    assert.ok(round.run.totalProcesses>=1);assert.ok(round.run.memory.peakProcessMemoryBytes>0);
    assert.equal(round.snapshot.sha256,dataset.fixedSnapshot.sha256);
    if(dataset.kind==='bundle'){assert.equal(round.bundleVerification.verified,true);assert.equal(round.bundleRestore.restored,true);}
   }
  }
 }
 const failure=measurement.runOwned(actual.runner.executable,path.join(output,'runs'),'intentional-nonzero',actual.runner.executable,
  ['allocate','-1','1'],root,10000);
 assert.equal(failure.passed,false);assert.notEqual(failure.exitCode,0);assert.equal(failure.timedOut,false);
 assert.throws(()=>measurement.summarizeRuns([failure]),/failed/);
 assert.throws(()=>measurement.runMeasurement({inputs:[input],output,tool}),/new directory/);
});
test('PowerShell public measurement entry point forwards profile and timeout failures without overwriting outputs',{skip:!windowsTools},t=>{
 const root=temporary(t),input=path.join(root,'application');put(input,'file.txt','minimal package input');
 const output=path.join(root,'public-output 中文'),script=path.resolve(__dirname,'../../tools/Measure-Compression.ps1');
 const run=args=>spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',script,
  '-InputPath',input,'-OutputRoot',output,'-SevenZipPath',tool,...args],{encoding:'utf8',windowsHide:true,
  env:{...process.env,PSModulePath:path.join(process.env.SystemRoot || 'C:/Windows','System32/WindowsPowerShell/v1.0/Modules')}});
 const result=run(['-Profiles','zip','-ExtractionRuns','1']);assert.equal(result.status,0,result.stdout+result.stderr);
 const report=id.readJson(path.join(output,'measurement.json'));assert.equal(report.passed,true);assert.equal(report.inputs[0].profiles.length,1);
 assert.equal(report.inputs[0].profiles[0].selection.profile,'zip');assert.equal(report.requestedExtractionRuns,1);
 assert.notEqual(run(['-Profiles','unknown']).status,0);assert.notEqual(run(['-TimeoutSeconds','0']).status,0);
 assert.notEqual(run(['-Profiles','zip']).status,0);
 assert.equal(id.readJson(path.join(output,'measurement.json')).passed,true);
});
