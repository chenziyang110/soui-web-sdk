'use strict';
// Guest-only data-name repair. No file content writes/deletes, product, SDK or time-budget changes.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const [stageArg,catalogFile,mapFile]=process.argv.slice(2);assert.equal(process.argv.length,5,'stage catalog map required');
const stage=path.resolve(stageArg),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
assert.match(stage,/^C:\\s06-stage-[a-f0-9]{32}$/i);
for(const p of ['C:/sr06','C:/release06-tools','C:/release06-control'])assert.equal(fs.existsSync(p),false,'original destination must remain absent');
const candidates=fs.readdirSync('C:/',{withFileTypes:true}).filter(e=>e.isDirectory()&&/^s06-stage-/.test(e.name));
assert.equal(candidates.length,1,'one unique retained stage');assert.equal(path.resolve('C:/',candidates[0].name).toLowerCase(),stage.toLowerCase());
const catalogRaw=fs.readFileSync(catalogFile),mapRaw=fs.readFileSync(mapFile);
assert.equal(hash(catalogRaw),'826c8b819562d1c989cc62b048f5ef608255b1c73670adcb2dfa8075a1f121cc');assert.equal(hash(mapRaw),'592f8caf4549584b4d9409289baf39f1a66e612afbbcf352fdb435aa3e345659');
const catalog=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(catalogRaw)),map=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(mapRaw));
assert.equal(catalog.schema,'soui.release06-smoke-carrier/v1');assert.equal(catalog.files.length,4815);assert.equal(map.moves.length,17);
function relative(name){assert.ok(typeof name==='string'&&name&&!name.includes('\\')&&!name.includes(':')&&!name.startsWith('/')&&!name.split('/').some(s=>!s||s==='.'||s==='..'));return name;}
function full(name){const p=path.resolve(stage,relative(name));assert.ok(p.toLowerCase().startsWith(stage.toLowerCase()+path.sep));return p;}
function plain(p){for(let at=p;;at=path.dirname(at)){if(fs.existsSync(at))assert.equal(fs.lstatSync(at).isSymbolicLink(),false,'reparse refused');if(at.toLowerCase()===stage.toLowerCase())return;assert.ok(at.toLowerCase().startsWith(stage.toLowerCase()+path.sep),'stage bounds');}}
function filePin(name){const p=full(name);plain(p);const s=fs.lstatSync(p,{bigint:true});assert.ok(s.isFile()&&s.nlink===1n,'plain single-link file required');const b=fs.readFileSync(p);assert.equal(BigInt(b.length),s.size);return {path:name,bytes:b.length,sha256:hash(b),device:String(s.dev),fileId:String(s.ino)};}
function walk(r=''){const p=r?full(r):stage;plain(p);const s=fs.lstatSync(p);assert.ok(s.isDirectory());return fs.readdirSync(p).sort().flatMap(n=>{const rel=r?r+'/'+n:n;return fs.lstatSync(full(rel)).isDirectory()?walk(rel):[rel];});}
const expected=new Map(catalog.files.map(r=>[relative(r.path),r]));assert.equal(expected.size,4815);
const byTarget=new Map(),sources=new Set(),targets=new Set();
for(const move of map.moves){relative(move.from);relative(move.to);assert.notEqual(move.from,move.to);assert.equal(sources.has(move.from.toLowerCase()),false);assert.equal(targets.has(move.to.toLowerCase()),false);
 sources.add(move.from.toLowerCase());targets.add(move.to.toLowerCase());const row=expected.get(move.to);assert.ok(row);assert.equal(move.bytes,row.bytes);assert.equal(move.sha256,row.sha256);
 assert.equal(move.encoding,'windows-1252');assert.equal(move.utf8Flag,false);assert.ok(/^[a-f0-9]+$/.test(move.filenameRawHex));
 assert.equal(new TextDecoder('windows-1252').decode(Buffer.from(move.filenameRawHex,'hex')),move.from);
 assert.equal(new TextDecoder('gbk',{fatal:true}).decode(Buffer.from(move.filenameRawHex,'hex')),move.to);byTarget.set(move.to,move);
 assert.equal(fs.existsSync(full(move.to)),false,'repair target must be absent');}
const preNames=catalog.files.map(row=>byTarget.get(row.path)?.from||row.path);assert.equal(new Set(preNames.map(n=>n.toLowerCase())).size,4815);assert.deepEqual(walk().sort(),[...preNames].sort(),'complete exact preimage names; no hash guessing');
const preimage=catalog.files.map(row=>{const source=byTarget.get(row.path)?.from||row.path,p=filePin(source);assert.equal(p.bytes,row.bytes);assert.equal(p.sha256,row.sha256,source);return {...p,expectedPath:row.path};});
const evidence='C:/s06-namefix-evidence-'+crypto.randomUUID();assert.equal(fs.existsSync(evidence),false);fs.mkdirSync(evidence);
const write=(n,v)=>fs.writeFileSync(path.join(evidence,n),JSON.stringify(v,null,2)+'\n',{flag:'wx'});
write('Preimage.actual.json',{stage,catalogSha256:hash(catalogRaw),mapSha256:hash(mapRaw),files:preimage,fullExactRawPreimage:true});
const moved=[];
try{
 for(const move of map.moves){const before=filePin(move.from);assert.equal(before.bytes,move.bytes);assert.equal(before.sha256,move.sha256);assert.equal(fs.existsSync(full(move.to)),false);
  const parent=path.dirname(full(move.to));fs.mkdirSync(parent,{recursive:true});plain(parent);fs.renameSync(full(move.from),full(move.to));
  const after=filePin(move.to);assert.equal(after.bytes,move.bytes);assert.equal(after.sha256,move.sha256);assert.equal(after.device,before.device);assert.equal(after.fileId,before.fileId);moved.push({...move,device:before.device,fileId:before.fileId});}
 assert.deepEqual(walk().sort(),catalog.files.map(r=>r.path).sort());const postimage=catalog.files.map(row=>{const p=filePin(row.path);assert.equal(p.bytes,row.bytes);assert.equal(p.sha256,row.sha256);return p;});
 write('Postimage.actual.json',{stage,files:postimage,fullExactRawPostimage:true});
 write('Repair.actual.json',{schema:'soui.release06-filename-transport-repair/v1',stage,moved,evidence,
  mapBidirectionallyUnique:true,contentBytesUnchanged:true,filesCreatedOrDeleted:false,emptyOldDirectoriesPreserved:true,
  noHashBasedNameGuessing:true,productStarted:false,vmControlled:false,releaseReady:false});
 console.log('NAME_REPAIR_OK stage='+stage+' files=4815 moves=17 evidence='+evidence);
}catch(error){write('Failure.actual.json',{message:error.message,stack:error.stack,stage,moved,originalsPreserved:true,automaticRollbackOrDeletion:false});throw error;}
