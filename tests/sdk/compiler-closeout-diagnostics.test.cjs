'use strict';
// Pure retained-stream diagnostics. No compiler, supervisor or process control.
const {test}=require('node:test');
const assert=require('node:assert/strict');
const crypto=require('node:crypto');
const {compilerCloseoutDiagnostics}=require('../../tools/internal/standard-build.cjs');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
function fixture(members=[{event:'owned-member',pid:42,creationFileTime:'134376846734993734',image:'C:\\tools\\vctip.exe'}]) {
  const terminal={event:'terminal',reason:'owned-descendants-remain',rootExitCode:0,activeBeforeCleanup:members.length,
    activeProcesses:0,terminatedOwnedJob:true,closureConfirmed:true,passed:false};
  const rows=[{event:'start'},{event:'root-exit',exitCode:0,elapsedMs:90},
    {event:'job-members',elapsedMs:91,activeProcesses:members.length,totalProcesses:7,limitTerminatedProcesses:0},...members,terminal];
  const raw=Buffer.from(rows.map(row=>JSON.stringify(row)).join('\n')+'\n');
  return {rows,raw,result:{exitCode:0,supervisorExitCode:124,passed:false,terminal,reportSha256:hash(raw)}};
}
test('forced helper cleanup retains false/124 and exact last observed identity without granting acceptance',()=>{
  const v=fixture(),before=structuredClone(v.result),answer=compilerCloseoutDiagnostics(v.raw,v.result);
  assert.equal(answer.rawReportMatchesResult,true);assert.equal(answer.diagnosticOnly,true);
  assert.equal(answer.clientGenericSupervisionPassed,false);assert.equal(answer.supervisorExitCode,124);
  assert.equal(answer.terminatedOwnedJob,true);assert.equal(answer.membersAtCleanupVerified,false);
  assert.equal(answer.lastPostRootJobObservation.elapsedMs,91);
  assert.equal(answer.lastPostRootJobObservation.matchesCleanupCount,true);
  assert.deepEqual(answer.memberObservations,[v.rows[3]]);assert.equal(answer.failure,null);
  assert.equal(Object.hasOwn(answer,'accepted'),false);assert.deepEqual(v.result,before);
});
test('unreadable and no-longer-owned members remain visible alongside identified helpers',()=>{
  const members=[{event:'owned-member',pid:42,creationFileTime:'134376846734993734',image:null,imageReadError:5},
    {event:'member-unavailable',pid:43,win32Error:5,operation:'read-identity'},
    {event:'member-no-longer-owned',pid:44}];
  const v=fixture(members),answer=compilerCloseoutDiagnostics(v.raw,v.result);
  assert.deepEqual(answer.memberObservations,members);assert.equal(answer.membersAtCleanupVerified,false);
});
test('a pre-root enumeration cannot be reported as residual member identities',()=>{
  const v=fixture();v.rows.splice(1,1);v.rows.splice(3,0,{event:'root-exit',elapsedMs:95,exitCode:0});
  v.raw=Buffer.from(v.rows.map(row=>JSON.stringify(row)).join('\n')+'\n');v.result.reportSha256=hash(v.raw);
  const answer=compilerCloseoutDiagnostics(v.raw,v.result);
  assert.equal(answer.rawReportMatchesResult,true);assert.equal(answer.lastPostRootJobObservation,null);
  assert.deepEqual(answer.memberObservations,[]);assert.match(answer.failure,/identities are unknown/);
});
test('different cleanup counts do not turn an earlier generic enumeration into a verified cleanup snapshot',()=>{
  const v=fixture();v.result.terminal.activeBeforeCleanup=2;
  v.raw=Buffer.from(v.rows.map(row=>JSON.stringify(row)).join('\n')+'\n');v.result.reportSha256=hash(v.raw);
  const answer=compilerCloseoutDiagnostics(v.raw,v.result);
  assert.equal(answer.rawReportMatchesResult,true);assert.equal(answer.lastPostRootJobObservation.matchesCleanupCount,false);
  assert.equal(answer.membersAtCleanupVerified,false);assert.equal(answer.clientGenericSupervisionPassed,false);
});
test('missing, truncated, duplicate-key or mismatched raw observations fail diagnostically',()=>{
  for(const mutate of [v=>{v.raw=null;},v=>{v.raw=v.raw.subarray(0,-1);},
    v=>{v.raw=Buffer.from(v.raw.toString().replace('"event":"start"','"event":"bad","event":"start"'));v.result.reportSha256=hash(v.raw);},
    v=>{v.result.reportSha256='f'.repeat(64);},v=>{v.result.terminal={...v.result.terminal,activeBeforeCleanup:99};}]) {
    const v=fixture();mutate(v);const answer=compilerCloseoutDiagnostics(v.raw,v.result);
    assert.equal(answer.rawReportMatchesResult,false);assert.ok(answer.failure);assert.deepEqual(answer.memberObservations,[]);
    assert.equal(answer.clientGenericSupervisionPassed,false);assert.equal(answer.supervisorExitCode,124);
  }
});
test('a naturally completed result needs no invented residual enumeration',()=>{
  const v=fixture([]);v.rows.splice(2,1);Object.assign(v.result.terminal,{reason:'completed',passed:true,terminatedOwnedJob:false});
  Object.assign(v.result,{passed:true,supervisorExitCode:0});
  v.raw=Buffer.from(v.rows.map(row=>JSON.stringify(row)).join('\n')+'\n');v.result.reportSha256=hash(v.raw);
  const answer=compilerCloseoutDiagnostics(v.raw,v.result);
  assert.equal(answer.clientGenericSupervisionPassed,true);assert.equal(answer.rawReportMatchesResult,true);
  assert.equal(answer.lastPostRootJobObservation,null);assert.equal(answer.failure,null);
});
