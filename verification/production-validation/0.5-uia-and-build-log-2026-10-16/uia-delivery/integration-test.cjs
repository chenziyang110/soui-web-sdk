'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
const file=__dirname+'/proposal/tests/sdk/llvm-evidence-readers.test.cjs';let text=fs.readFileSync(file,'utf8');
const from="require:name=>{assert.equal(name,'./host64-build-debug-observer.cjs');return {recordedEvidence:()=>proof};}};";
assert.equal(text.split(from).length,2);text=text.replace(from,"require:name=>{if(name==='./configure-log-evidence.cjs')return require('../../tools/internal/configure-log-evidence.cjs');assert.equal(name,'./host64-build-debug-observer.cjs');return {recordedEvidence:()=>proof};}};");
const end="r=>r.binaryRecords.at(-1).output.sha256='b'.repeat(64),r=>r.binaryRecords.push(structuredClone(archive))]){\n    const bad=structuredClone(row);mutate(bad);assert.throws(()=>run(bad),/exact actual archive proof/);\n  }";
assert.equal(text.split(end).length,2);text=text.replace(end,end+"\n  const badCapture=structuredClone(row);badCapture.configureLogCapture={schema:'incorrect'};\n  assert.throws(()=>run(badCapture),/Configure log capture schema differs/);");fs.writeFileSync(file,text);
