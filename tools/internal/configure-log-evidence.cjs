'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const id=require('./delivery-identity.cjs'),{parseStrictJson}=require('./owned-tail-evidence.cjs');
const schema='soui.cmake-configure-log-capture/v1',filename='cmake-configure-log.input.yaml';
const need=(ok,message)=>{if(!ok)throw Error('Configure log capture '+message);};
function shape(value){
 need(value&&Object.keys(value).sort().join(',')==='bytes,path,sha256'&&typeof value.path==='string'&&path.isAbsolute(value.path)&&
  Number.isSafeInteger(value.bytes)&&value.bytes>0&&value.bytes<=67108864&&/^[a-f0-9]{64}$/.test(value.sha256),'pin differs');
}
function match(value){
 shape(value);const file=id.safePath(value.path),stat=fs.statSync(file);need(stat.isFile()&&stat.size===value.bytes,'file size differs');
 need(crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')===value.sha256,'file bytes differ');
}
function validate(binding,{buildDirectory,observationPath,observerContextBytes,evidencePins,requireLive=false}){
 need(binding&&Object.keys(binding).sort().join(',')==='live,schema,snapshot'&&binding.schema===schema,'schema differs');
 shape(binding.live);shape(binding.snapshot);
 need(path.isAbsolute(buildDirectory)&&path.isAbsolute(observationPath)&&path.basename(observationPath)==='llvm-observation.json','owner paths differ');
 const live=path.resolve(buildDirectory,'CMakeFiles/CMakeConfigureLog.yaml'),snapshot=path.resolve(path.dirname(observationPath),filename);
 need(path.resolve(binding.live.path)===live&&path.resolve(binding.snapshot.path)===snapshot,'capture ownership differs');
 need(binding.live.bytes===binding.snapshot.bytes&&binding.live.sha256===binding.snapshot.sha256,'copied identity differs');
 need(Buffer.isBuffer(observerContextBytes),'original observer context missing');
 const context=parseStrictJson(observerContextBytes.toString('utf8')),args=context.args;
 need(context.schema==='soui.owned-process-context/v1'&&Array.isArray(args)&&args.filter(v=>v==='-ObservationPath').length===1,'observer output selection differs');
 const declared=args[args.indexOf('-ObservationPath')+1];
 need(typeof declared==='string'&&path.isAbsolute(declared)&&path.resolve(declared)===path.resolve(observationPath),'actual observer output differs');
 need(Array.isArray(evidencePins),'evidence inventory missing');
 const at=p=>evidencePins.filter(row=>row&&typeof row.path==='string'&&path.resolve(row.path)===p);
 const copies=at(snapshot);
 need(at(live).length===0&&copies.length===1&&copies[0].bytes===binding.snapshot.bytes&&copies[0].sha256===binding.snapshot.sha256,'snapshot inventory differs');
 match(binding.snapshot);if(requireLive)match(binding.live);
 return {captured:true,liveRechecked:requireLive,scope:'historical CMake configure log only; current cache/compiler/source/object/link pins remain independently required'};
}
module.exports={schema,filename,validate};
