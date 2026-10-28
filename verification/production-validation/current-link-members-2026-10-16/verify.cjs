'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),zlib=require('node:zlib'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../..');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8'));
const summary=read(path.join(__dirname,'SUMMARY.json'));
for(const arch of ['x64','x86']){
  const row=read(path.join(__dirname,arch+'.json'));
  const original=read(path.join(root,'verification/production-validation/0.5-post-upgrade-rebuild-2026-10-16/kernel',arch,'build-Miniblink.json'));
  assert.deepEqual(row.map,original.kernelLlvm.map);assert.deepEqual(row.dll,original.kernelLlvm.dll);
  const gzip=fs.readFileSync(path.join(__dirname,arch+'.map.gz'));
  assert.equal(hash(gzip),row.archivedMap.sha256);assert.equal(gzip.length,row.archivedMap.bytes);
  const map=zlib.gunzipSync(gzip);assert.equal(hash(map),row.map.sha256);assert.equal(map.length,row.map.bytes);
  const dll=fs.readFileSync(path.join(root,'third_party/prebuilt',arch,'miniblink/node.dll'));
  assert.equal(hash(dll),row.dll.sha256);assert.equal(dll.length,row.dll.bytes);
  const lines=map.toString('utf8').split(/\r?\n/),groups=new Map();let total=0;
  for(const line of lines){
    const m=line.match(/^\s+([\da-f]{4}):([\da-f]{8})\s+(\S+)\s+([\da-f]{8,16})(?:\s+(?:(?:f|i)\s+)*(\S.*?))?\s*$/i);
    if(!m){assert(!/^\s+[\da-f]{4}:[\da-f]{8}\s+/i.test(line)||/\s+[\da-f]+H\s+/i.test(line));continue;}
    total++;const origin=m[5]||'<unattributed>',colon=origin.indexOf(':'),library=origin==='<unattributed>'?origin:colon<0?'<direct>':origin.slice(0,colon),member=colon<0?origin:origin.slice(colon+1);
    if(!groups.has(library))groups.set(library,{library,symbolRows:0,members:new Set()});
    const g=groups.get(library);g.symbolRows++;g.members.add(member);
  }
  assert.equal(total,row.symbolRows);
  assert.deepEqual([...groups.values()].map(g=>({...g,members:[...g.members].sort(),memberCount:g.members.size})).sort((a,b)=>a.library.localeCompare(b.library)),row.groups);
  const pe=dll.readUInt32LE(0x3c),optional=pe+24,base=dll.readUInt16LE(optional)===0x20b?Number(dll.readBigUInt64LE(optional+24)):dll.readUInt32LE(optional+28);
  assert.equal(dll.readUInt32LE(pe+8),row.pe.timestamp);assert.equal(base,row.pe.preferredBase);
  assert.equal(parseInt(lines.find(x=>x.includes('Timestamp is')).match(/Timestamp is ([a-f\d]+)/i)[1],16),row.pe.timestamp);
  for(const item of row.openssl.examples){
    const at=optional+dll.readUInt16LE(pe+20)+(item.section-1)*40;
    assert.equal(base+dll.readUInt32LE(at+12)+item.offset,item.address);
    const start=dll.readUInt32LE(at+20)+item.offset;
    assert.equal(dll.subarray(start,start+16).toString('hex'),item.machineCodeFirst16Hex);
    assert(lines[item.line-1].includes(item.symbol)&&lines[item.line-1].endsWith('openssl:'+item.member));
  }
  assert.equal(row.openssl.memberCount,groups.get('openssl').members.size);
  assert.equal(row.openssl.symbolRows,groups.get('openssl').symbolRows);
  assert.equal(row.releaseReady,false);
}
assert.equal(summary.binaryLicenseReviewComplete,false);assert.equal(summary.releaseReady,false);
console.log(JSON.stringify({architectures:2,opensslMembers:{x64:486,x86:489},rawMapsVerified:true,exactDllCodeVerified:true,releaseReady:false}));
