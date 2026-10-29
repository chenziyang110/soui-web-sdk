'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process');
const root='C:/sc07',output=path.join(root,'dist/sdk/0.6.0'),folder=path.join(__dirname,'canonical-pack-'+crypto.randomUUID());fs.mkdirSync(folder);
if(fs.existsSync(output))throw Error('Existing candidate output must not be overwritten');
const args=['tools/sdk/soui-sdk.cjs','pack','--source',root,'--output',output,'--version','0.6.0','--retain-failed-publication'];
const started=new Date().toISOString(),r=cp.spawnSync(process.execPath,args,{cwd:root,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:1024*1024});
fs.writeFileSync(path.join(folder,'stdout.log'),r.stdout||'');fs.writeFileSync(path.join(folder,'stderr.log'),r.stderr||'');
const result={schema:'soui.canonical-candidate-sdk-pack/v1',args,started,finished:new Date().toISOString(),exitCode:r.status,signal:r.signal,error:r.error?.message||null,
 output,sourceCandidateOnly:true,publishedReleaseModified:false,productionQualified:false};
if(r.status===0){
 const sdk=require(path.join(root,'tools/sdk/soui-sdk.cjs')),id=require(path.join(root,'tools/internal/delivery-identity.cjs'));
 const manifest=sdk.readPackage(output),actual=id.walk(output).sort(),expected=['sdk-manifest.json',...manifest.files.map(row=>'payload/'+row.path)].sort();
 if(JSON.stringify(actual)!==JSON.stringify(expected))throw Error('Published candidate file set differs');
 const raw=fs.readFileSync(path.join(output,'sdk-manifest.json'));result.manifest={bytes:raw.length,sha256:crypto.createHash('sha256').update(raw).digest('hex')};
 result.files=manifest.files.length;result.payloadBytes=manifest.files.reduce((n,row)=>n+row.bytes,0);result.verified=true;
 fs.writeFileSync(path.join(folder,'sdk-manifest.json'),raw,{flag:'wx'});
}else{result.verified=false;}
fs.writeFileSync(path.join(folder,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({folder,...result}));process.exitCode=result.verified?0:1;
