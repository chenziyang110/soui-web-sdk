'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',work=__dirname,e=require(root+'/tools/internal/dependency-evidence.cjs');
const predecessorFile='C:/work/sm05-uia-delivery-1/PROPOSAL.json',predecessor=JSON.parse(fs.readFileSync(predecessorFile));
e.checkPin(predecessor.currentSource);predecessor.files.forEach(row=>{if(row.before)e.checkPin(row.before);e.checkPin(row.after);});
const names=[...predecessor.files.map(row=>row.relative),'tools/internal/Observe-LlvmBuild.ps1','tools/internal/configure-log-evidence.cjs','tools/internal/Capture-CMakeConfigureLog.ps1','docs/adr/0111-immutable-cmake-configure-log.md'];
const files=names.map(relative=>{
 const current=root+'/'+relative,before=work+'/before/'+relative,after=work+'/proposal/'+relative;
 if(fs.existsSync(current))assert(fs.existsSync(before)&&fs.readFileSync(current).equals(fs.readFileSync(before)),relative+' baseline changed');
 else assert(!fs.existsSync(before));
 return {relative,before:fs.existsSync(current)?e.pin(current):null,after:e.pin(after)};
});
const checks=[['standard-build.test.cjs.txt',46,0],['integration-green2.txt',14,0],['host64-build-debug-observer.test.cjs.txt',175,0],['devkit-command.test.cjs.txt',24,0],['build-source-manifest.test.cjs.txt',6,0],['llvm-profile-green3.txt',39,1],['sdk-build-inputs.test.cjs.txt',2,0]].map(([file,count,skipped])=>{
 const text=fs.readFileSync(work+'/'+file,'utf8');assert.match(text,new RegExp('pass '+count+'\\b'));assert.match(text,/fail 0\b/);assert.match(text,new RegExp('skipped '+skipped+'\\b'));
 return {file:e.pin(work+'/'+file),passed:count,skipped};
});
const actual=JSON.parse(fs.readFileSync(work+'/ACTUAL-LOG-RESULT.json'));assert(actual.natural&&actual.result.exitCode===0&&actual.result.supervisorExitCode===0);actual.pins.forEach(e.checkPin);
const installed=['x64','x86'].map(arch=>{const file='C:/work/sm05-uia-install-2/'+arch+'-2-VERIFIED.json',value=JSON.parse(fs.readFileSync(file));assert(value.passed&&value.actualNativeCases===11&&value.offRequiredComponentRejected&&value.publicBuildEntryVerified===false);return e.pin(file);});
const whitespace=files.map(row=>{
 const result=cp.spawnSync('C:/Program Files/Git/cmd/git.exe',['-c','core.autocrlf=false','-c','core.whitespace=cr-at-eol','diff','--no-index','--check','--',row.before?work+'/before/'+row.relative:'NUL',row.after.path],{encoding:'utf8',windowsHide:true});
 assert([0,1].includes(result.status)&&result.stdout===''&&result.stderr==='',row.relative+': '+result.stdout+result.stderr);return {relative:row.relative,exitCode:result.status,stdout:result.stdout,stderr:result.stderr};
});
fs.writeFileSync(work+'/WHITESPACE.json',JSON.stringify(whitespace,null,2)+'\n',{flag:'wx'});
fs.writeFileSync(work+'/PROPOSAL.json',JSON.stringify({schema:'soui.private-change-proposal/1',adopted:false,supersedes:e.pin(predecessorFile),currentSource:predecessor.currentSource,
 scope:'Explicit optional UIA delivery and immutable shared CMake log capture; final public build and Debug desktop remain pending',files,checks,purePassed:306,pureSkipped:1,
 actualRepeatedConfigure:e.pin(work+'/ACTUAL-LOG-RESULT.json'),installedUia:installed,source24DebugFailure:e.pin(root+'/../sm05-source24-pipeline-2/desktop-x64-Debug-source24-2/RESULT.json'),releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({sealed:true,files:files.length,purePassed:306,pureSkipped:1,adopted:false}));
