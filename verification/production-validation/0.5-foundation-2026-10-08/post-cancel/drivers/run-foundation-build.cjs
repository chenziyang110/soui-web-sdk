'use strict';
const fs=require('node:fs'), path=require('node:path'), crypto=require('node:crypto');
const {launchOwned}=require('../tools/internal/owned-process.cjs');
const root=path.resolve(__dirname,'..');
const [arch='x64', config='Debug', ...extra]=process.argv.slice(2);
if(!['x64','x86'].includes(arch)||!['Debug','Release'].includes(config))throw new Error('Architecture/configuration');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const original=path.join(root,'out/benchmark-protocol/Release/OwnedProcessRunner.exe');
const dir=path.join(root,'out/validation-bootstrap',hash(original));fs.mkdirSync(dir,{recursive:true});
const runner=path.join(dir,'OwnedProcessRunner.exe');
if(!fs.existsSync(runner))fs.copyFileSync(original,runner,fs.constants.COPYFILE_EXCL);
if(hash(runner)!==hash(original))throw new Error('Bootstrap runner identity');
const attempt=path.join(root,'out',`0.5.0-preview.1-build-${arch}-${config}-`+crypto.randomUUID());fs.mkdirSync(attempt);
const log=fs.openSync(path.join(attempt,'build.log'),'wx');
const env={...process.env,MSBUILDDISABLENODEREUSE:'1',
 PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
if(process.env.SOUITEST_VSTEL_EWDK)env.VSTEL_EWDK=process.env.SOUITEST_VSTEL_EWDK;
const exe=path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe');
const rebuildWeb=extra.includes('--rebuild-web');
const args=['-NoProfile','-ExecutionPolicy','Bypass','-File','tools/Build.ps1',
 '-Architecture',arch,'-Configuration',config,'-WithWebView2',
 ...(rebuildWeb?[]:['-SkipWeb']),...extra.filter(arg=>arg!=='--rebuild-web')];
console.log('Build attempt: '+attempt);
const owned=launchOwned(exe,args,{runner,cwd:root,report:path.join(attempt,'owned.ndjson'),
 timeoutMs:3600000,drainMs:30000,env,stdout:log,stderr:log});
owned.finished.then(result=>{
 fs.closeSync(log);fs.writeFileSync(path.join(attempt,'result.json'),JSON.stringify({
  schema:'soui.foundation-build-check/1',version:'0.5.0-preview.1',releaseReady:false,
  environment:{MSBUILDDISABLENODEREUSE:'1',PSModulePath:env.PSModulePath,VSTEL_EWDK:env.VSTEL_EWDK??null},...result},null,2)+'\n');
 const metadata=path.join(root,'out',arch,`build-${config}.json`);
 if(fs.existsSync(metadata))fs.copyFileSync(metadata,path.join(attempt,'build.json'),fs.constants.COPYFILE_EXCL);
 console.log(JSON.stringify({passed:result.passed,exitCode:result.exitCode,
  closureConfirmed:result.closureConfirmed,terminal:result.terminal}));
 console.log(fs.readFileSync(path.join(attempt,'build.log'),'utf8').slice(-1800));
 process.exitCode=result.passed?0:1;
}).catch(error=>{console.error(error.stack);process.exitCode=1;});
