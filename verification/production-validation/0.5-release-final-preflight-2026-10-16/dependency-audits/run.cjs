'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',e=require(root+'/tools/internal/dependency-evidence.cjs');
const standard=require(root+'/tools/internal/standard-build.cjs');
const npm=path.join(path.dirname(process.execPath),'node_modules/npm/bin/npm-cli.js');
const scopes=['sdk/web','sdk/bindings/react','sdk/bindings/vue','templates/desktop-app/web',
 'templates/modern-app/web','demos/workbench/web','demos/reference-workbench/web-modern',
 'demos/reference-workbench/web-compat','tests/acceptance','tests/gui-bindings','tests/gui-bindings/react18'];
const originalSource=e.pin(root+'/source-manifest.json'),tools=[__filename,process.execPath,npm].map(e.pin);
fs.writeFileSync(__dirname+'/INPUTS.json',JSON.stringify({sourceManifest:originalSource,tools,
 scopes,operation:'npm audit --json --package-lock-only --ignore-scripts; dependency contents unchanged'},null,2)+'\n',{flag:'wx'});
async function audit(scope){
 const folder=path.join(__dirname,scope.replaceAll('/','-'));fs.mkdirSync(folder);
 const pins=['package.json','package-lock.json'].map(name=>e.pin(root+'/'+scope+'/'+name));
 const args=[npm,'--prefix',root+'/'+scope,'audit','--json','--package-lock-only','--ignore-scripts'];
 fs.writeFileSync(folder+'/INPUTS.json',JSON.stringify({scope,pins,args},null,2)+'\n',{flag:'wx'});
 const c=await standard.capture(folder,'audit',process.execPath,args,{runner:'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe',
  cwd:root,env:{...process.env},timeoutMs:120000,drainMs:2000});
 const observed=standard.verifyStream(c.rawReport,JSON.parse(c.contextBytes.toString('utf8')),c.ready,c.result);
 pins.forEach(e.checkPin);tools.forEach(e.checkPin);e.checkPin(originalSource);
 const natural=observed.terminal.closureConfirmed&&observed.terminal.activeProcesses===0&&!observed.terminal.terminatedOwnedJob&&
  !c.result.stopRequested&&!c.result.fallbackUsed&&c.result.exitCode===c.result.supervisorExitCode;
 let report,parseError;
 try{report=JSON.parse(fs.readFileSync(folder+'/audit-stdout.bin','utf8'));}catch(error){parseError=error.message;}
 const vulnerabilities=report?.metadata?.vulnerabilities||null;
 const completed=natural&&vulnerabilities!==null&&!report?.error;
 const passed=completed&&vulnerabilities.total===0&&c.result.exitCode===0;
 const result={scope,pins,natural,completed,passed,vulnerabilities,parseError,error:report?.error,
  result:c.result,terminal:observed.terminal,sourceManifest:originalSource};
 fs.writeFileSync(folder+'/RESULT.json',JSON.stringify(result,null,2)+'\n',{flag:'wx'});
 console.log(JSON.stringify({scope,natural,completed,passed,vulnerabilities,error:report?.error}));return e.pin(folder+'/RESULT.json');
}
(async()=>{
 const records=[];
 for(let index=0;index<scopes.length;index+=2){
  const pair=await Promise.allSettled(scopes.slice(index,index+2).map(audit));
  for(const item of pair){if(item.status==='rejected')throw item.reason;records.push(item.value);}
 }
 const results=records.map(row=>JSON.parse(fs.readFileSync(row.path)));
 fs.writeFileSync(__dirname+'/AUDIT-RESULTS.json',JSON.stringify({sourceManifest:originalSource,tools,records,
  completed:results.every(row=>row.completed),passed:results.every(row=>row.passed),
  currentRegistryAuditOnly:true,binaryLicenseReviewComplete:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
 if(!results.every(row=>row.passed))process.exitCode=1;
})().catch(error=>{console.error(error);process.exitCode=1;});
