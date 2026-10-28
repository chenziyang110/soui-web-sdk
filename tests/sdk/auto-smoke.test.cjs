'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const {settings,lifecycle,validateReport}=require('../../tools/internal/auto-smoke.cjs');
// Authored parser fixtures, never a receipt for an actual application run.
const subject={pid:123,creationFileTime:'134000000000000000'};
const log=['process-start;pid=123;creationFileTime=134000000000000000',
  'backend-selected;backend=webview2;reason=runtime-available',
  'runtime-discovery;attempted=true;available=true;hresult=0;version=fixture','host-started;runtime=fixture',
  'action-roundtrip;method=app.greet;calls=1','application-ready','host-close-complete','process-complete;exit=0',''].join('\n');
test('auto log parser requires actual expected identity/backend and ordered roundtrip',()=>{
  assert.equal(lifecycle(log,subject,'webview2').backend,'webview2');
  for(const changed of [log.slice(0,-1),log.replace('pid=123','pid=124'),log.replace('backend=webview2','backend=miniblink'),
    log.replace('action-roundtrip;method=app.greet;calls=1\n',''),log.replace('application-ready\n',''),log.replace('host-close-complete\n',''),
    log.replace('action-roundtrip;method=app.greet;calls=1\napplication-ready','application-ready\naction-roundtrip;method=app.greet;calls=1'),
    log.replace('application-ready','application-ready\napplication-ready'),
    log.replace('hresult=0','hresult=-2147024894'),log.replace('process-complete;exit=0','process-complete;exit=3'),
    log.replace('host-close-complete','error;E_FAILURE\nhost-close-complete')])
    assert.throws(()=>lifecycle(changed,subject,'webview2'));
});
test('auto command parsing rejects unsupported or ambiguous invocation',()=>{
  for(const args of [[],['run','arm64','Release','F:/fixture'],['run','x64','Debug','relative'],
    ['run','x64','Release','F:/fixture','--fake-backend','miniblink']])assert.throws(()=>settings(args));
});
test('claimed auto success without current build binding cannot authorize packaging',()=>{
  const build={version:'fixture',buildId:'current',source:{sha256:'current'},architecture:'x64',configuration:'Release'};
  for(const report of [null,{}, {schema:'soui.auto-smoke/v1',passed:true,releaseReady:false,finishedAt:'fixture',
    version:'fixture',buildId:'old',sourceSha256:'current',architecture:'x64',configuration:'Release'}])
    assert.throws(()=>validateReport('F:/fixture',report,build,{}),/current build/);
});
