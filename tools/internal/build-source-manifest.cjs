'use strict';
// A historical capture input, never a substitute for current compiled-input
// validation or the final full-source archive verification.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const identity=require('./delivery-identity.cjs');
const schema='soui.build-source-manifest-input/v1',name='source-manifest.input.json';
function need(value,message){if(!value)throw new Error('Build source manifest '+message);}
function shape(pin){
  need(pin&&Object.keys(pin).sort().join(',')==='bytes,path,sha256'&&typeof pin.path==='string'&&path.isAbsolute(pin.path)&&
    Number.isSafeInteger(pin.bytes)&&pin.bytes>0&&pin.bytes<=67108864&&/^[a-f0-9]{64}$/.test(pin.sha256),'pin is invalid');
}
function pin(filename){
  const full=identity.safePath(path.resolve(filename)),stat=fs.statSync(full);
  need(stat.isFile()&&stat.size>0&&stat.size<=67108864,'input must be a bounded plain file');
  const bytes=fs.readFileSync(full);
  return {path:full,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')};
}
function match(expected){
  shape(expected);const actual=pin(expected.path);
  need(actual.bytes===expected.bytes&&actual.sha256===expected.sha256,'bytes changed: '+expected.path);
  return actual;
}
function same(a,b){return path.resolve(a.path)===path.resolve(b.path)&&a.bytes===b.bytes&&a.sha256===b.sha256;}
function capture(root,folder){
  const live=pin(path.join(root,'source-manifest.json'));
  const destination=identity.safePath(path.join(folder,name));
  fs.writeFileSync(destination,fs.readFileSync(live.path),{flag:'wx'});
  const binding={schema,live,snapshot:pin(destination)};
  need(binding.live.bytes===binding.snapshot.bytes&&binding.live.sha256===binding.snapshot.sha256,'copy differs');
  assertLive(binding);
  return binding;
}
function assertLive(binding){match(binding.live);match(binding.snapshot);}
function validate(binding,root,folder,controls){
  need(Array.isArray(controls),'control list is missing');
  const livePath=path.resolve(root,'source-manifest.json'),snapshotPath=path.resolve(folder,name);
  const liveRows=controls.filter(row=>row&&typeof row.path==='string'&&path.resolve(row.path)===livePath);
  const snapshotRows=controls.filter(row=>row&&typeof row.path==='string'&&path.resolve(row.path)===snapshotPath);
  if(binding===undefined){
    need(liveRows.length===1&&snapshotRows.length===0,'legacy capture must retain its live input');
    match(liveRows[0]);return;
  }
  need(binding&&Object.keys(binding).sort().join(',')==='live,schema,snapshot'&&binding.schema===schema,'binding schema differs');
  shape(binding.live);shape(binding.snapshot);
  need(path.resolve(binding.live.path)===livePath&&path.resolve(binding.snapshot.path)===snapshotPath,'capture ownership differs');
  need(binding.live.bytes===binding.snapshot.bytes&&binding.live.sha256===binding.snapshot.sha256,'live/captured identity differs');
  need(liveRows.length===0&&snapshotRows.length===1&&same(snapshotRows[0],binding.snapshot),'captured control input differs');
  match(binding.snapshot);
}
module.exports={capture,assertLive,validate};
