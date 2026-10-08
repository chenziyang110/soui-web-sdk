import test from 'node:test';
import assert from 'node:assert/strict';
import { readSouiActionContract, validateSouiActionInput, validateSouiActionOutput,
  souiToolDefinitions, SouiNativeClientImpl } from '../../sdk/web/dist/index.js';

const object = (properties = {}, required = []) => ({type:'object', properties, required, additionalProperties:false});
const contract = (inputSchema = object({name:{type:'string', minLength:1, maxLength:2}}, ['name']), outputSchema = object({ok:{type:'boolean'}}, ['ok'])) =>
  ({protocol:'soui.action/1', title:'问候', description:'受权的原生问候', inputSchema, outputSchema});
const catalog = action => ({protocol:'soui.capabilities/1', sdkVersion:'preview-test',
  methods:[{name:'app.greet', capability:'app.greet', action}, {name:'app.legacy', capability:'app.legacy'}]});

test('contracts and tool definitions are validated defensive copies', () => {
  const raw = contract();
  const parsed = readSouiActionContract(raw);
  const tools = souiToolDefinitions(catalog(parsed));
  assert.deepEqual(tools, [{name:'app.greet', description:raw.description, inputSchema:raw.inputSchema}]);
  raw.inputSchema.properties.name.maxLength = 99;
  parsed.inputSchema.properties.name.maxLength = 42;
  assert.equal(tools[0].inputSchema.properties.name.maxLength, 2);
  assert.deepEqual(souiToolDefinitions({...catalog(contract()), methods:[]}), []);
});

test('Unicode length uses scalars rather than UTF-16 units and rejects lone surrogates', () => {
  const action = contract();
  validateSouiActionInput(action, {name:'😀汉'});
  assert.throws(() => validateSouiActionInput(action, {name:'😀汉文'}), {code:'E_INVALID_PARAMS'});
  assert.throws(() => validateSouiActionInput(action, {name:'\ud800'}), {code:'E_INVALID_PARAMS'});
  assert.throws(() => readSouiActionContract({...action, title:'\udc00'}), {code:'E_INVALID_RESPONSE'});
  const large=contract(object({value:{type:'string',maxLength:32768}},['value']));
  validateSouiActionInput(large,{value:'x'.repeat(32768)});
});

test('unknown keywords, missing bounds and invalid roots are refused instead of silently weakened', () => {
  const schemas = [object({x:{type:'string'}}), object({x:{type:'string', maxLength:5, pattern:'.*'}}),
    object({x:{type:'array', items:{type:'boolean'}}}), {...object(), additionalProperties:true},
    {type:'object'}, object({x:{type:'any'}}), object({}, ['undeclared']), object({x:{type:'null'}}, ['x','x']),
    object({'':{type:'null'}}), object({['汉'.repeat(43)]:{type:'null'}}),
    object({x:{type:'array', items:{type:'null'}, minItems:2, maxItems:1}}),
    object({x:{type:'string', minLength:3, maxLength:2}}), object({x:{type:'string', maxLength:2, enum:['a','a']}}),
    object({x:{type:'string', maxLength:2, enum:[]}}), object({x:{type:'boolean', default:false}}),
    object({x:{type:'number', minimum:Infinity}}),
    object({x:{type:'number', minimum:2, maximum:1}}), {type:'string', maxLength:2}];
  for (const schema of schemas) assert.throws(() => readSouiActionContract(contract(schema)), {code:'E_INVALID_RESPONSE'});
  for (const change of [{protocol:'soui.action/2'}, {title:''}, {title:'汉'.repeat(86)},
    {description:'x'.repeat(1025)}, {actor:'agent'}])
    assert.throws(() => readSouiActionContract({...contract(), ...change}), {code:'E_INVALID_RESPONSE'});
});

test('object, array, string enum, numeric and null schemas enforce exact values', () => {
  const action = contract(object({
    items:{type:'array', maxItems:2, minItems:1, items:{type:'integer', minimum:0, maximum:2}},
    choice:{type:'string', maxLength:3, enum:['一','two']}, ratio:{type:'number', minimum:-1.5, maximum:2.5}, optional:{type:'null'},
  }, ['items','choice','ratio']));
  const valid = {items:[1,2], choice:'一', ratio:1.25, optional:null};
  validateSouiActionInput(action, valid);
  for (const input of [{...valid, items:[]}, {...valid, items:[1,2,0]}, {...valid, items:[1.5]},
    {...valid, choice:'bad'}, {...valid, ratio:3}, {...valid, optional:false}, {...valid, extra:true},
    {items:[0],choice:'一'}, {...valid, ratio:NaN}, {...valid, ratio:Number.MAX_SAFE_INTEGER+1}])
    assert.throws(() => validateSouiActionInput(action, input), {code:'E_INVALID_PARAMS'});
  validateSouiActionOutput(action, {ok:true});
  assert.throws(() => validateSouiActionOutput(action, {ok:'yes'}), {code:'E_SCHEMA_INVALID'});
  // An empty enum/bounds intersection is a valid schema whose input can never match.
  const empty = contract(object({value:{type:'string', maxLength:1, enum:['long']}}, ['value']));
  readSouiActionContract(empty);
  assert.throws(() => validateSouiActionInput(empty, {value:'long'}), {code:'E_INVALID_PARAMS'});
  const fractional = contract(object({value:{type:'integer',minimum:1.5,maximum:2.5}}, ['value']));
  validateSouiActionInput(fractional,{value:2});
  assert.throws(()=>validateSouiActionInput(fractional,{value:1}),{code:'E_INVALID_PARAMS'});
});

