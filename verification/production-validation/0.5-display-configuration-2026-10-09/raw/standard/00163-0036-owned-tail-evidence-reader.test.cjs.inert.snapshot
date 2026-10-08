'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const {readTailEvidence, validateTailImagePin} = require('../../tools/internal/owned-tail-evidence.cjs');
const {fixture, digest} = require('./tail-evidence-fixtures.cjs');
function read(value) { const raw = value.raw(); return readTailEvidence(raw, {context: value.context, reportSha256: digest(raw)}); }
test('synthetic exact uint64 disk identity binds a single tail without changing generic failure', () => {
  const value = fixture(); const result = read(value);
  assert.equal(result.accepted, true); assert.equal(result.diskBytesOnly, true);
  assert.equal(result.member.creationFileTime, '134359391934583654');
  assert.equal(result.pin.fileId, '18446744073709551615');
  assert.equal(value.rows.at(-1).passed, false);
});
test('legacy unpinned records are readable but cannot be retroactively accepted', () => {
  const value = fixture(); value.rows = [value.rows[0], value.rows.at(-1)];
  const raw = Buffer.from(value.rows.map(JSON.stringify).join('\n') + '\n');
  const result = readTailEvidence(raw, {context: {...value.context, tailImagePin: undefined}, reportSha256: digest(raw)});
  assert.equal(result.accepted, false); assert.equal(result.available, false);
});
const mutations = {
  'nonce differs': f => { f.rows[1].nonce = '0'.repeat(32); },
  'sequence missing': f => { f.rows.find(r => r.event === 'snapshot.begin').sequence = 90; },
  'duplicate pin': f => { f.rows.splice(2, 0, {...f.rows[1]}); },
  'numeric high FILETIME': f => { f.rows[0].creationFileTime = 134359391934214888; },
  'one tick identity reuse': f => { f.rows.find(r => r.event === 'cleanup.member').creationFileTime = '134359391934583655'; },
  'overflow uint64': f => { f.rows[1].fileId = '18446744073709551616'; },
  'noncanonical decimal': f => { f.rows[1].fileId = '01'; },
  'pin after subject creation': f => { f.rows[1].observedBeforeLaunchUtcFileTime = '134359391934214889'; },
  'different context SHA': f => { f.context.tailImagePin.sha256 = 'e'.repeat(64); },
  'different disk id': f => { f.rows.find(r => r.event === 'snapshot.member').diskObject.fileId = '18446744073709551614'; },
  'different canonical path': f => { f.rows.find(r => r.event === 'snapshot.member').diskObject.canonicalPath += '.copy'; },
  'unknown image': f => { f.rows.find(r => r.event === 'snapshot.member').pinMatched = false; },
  'index hole': f => { f.rows.find(r => r.event === 'snapshot.member').index = 1; },
  'second set differs': f => { f.rows.find(r => r.event === 'snapshot.end').secondPids = [43]; },
  'duplicate PID': f => { f.rows.find(r => r.event === 'snapshot.end').pids = [42,42]; },
  'accounting changed': f => { f.rows.find(r => r.event === 'snapshot.end').accountingAfter.totalProcesses++; },
  'last accounting total differs': f => { f.rows.find(r => r.event === 'pre-cleanup-confirm').accounting.totalProcesses++; },
  'identity lost at last check': f => { f.rows.find(r => r.event === 'pre-cleanup-confirm').identitiesMatch = false; },
  'supervisor argv pin differs': f => { f.context.runnerArgs[10] += '.copy'; },
  'new process during cleanup': f => { f.rows.find(r => r.event === 'cleanup.confirm').accounting.totalProcesses++; },
  'only one empty observation': f => { f.rows.splice(f.rows.findIndex(r => r.event === 'cleanup.confirm'), 1); },
  'held handle not signaled': f => { f.rows.find(r => r.event === 'cleanup.member').signaled = false; },
  'terminal total changed': f => { f.rows.at(-1).totalProcesses++; },
  'terminal reference differs': f => { f.rows.at(-1).preCleanupSnapshotSequence++; },
  'native true flag cannot wash sticky error': f => { f.rows.find(r => r.event === 'snapshot.end').stickyFailure = true; },
  'native true flag cannot replace listing': f => { f.rows.splice(f.rows.findIndex(r => r.pass === 'second'), 1); },
  'STOP is not compilation': f => { f.rows.at(-1).reason = 'supervisor-stop'; },
  'timeout is not compilation': f => { f.rows.at(-1).reason = 'timeout'; },
  'root fails': f => { f.rows.at(-1).rootExitCode = 1; },
  'generic true tail is contradictory': f => { f.rows.at(-1).passed = true; },
  'unknown schema': f => { f.rows.find(r => r.event === 'snapshot.begin').schema += '-future'; },
  'unknown generic event': f => { f.rows.splice(2,0,{event:'invented-enumeration'}); },
  'enumeration unavailable stays rejected': f => { f.rows.splice(2,0,{event:'member-unavailable',pid:42,win32Error:5}); },
  'earlier image read failure stays rejected': f => { f.rows.find(r => r.event === 'owned-member').imageReadError = 5; },
};
for (const [name, mutate] of Object.entries(mutations)) test(name, () => {
  const value = fixture(); mutate(value); assert.equal(read(value).accepted, false);
});
test('truncated/blank/invalid UTF8 lines and mismatching raw SHA refuse acceptance', () => {
  const value = fixture(); const raw = value.raw();
  for (const bytes of [raw.subarray(0,-1),Buffer.concat([raw,Buffer.from('\n')]),Buffer.concat([Buffer.from([255]),raw])])
    assert.equal(readTailEvidence(bytes,{context:value.context,reportSha256:digest(bytes)}).accepted,false);
  assert.equal(readTailEvidence(raw,{context:value.context,reportSha256:'e'.repeat(64)}).accepted,false);
});
test('incomplete or aliased caller pin cannot launch an opt-in subject', () => {
  for (const pin of [{}, {image:'C:\\x.exe'}, {...fixture().context.tailImagePin,nonce:'A'.repeat(32)},
    {...fixture().context.tailImagePin,image:'C:\\x\\..\\x.exe'}, {...fixture().context.tailImagePin,image:'\\\\server\\x.exe'}])
    assert.throws(() => validateTailImagePin(pin));
});
test('duplicate JSON fields are rejected even if the last value would validate',()=>{
  const f=fixture();const raw=Buffer.from(f.raw().toString().replace('"fileId":"18446744073709551615"','"fileId":"1","fileId":"18446744073709551615"'));
  assert.equal(readTailEvidence(raw,{context:f.context,reportSha256:digest(raw)}).accepted,false);
});
test('a successful partial listing must grow to a complete list before any decision',()=>{
  const f=fixture();const index=f.rows.findIndex(r=>r.pass==='first');const original=f.rows[index];
  f.rows.splice(index,0,{...original,assigned:2,listed:1});original.growth=1;original.capacity=8;
  reindex(f);assert.equal(read(f).accepted,true);
  original.growth=2;assert.equal(read(f).accepted,false);
});
function reindex(f){let sequence=0,decisive=null;for(const row of f.rows)if(row.schema==='soui.owned-tail-snapshot/v1'){
  row.sequence=++sequence;if(row.event==='snapshot.end'&&row.eligible)decisive=row.sequence;
  if(row.snapshotSequence!==undefined)row.snapshotSequence=decisive;
  if(row.event==='tail-evidence')row.preCleanupSnapshotSequence=decisive;
}f.rows.at(-1).preCleanupSnapshotSequence=decisive;}
test('ordinary active-count change can retry once while every error remains sticky',()=>{
  const f=fixture();const begin=f.rows.findIndex(r=>r.event==='snapshot.begin'),end=f.rows.findIndex(r=>r.event==='snapshot.end');
  const first=structuredClone(f.rows.slice(begin,end+1));first[0].accountingBefore.activeProcesses=2;
  first.at(-1).complete=false;first.at(-1).eligible=false;
  for(const row of f.rows)if(row.phase==='pre-cleanup'||row.phase==='pre-cleanup-confirm')row.attempt=2;
  f.rows.splice(begin,0,...first);reindex(f);assert.equal(read(f).accepted,true);
  first.at(-1).stickyFailure=true;assert.equal(read(f).accepted,false);
});

