'use strict';
// Synthetic verifier fixtures only: this file never launches guest/application code.
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const assert=require('node:assert/strict');
const {verify,required}=require('./Verify-AppSerial.cjs');
const root=__dirname;
const input=JSON.parse(fs.readFileSync(path.join(root,'input.json'),'utf8'));
const label=process.argv[2]||'self-test-only';
if(!/^self-test-only(?:-v[1-9]\d*)?$/.test(label)) throw new Error('Invalid synthetic test directory name');
const work=path.join(root,label);
fs.mkdirSync(work); // Never overwrite an earlier set of synthetic samples.
const sha=b=>crypto.createHash('sha256').update(b).digest('hex').toUpperCase();
const values=new Map();
const put=(name,text)=>values.set(name,Buffer.from(text,'utf8'));
const tsv=records=>records.map(f=>`${f.sha256}\t${f.bytes}\t${f.path}`).join('\r\n')+'\r\n';
put('guest-identity.txt','Version=6.1.7601\r\nServicePackMajorVersion=1\r\nOSArchitecture=32-bit\r\nPointerBytes=4\r\nPowerShellMajor=2\r\nCaption=Microsoft Windows 7 Enterprise\r\n');
put('transfer-observed.tsv',tsv(input.transferFiles));
const diagnosticRecords=input.transferFiles.filter(f=>f.path!=='transfer-files.tsv');
const manifestBytes=Buffer.from('\uFEFF'+tsv(diagnosticRecords),'utf8');
const b64=text=>Buffer.from(text,'utf8').toString('base64');
values.set('transfer-manifest-raw.tsv',manifestBytes);
put('transfer-entries.tsv',input.transferFiles.map((f,i)=>`${i}\t${b64(f.path)}\t${b64('D:\\soui-input\\'+f.path)}\t${b64('Archive')}\tFalse\t${f.bytes}\t${b64('System.IO.FileInfo')}`).join('\r\n')+'\r\n');
put('transfer-lines.tsv',diagnosticRecords.map((f,i)=>{
  const line=`${f.sha256}\t${f.bytes}\t${f.path}`;
  return `${i}\t${line.length}\t3\t${b64(line)}`;
}).join('\r\n')+'\r\n');
put('transfer-diagnostics.txt',Object.entries({DiagnosticsVersion:'2',Transfer:'D:\\soui-input',PowerShellVersion:'2.0',ClrVersion:'2.0.50727.5420',
  ManifestBytes:String(manifestBytes.length),ManifestSha256:sha(manifestBytes).toLowerCase(),
  ManifestPrefixHex:[...manifestBytes.subarray(0,64)].map(b=>b.toString(16).padStart(2,'0').toUpperCase()).join('-'),
  RawLineCount:String(diagnosticRecords.length),FilteredLineCount:String(diagnosticRecords.length),EnumeratedCount:String(input.transferFiles.length),
  DirectoryCount:'0',ExpectedCount:String(input.transferFiles.length),FilesType:'System.Object[]',FilteredLinesType:'System.Object[]'
}).map(([k,v])=>k+'='+v).join('\r\n')+'\r\n');
for(const name of required.filter(n=>n.endsWith('-exitcode.txt'))) put(name,'0\r\n');
for(const name of ['starter-zip-test','workbench-zip-test','starter-extract','workbench-extract']) put(name+'.log','Everything is Ok\r\n');
for(const phase of ['pre','post']) {
  put('payload-'+phase+'.log',`SouiStarter=71,legal=5,all-hashes=matched\r\nSouiWorkbench=475,legal=5,all-hashes=matched\r\nPayload ${phase} complete\r\n`);
  for(const name of ['SouiStarter','SouiWorkbench']) put(name.toLowerCase()+'-payload-'+phase+'.tsv','\uFEFF'+tsv(input.packages[name].files));
}
for(const run of input.runs) {
  put(run.name+'-run.txt',`Name=${run.name}\r\nApplication=${run.application}\r\nArguments=${run.arguments}\r\nPID=1234\r\nStartUtc=2026-09-26T00:00:00.0000000Z\r\nTimeoutMs=60000\r\nTimedOut=False\r\nRemainingProcess=False\r\nExitCode=0\r\n`);
  put(run.name+'-smoke.log',run.name==='starter'
    ? 'process-start\r\nhost-started\r\napplication-ready\r\nhost-close-complete\r\n'
    : 'process-start\r\nwindow-constructed\r\nhost-started\r\nwindow-show-update-returned\r\ntimer-waiting-document\r\ndocument=waiting,bridge=waiting,draw_interval_ms=-1\r\ndocument=ready,bridge=ready,draw_interval_ms=17,probe=script/json/event/error/local/proxy/promise/symbol/reflect/vue/reactivity/dom/bridge\r\napplication-ready\r\nhost-close-start\r\nadapter-unmount-start\r\nadapter-bridge-shutdown\r\nadapter-events-drained\r\nadapter-webview-destroyed\r\nhost-close-complete\r\nwindow-destroyed\r\n');
}
put('harness.log','transfer verified\r\npayload pre verified\r\nstarter exit=0\r\nworkbench-default exit=0\r\nworkbench-react exit=0\r\npayload post verified\r\n');
put('run-complete.txt',`version=${input.version};buildId=${input.buildId};source=${input.sourceSha256};apps=3;status=completed\r\n`);
assert.deepEqual([...values.keys()].sort(),[...required].sort());
function serial(entries) {
  return Buffer.from(['SOUI_SERIAL_V1',...[...entries].sort(([a],[b])=>a.localeCompare(b)).flatMap(([name,b])=>[`FILE|${name}|${b.length}|${sha(b)}`,b.toString('base64')]),'SOUI_SERIAL_END',''].join('\n'),'ascii');
}
function replace(entries,name,fn) {
  entries.set(name,Buffer.from(fn(entries.get(name).toString('utf8')),'utf8'));
}
function tree(dir,relative='') {
  return fs.readdirSync(path.join(dir,relative),{withFileTypes:true}).flatMap(e=>{
    const p=path.join(relative,e.name);return e.isDirectory()?tree(dir,p):[p];
  }).sort();
}
const passed=[];
function check(name,raw,accepted) {
  const file=path.join(work,name+'.synthetic-serial.txt');
  fs.writeFileSync(file,raw,{flag:'wx'});
  const before=tree(root);
  let result,error;
  try {result=verify(fs.readFileSync(file),input);} catch(e) {error=e;}
  assert.equal(Boolean(error),!accepted,`${name}: ${error?.message || 'unexpected acceptance'}`);
  assert.deepEqual(tree(root),before,`${name}: verifier modified file set`);
  assert.equal(fs.existsSync(path.join(root,'results')),false,'Verifier polluted a results archive');
  passed.push({case:name,accepted,reason:error?.message || 'synthetic complete fixture accepted'});
}
check('complete',serial(values),true);
const negatives=[
  ['volume-root-instead-of-input-subdir',e=>replace(e,'transfer-diagnostics.txt',v=>v.replace('Transfer=D:\\soui-input','Transfer=D:\\'))],
  ['unexpected-input-subdir',e=>replace(e,'transfer-diagnostics.txt',v=>v.replace('Transfer=D:\\soui-input','Transfer=D:\\other-input'))],
  ['extra-input-file-even-with-consistent-count',e=>{
    replace(e,'transfer-diagnostics.txt',v=>v.replace('EnumeratedCount=10','EnumeratedCount=11'));
    replace(e,'transfer-entries.tsv',v=>v+`10\t${b64('unknown.txt')}\t${b64('D:\\soui-input\\unknown.txt')}\t${b64('Archive')}\tFalse\t0\t${b64('System.IO.FileInfo')}\r\n`);
  }],
  ['recycle-directory-inside-input-still-rejected',e=>{
    replace(e,'transfer-diagnostics.txt',v=>v.replace('EnumeratedCount=10','EnumeratedCount=11').replace('DirectoryCount=0','DirectoryCount=1'));
    replace(e,'transfer-entries.tsv',v=>v+`10\t${b64('$RECYCLE.BIN')}\t${b64('D:\\soui-input\\$RECYCLE.BIN')}\t${b64('Hidden, System, Directory')}\tTrue\t\t${b64('System.IO.DirectoryInfo')}\r\n`);
  }],
  ['missing-transfer-diagnostics',e=>e.delete('transfer-diagnostics.txt')],
  ['wrong-raw-manifest-valid-frame',e=>replace(e,'transfer-manifest-raw.tsv',v=>v.replace(input.transferFiles[0].sha256,'0'.repeat(64)))],
  ['wrong-diagnostic-count-valid-frame',e=>replace(e,'transfer-diagnostics.txt',v=>v.replace('EnumeratedCount=10','EnumeratedCount=11'))],
  ['diagnostic-directory-valid-frame',e=>replace(e,'transfer-entries.tsv',v=>v.replace('\tFalse\t','\tTrue\t'))],
  ['wrong-diagnostic-line-valid-frame',e=>replace(e,'transfer-lines.tsv',v=>v.replace(/\t3\t/,'\t2\t'))],
  ['wrong-diagnostic-name-valid-frame',e=>replace(e,'transfer-entries.tsv',v=>v.replace(b64(input.transferFiles[0].path),b64('unknown.exe')))],
  ['wrong-engine-with-valid-frame-hash',e=>replace(e,'souiworkbench-payload-pre.tsv',v=>v.replace(input.packages.SouiWorkbench.engineSha256,'0'.repeat(64)))],
  ['wrong-legal-with-valid-frame-hash',e=>replace(e,'souistarter-payload-post.tsv',v=>v.replace(input.packages.SouiStarter.files.find(f=>f.path.startsWith('resources/legal/')).sha256,'0'.repeat(64)))],
  ['missing-required-file',e=>e.delete('workbench-react-smoke.log')],
  ['missing-payload-file',e=>replace(e,'souiworkbench-payload-post.tsv',v=>v.split('\r\n').slice(1).join('\r\n'))],
  ['nonzero-exit-with-valid-frame-hash',e=>e.set('workbench-default-exitcode.txt',Buffer.from('7\r\n'))],
  ['missing-close-with-valid-frame-hash',e=>replace(e,'workbench-react-smoke.log',v=>v.replace('adapter-events-drained\r\n',''))],
  ['reordered-close-with-valid-frame-hash',e=>replace(e,'workbench-default-smoke.log',v=>v.replace('adapter-bridge-shutdown\r\nadapter-events-drained','adapter-events-drained\r\nadapter-bridge-shutdown'))],
  ['duplicate-close',e=>replace(e,'workbench-react-smoke.log',v=>v.replace('host-close-complete','host-close-complete\r\nhost-close-complete'))],
  ['missing-ready',e=>replace(e,'starter-smoke.log',v=>v.replace('application-ready\r\n',''))],
  ['wrong-guest',e=>replace(e,'guest-identity.txt',v=>v.replace('6.1.7601','10.0.19045'))],
  ['timeout-even-with-zero-exit',e=>replace(e,'starter-run.txt',v=>v.replace('TimedOut=False','TimedOut=True'))],
  ['wrong-command-line',e=>replace(e,'workbench-react-run.txt',v=>v.replace(' --react',''))],
  ['extra-file',e=>e.set('stale-result.txt',Buffer.from('old'))],
  ['wrong-transfer-script-hash',e=>replace(e,'transfer-observed.tsv',v=>v.replace(input.transferFiles.find(f=>f.path==='run-apps.ps1').sha256,'0'.repeat(64)))]
];
for(const [name,mutate] of negatives) {const e=new Map(values);mutate(e);check(name,serial(e),false);}
check('truncated-end',serial(values).subarray(0,-6),false);
check('corrupted-frame-hash',Buffer.from(serial(values).toString('ascii').replace(/^FILE\|([^\n]+)\|[A-F0-9]{64}$/m,'FILE|$1|'+'0'.repeat(64)),'ascii'),false);
const lines=serial(values).toString('ascii').split('\n');
lines.splice(3,0,lines[1],lines[2]);
check('duplicate-frame',Buffer.from(lines.join('\n'),'ascii'),false);
const report={status:'synthetic-verifier-self-test-passed',syntheticOnly:true,guestExecuted:false,
  inputSha256:sha(fs.readFileSync(path.join(root,'input.json'))).toLowerCase(),
  acceptedFixtures:1,rejectedFixtures:passed.length-1,requiredRawFiles:required.length,
  verifierWritesResults:false,cases:passed,releaseReady:false};
fs.writeFileSync(path.join(root,label==='self-test-only'?'self-test-results.json':label+'-results.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});
process.stdout.write(JSON.stringify(report,null,2)+'\n');
