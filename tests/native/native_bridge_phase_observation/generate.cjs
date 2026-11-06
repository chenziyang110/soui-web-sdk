'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const [root,output]=process.argv.slice(2);
if(!root||!output||process.argv.length!==4)throw new Error('usage: generate.cjs source-root output');
const source='sdk/src/host/soui_miniblink_host.cpp';
const bytes=fs.readFileSync(path.join(root,source)),entries=[];
const sha=value=>crypto.createHash('sha256').update(value).digest('hex');
function extract(begin,end,name){
  const needle=Buffer.from(begin,'ascii'),start=bytes.indexOf(needle);
  const stop=bytes.indexOf(Buffer.from(end,'ascii'),start+needle.length);
  if(start<0||stop<=start||bytes.indexOf(needle,start+needle.length)>=0)throw new Error('ambiguous production projection: '+begin);
  const fragment=bytes.subarray(start,stop);
  if(fragment.some(value=>value>127))throw new Error('expected exact ASCII production fragment');
  entries.push({name,startByte:start,endByteExclusive:stop,sha256:sha(fragment)});
  return fragment;
}
function ordered(value,needles){
  let offset=0;
  for(const needle of needles){const next=value.indexOf(needle,offset);assert.ok(next>=offset,'production boundary/order missing: '+needle);offset=next+needle.length;}
}
const serializer=extract('std::string native_bridge_phase_json(', '// Owner-thread reentrancy guard:', 'serializer');
const binding=extract('jsValue WKE_CALL_TYPE native_post_message(jsExecState es, void*) {', 'soui_miniblink_host::soui_miniblink_host(', 'native-binding');
const reply=extract('void dynamic_miniblink_adapter::resolve_bridge_response(', 'bool dynamic_miniblink_adapter::processing_user_gesture()', 'async-reply');
const diagnostic=extract('std::string dynamic_miniblink_adapter::application_diagnostics() {', 'void dynamic_miniblink_adapter::set_session_id(', 'diagnostic-placement');
const all=bytes.toString('utf8'),native=binding.toString('ascii'),response=reply.toString('ascii'),getter=diagnostic.toString('ascii');
for(const phase of ['native_first_parse','resolver_create_js','reply_run_js']){
  const token='detail::native_bridge_phase::'+phase;
  assert.equal(all.split(token).length-1,1,'Exactly one production scope for '+phase);
}
ordered(native,['std::string request(raw_request);','if (!adapter->owns_execution_context(es, context_receipt))',
  'const auto packet = [&] {','detail::native_bridge_phase::native_first_parse','return bounded_bridge_packet(request);',
  'request_id = packet_string(packet, "requestId");']);
ordered(native,['const detail::miniblink_admission_metadata metadata(',
  'if (!adapter->owns_execution_context(es, context_receipt)) return jsUndefined();',
  'const auto create =','const auto waiting = [&] {','detail::native_bridge_phase::resolver_create_js',
  'return jsEval(es, create.c_str());','if (!adapter->accepts_execution_context_receipt(context_receipt)) return jsUndefined();',
  'const bool waiting_is_string = jsIsString(waiting);','if (!adapter->owns_execution_context(es, context_receipt)) return jsUndefined();',
  'if (waiting_is_string) return waiting;','miniblink_native_admission_access::submit(']);
ordered(response,['if (generation != bridge_generation_.load(', 'const auto script = [&] {',
  'if (!accepts_execution_context_receipt(context_receipt) ||','const auto view =',
  'detail::native_bridge_phase::reply_run_js','(void)wkeRunJS(view, script.c_str());',
  'if (webview_ != view || !accepts_execution_context_receipt(context_receipt) ||']);
ordered(getter,['append_phase_diagnostics(*this, diagnostics, false);','char script_diagnostics[2]',
  'diagnostics += ",script=" + *script_result;']);
const projections={'native-phase-json.inc':serializer,'native-binding-source.txt':binding,
  'async-reply-source.txt':reply,'diagnostic-placement-source.txt':diagnostic};
fs.mkdirSync(output,{recursive:true});
function writeIfChanged(name,value){
  const target=path.join(output,name),buffer=Buffer.isBuffer(value)?value:Buffer.from(value);
  if(!fs.existsSync(target)||!fs.readFileSync(target).equals(buffer))fs.writeFileSync(target,buffer);
}
for(const [name,value]of Object.entries(projections))writeIfChanged(name,value);
writeIfChanged('source-binding.json',JSON.stringify({schema:'soui.native-bridge-phase-source/v1',source,sourceSha256:sha(bytes),entries,
  serializerSha256:sha(serializer),productionBoundariesChecked:true,
  scope:'Actual serializer bytes execute in the fixture; native guard/scope excerpts are source-order contracts, not mounted lifecycle execution.'},null,2)+'\n');
