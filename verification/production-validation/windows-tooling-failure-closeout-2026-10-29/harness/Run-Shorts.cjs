'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process');
const root='C:/sc07',base=__dirname,out=path.join(base,'shorts-'+crypto.randomUUID());fs.mkdirSync(out);
const files=cp.execFileSync('git',['diff','--name-only'],{cwd:root,encoding:'utf8'}).trim().split(/\r?\n/).filter(Boolean)
 .concat(cp.execFileSync('git',['ls-files','--others','--exclude-standard'],{cwd:root,encoding:'utf8'}).trim().split(/\r?\n/).filter(Boolean));
const pins=()=>files.map(name=>{const bytes=fs.readFileSync(path.join(root,name));return {path:name,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')};});
fs.writeFileSync(path.join(out,'sources-before.json'),JSON.stringify(pins(),null,2)+'\n');
const groups=[
 ['changed-contracts',[],['tests/sdk/msvc-compiler-lifecycle.test.cjs','tests/sdk/msbuild-entrypoint-policy.test.cjs',
  'tests/sdk/sdk-publication-recovery.test.cjs','tests/sdk/sdk-publication-diagnostics.test.cjs','tests/sdk/archive-stage.test.cjs']],
 ['changed-preparation-fingerprint',['--test-name-pattern=compiler lifecycle input drift|unchanged source and matching'],['tests/benchmark/measurement-preparation-source.test.cjs']],
 ['archive-tool-identity-compatibility',['--test-name-pattern=tool identity'],['tests/sdk/compression-profiles.test.cjs']]
];
const results=[];
for(const [name,options,tests]of groups){
 const args=['--test','--test-reporter=tap','--test-concurrency=1',...options,...tests],start=new Date().toISOString();
 const result=cp.spawnSync(process.execPath,args,{cwd:root,encoding:'utf8',windowsHide:true,timeout:180000,maxBuffer:32*1024*1024});
 fs.writeFileSync(path.join(out,name+'.stdout.log'),result.stdout||'');fs.writeFileSync(path.join(out,name+'.stderr.log'),result.stderr||'');
 const row={name,args,start,finish:new Date().toISOString(),exitCode:result.status,signal:result.signal,error:result.error?.message||null};
 for(const key of ['tests','pass','fail','skipped'])row[key]=Number(new RegExp('# '+key+' (\\d+)').exec(result.stdout||'')?.[1]??NaN);
 fs.writeFileSync(path.join(out,name+'.actual.json'),JSON.stringify(row,null,2)+'\n');results.push(row);
}
const after=pins();fs.writeFileSync(path.join(out,'sources-after.json'),JSON.stringify(after,null,2)+'\n');
const unchanged=JSON.stringify(after)===fs.readFileSync(path.join(out,'sources-before.json'),'utf8').trim().replace(/\s+/g,'') /* overwritten below by structural comparison */;
const sourcesUnchanged=JSON.stringify(after)===JSON.stringify(JSON.parse(fs.readFileSync(path.join(out,'sources-before.json'),'utf8')));
const passed=sourcesUnchanged&&results.every(r=>r.exitCode===0&&r.fail===0);
fs.writeFileSync(path.join(out,'Summary.actual.json'),JSON.stringify({schema:'soui.windows-tooling-short/v1',results,sourcesUnchanged,passed,qualificationPassed:false},null,2)+'\n');
console.log(JSON.stringify({out,results,sourcesUnchanged,passed}));process.exitCode=passed?0:1;
