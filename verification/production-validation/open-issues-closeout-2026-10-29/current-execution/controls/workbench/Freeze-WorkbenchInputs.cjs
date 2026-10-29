'use strict';
// Authoring this script does not freeze in-progress source. Root invokes it only
// after the independent source review. No build, product or VM operation.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=f=>{const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:hash(b)};};
const walk=(d,p='')=>fs.readdirSync(path.join(d,p),{withFileTypes:true}).flatMap(e=>{assert(!e.isSymbolicLink());const n=p?p+'/'+e.name:e.name;return e.isDirectory()?walk(d,n):(assert(e.isFile()),[n]);}).sort();
const [gate]=process.argv.slice(2);assert.equal(gate,'ROOT_REVIEWED_CURRENT_WORKBENCH_SOURCE_V1');
const source=path.join(__dirname,'source');assert(!fs.existsSync(source),'Preserve earlier freeze; no overwrite');
assert(!fs.existsSync(path.join(__dirname,'FrozenSource.actual.json')));
const repo='C:/sc07',seal='C:/sr06';
const selected=[...walk(repo+'/demos/workbench/app').filter(n=>/\.(cpp|h)$/.test(n)).map(n=>'demos/workbench/app/'+n),
  ...walk(repo+'/demos/workbench/integration').filter(n=>/\.(cpp|h)$/.test(n)).map(n=>'demos/workbench/integration/'+n),
  'demos/workbench/generated/contracts/soui_native.generated.h',
  ...['demo.rc','app.manifest','uidef.xml'].map(n=>'demos/workbench/resources/'+n),
  'third_party/include/json/json.hpp','third_party/sources/zlib/zlib.h','third_party/sources/zlib/zconf.h'];
assert.equal(selected.filter(n=>n.endsWith('.cpp')).length,4);
const before=selected.map(relative=>({relative,...pin(repo+'/'+relative)}));
for(const p of before.filter(p=>!p.relative.startsWith('demos/workbench/app/'))){const s=pin(seal+'/'+p.relative);assert.equal(s.bytes,p.bytes);assert.equal(s.sha256,p.sha256,'Only reviewed app changes may differ from sealed dependencies');}
const recipe=pin(__dirname+'/recipe/CMakeLists.txt');fs.mkdirSync(source);
const copies=[];for(const p of [...before,{relative:'CMakeLists.txt',...recipe}]){const target=path.join(source,p.relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(p.path,target,fs.constants.COPYFILE_EXCL);const copied=pin(target);assert.equal(copied.bytes,p.bytes);assert.equal(copied.sha256,p.sha256);copies.push({relative:p.relative,source:p,copy:copied});}
for(const p of before)assert.deepEqual(pin(p.path),{path:p.path,bytes:p.bytes,sha256:p.sha256});assert.deepEqual(pin(recipe.path),recipe);
const record={schema:'soui.current-workbench-consumer-source-freeze/v1',createdUtc:new Date().toISOString(),rootGate:gate,producer:pin(__filename),sourceRoot:repo,sealedDependenciesRoot:seal,
  sourceFiles:copies,cppTranslationUnits:4,resourceTranslationUnits:1,sourceCurrentAppScopeOnly:true,nonAppDependenciesByteIdenticalToSeal:true,
  sdkRebuilt:false,webRebuilt:false,productExecuted:false,vmTouched:false,releaseReady:false};
fs.writeFileSync(__dirname+'/FrozenSource.actual.json',JSON.stringify(record,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({freeze:pin(__dirname+'/FrozenSource.actual.json'),sourceFiles:copies.length,productExecuted:false}));
