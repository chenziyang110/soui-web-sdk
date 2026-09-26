'use strict';
// Exercise the actual gate query and authorization condition without starting a compiler.
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),vm=require('node:vm'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=__dirname,label=process.argv[2];assert(/^[a-z0-9-]+$/.test(label||''));
const dir=path.join(root,'gate-selftests',label);assert(!fs.existsSync(dir));fs.mkdirSync(dir,{recursive:true});
const file=path.join(root,'Build-Sdk.cjs'),source=fs.readFileSync(file,'utf8'),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const begin=source.indexOf('function gate(){'),end=source.indexOf('async function main(){');assert(begin>=0&&end>begin);
const rejectStart=source.indexOf('if(g.exitCode'),rejectEnd=source.indexOf('){console.log',rejectStart);assert(rejectStart>=0&&rejectEnd>rejectStart);
const condition=source.slice(rejectStart+3,rejectEnd),gateSource=source.slice(begin,end)+'\ngate';
const env={...process.env,PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
const fixtures=[
 {name:'query-error-must-block',blocked:true,prefix:"function Get-CimInstance { [CmdletBinding()] param([string]$ClassName) Write-Error 'synthetic CIM provider unavailable' };"},
 {name:'observed-qemu-must-block',blocked:true,prefix:"function Get-CimInstance { [CmdletBinding()] param([string]$ClassName) [pscustomobject]@{Name='qemu-system-i386.exe';ProcessId=7654321;CreationDate='fixture'} };"},
 {name:'verified-empty-query-may-proceed',blocked:false,prefix:"function Get-CimInstance { [CmdletBinding()] param([string]$ClassName) @() };"},
 {name:'stderr-despite-zero-must-block',blocked:true,result:{status:0,stdout:'',stderr:'provider warning: query incomplete',error:undefined}},
 {name:'unexpected-stdout-must-block',blocked:true,result:{status:0,stdout:'unexpected provider output',stderr:'',error:undefined}},
 {name:'process-start-error-must-block',blocked:true,result:{status:null,stdout:'',stderr:'',error:{message:'synthetic process start failure'}}}
];
const cases=[];
for(const fixture of fixtures){let invocation=null;
 const capture={spawnSync(program,args,options){invocation={program,args:[...args.slice(0,-1),(fixture.prefix||'')+args.at(-1)],windowsHide:options.windowsHide};return fixture.result||cp.spawnSync(program,invocation.args,options);}};
 const gate=vm.runInNewContext(gateSource,{cp:capture,env,Date});const observed=gate();
 const blocked=Boolean(vm.runInNewContext(condition,{g:observed}));
 cases.push({name:fixture.name,expectedBlocked:fixture.blocked,blocked,passed:blocked===fixture.blocked,actualPowerShellQuery:!!fixture.prefix,invocation,observed});
}
const report={sourceSha256:sha(Buffer.from(source)),cases,passed:cases.filter(c=>c.passed).length,failed:cases.filter(c=>!c.passed).length,compilerStarted:false,syntheticOnly:true,releaseReady:false};
fs.writeFileSync(path.join(dir,'result.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({cases:cases.length,passed:report.passed,failed:report.failed,compilerStarted:false,sourceSha256:report.sourceSha256}));
if(report.failed)process.exitCode=1;
