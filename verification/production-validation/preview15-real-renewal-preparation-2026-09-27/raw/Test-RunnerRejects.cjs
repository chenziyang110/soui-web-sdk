'use strict';
// Two early binding rejections only. No current compiled-input positive is claimed.
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const root=__dirname,hash=b=>crypto.createHash('sha256').update(b).digest('hex'),encode=o=>JSON.stringify(o,null,2)+'\n';
const run=fs.readFileSync(path.join(root,'Run.ps1')),text=run.toString('utf8'),prefix="$checkerSource = @'",start=text.indexOf(prefix)+prefix.length;
assert(start>prefix.length);const newline=text.slice(start,start+2)==='\r\n'?'\r\n':'\n';assert(text.startsWith(newline,start));const end=text.indexOf(newline+"'@",start+newline.length);assert(end>start);
const checker=Buffer.from(text.slice(start+newline.length,end));
const target=path.join(root,'runner-rejection-self-test');assert(!fs.existsSync(target));fs.mkdirSync(target);fs.writeFileSync(path.join(target,'checker.cjs'),checker,{flag:'wx'});
const rows=[];
for(const [name,freeze,error] of [
 ['old-clock-parent',fs.readFileSync(path.join(root,'provenance/clock-parent-freeze.json')),'Wrong compiled freeze'],
 ['current-version-wrong-parent',Buffer.from(encode({schema:'soui.real-renewal-preparation/v1',status:'compiled-inputs-frozen',version:'0.3.0-preview.15',sourceInputSha256:'b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07',fixtureRevision:2,parent:{freezeSha256:'0'.repeat(64)}})),'Wrong v2 fixture revision or parent freeze']
]){
 const base=path.join(target,name),fixture=path.join(base,'fixture'),output=path.join(base,'output');fs.mkdirSync(fixture,{recursive:true});fs.mkdirSync(output);
 fs.writeFileSync(path.join(fixture,'freeze.json'),freeze,{flag:'wx'});
 const args=[path.join(target,'checker.cjs'),'before',fixture,output,'x86',process.execPath],startedAt=new Date().toISOString();
 const result=cp.spawnSync(process.execPath,args,{windowsHide:true,encoding:null,timeout:30000});
 const stdout=result.stdout||Buffer.alloc(0),stderr=result.stderr||Buffer.alloc(0);
 fs.writeFileSync(path.join(base,'stdout.log'),stdout,{flag:'wx'});fs.writeFileSync(path.join(base,'stderr.log'),stderr,{flag:'wx'});
 const row={name,program:process.execPath,args,startedAt,finishedAt:new Date().toISOString(),exitCode:result.status,signal:result.signal,error:result.error?.message||null,expectedRejection:error,checkerSha256:hash(checker),runSha256:hash(run),freezeSha256:hash(freeze),stdout:{bytes:stdout.length,sha256:hash(stdout)},stderr:{bytes:stderr.length,sha256:hash(stderr)},syntheticOnly:true,guiExecuted:false};
 fs.writeFileSync(path.join(base,'result.json'),encode(row),{flag:'wx'});rows.push(row);
 assert.equal(result.status,1);assert(!result.error);assert.equal(stderr.toString('utf8').split(/\r?\n/).find(l=>l.startsWith('Error: ')),'Error: '+error);assert(!fs.existsSync(path.join(output,'identity-before.json')));
}
assert.equal(hash(fs.readFileSync(path.join(root,'Run.ps1'))),hash(run));
fs.writeFileSync(path.join(target,'summary.json'),encode({status:'two-early-rejections-passed',cases:rows.length,syntheticOnly:true,actualCurrentInputPositives:0,guiExecuted:false,realRenewalVerified:false,releaseReady:false}),{flag:'wx'});
console.log(JSON.stringify({status:'two-early-rejections-passed',cases:rows.length,actualCurrentInputPositives:0,guiExecuted:false}));
