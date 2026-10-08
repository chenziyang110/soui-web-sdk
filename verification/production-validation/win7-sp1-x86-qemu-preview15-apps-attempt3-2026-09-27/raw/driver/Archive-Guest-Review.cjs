'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview14-scheduler',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const base=path.join(repo,'verification/production-validation/win7-sp1-x86-qemu-preview15-apps-attempt3-2026-09-27');
const m=id.readJson(path.join(base,'raw-manifest.json')),commandRoot=path.join(__dirname,'command-logs/review-guest-attempt3');
const r=id.readJson(path.join(commandRoot,'result.json'));assert.equal(r.exitCode,0);assert.equal(r.timedOut,false);assert.equal(r.error,null);
for(const stream of ['stdout','stderr']){const b=fs.readFileSync(path.join(commandRoot,stream+'.log'));assert.equal(b.length,r[stream].bytes);assert.equal(id.hash(b),r[stream].sha256);}
function append(original,relative){assert(!m.files.some(f=>f.path===relative));const b=fs.readFileSync(original),dest=path.join(base,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,b,{flag:'wx'});m.files.push({path:relative,original,bytes:b.length,sha256:id.hash(b)});}
for(const n of id.walk(commandRoot))append(path.join(commandRoot,n),'raw/review-command/'+n);
append(__filename,'raw/driver/Archive-Guest-Review.cjs');
fs.writeFileSync(path.join(base,'raw-manifest.json'),id.encoded(m));
console.log(JSON.stringify({rawFiles:m.files.length,reviewExitCode:r.exitCode,guestApplicationExecuted:true}));