function history(value, ...rows) {
  value.rows.splice(value.rows.findIndex(row => row.event === 'root-exit'), 0, ...rows);
  return value;
}
const historicalMember = (extra = {}) => ({event:'owned-member',pid:43,
  creationFileTime:'134359391934583655',image:'C:\\Windows\\System32\\cmd.exe',...extra});
test('pre-root noncandidate image and enumeration diagnostics remain visible without certifying the tail',()=>{
  const f=history(fixture(),historicalMember({image:null,imageReadError:5}),
    {event:'member-unavailable',pid:44,operation:'read-identity',win32Error:87},
    {event:'member-no-longer-owned',pid:44});
  const result=read(f);
  assert.equal(result.accepted,true,result.failure);
  assert.deepEqual(result.historicalDiagnostics,{imageReadFailures:1,unavailableMembers:1,noLongerOwnedMembers:1,pidReuses:0});
  assert.equal(result.member.pid,42);
  assert.equal(f.rows.at(-1).passed,false);
});
test('pre-root noncandidate PID reuse refers to different historical processes only',()=>{
  const f=history(fixture(),historicalMember(),historicalMember({creationFileTime:'134359391934583656',
    image:'C:\\Windows\\System32\\conhost.exe'}));
  const result=read(f);
  assert.equal(result.accepted,true,result.failure);
  assert.equal(result.historicalDiagnostics.pidReuses,1);
  assert.equal(result.member.creationFileTime,'134359391934583654');
});
const malformedHistory={
  'null image without error':{image:null},
  'null image with string error':{image:null,imageReadError:'5'},
  'null image with zero error':{image:null,imageReadError:0},
  'null image with fractional error':{image:null,imageReadError:5.5},
  'null image with overflow error':{image:null,imageReadError:4294967296},
  'known image carrying error':{imageReadError:5},
  'non-string image':{image:12,imageReadError:5},
  'numeric FILETIME':{creationFileTime:134359391934583655},
  'overflow FILETIME':{creationFileTime:'18446744073709551616'},
  'noncanonical FILETIME':{creationFileTime:'01'},
  'zero PID':{pid:0},
  'string PID':{pid:'43'},
  'fractional PID':{pid:43.5},
  'overflow PID':{pid:4294967296},
  'relative image':{image:'cmd.exe'},
  'unrecognised field':{claimedExited:true},
};
for(const [name,extra] of Object.entries(malformedHistory))test('historical diagnostic rejects '+name,()=>{
  assert.equal(read(history(fixture(),historicalMember(extra))).accepted,false);
});
for(const row of [
  {event:'member-unavailable',pid:43,win32Error:'87'},
  {event:'member-unavailable',pid:43,win32Error:0},
  {event:'member-unavailable',pid:43,win32Error:4294967296},
  {event:'member-unavailable',pid:43,win32Error:5,operation:'invented-operation'},
  {event:'member-unavailable',pid:43,win32Error:5,claimedExited:true},
  {event:'member-no-longer-owned',pid:0},
  {event:'member-no-longer-owned',pid:43,win32Error:5},
])test('invalid historical native diagnostic shape '+JSON.stringify(row),()=>{
  assert.equal(read(history(fixture(),row)).accepted,false);
});
for(const protectedPid of [41,42]){
  for(const kind of ['image','unavailable','membership','reuse'])test('historical '+kind+' cannot hide root/tail PID '+protectedPid,()=>{
    const f=fixture();const original=protectedPid===41?f.rows[0].creationFileTime:'134359391934583654';
    let rows;
    if(kind==='image')rows=[historicalMember({pid:protectedPid,creationFileTime:original,image:null,imageReadError:31})];
    else if(kind==='unavailable')rows=[{event:'member-unavailable',pid:protectedPid,win32Error:87}];
    else if(kind==='membership')rows=[{event:'member-no-longer-owned',pid:protectedPid}];
    else rows=[historicalMember({pid:protectedPid,creationFileTime:(BigInt(original)+1n).toString()})];
    assert.equal(read(history(f,...rows)).accepted,false);
  });
}
for(const row of [historicalMember({image:null,imageReadError:5}),
  {event:'member-unavailable',pid:43,win32Error:87},{event:'member-no-longer-owned',pid:43}])
  test('post-root diagnostics still reject '+row.event,()=>{
    const f=fixture();f.rows.splice(f.rows.findIndex(value=>value.event==='root-exit')+1,0,row);
    assert.equal(read(f).accepted,false);
  });
