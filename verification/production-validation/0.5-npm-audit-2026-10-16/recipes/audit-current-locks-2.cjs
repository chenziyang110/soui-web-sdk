'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',output='C:/work/sm05-current-lock-audit-2';
fs.mkdirSync(output);
const npm=path.join(path.dirname(process.execPath),'node_modules/npm/bin/npm-cli.js');
const runner='F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe';
const standard=require('D:/work/sm05-private-storage-readers-integrated-v3/tools/internal/standard-build.cjs');
const names=['tests/gui-bindings','tests/gui-bindings/react18'];
const pin=p=>({path:p,bytes:fs.statSync(p).size,sha256:crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex')});
const inputs=names.map(name=>({name,lock:pin(root+'/'+name+'/package-lock.json'),package:pin(root+'/'+name+'/package.json')}));
fs.writeFileSync(output+'/INPUTS.json',JSON.stringify({tool:pin(npm),node:pin(process.execPath),inputs},null,2));
const env={...process.env};
const policy=JSON.parse(fs.readFileSync('D:/work/sm05-short-tool-debug-feasibility-source-v2/COMMANDS.json')).environment;
for(const key of Object.keys(env))if([...policy.remove,'PATH','COMSPEC'].some(n=>n.toUpperCase()===key.toUpperCase()))delete env[key];
Object.assign(env,policy.set);env.ComSpec='C:\\Windows\\System32\\cmd.exe';env.Path=path.dirname(process.execPath)+';C:\\Windows\\System32;C:\\Windows';
const results=[];let cursor=0;
async function worker(){while(cursor<inputs.length){const input=inputs[cursor++],folder=output+'/'+input.name.replaceAll('/','-');fs.mkdirSync(folder);
 const args=[npm,'audit','--json','--ignore-scripts','--prefix',root+'/'+input.name,'--cache',output+'/npm-cache'];
 const c=await standard.capture(folder,'audit',process.execPath,args,{runner,cwd:root,env,timeoutMs:180000,drainMs:2000});
 const v=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 let audit;try{audit=JSON.parse(fs.readFileSync(folder+'/audit-stdout.bin','utf8'));}catch(error){audit={error:error.message};}
 const stable=[input.lock,input.package].every(item=>pin(item.path).sha256===item.sha256);
 const natural=c.result.closureConfirmed&&!c.result.stopRequested&&!c.result.fallbackUsed&&!v.terminal.terminatedOwnedJob&&v.terminal.activeProcesses===0;
 const vulnerabilities=audit.metadata?.vulnerabilities||null;
 const passed=stable&&natural&&c.result.exitCode===0&&c.result.supervisorExitCode===0&&vulnerabilities?.total===0;
 const result={name:input.name,passed,stable,natural,vulnerabilities,error:audit.error||null,result:c.result,pins:input};
 fs.writeFileSync(folder+'/RESULT.json',JSON.stringify(result,null,2));results.push(result);console.log(JSON.stringify({name:input.name,passed,vulnerabilities}));
}}
(async()=>{await Promise.all([worker()]);assert.equal(results.length,inputs.length);const passed=results.every(r=>r.passed);fs.writeFileSync(output+'/RESULTS.json',JSON.stringify({finishedAt:new Date().toISOString(),passed,results},null,2));assert(passed,'Audit failures retained; review each original result');})().catch(error=>{console.error(error);process.exitCode=1;});
