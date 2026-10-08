'use strict';
// Read-only verification: no output directory is created and no frames are extracted.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const SOURCE = 'a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550';
const BUILD = '7e000158-98f0-4349-854e-15c581663737';
const required = [
  'guest-identity.txt', 'transfer-observed.tsv', 'harness.log', 'run-exitcode.txt', 'run-complete.txt',
  'transfer-manifest-raw.tsv', 'transfer-entries.tsv', 'transfer-lines.tsv', 'transfer-diagnostics.txt',
  ...['starter-zip-test', 'workbench-zip-test', 'starter-extract', 'workbench-extract'].flatMap(n => [n+'.log', n+'-exitcode.txt']),
  ...['pre', 'post'].flatMap(p => ['payload-'+p+'.log', 'payload-'+p+'-exitcode.txt', 'souistarter-payload-'+p+'.tsv', 'souiworkbench-payload-'+p+'.tsv']),
  ...['starter', 'workbench-default', 'workbench-react'].flatMap(n => [n+'-run.txt', n+'-exitcode.txt', n+'-smoke.log'])
];
const optional = ['starter-navigation.log', 'workbench-default-navigation.log', 'workbench-react-navigation.log'];
function fail(message) {throw new Error(message);}
function validateInput(input) {
  if (input.schema !== 'soui.win7-x86-preview11-compiled-apps-input/v1' || input.version !== '0.3.0-preview.11' ||
      input.buildId !== BUILD || input.sourceSha256 !== SOURCE || input.architecture !== 'x86' || input.configuration !== 'Release' ||
      input.candidateKind !== 'existing-compiled-candidate' || input.sourceReproductionClaim !== false || input.releaseReady !== false ||
      input.appTimeoutMs !== 60000 || input.runs?.length !== 3) fail('Frozen candidate identity is invalid');
  if (input.diagnosticsVersion !== 2 || input.parentFreezeSha256 !== '96d05251d6dd78848306fa3868c671f511c2b9ebc658951bf2f29d8e6a81b5ba') fail('Diagnostic preparation identity differs');
  const expectedRuns = [
    ['starter', 'SouiStarter', '--smoke-test', 'smoke.log'],
    ['workbench-default', 'SouiWorkbench', '--webview-smoke-test --arch=x86 --renderer=cpu', 'webview-smoke.log'],
    ['workbench-react', 'SouiWorkbench', '--webview-smoke-test --arch=x86 --renderer=cpu --react', 'webview-smoke.log']
  ];
  for (let i=0;i<3;i++) {
    const r = input.runs[i];
    if (JSON.stringify([r.name,r.application,r.arguments,r.logName]) !== JSON.stringify(expectedRuns[i])) fail('Run configuration differs');
  }
  for (const [name, count] of [['SouiStarter',71],['SouiWorkbench',475]]) {
    const p=input.packages[name];
    if (!p || p.files.length !== count || p.fileCount !== count || hash(JSON.stringify(p.files,null,2)+'\n') !== p.payloadSha256 ||
        p.files.reduce((sum,f)=>sum+f.bytes,0) !== p.payloadBytes || p.files.filter(f=>f.path.startsWith('resources/legal/')).length !== 5) fail('Invalid full payload manifest: '+name);
    for (const [file, property] of [[name+'.exe','exeSha256'],['node.dll','engineSha256'],['soui.dll','souiDllSha256']]) {
      if (p.files.find(f=>f.path===file)?.sha256 !== p[property]) fail('Manifest identity disagrees: '+file);
    }
    const expectedManifest=Buffer.from('\uFEFF'+p.files.map(f=>`${f.sha256}\t${f.bytes}\t${f.path}`).join('\r\n')+'\r\n','utf8');
    if (hash(expectedManifest)!==p.manifestSha256) fail('Payload TSV identity disagrees');
    for (const [filename,sha,bytes] of [[p.zipName,p.zipSha256,p.zipBytes],[p.manifestName,p.manifestSha256,expectedManifest.length]]) {
      const found=input.transferFiles.find(f=>f.path===filename);
      if (!found || found.sha256!==sha || found.bytes!==bytes) fail('Transfer binding disagrees: '+filename);
    }
  }
  const seen=new Set();
  for (const f of input.transferFiles) {
    if (!/^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(f.path) || !/^[a-f0-9]{64}$/.test(f.sha256) || !Number.isSafeInteger(f.bytes) || f.bytes<0 || seen.has(f.path.toLowerCase())) fail('Invalid transfer file');
    seen.add(f.path.toLowerCase());
  }
  if (input.transferFiles.length!==10 || !seen.has('transfer-files.tsv')) fail('Invalid transfer file set');
}
function decodeSerial(serial) {
  const bytes=Buffer.isBuffer(serial)?serial:Buffer.from(serial,'utf8');
  if (bytes.length>8*1024*1024 || bytes.some(c=>c>127)) fail('Serial is oversized or not ASCII');
  const lines=bytes.toString('ascii').replace(/\r\n/g,'\n').split('\n');
  if (lines.at(-1)==='') lines.pop();
  if (lines[0]!=='SOUI_SERIAL_V1' || lines.at(-1)!=='SOUI_SERIAL_END' || lines.length<4 || (lines.length-2)%2) fail('Incomplete serial framing');
  const decoded=new Map();
  for(let i=1;i<lines.length-1;i+=2) {
    const match=/^FILE\|([a-z0-9][a-z0-9._-]*)\|(0|[1-9]\d*)\|([A-F0-9]{64})$/.exec(lines[i]);
    if (!match || Number(match[2])>1048576 || decoded.has(match[1])) fail('Invalid or duplicate serial header');
    const base64=lines[i+1];
    if (base64.length%4 || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(base64)) fail('Invalid Base64');
    const value=Buffer.from(base64,'base64');
    if (value.toString('base64')!==base64 || value.length!==Number(match[2]) || hash(value).toUpperCase()!==match[3]) fail('Frame length/hash mismatch: '+match[1]);
    decoded.set(match[1],value);
  }
  return decoded;
}
function decodeText(bytes) {
  if(bytes[0]===0xff && bytes[1]===0xfe) return bytes.subarray(2).toString('utf16le');
  return bytes.toString('utf8').replace(/^\uFEFF/,'');
}
function parseRecords(text) {
  const result=new Map();
  for (const line of text.trim().split(/\r?\n/)) {
    const match=/^([^=]+)=(.*)$/.exec(line);
    if(!match || result.has(match[1])) fail('Malformed or duplicate record');
    result.set(match[1],match[2]);
  }
  return result;
}
function compareTsv(text,expected,label) {
  const found=new Map();
  for(const line of text.trim().split(/\r?\n/)) {
    const parts=line.split('\t');
    if(parts.length!==3 || !/^[a-f0-9]{64}$/.test(parts[0]) || !/^(0|[1-9]\d*)$/.test(parts[1]) || found.has(parts[2])) fail('Invalid observed manifest: '+label);
    found.set(parts[2],{bytes:Number(parts[1]),sha256:parts[0]});
  }
  if(found.size!==expected.length) fail('Observed file set differs: '+label);
  for(const f of expected) {
    const actual=found.get(f.path);
    if(!actual || actual.bytes!==f.bytes || actual.sha256!==f.sha256) fail('Observed identity differs: '+label+'/'+f.path);
  }
}
function orderedUnique(events,steps,label) {
  let previous=-1;
  for(const step of steps) {
    const index=events.indexOf(step);
    if(index<=previous || events.lastIndexOf(step)!==index) fail('Missing/duplicate/out-of-order lifecycle: '+label+'/'+step);
    previous=index;
  }
}
function verifyTransferDiagnostics(decoded,input) {
  const raw=decoded.get('transfer-manifest-raw.tsv');
  const manifest=input.transferFiles.find(f=>f.path==='transfer-files.tsv');
  if(raw.length!==manifest.bytes || hash(raw)!==manifest.sha256) fail('Diagnostic raw manifest differs');
  const records=input.transferFiles.filter(f=>f.path!=='transfer-files.tsv');
  const expected=Buffer.from('\uFEFF'+records.map(f=>`${f.sha256}\t${f.bytes}\t${f.path}`).join('\r\n')+'\r\n','utf8');
  if(!raw.equals(expected)) fail('Diagnostic raw manifest contents differ');
  const diag=parseRecords(decodeText(decoded.get('transfer-diagnostics.txt')));
  const exact={DiagnosticsVersion:'2',ManifestBytes:String(raw.length),ManifestSha256:hash(raw),
    ManifestPrefixHex:[...raw.subarray(0,64)].map(b=>b.toString(16).padStart(2,'0').toUpperCase()).join('-'),
    RawLineCount:String(records.length),FilteredLineCount:String(records.length),
    EnumeratedCount:String(input.transferFiles.length),DirectoryCount:'0',ExpectedCount:String(input.transferFiles.length),
    FilesType:'System.Object[]',FilteredLinesType:'System.Object[]'};
  for(const[k,v]of Object.entries(exact)) if(diag.get(k)!==v) fail('Diagnostic count/metadata differs: '+k);
  if(diag.size!==14 || !/^[A-Za-z]:\\[^\r\n]*$/.test(diag.get('Transfer')||'') ||
    !/^[2-5]\.\d+(?:\.\d+){0,2}$/.test(diag.get('PowerShellVersion')||'') ||
    !/^\d+\.\d+\.\d+(?:\.\d+)?$/.test(diag.get('ClrVersion')||'')) fail('Diagnostic environment missing');
  const from64=value=>{
    const bytes=Buffer.from(value,'base64');
    if(bytes.toString('base64')!==value) fail('Diagnostic Base64 differs');
    return bytes.toString('utf8');
  };
  const entries=decodeText(decoded.get('transfer-entries.tsv')).trim().split(/\r?\n/);
  if(entries.length!==input.transferFiles.length) fail('Diagnostic enumeration count differs');
  const seen=new Set();
  for(let i=0;i<entries.length;i++) {
    const f=entries[i].split('\t');
    if(f.length!==7 || f[0]!==String(i) || f[4]!=='False') fail('Diagnostic enumeration invalid');
    const name=from64(f[1]),full=from64(f[2]),attributes=from64(f[3]),type=from64(f[6]);
    const file=input.transferFiles.find(p=>p.path===name);
    if(!file || seen.has(name) || f[5]!==String(file.bytes) || type!=='System.IO.FileInfo' || !attributes ||
      full.toLowerCase()!==path.win32.join(diag.get('Transfer'),name).toLowerCase()) fail('Diagnostic enumeration identity differs');
    seen.add(name);
  }
  const lines=decodeText(decoded.get('transfer-lines.tsv')).trim().split(/\r?\n/);
  if(lines.length!==records.length) fail('Diagnostic manifest line count differs');
  for(let i=0;i<lines.length;i++) {
    const f=lines[i].split('\t'),expectedLine=`${records[i].sha256}\t${records[i].bytes}\t${records[i].path}`;
    if(f.length!==4 || f[0]!==String(i) || f[1]!==String(expectedLine.length) || f[2]!=='3' || from64(f[3])!==expectedLine) fail('Diagnostic parsed line differs');
  }
}
function verify(serial,input) {
  validateInput(input);
  const decoded=decodeSerial(serial);
  for(const name of required) if(!decoded.has(name)) fail('Required raw file missing: '+name);
  for(const name of decoded.keys()) if(!required.includes(name) && !optional.includes(name)) fail('Unexpected raw file: '+name);
  const text=name=>decodeText(decoded.get(name));
  const guest=parseRecords(text('guest-identity.txt'));
  for(const [key,value] of [['Version','6.1.7601'],['ServicePackMajorVersion','1'],['OSArchitecture','32-bit'],['PointerBytes','4']]) {
    if(guest.get(key)!==value) fail('Guest identity differs: '+key);
  }
  if(!/^[2-5]$/.test(guest.get('PowerShellMajor')||'') || !/Windows 7/.test(guest.get('Caption')||'')) fail('Guest PowerShell/OS identity missing');
  verifyTransferDiagnostics(decoded,input);
  compareTsv(text('transfer-observed.tsv'),input.transferFiles,'transfer');
  for(const name of required.filter(n=>n.endsWith('-exitcode.txt'))) if(text(name).trim()!=='0') fail('Nonzero/invalid exit code: '+name);
  for(const name of ['starter-zip-test','workbench-zip-test','starter-extract','workbench-extract']) {
    if(!text(name+'.log').includes('Everything is Ok')) fail('Archive operation incomplete: '+name);
  }
  for(const phase of ['pre','post']) {
    for(const name of ['SouiStarter','SouiWorkbench']) {
      compareTsv(text(name.toLowerCase()+'-payload-'+phase+'.tsv'),input.packages[name].files,name+'/'+phase);
      if(!text('payload-'+phase+'.log').includes(name+'='+input.packages[name].fileCount+',legal=5,all-hashes=matched')) fail('Payload/notice confirmation missing');
    }
    if(!text('payload-'+phase+'.log').includes('Payload '+phase+' complete')) fail('Payload verification incomplete');
  }
  for(const run of input.runs) {
    const record=parseRecords(text(run.name+'-run.txt'));
    const exact={Name:run.name,Application:run.application,Arguments:run.arguments,TimeoutMs:'60000',TimedOut:'False',RemainingProcess:'False',ExitCode:'0'};
    for(const [key,value] of Object.entries(exact)) if(record.get(key)!==value) fail('Run metadata differs: '+run.name+'/'+key);
    if(record.size!==9 || !/^[1-9]\d*$/.test(record.get('PID')||'') || !Number.isFinite(Date.parse(record.get('StartUtc')))) fail('Missing process identity: '+run.name);
    const events=text(run.name+'-smoke.log').trim().split(/\r?\n/);
    if(events[0]!=='process-start' || events.some(e=>/timeout|^E_|failed|error/i.test(e) && !e.startsWith('document='))) fail('Smoke failure marker: '+run.name);
    if(run.name==='starter') {
      orderedUnique(events,['process-start','host-started','application-ready','host-close-complete'],run.name);
      if(events.at(-1)!=='host-close-complete') fail('Starter terminal close missing');
    } else {
      orderedUnique(events,['process-start','window-constructed','host-started','application-ready','host-close-start',
        'adapter-unmount-start','adapter-bridge-shutdown','adapter-events-drained','adapter-webview-destroyed','host-close-complete','window-destroyed'],run.name);
      const ready=events.map((e,i)=>/^document=ready,bridge=ready,.*probe=.+/.test(e)?i:-1).filter(i=>i>=0);
      if(ready.length!==1 || ready[0]>=events.indexOf('application-ready') || events.at(-1)!=='window-destroyed') fail('Workbench ready/terminal close invalid: '+run.name);
    }
  }
  const expectedTrace=['transfer verified','payload pre verified','starter exit=0','workbench-default exit=0','workbench-react exit=0','payload post verified'];
  if(text('harness.log').trim().replace(/\r\n/g,'\n')!==expectedTrace.join('\n')) fail('Harness sequence incomplete');
  if(text('run-complete.txt').trim()!==`version=0.3.0-preview.11;buildId=${BUILD};source=${SOURCE};apps=3;status=completed`) fail('Candidate completion binding differs');
  return {status:'verified-functional-smoke-evidence',version:input.version,buildId:BUILD,sourceSha256:SOURCE,
    rawFiles:decoded.size,applications:3,guest:'Windows 7 SP1 x86',releaseReady:false,sourceReproductionClaim:false};
}
module.exports={verify,decodeSerial,required,optional};
if(require.main===module) {
  try {
    if(process.argv.length!==3) fail('Usage: node Verify-AppSerial.cjs <absolute-raw-serial-file>');
    const input=JSON.parse(fs.readFileSync(path.join(__dirname,'input.json'),'utf8'));
    const result=verify(fs.readFileSync(process.argv[2]),input);
    process.stdout.write(JSON.stringify(result,null,2)+'\n');
  } catch(error) {process.stderr.write(error.message+'\n');process.exitCode=1;}
}
