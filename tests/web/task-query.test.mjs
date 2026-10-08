import test from 'node:test';
import assert from 'node:assert/strict';
import {SouiNativeClientImpl, souiToolDefinitions, readSouiActionContract, validateSouiActionInput} from '../../sdk/web/dist/index.js';
const options={sessionId:'operation-session',origin:'app://operation-query',token:'native-grant',requestTimeoutMs:20};
const snapshot={taskId:'sdk-task-1',state:'succeeded',progress:100,message:'actual result',sequence:4,result:{committed:true}};
function fixture(t,respond=()=>snapshot) {
  const seen=[];
  const sdk=new SouiNativeClientImpl({onEvent:()=>()=>{},request:async raw=>{
    const request=JSON.parse(raw);seen.push(request);
    const result=await respond(request);
    return JSON.stringify({requestId:request.requestId,ok:true,result});
  }},options);
  t.after(()=>sdk.dispose());return {sdk,seen};
}
test('taskId and operation helpers and direct calls preserve one read-only snapshot',async t=>{
  const {sdk,seen}=fixture(t);
  assert.deepEqual(await sdk.query('sdk-task-1'),snapshot);
  assert.deepEqual(await sdk.queryOperation('operation:1-._'),snapshot);
  assert.deepEqual(await sdk.call('task.query',{operationId:'operation:1-._'}),snapshot);
  assert.deepEqual(seen.map(x=>x.params),[{taskId:'sdk-task-1'},{operationId:'operation:1-._'},{operationId:'operation:1-._'}]);
  assert.ok(seen.every(x=>x.method==='task.query'));
});
test('all invalid XOR/ASCII/length shapes fail before transport and capacity admission',async t=>{
  const {sdk,seen}=fixture(t);
  const invalid=[null,[],true,'id',{}, {taskId:'task-1',operationId:'op'}, {taskId:'task-1',extra:1},
    {operationId:'op',extra:null},{unknown:'op'},{taskId:undefined},{taskId:null},{taskId:1},{taskId:{}},
    {operationId:null},{operationId:1},{operationId:[]},{taskId:''},{operationId:''},
    {taskId:'a'.repeat(129)},{operationId:'a'.repeat(129)},{taskId:'space id'},
    {taskId:'tail\n'},{operationId:'tail\n'},{operationId:'中文'},{operationId:'nul\0id'}];
  for(const params of invalid) await assert.rejects(sdk.call('task.query',params),{code:'E_INVALID_PARAMS'});
  for(const id of ['',null,1,'a'.repeat(129),'bad\n']) {
    await assert.rejects(sdk.query(id),{code:'E_INVALID_PARAMS'});
    await assert.rejects(sdk.queryOperation(id),{code:'E_INVALID_PARAMS'});
  }
  assert.equal(seen.length,0);assert.equal(sdk.pendingRequestCount,0);
  await sdk.queryOperation('A'.repeat(128));assert.equal(seen.length,1);
});
test('exact query input cannot use inherited identifiers or serialization hooks to change it',async t=>{
  const {sdk,seen}=fixture(t);
  const inherited=Object.create({operationId:'inherited'});
  const nonEnumerable={operationId:'valid'};Object.defineProperty(nonEnumerable,'extra',{value:true});
  const symbolExtra={operationId:'valid',[Symbol('extra')]:true};
  const hook={operationId:'valid',toJSON(){return {taskId:'other'};}};
  for(const value of [inherited,nonEnumerable,symbolExtra,hook])
    await assert.rejects(sdk.call('task.query',value),{code:'E_INVALID_PARAMS'});
  let reads=0;
  const input={get operationId(){reads++;return reads===1?'original':'changed';}};
  await sdk.call('task.query',input);
  assert.equal(reads,1);assert.deepEqual(seen.map(x=>x.params),[{operationId:'original'}]);
});
test('timeout recovery uses operation query without restarting or cancelling native work',async t=>{
  let count=0;const {sdk,seen}=fixture(t,()=>++count===1?new Promise(()=>{}):snapshot);
  await assert.rejects(sdk.call('task.start',{kind:'instant',operationId:'unknown-outcome',input:{}}),{code:'E_TIMEOUT'});
  assert.deepEqual(await sdk.queryOperation('unknown-outcome'),snapshot);
  assert.deepEqual(await sdk.queryOperation('unknown-outcome'),snapshot);
  assert.deepEqual(seen.map(x=>x.method),['task.start','task.query','task.query']);
  assert.equal(new Set(seen.map(x=>x.requestId)).size,3);
});
test('native not-found and unauthorized are preserved without retries',async t=>{
  const seen=[];let code='E_TASK_NOT_FOUND';
  const sdk=new SouiNativeClientImpl({onEvent:()=>()=>{},request:async raw=>{const r=JSON.parse(raw);seen.push(r);return JSON.stringify({requestId:r.requestId,ok:false,error:{code,message:'native denial'}});}},options);
  t.after(()=>sdk.dispose());
  await assert.rejects(sdk.queryOperation('evicted'),{code:'E_TASK_NOT_FOUND'});code='E_UNAUTHORIZED';
  await assert.rejects(sdk.queryOperation('revoked'),{code:'E_UNAUTHORIZED'});
  assert.equal(seen.length,2);assert.ok(seen.every(x=>x.method==='task.query'));
});
const catalog=methods=>({protocol:'soui.capabilities/1',sdkVersion:'0.5.0-preview.1',methods});
test('query tool fallback only describes the authorized catalog entry',()=>{
  assert.deepEqual(souiToolDefinitions(catalog([])),[]);
  assert.deepEqual(souiToolDefinitions(catalog([{name:'task.query',capability:'other'}])),[]);
  assert.deepEqual(souiToolDefinitions(catalog([{name:'task.query',capability:'task.query',available:false}])),[]);
  assert.deepEqual(souiToolDefinitions(catalog([{name:'task.query',capability:'task.query',authorized:false}])),[]);
  assert.deepEqual(souiToolDefinitions(catalog([{name:'task.start',capability:'task.start'}])),[]);
  const [tool]=souiToolDefinitions(catalog([{name:'task.query',capability:'task.query'}]));
  assert.equal(tool.name,'task.query');assert.match(tool.description,/exactly one/i);assert.match(tool.description,/ASCII/);
  assert.deepEqual(tool.inputSchema,{type:'object',properties:{taskId:{type:'string',minLength:1,maxLength:128},operationId:{type:'string',minLength:1,maxLength:128}},additionalProperties:false});
  // Limited schema permits both/neither/Unicode; runtime and native enforce the rest.
  const action=readSouiActionContract({protocol:'soui.action/1',title:'Read task',description:tool.description,inputSchema:tool.inputSchema,
    outputSchema:{type:'object',properties:{},additionalProperties:false}});
  for(const value of [{},{taskId:'a',operationId:'b'},{operationId:'中文'}])validateSouiActionInput(action,value);
  assert.throws(()=>validateSouiActionInput(action,{operationId:'a',extra:1}),{code:'E_INVALID_PARAMS'});
  const tools=souiToolDefinitions(catalog([{name:'task.query',capability:'task.query'}]));tools[0].inputSchema.properties.taskId.maxLength=999;
  assert.equal(souiToolDefinitions(catalog([{name:'task.query',capability:'task.query'}]))[0].inputSchema.properties.taskId.maxLength,128);
});
test('an existing native action description takes precedence over task fallback',()=>{
  const action={protocol:'soui.action/1',title:'Registered task query',description:'Registered description',
    inputSchema:{type:'object',properties:{taskId:{type:'string',maxLength:128}},required:['taskId'],additionalProperties:false},
    outputSchema:{type:'object',properties:{},additionalProperties:false}};
  const [tool]=souiToolDefinitions(catalog([{name:'task.query',capability:'task.query',action}]));
  assert.equal(tool.description,action.description);assert.deepEqual(tool.inputSchema,action.inputSchema);
});
