'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process');
const root='C:/sc07',out=path.join(__dirname,'repairs-'+crypto.randomUUID());fs.mkdirSync(out);
const groups=[
 ['archive-affected-fixtures','archive tool runtime changes|optional tool identity|existing destinations|CLI preserves a native extraction|PS5 metadata|actual ZIP metadata','tests/sdk/archive-stage.test.cjs'],
 ['msvc-model',null,'tests/sdk/msvc-compiler-lifecycle.test.cjs'],
 ['sdk-cli-and-native-recovery','CLI retention|real PS5 descendant','tests/sdk/sdk-publication-recovery.test.cjs']
];
const results=[];
for(const [name,pattern,file]of groups){
 const args=['--test','--test-reporter=tap','--test-concurrency=1',...(pattern?['--test-name-pattern='+pattern]:[]),file];
 const source=fs.readFileSync(path.join(root,file)),sourceSha256=crypto.createHash('sha256').update(source).digest('hex');
 const start=new Date().toISOString(),r=cp.spawnSync(process.execPath,args,{cwd:root,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:32*1024*1024});
 fs.writeFileSync(path.join(out,name+'.stdout.log'),r.stdout||'');fs.writeFileSync(path.join(out,name+'.stderr.log'),r.stderr||'');
 const row={name,args,sourceSha256,start,finish:new Date().toISOString(),exitCode:r.status,signal:r.signal,error:r.error?.message||null};
 for(const key of ['tests','pass','fail','skipped'])row[key]=Number(new RegExp('# '+key+' (\\d+)').exec(r.stdout||'')?.[1]??NaN);
 fs.writeFileSync(path.join(out,name+'.actual.json'),JSON.stringify(row,null,2)+'\n');results.push(row);
}
const passed=results.every(r=>r.exitCode===0&&r.fail===0);
fs.writeFileSync(path.join(out,'Summary.actual.json'),JSON.stringify({schema:'soui.targeted-repair-short/v1',results,passed,qualificationPassed:false},null,2)+'\n');
console.log(JSON.stringify({out,results,passed}));process.exitCode=passed?0:1;
