'use strict';
// File/data regressions only. This never runs CMake or any Native test program.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const test=require('node:test'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../..');
const digest=buffer=>crypto.createHash('sha256').update(buffer).digest('hex');
function read(relative){assert.equal(typeof relative,'string');assert.ok(!path.isAbsolute(relative)&&!/[\\:;]/.test(relative)&&!relative.split('/').includes('..'));
  return fs.readFileSync(path.join(root,relative));}
function pin(buffer,row){assert.ok(Number.isSafeInteger(row.bytes)&&row.bytes>0);assert.match(row.sha256,/^[0-9a-f]{64}$/);
  assert.equal(buffer.length,row.bytes);assert.equal(digest(buffer),row.sha256);}
function slice(buffer,start,end){assert.ok(Number.isSafeInteger(start)&&Number.isSafeInteger(end)&&start>=0&&end>start&&end<=buffer.length);return buffer.subarray(start,end);}
const workerDirectory='tests/native/vendor_lifetime/worker_lifetime/';
const worker=JSON.parse(read(workerDirectory+'source-map.json'));
function workerChecks(map=worker){
  assert.equal(map.schema,'worker-lifetime-source-map/1');assert.equal(map.sources.length,13);assert.equal(map.copies.length,12);assert.equal(map.payloads.length,22);
  const sources=new Map();
  for(const row of map.sources){assert.ok(!sources.has(row.path));const bytes=read(row.path);pin(bytes,row);sources.set(row.path,bytes);}
  for(const row of map.payloads)pin(read(workerDirectory+row.path),row);
  const outputs=new Set();
  for(const row of map.copies){assert.ok(sources.has(row.source));assert.ok(!outputs.has(row.output));outputs.add(row.output);
    const bytes=sources.get(row.source),fragment=slice(bytes,row.startByte,row.endByteExclusive);pin(fragment,row);
    if(row.exactWholeFile){assert.equal(row.startByte,0);assert.equal(row.endByteExclusive,bytes.length);}
    else {assert.equal(fragment.at(-1),10);assert.ok(!fragment.includes(0)&&!fragment.includes(13));}
  }
}
function table(source,name){const expression=new RegExp(`set\\(${name}\\s+([\\s\\S]*?)\\n\\)`);const match=expression.exec(source);
  assert.ok(match,name);return [...match[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1].split('|'));}