test('schema cardinality, bytes, numeric bounds and raw structural depth are bounded', () => {
  const props = Object.fromEntries(Array.from({length:65}, (_,i) => [`p${i}`,{type:'null'}]));
  assert.throws(() => readSouiActionContract(contract(object(props))), {code:'E_INVALID_RESPONSE'});
  const maximumProps = Object.fromEntries(Array.from({length:64}, (_,i) => [`p${i}`,{type:'null'}]));
  readSouiActionContract(contract(object(maximumProps)));
  for (const schema of [object({x:{type:'array', items:{type:'null'}, maxItems:1025}}),
    object({x:{type:'string', maxLength:32769}}), object({x:{type:'integer', maximum:Number.MAX_SAFE_INTEGER+1}})])
    assert.throws(() => readSouiActionContract(contract(schema)), {code:'E_INVALID_RESPONSE'});
  let deep = {type:'null'};
  for (let i=0;i<4;i++) deep=object({nested:deep});
  assert.throws(() => readSouiActionContract(contract(deep)), {code:'E_INVALID_RESPONSE'});
  const enumValues=Array.from({length:64},(_,i)=>`${String(i).padStart(2,'0')}${'x'.repeat(125)}`);
  assert.throws(() => readSouiActionContract(contract(object({x:{type:'string', maxLength:32768, enum:enumValues}}))), {code:'E_INVALID_RESPONSE'});
  const almost=Array.from({length:64},(_,i)=>`${String(i).padStart(2,'0')}${'x'.repeat(116)}`);
  const wide=object({x:{type:'string', maxLength:32768, enum:almost}});
  assert.throws(() => readSouiActionContract({...contract(wide,wide),title:'x'.repeat(256),description:'x'.repeat(1024)}), {code:'E_INVALID_RESPONSE'});
});

test('prototype-named properties remain data and custom serializers cannot change validation', () => {
  const props=JSON.parse('{"__proto__":{"type":"boolean"},"constructor":{"type":"null"}}');
  const action=contract(object(props, ['__proto__','constructor']));
  validateSouiActionInput(action, JSON.parse('{"__proto__":true,"constructor":null}'));
  assert.throws(() => validateSouiActionInput(action, {}), {code:'E_INVALID_PARAMS'});
  const deceptive={name:'a'};
  Object.defineProperty(deceptive,'toJSON',{value:()=>({name:'way-too-long'})});
  // The validator captures JSON data, never executes custom serialization.
  validateSouiActionInput(contract(), deceptive);
  assert.throws(() => validateSouiActionInput(contract(), new Date()), {code:'E_INVALID_PARAMS'});
  const circular={name:'a'};circular.self=circular;
  assert.throws(() => validateSouiActionInput(contract(), circular), {code:'E_INVALID_PARAMS'});
});

test('authorized discovery preserves action contracts and rejects malformed native schemas', async t => {
  const good=contract();
  let action=good;
  const client=new SouiNativeClientImpl({request:async raw=>{
    const request=JSON.parse(raw);
    return JSON.stringify({requestId:request.requestId,ok:true,result:{...catalog(action),nextCursor:null}});
  },onEvent:()=>()=>{}},{sessionId:'s',origin:'app://test',token:'t'});
  t.after(()=>client.dispose());
  assert.deepEqual((await client.discover()).methods[0].action,good);
  action={...good,inputSchema:{...good.inputSchema,additionalProperties:true}};
  await assert.rejects(client.discover(),{code:'E_INVALID_RESPONSE'});
});

test('tools are advisory and do not change the native denial or invoke discovered methods', async t => {
  let calls=0;
  const client=new SouiNativeClientImpl({request:async raw=>{
    calls++;
    return JSON.stringify({requestId:JSON.parse(raw).requestId,ok:false,error:{code:'E_UNAUTHORIZED',message:'denied'}});
  },onEvent:()=>()=>{}},{sessionId:'s',origin:'app://test',token:'t'});
  t.after(()=>client.dispose());
  assert.equal(souiToolDefinitions(catalog(contract())).length,1);
  assert.equal(calls,0);
  await assert.rejects(client.call('app.greet',{name:'汉'}),{code:'E_UNAUTHORIZED'});
  assert.equal(calls,1);
  assert.throws(()=>souiToolDefinitions({...catalog(contract()),methods:[...catalog(contract()).methods,...catalog(contract()).methods]}),{code:'E_INVALID_RESPONSE'});
});
