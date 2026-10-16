'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root='C:/work/sm05-release-050',relative='verification/production-validation/0.5-npm-audit-2026-10-16',target=root+'/'+relative;
assert(!fs.existsSync(__dirname+'/source20-preparation-1'),'Source20 already sealing; defer this addition');assert(!fs.existsSync(target));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),records=[];
const secrets=Object.entries(process.env).filter(([name,value])=>/TOKEN|SECRET|PASSWORD|API_KEY|PRIVATE_KEY/i.test(name)&&value&&value.length>=8).flatMap(([name,value])=>['utf8','utf16le'].map(encoding=>({name,value:Buffer.from(value,encoding)})));
function copy(source,name){const bytes=fs.readFileSync(source);assert(!secrets.some(secret=>bytes.includes(secret.value)),'Credential-like environment value found in source; do not copy');const to=target+'/'+name;fs.mkdirSync(path.dirname(to),{recursive:true});fs.writeFileSync(to,bytes,{flag:'wx'});assert.equal(hash(fs.readFileSync(to)),hash(bytes));records.push({path:name,bytes:bytes.length,sha256:hash(bytes),original:source});}
const first='C:/work/sm05-current-lock-audit-1',second='C:/work/sm05-current-lock-audit-2';
const a=JSON.parse(fs.readFileSync(first+'/RESULTS.json')),b=JSON.parse(fs.readFileSync(second+'/RESULTS.json'));assert(b.passed);
const latest=new Map(a.results.map(row=>[row.name,row]));for(const row of b.results)latest.set(row.name,row);assert.equal(latest.size,12);
for(const row of latest.values()){assert(row.passed&&row.natural&&row.stable);assert.equal(row.vulnerabilities.total,0);for(const pin of [row.pins.lock,row.pins.package])assert.equal(hash(fs.readFileSync(pin.path)),pin.sha256);}
for(const [folder,tag] of [[first,'first'],[second,'retry']]){
 copy(folder+'/INPUTS.json',tag+'/INPUTS.json');copy(folder+'/RESULTS.json',tag+'/RESULTS.json');
 for(const row of JSON.parse(fs.readFileSync(folder+'/RESULTS.json')).results){const name=row.name.replaceAll('/','-');for(const file of fs.readdirSync(folder+'/'+name)){assert(fs.statSync(folder+'/'+name+'/'+file).isFile());copy(folder+'/'+name+'/'+file,tag+'/'+name+'/'+file);}}
}
for(const file of ['audit-current-locks-1.cjs','audit-current-locks-2.cjs',path.basename(__filename)])copy(__dirname+'/'+file,'recipes/'+file);
const rows=[...latest.values()].map(row=>({scope:row.name,passed:true,vulnerabilities:row.vulnerabilities,lock:row.pins.lock,package:row.pins.package,originalPass:row.name.startsWith('tests/gui-bindings')?'retry':'first'}));
fs.writeFileSync(target+'/index.json',JSON.stringify({schema:'soui.npm-audit-archive/v1',packages:12,rows,files:records,knownCredentialEnvironmentScanPassed:true,historicalConnectionFailuresRetained:true,releaseReady:false},null,2)+'\n');
fs.writeFileSync(target+'/README.md','# 当前 npm 锁文件审计\n\n2026-10-16 在同一候选的 12 个 Web、绑定、应用与测试工程执行完整 npm audit。10 个首轮成功；两个组件测试工程首轮发生 TLS 连接中断，保留原始失败后单独重试成功。最终 12 个工程均为零已知漏洞，记录退出码、自然进程组关闭、原始 JSON 和执行前后不变的 package/lock 散列。\n\n这项结果只覆盖登记时的 npm 漏洞数据库与当前锁文件，不认证原生依赖安全、许可、应用行为或正式发布。index.json 指向每项成功原件；first 下的两个网络失败保持失败。\n');
const attrs=root+'/.gitattributes';fs.appendFileSync(attrs,'\n# Preserve original npm audit reports and process observations.\n'+relative+'/** -text -eol whitespace=cr-at-eol,-blank-at-eof,-blank-at-eol\n');
const git='C:/Program Files/Git/cmd/git.exe';let r=cp.spawnSync(git,['add','-f','--',relative,'.gitattributes'],{cwd:root,windowsHide:true});assert.equal(r.status,0,r.stderr?.toString());r=cp.spawnSync(git,['diff','--cached','--check'],{cwd:root,windowsHide:true});assert.equal(r.status,0,r.stdout?.toString());
fs.writeFileSync(__dirname+'/current-audits-archive-1.json',JSON.stringify({archive:relative,packages:12,rawFiles:records.length,allCurrentLocksMatched:true,staged:true,sourceSealPending:true,releaseReady:false},null,2),{flag:'wx'});
console.log(JSON.stringify({archive:relative,packages:12,rawFiles:records.length,allCurrentLocksMatched:true}));