test('PID reuse across or after root exit cannot be treated as historical',()=>{
  const f=history(fixture(),historicalMember());
  f.rows.splice(f.rows.findIndex(row=>row.event==='root-exit')+1,0,
    historicalMember({creationFileTime:'134359391934583656'}));
  assert.equal(read(f).accepted,false);
});
test('historical PID reuse must advance the full FILETIME',()=>{
  const f=history(fixture(),historicalMember(),historicalMember({creationFileTime:'134359391934583653'}));
  assert.equal(read(f).accepted,false);
});
for(const event of ['snapshot.begin','cleanup.member'])test('generic history cannot appear after '+event,()=>{
  const f=fixture();f.rows.splice(f.rows.findIndex(row=>row.event===event)+1,0,historicalMember({image:null,imageReadError:5}));
  assert.equal(read(f).accepted,false);
});
test('safe historical diagnostics cannot wash a snapshot sticky error or cleanup identity loss',()=>{
  for(const event of ['snapshot.end','cleanup.member']){
    const f=history(fixture(),historicalMember({image:null,imageReadError:5}));
    const row=f.rows.find(value=>value.event===event);
    if(event==='snapshot.end')row.stickyFailure=true;
    else row.creationFileTime='134359391934583655';
    assert.equal(read(f).accepted,false);
  }
});
