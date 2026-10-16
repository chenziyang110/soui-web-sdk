'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',work=__dirname,e=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const [arch='x64',attempt='2',resume='false']=process.argv.slice(2);assert(['x64','x86'].includes(arch)&&/^[1-9][0-9]*$/.test(attempt)&&['true','false'].includes(resume));
const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const packet=JSON.parse(fs.readFileSync('C:/work/sm05-uia-events-1/PROPOSAL.json'));e.checkPin(packet.currentSource);packet.files.forEach(row=>{if(row.before)e.checkPin(row.before);e.checkPin(row.after);});
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];Object.assign(env,policy.set);
env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:/Program Files/CMake/bin;C:/Program Files/Git/cmd;C:/Windows/System32;C:/Windows';
const pins=[__filename,work+'/build.ps1',work+'/native/CMakeLists.txt',work+'/consumer/CMakeLists.txt',root+'/source-manifest.json'].map(e.pin),records=[];
function include(folder){for(const ent of fs.readdirSync(folder,{withFileTypes:true})){const file=path.join(folder,ent.name);if(ent.isDirectory())include(file);else if(ent.isFile())pins.push(e.pin(file));else throw Error('Unexpected nonregular input '+file);}}
for(const folder of [root+'/sdk/src',root+'/sdk/include',root+'/sdk/cmake',root+'/sdk/accessibility',root+'/third_party/sources/zlib',root+'/third_party/include/json',root+'/third_party/licenses',work+'/cmake',work+'/tools'])include(folder);
pins.push(e.pin(root+'/sdk/CMakeLists.txt'),e.pin(root+'/THIRD_PARTY_NOTICES.md'),...packet.files.map(row=>row.after));
fs.writeFileSync(work+'/'+arch+'-'+attempt+'-INPUTS.json',JSON.stringify({pins,scope:'Private native SDK install and relocated imported UIA consumer; public Build entry still pending'},null,2),{flag:'wx'});
async function run(name,exe,args,expected=0){
 const output=work+'/'+arch+'-'+attempt+'-'+name;fs.mkdirSync(output);
 const c=await standard.capture(output,name,exe,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:work,env,timeoutMs:900000,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result),t=v.terminal;
 const natural=c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!t.terminatedOwnedJob&&t.activeBeforeCleanup===0&&t.activeProcesses===0;
 const matched=c.result.exitCode===expected&&c.result.supervisorExitCode===expected;
 records.push({name,expected,natural,matched,result:c.result,terminal:t});
 fs.writeFileSync(work+'/'+arch+'-'+attempt+'-RESULTS.json',JSON.stringify({pins,records,releaseReady:false},null,2));
 console.log(JSON.stringify({arch,name,natural,matched,exitCode:c.result.exitCode}));pins.forEach(e.checkPin);assert(matched&&natural,'Actual result differs; preserve '+output);
}
const build=mode=>run(mode,ps,['-NoProfile','-NonInteractive','-File',work+'/build.ps1','-Mode',mode,'-Architecture',arch],mode==='off-reject'?1:0);
(async()=>{
 if(resume==='true'){
  assert.equal(arch,'x64');
  const prior=JSON.parse(fs.readFileSync(work+'/x64-RESULTS.json'));assert.equal(prior.records[0].name,'on');assert(prior.records[0].natural&&prior.records[0].matched);
  for(const pin of prior.pins){
   if(path.resolve(pin.path)===path.resolve(__filename))assert.equal(e.pin(work+'/run-v1.cjs').sha256,pin.sha256);
   else if(path.resolve(pin.path)===path.resolve(work+'/consumer/CMakeLists.txt'))assert.equal(e.pin(work+'/consumer-v1.CMakeLists.txt').sha256,pin.sha256);
   else e.checkPin(pin);
  }
  const moved=JSON.parse(fs.readFileSync(work+'/x64-RELOCATED.json'));for(const row of moved.installed){const current=e.pin(path.join(moved.to,row.relative));assert.equal(current.bytes,row.bytes);assert.equal(current.sha256,row.sha256);pins.push(current);}
  records.push({reusedOriginalOn:e.pin(work+'/x64-RESULTS.json'),relocation:e.pin(work+'/x64-RELOCATED.json'),scope:'Unchanged actual installed SDK from preceding natural ON build; no new compile claim'});
 }else{
 await build('on');
 const from=fs.realpathSync(work+'/prefix/'+arch+'/on'),to=path.resolve(work,'prefix',arch,'relocated-on'),base=fs.realpathSync(work)+path.sep;
 assert(from.startsWith(base)&&to.startsWith(base)&&!fs.existsSync(to)&&!fs.lstatSync(from).isSymbolicLink());
 const installed=[];function tree(folder){for(const ent of fs.readdirSync(folder,{withFileTypes:true})){const file=path.join(folder,ent.name);if(ent.isDirectory())tree(file);else installed.push({relative:path.relative(from,file),...e.pin(file)});}}tree(from);
 fs.renameSync(from,to);for(const row of installed){const relocated=e.pin(path.join(to,row.relative));assert.equal(relocated.bytes,row.bytes);assert.equal(relocated.sha256,row.sha256);}
 fs.writeFileSync(work+'/'+arch+'-RELOCATED.json',JSON.stringify({from,to,installed},null,2),{flag:'wx'});
 for(const row of installed)pins.push(e.pin(path.join(to,row.relative)));
 }
 await build('consumer');
 const exe=work+'/out/'+arch+'/consumer/Release/installed_uia_tests.exe';pins.push(e.pin(exe));
 for(const name of ['properties','mutations','stale-secret','bounded','callback-close','uia-client','com-initialization','api-close','property-events','tree-events','revoked-events'])await run('case-'+name,exe,[name]);
 await build('off');
 const off=work+'/prefix/'+arch+'/off';assert(!fs.existsSync(off+'/lib/Release/soui_uia.lib'));
 assert.match(fs.readFileSync(off+'/lib/cmake/SouiSdk/SouiSdkConfig.cmake','utf8'),/set\(SouiSdk_Uia_FOUND OFF\)/);
 await build('off-reject');
 const stderr=fs.readFileSync(work+'/'+arch+'-'+attempt+'-off-reject/off-reject-stdout.bin','utf8')+fs.readFileSync(work+'/'+arch+'-'+attempt+'-off-reject/off-reject-stderr.bin','utf8');
 assert.match(stderr,/SouiSdk_FOUND to FALSE/);
 pins.forEach(e.checkPin);fs.writeFileSync(work+'/'+arch+'-'+attempt+'-VERIFIED.json',JSON.stringify({passed:true,records,relocatedImportedConsumer:true,actualNativeCases:11,offRequiredComponentRejected:true,publicBuildEntryVerified:false,releaseReady:false},null,2),{flag:'wx'});
})().catch(error=>{console.error(error);process.exitCode=1;});