function inlineChecks(directory,prefix){const text=read(directory+'CMakeLists.txt').toString('utf8');
  for(const kind of ['source','fixture'])for(const [relative,bytes,sha256]of table(text,`${prefix}_${kind}_pins`))
    pin(read(kind==='source'?relative:directory+relative),{bytes:Number(bytes),sha256});
  const ranges=table(text,`${prefix}_ranges`);
  for(const [source,start,fixture,offset,length]of ranges){const begin=Number(start),at=Number(offset),size=Number(length);
    const actual=slice(read(source),begin,begin+size),expected=slice(read(directory+fixture),at,at+size);assert.deepEqual(actual,expected);}
  return ranges;
}
test('Worker current whole pins, all payloads and twelve exact outputs match',()=>workerChecks());
test('Worker stale offsets or independently changed pins fail without disabling gates',()=>{
  for(const mutate of [m=>m.sources[0].sha256='0'.repeat(64),m=>m.copies[2].startByte++,m=>m.copies[0].endByteExclusive--,
    m=>m.payloads[0].bytes++,m=>m.copies.pop()]){const m=structuredClone(worker);mutate(m);assert.throws(()=>workerChecks(m));}
});
test('preserved registry and TLS bodies retain their original digests after relocation',()=>{
  const original={
    'fragments/registry_lock.inc':'f28320b3245b147e939fb7299797f00dd4276bf68eae722001cc55f2f1949c00',
    'fragments/tls.inc':'43594f649539ccaefe403d9a861a3366024b3aadec079618560beb2501b9129b',
    'fragments/registry_and_tls_methods.inc':'d4d49c82a2c2a335b5641bbbce4cdf676fbb66aa678a586c26c48ee5c1e2649c'};
  for(const [output,sha256]of Object.entries(original)){const row=worker.copies.find(row=>row.output===output);assert.equal(row.sha256,sha256);
    assert.equal(digest(slice(read(row.source),row.startByte,row.endByteExclusive)),sha256);}
});
test('new shutdown interface is actual declaration bytes used by the fixture projection',()=>{
  const row=worker.copies.find(row=>row.output==='fragments/webthread_shutdown_interface.inc');assert.ok(row);
  const fragment=slice(read(row.source),row.startByte,row.endByteExclusive).toString('utf8');
  assert.match(fragment,/enum class Retirement \{ Joined, Pending, Failed, Unsupported \}/);
  assert.match(fragment,/virtual Retirement tryShutdownAndJoin\(\)/);assert.match(fragment,/struct ShutdownWitness/);
  assert.match(fragment,/virtual bool duplicateShutdownWitness\(ShutdownWitness&\)/);
  assert.match(read(workerDirectory+'fixture_support.h').toString('utf8'),/#include "fragments\/webthread_shutdown_interface.inc"/);
  const gate=read(workerDirectory+'source-bytes.cmake').toString('utf8');
  assert.match(gate,/NOT copy_count EQUAL 12/);assert.match(gate,/NOT observed_hex STREQUAL expected_hex/);
  assert.match(gate,/Recheck every live input/);
});
test('dragdata all pins and ten source-to-fixture HEX ranges agree',()=>{
  assert.equal(inlineChecks('tests/native/vendor_lifetime/dragdata/','public_dragdata').length,10);
});
test('ctor-shell current pins and eight extracted ranges remain exact',()=>{
  assert.equal(inlineChecks('tests/native/vendor_lifetime/ctor_shell/','public_ctor_shell').length,8);
});
test('legacy-drag current pins and all three original ranges remain exact',()=>{
  const directory='tests/native/vendor_lifetime/legacy_drag/',m=JSON.parse(read(directory+'source-map.json'));
  assert.equal(m.schema,'legacy-drag-leaf-source-map/1');const sources={};
  for(const [name,row]of Object.entries(m.sources)){sources[name]=read(row.relative);pin(sources[name],row);}
  const fixture=read(directory+'legacy_drag_leaf_tests.cpp');pin(fixture,m.fixture);assert.equal(m.ranges.length,3);
  for(const row of m.ranges){const actual=slice(sources[row.source],row.sourceStart,row.sourceEnd);
    const expected=slice(fixture,row.fixtureStart,row.fixtureEnd);pin(actual,row);assert.deepEqual(actual,expected);}
  // The inline payload pins also cover the map and checked-in build entry.
  const text=read(directory+'CMakeLists.txt').toString('utf8');
  for(const [relative,bytes,sha256]of table(text,'public_legacy_drag_payload_pins'))pin(read(directory+relative),{bytes:Number(bytes),sha256});
});
test('endpoint fragment uses unique actual markers and supported raw LF bytes',()=>{
  const source=read('third_party/sources/miniblink/wke/wke.cpp');
  const begin=Buffer.from('struct InteractionEndpointImpl {'),end=Buffer.from('void WKE_CALL_TYPE retainInteractionEndpoint(');
  const start=source.indexOf(begin),stop=source.indexOf(end);assert.ok(start>=0&&stop>start);
  assert.equal(source.indexOf(begin,start+1),-1);assert.equal(source.indexOf(end,stop+1),-1);
  const fragment=slice(source,start,stop);assert.equal(fragment.at(-1),10);assert.ok(!fragment.includes(0)&&!fragment.includes(13));
  const gate=read('tests/native/vendor_lifetime/CMakeLists.txt').toString('utf8');
  assert.match(gate,/NOT generated_hex STREQUAL expected_hex/);assert.match(gate,/current_source_sha STREQUAL LIFETIME_SOURCE_SHA/);
});
