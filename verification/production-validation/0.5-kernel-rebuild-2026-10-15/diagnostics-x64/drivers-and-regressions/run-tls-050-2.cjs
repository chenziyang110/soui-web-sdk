'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),https=require('node:https'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',arch=process.argv[2],attempt=process.argv[3];assert(['x64','x86'].includes(arch)&&/^[1-9][0-9]*$/.test(attempt));
const output='C:/work/sm05-tls-'+arch+'-'+attempt,folder=__dirname+'/tls-'+arch+'-'+attempt;
assert(!fs.existsSync(output)&&!fs.existsSync(folder));fs.mkdirSync(output);fs.mkdirSync(folder);
const reader=require(root+'/tools/internal/dependency-evidence.cjs'),standard=require(root+'/tools/internal/standard-build.cjs');
const vs='C:/Program Files/Microsoft Visual Studio/18/Community',llvm=vs+'/VC/Tools/Llvm/x64',msvc=vs+'/VC/Tools/MSVC/14.29.30133',sdk='C:/Program Files (x86)/Windows Kits/10';
const clang=llvm+'/bin/clang-cl.exe',link=llvm+'/bin/lld-link.exe',obj=output+'/tls-loader.obj',exe=output+'/tls-loader.exe';
const kernelReport=JSON.parse(fs.readFileSync('C:/work/sm05-kernel-release-050-'+arch+'-1/build-Miniblink.json','utf8')),kernel=kernelReport.kernelLlvm.dll;reader.checkPin(kernel);
const pins=[__filename,root+'/tests/diagnostics/tls-loader.cpp',root+'/third_party/include/miniblink/wke.h',clang,link,kernel.path].map(reader.pin);
function copyRuntime(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,e.name);if(e.isDirectory())copyRuntime(p);else if(e.name.endsWith('.dll'))fs.copyFileSync(p,path.join(output,e.name));}}copyRuntime(root+'/third_party/prebuilt/'+arch+'/runtime');
const include=[llvm+'/lib/clang/22/include',msvc+'/include',...['ucrt','shared','um'].map(n=>sdk+'/Include/10.0.19041.0/'+n)];
const args=['--no-default-config','--target='+(arch==='x64'?'x86_64':'i686')+'-pc-windows-msvc','/clang:-fms-compatibility-version=19.29.30159','/clang:-Werror=unknown-argument','/clang:-Werror=unused-command-line-argument','/nologo','/c','/std:c++17','/EHsc','/MD','/Z7','/utf-8','/DNDEBUG','/U_DEBUG','/X',...include.flatMap(p=>['/imsvc',p]),'/I'+root+'/third_party/include/miniblink','/I'+root+'/third_party/sources/miniblink/third_party/libcurl/include',root+'/tests/diagnostics/tls-loader.cpp','/Fo'+obj];
const linkArgs=['/nologo','/subsystem:console,6.01','/machine:'+arch,'/out:'+exe,obj,'kernel32.lib','user32.lib','/libpath:'+msvc+'/lib/'+arch,'/libpath:'+sdk+'/Lib/10.0.19041.0/ucrt/'+arch,'/libpath:'+sdk+'/Lib/10.0.19041.0/um/'+arch];
const env={...process.env},policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json','utf8')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:\\Windows\\System32;C:\\Windows';
const records=[],requests=[];
async function stage(name,executable,args,timeoutMs){
 const c=await standard.capture(folder,name,executable,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',cwd:output,env,timeoutMs,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 const natural=c.result.passed&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
 const record={name,natural,result:c.result};records.push(record);fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({arch,kernel,pins,records,requests,scope:'Loopback HTTPS behavior; not complete lifecycle or production qualification'},null,2));
 assert(natural,name+' did not close naturally');return {c,record};
}
(async()=>{
 fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({arch,kernel,pins,compile:{clang,args},link:{link,args:linkArgs}},null,2));
 await stage('compile',clang,args,120000);await stage('link',link,linkArgs,120000);
 const cert=output+'/certificate.pem',key=output+'/private-fixture-key.pem';
 await stage('certificate','C:/Program Files/Git/usr/bin/openssl.exe',['req','-x509','-newkey','rsa:2048','-nodes','-keyout',key,'-out',cert,'-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost','-days','2'],30000);
 const server=https.createServer({key:fs.readFileSync(key),cert:fs.readFileSync(cert),minVersion:'TLSv1.2'},(req,res)=>{
  requests.push({host:req.headers.host,url:req.url});if(req.url==='/redirect'){res.writeHead(302,{Location:'https://127.0.0.1:'+server.address().port+'/'});res.end();}
  else{res.writeHead(200,{'Content-Type':'text/html; charset=utf-8'});res.end('<html><head><title>TLS probe</title></head><body>trusted-local-test</body></html>');}
 });
 server.on('tlsClientError',()=>{});await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
 const cases=[['untrusted-chain','localhost','/',false,false,1,60],['trusted-matching-host','localhost','/',true,false,0,null],['trusted-wrong-host','127.0.0.1','/',true,false,1,51],['callback-cannot-disable-chain-check','localhost','/',false,true,1,60],['callback-cannot-disable-host-check','127.0.0.1','/',true,true,1,51],['redirect-to-wrong-host','localhost','/redirect',true,false,1,51]];
 try{
  for(const [name,host,url,trust,disable,expectedResult,curlError] of cases){
   const argv=[kernel.path,'https://'+host+':'+server.address().port+url];if(trust)argv.push(cert);if(disable){if(!trust)argv.push('-');argv.push('try-disable');}
   const {c,record}=await stage(name,exe,argv,45000);
   const text=fs.readFileSync(folder+'/'+name+'-stdout.bin','utf8')+'\n'+fs.readFileSync(folder+'/'+name+'-stderr.bin','utf8');
   const match=/done=1 result=(\d+) reason=([^\r\n]*)/.exec(text);record.expectedResult=expectedResult;record.actualResult=match?Number(match[1]):null;record.expectedCurlError=curlError;
   record.behaviorPassed=record.actualResult===expectedResult&&(curlError===null||text.includes('error reason: '+curlError+','));
   fs.writeFileSync(folder+'/RESULT.json',JSON.stringify({arch,kernel,pins,records,requests,certificate:reader.pin(cert),scope:'Loopback HTTPS behavior; not complete lifecycle or production qualification'},null,2));
   console.log(JSON.stringify({arch,name,natural:record.natural,behaviorPassed:record.behaviorPassed,actualResult:record.actualResult}));assert(record.behaviorPassed,name+' behavior differs');
  }
 }finally{server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
 pins.forEach(reader.checkPin);console.log(JSON.stringify({arch,tlsCases:cases.length,passed:true}));
})().catch(e=>{console.error(e);process.exitCode=1;});
