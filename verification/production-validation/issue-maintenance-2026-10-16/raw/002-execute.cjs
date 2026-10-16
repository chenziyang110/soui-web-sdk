'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const repo='F:/work/soui-issue96',base=__dirname,root=path.join(base,'portable');
const identity=require(repo+'/tools/internal/delivery-identity.cjs');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const pin=p=>({path:p,bytes:fs.statSync(p).size,sha256:hash(p)});
const save=(p,x)=>fs.writeFileSync(p,JSON.stringify(x,null,2)+'\n',{flag:'wx'});
const ps='C:/Users/11034/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe';
const runner='F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe';
async function main(){
 if(process.argv[2]==='prepare'){
  assert(!fs.existsSync(root),'Fresh portable destination required');
  const original=identity.source(repo),names=new Set([...original.files.map(x=>x.path),...identity.walk(repo,'third_party/manifests'),'.gitattributes','AGENTS.md']);
  for(const name of names){const target=path.join(root,name);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(repo,name),target,fs.constants.COPYFILE_EXCL);}
  assert.equal(identity.source(root).sha256,original.sha256);
  assert(!fs.existsSync(path.join(root,'third_party/sources/miniblink')));
  save(path.join(base,'PREPARED.json'),{sourceCommit:'40f04eb1f63158c6489fd23509d66fb47f2f38c6',root,source:original,copiedFiles:names.size,privateRootAbsent:true,scope:'fresh portable SDK source, not complete third-party source archive',releaseReady:false});
  console.log(JSON.stringify({prepared:true,files:names.size,sourceSha256:original.sha256}));return;
 }
 const arch=process.argv[2];assert(['x64','x86'].includes(arch));
 const initial=JSON.parse(fs.readFileSync(path.join(base,'PREPARED.json'),'utf8'));
 assert.equal(identity.source(root).sha256,initial.source.sha256);
 assert(!fs.existsSync(path.join(root,'out',arch)),'Fresh architecture build required');
 const folder=path.join(base,'build-'+arch);fs.mkdirSync(folder);
 const tools={LlvmRoot:'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64',MsvcRoot:'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.29.30133',WindowsSdkRoot:'C:/Program Files (x86)/Windows Kits/10',NinjaExe:'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe',WindowsCmdExe:'C:/Windows/System32/cmd.exe',PowerShellExe:ps};
 const args=['-NoProfile','-NonInteractive','-File',root+'/tools/Build.ps1','-Architecture',arch,'-Configuration','Release','-NativeToolchain','LLVM22MSVC142',...Object.entries(tools).flatMap(([k,v])=>['-'+k,v]),'-WithWebView2'];
 const env={...process.env};
 const policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json','utf8')).environment;
 for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];
 Object.assign(env,policy.set);env.ComSpec='C:/Windows/System32/cmd.exe';
 env.Path=path.dirname(process.execPath)+';C:/Program Files/CMake/bin;C:/Program Files/Git/cmd;C:/Windows/System32;C:/Windows';
 save(folder+'/INPUTS.json',{architecture:arch,configuration:'Release',executable:pin(ps),runner:pin(runner),script:pin(root+'/tools/Build.ps1'),helper:pin(__filename),args,sourceSha256:initial.source.sha256,vendorOverrideSupplied:false,sdkOnly:false,skipWeb:false});
 console.log(JSON.stringify({started:arch,folder}));
 const standard=require(root+'/tools/internal/standard-build.cjs');
 const captured=await standard.capture(folder,'build',ps,args,{runner,cwd:root,env,timeoutMs:1200000,drainMs:2000});
 const verified=standard.verifyStream(captured.rawReport,JSON.parse(captured.contextBytes.toString('utf8')),captured.ready,captured.result);
 const natural=captured.result.passed&&captured.result.exitCode===0&&captured.result.supervisorExitCode===0&&captured.result.closureConfirmed&&!captured.result.stopRequested&&!captured.result.fallbackUsed&&!verified.terminal.terminatedOwnedJob&&verified.terminal.activeProcesses===0;
 const sourceUnchanged=identity.source(root).sha256===initial.source.sha256;
 const cache=path.join(root,'out',arch,'CMakeCache.txt'),text=fs.existsSync(cache)?fs.readFileSync(cache,'utf8'):'';
 const vendorDefaultOff=/^SOUI_BUILD_VENDOR_TESTS:BOOL=OFF\r?$/m.test(text),sdkTestsOn=/^SOUI_BUILD_TESTS:BOOL=ON\r?$/m.test(text);
 save(folder+'/RESULT.json',{architecture:arch,natural,sourceUnchanged,vendorDefaultOff,sdkTestsOn,result:captured.result,terminal:verified.terminal,cache:fs.existsSync(cache)?pin(cache):null,releaseReady:false});
 console.log(JSON.stringify({architecture:arch,natural,sourceUnchanged,vendorDefaultOff,sdkTestsOn}));
 assert(natural&&sourceUnchanged&&vendorDefaultOff&&sdkTestsOn,'Portable build did not pass; original evidence retained');
}
main().catch(e=>{console.error(e.stack);process.exitCode=1;});
