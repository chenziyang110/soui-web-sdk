'use strict';
const fs=require('node:fs'),path=require('node:path'),zlib=require('node:zlib'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const {Transform}=require('node:stream'),{pipeline}=require('node:stream/promises');
const root='C:/work/sm05-release-050',arch=process.argv[2];assert(['x64','x86'].includes(arch));
const build='C:/work/sm05-kernel-release-050-'+arch+'-1',capture=__dirname+'/kernel-'+arch+'-1';
const archive=root+'/verification/production-validation/0.5-kernel-rebuild-2026-10-15/'+arch;
assert(!fs.existsSync(archive));fs.mkdirSync(archive,{recursive:true});
const reader=require(root+'/tools/internal/dependency-evidence.cjs');
const secrets=Object.entries(process.env).filter(([key,value])=>/TOKEN|SECRET|PASSWORD|API_KEY|PRIVATE_KEY/i.test(key)&&value&&value.length>=8)
 .flatMap(([name,value])=>['utf8','utf16le'].map(encoding=>({name,bytes:Buffer.from(value,encoding)})));
const overlap=Math.max(1,...secrets.map(x=>x.bytes.length)),matches=new Set();
class Scan extends Transform{
 constructor(label){super();this.label=label;this.tail=Buffer.alloc(0);this.bytes=0;this.hash=crypto.createHash('sha256');}
 _transform(chunk,encoding,done){this.bytes+=chunk.length;this.hash.update(chunk);const window=Buffer.concat([this.tail,chunk]);
  for(const secret of secrets)if(window.includes(secret.bytes))matches.add(this.label+':'+secret.name);
  this.tail=window.subarray(Math.max(0,window.length-overlap));done(null,chunk);}
}
const rows=[],decodedScans=[];
async function copy(source,relative,compress=false){
 const target=path.join(archive,relative),before=reader.pin(source),scan=new Scan(relative);fs.mkdirSync(path.dirname(target),{recursive:true});
 await pipeline(fs.createReadStream(source),scan,...(compress?[zlib.createGzip({level:6})]:[]),fs.createWriteStream(target,{flags:'wx'}));
 assert.equal(scan.bytes,before.bytes);assert.equal(scan.hash.digest('hex'),before.sha256);reader.checkPin(before);
 const stored=reader.pin(target);rows.push({path:relative,bytes:stored.bytes,sha256:stored.sha256,encoding:compress?'gzip':'identity',original:before});
 if(source.endsWith('.binlog')){
  const decoded=new Scan(relative+':decompressed');decoded.resume();await pipeline(fs.createReadStream(source),zlib.createGunzip(),decoded);
  decodedScans.push({path:relative,bytes:decoded.bytes,sha256:decoded.hash.digest('hex')});
 }
}
(async()=>{
 const proof=reader.verifyComponent(root,arch,'kernel',{report:build+'/build-Miniblink.json',capture:capture+'/kernel'},'F:/work/sm05/out/benchmark-protocol/Release/OwnedProcessRunner.exe');
 const api=JSON.parse(fs.readFileSync(capture+'/public-api-audit.json','utf8'));assert(api.sdkRuntimeRequiredExportsPreserved&&api.legacyMiniblinkFunctionExportsPreserved);
 for(const entry of fs.readdirSync(build,{withFileTypes:true}))if(entry.isFile())await copy(build+'/'+entry.name,'build/'+entry.name+(entry.name.endsWith('.log')?'.gz':''),entry.name.endsWith('.log'));
 for(const entry of fs.readdirSync(build+'/libraries',{withFileTypes:true}))if(entry.isFile()&&entry.name.endsWith('.binlog'))await copy(build+'/libraries/'+entry.name,'build/libraries/'+entry.name);
 await copy(build+'/miniblink/build.binlog','build/miniblink/build.binlog');
 const map=JSON.parse(fs.readFileSync(build+'/build-Miniblink.json','utf8')).kernelLlvm.map;await copy(map.path,'build/miniblink/'+path.basename(map.path)+'.gz',true);
 const tlogs=build+'/miniblink/obj/miniblink.tlog';for(const entry of fs.readdirSync(tlogs,{withFileTypes:true}))if(entry.isFile())await copy(tlogs+'/'+entry.name,'build/miniblink/tlog/'+entry.name+'.gz',true);
 for(const entry of fs.readdirSync(capture,{withFileTypes:true}))if(entry.isFile())await copy(capture+'/'+entry.name,'capture/'+entry.name);
 await copy(__filename,'archive-recipe.cjs');
 const report={schema:'soui.kernel-evidence-archive/v1',architecture:arch,sourceIdentity:proof.sourceIdentity,files:rows,
  credentialScan:{scope:'Known credential-valued environment entries, including decompressed binlogs',names:[...new Set(secrets.map(x=>x.name))],matches:[...matches],decodedScans,passed:matches.size===0},
  buildAndProcessClosureVerified:true,sdkRuntimeRequiredExportsPreserved:api.sdkRuntimeRequiredExportsPreserved,allOldExportsPreserved:api.allOldExportsPreserved,
  fullSdkApplicationValidationComplete:false,licenseReviewComplete:false,releaseReady:false};
 fs.writeFileSync(archive+'/index.json',JSON.stringify(report,null,2)+'\n');assert.equal(matches.size,0,'Credential scan failed; do not publish archive');
 assert(rows.every(r=>r.bytes<100*1024*1024),'Archive member exceeds repository delivery boundary');
 console.log(JSON.stringify({arch,sourceIdentity:proof.sourceIdentity,files:rows.length,storedBytes:rows.reduce((n,r)=>n+r.bytes,0),credentialScanPassed:matches.size===0,archive}));
})().catch(e=>{console.error(e);process.exitCode=1;});
