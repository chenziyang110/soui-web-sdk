import test from 'node:test';
import assert from 'node:assert/strict';
import {SouiNativeClientImpl,SouiBridgeError} from '../../sdk/web/dist/index.js';
const options={sessionId:'test-session',origin:'app://test',token:'test-token',requestTimeoutMs:50};
const client=reply=>new SouiNativeClientImpl({request:async text=>reply(JSON.parse(text)),onEvent:()=>()=>{}},options);
test('custom method sends the scoped envelope and returns native data',async()=>{
  let seen;
  const sdk=client(request=>{seen=request;return JSON.stringify({requestId:request.requestId,ok:true,result:{message:'native'}})});
  assert.deepEqual(await sdk.call('app.custom',{name:'SDK'}),{message:'native'});
  assert.equal(seen.sessionId,options.sessionId);assert.equal(seen.token,options.token);assert.equal(seen.method,'app.custom');
});
test('a response from another request is rejected',async()=>{
  await assert.rejects(client(()=>JSON.stringify({requestId:'other',ok:true})).call('app.custom',{}),error=>error instanceof SouiBridgeError && error.code==='E_INVALID_RESPONSE');
});
test('malformed JSON becomes a structured response error',async()=>{
  await assert.rejects(client(()=>'{broken').call('app.custom',{}),{code:'E_INVALID_RESPONSE'});
});
test('native denials retain the native error code',async()=>{
  await assert.rejects(client(request=>JSON.stringify({requestId:request.requestId,ok:false,error:{code:'E_UNAUTHORIZED',message:'Denied'}})).call('app.custom',{}),{code:'E_UNAUTHORIZED'});
});

test('valid JSON with an invalid response shape produces a structured error',async()=>{
  for(const value of [null,[],true,'response']) {
    await assert.rejects(client(()=>JSON.stringify(value)).call('app.custom',{}),{code:'E_INVALID_RESPONSE'});
  }
  await assert.rejects(client(request=>JSON.stringify({requestId:request.requestId,ok:'true'})).call('app.custom',{}),{code:'E_INVALID_RESPONSE'});
  await assert.rejects(client(request=>JSON.stringify({requestId:request.requestId,ok:false,error:{code:42,message:[]}})).call('app.custom',{}),{code:'E_INVALID_RESPONSE'});
});
test('timeout is bounded without claiming native cancellation',async()=>{
  const sdk=new SouiNativeClientImpl({request:()=>new Promise(()=>{}),onEvent:()=>()=>{}},options);
  await assert.rejects(sdk.call('app.custom',{}),{code:'E_TIMEOUT'});
});
