'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {decodeMarkerHex,distribution,checkDesktopTrace}=require('../../benchmarks/protocol/desktop-visual.cjs');
// Authored protocol fixtures, never native desktop observations.
function marker(ack=0,frame=0,motion=false,scene=5,nonce=123) {
  const bytes=Buffer.alloc(16);bytes.writeUInt16LE(0x53a7);bytes.writeUInt32LE(nonce,2);
  bytes.writeUIntLE(ack,6,3);bytes.writeUInt16LE(frame,9);bytes[11]=scene|(motion?8:0);
  let sum=0xffff;for(let i=0;i<12;++i){sum^=bytes[i]<<8;for(let bit=0;bit<8;++bit)sum=((sum<<1)^((sum&0x8000)?0x1021:0))&65535;}
  bytes.writeUInt16LE(sum,12);return bytes.toString('hex');
}
function fixture() {
  const begin={event:'desktop.begin',schema:'soui.desktop-visual-trace/v1',pid:11,creationFileTime:'134360000000000001',collectorPid:22,collectorCreationFileTime:'134360000000000003',hwnd:'12345',scene:5,probe:false,qpcFrequency:'1000',startedQpc:'1000000',originX:10,originY:10,dpi:96,rotation:0,region:[8,8,64,32]};
  begin.adapterLow=77;begin.adapterHigh=0;
  const rows=[begin,{event:'desktop.output',deviceName:'\\\\.\\DISPLAY1',left:0,top:0,right:1920,bottom:1080,adapterLow:77,adapterHigh:0}];let count=0;
  const add=(qpc,ack,frame,motion=false,scene=5)=>{
    rows.push({event:'desktop.acquire',lastPresentQpc:String(qpc),accumulatedFrames:1,protected:false});
    if(!count)rows.push({event:'desktop.ready',lastPresentQpc:String(qpc),nonce:123,ack});
    rows.push({event:'desktop.frame',lastPresentQpc:String(qpc),observedQpc:String(qpc+1),nonce:123,ack,frame,scene,motion,markerHex:marker(ack,frame,motion,scene),width:1100,height:720});++count;
  };
  add(1000001,0,0,false,1);add(1001020,1,0);add(1001520,2,0);
  for(let i=0;i<=120;++i)add(1002020+i*1000,3,i+1,true);
  add(1123020,3,121,false);
  rows.push({event:'desktop.end',completed:true,frames:count,pointerOnly:0,endedQpc:'1123100'});
  const input=[{event:'run.begin',schema:'soui.external-input-trace/v2',scene:'S05',mode:'full',pid:11,processCreationTime100ns:begin.creationFileTime,expectedCreationTime100ns:begin.creationFileTime,qpcFrequency:1000}];
  ['select-scene','select-scene-confirm','start-motion'].forEach((action,index)=>{
    const before=[1001000,1001500,1002000][index];input.push({event:'input',action,elapsedMs:before-1000000,qpcBefore:before,qpcAfter:before+1,desktopEventId:index+1,desktopQpcBefore:String(before),desktopQpcAfter:String(before+1)});
  });
  input.push({event:'input',action:'finish-scene',qpcBefore:1123200,qpcAfter:1123201,elapsedMs:123200},{event:'run.end',actions:4,elapsedMs:123201});
  const options={ownerSubject:{pid:11,creationFileTime:begin.creationFileTime},collectorSubject:{pid:22,creationFileTime:begin.collectorCreationFileTime},pageMarker:{schema:'soui.visual-marker/v1',nonce:123,devicePixelRatio:1,region:[8,8,64,32]}};
  return {input,rows,options};
}
function check(value) {return checkDesktopTrace(value.input,value.rows,'S05',value.options);}
test('real meaning and signed time bounds stay finite and never claim physical/performance pass',()=>{
  const result=check(fixture());assert.equal(result.desktopObservationAccepted,true);assert.equal(result.visualResponseUpperMs.p95,20);assert.equal(result.markerFrameIntervalsMs.p95,1000);
  assert.equal(result.physicalPresentation,false);assert.equal(result.performanceGate,false);assert.equal(result.releaseReady,false);
});
test('absence of caller verified held identities remains unbound',()=>{const f=fixture();f.options={};assert.equal(check(f).desktopObservationAccepted,false);});
test('adjacent native FILETIME is not rounded together',()=>{const f=fixture();f.options.ownerSubject.creationFileTime='134360000000000002';assert.throws(()=>check(f),/owned subjects/);});
test('marker CRC corruption is refused',()=>{const raw=Buffer.from(marker(),'hex');raw[4]^=1;assert.throws(()=>decodeMarkerHex(raw.toString('hex')),/CRC/);});
test('marker reserved fields and malformed length are refused',()=>{const raw=Buffer.from(marker(),'hex');raw[15]=1;assert.throws(()=>decodeMarkerHex(raw.toString('hex')),/reserved/);assert.throws(()=>decodeMarkerHex('a753'),/bytes/);});
test('20 bit ACK admits boundary and rejects overflow without wrap',()=>{assert.equal(decodeMarkerHex(marker(0xfffff)).ack,0xfffff);assert.throws(()=>decodeMarkerHex(marker(0x100000)),/fields/);});
test('zero marker incarnation is refused',()=>{assert.throws(()=>decodeMarkerHex(marker(0,0,false,5,0)),/fields/);});
test('merged native desktop frames cannot become successful samples',()=>{const f=fixture();f.rows.find(row=>row.event==='desktop.acquire').accumulatedFrames=2;assert.throws(()=>check(f),/Merged/);});
test('pointer-only is counted separately and cannot carry image timestamp',()=>{const f=fixture();f.rows.splice(2,0,{event:'desktop.pointer',lastPresentQpc:'0',accumulatedFrames:0,observedQpc:'1000001'});f.rows.at(-1).pointerOnly=1;assert.equal(check(f).desktopObservationAccepted,true);f.rows[2].lastPresentQpc='1000001';assert.throws(()=>check(f),/Pointer-only/);});
test('unknown/raw decoded marker mismatch refuses fabricated counters',()=>{const f=fixture();f.rows.find(row=>row.event==='desktop.frame').ack=1;assert.throws(()=>check(f),/raw bytes/);});
test('ACK jump and missing event are not silently dropped',()=>{const f=fixture();f.input[2].desktopEventId=3;assert.throws(()=>check(f),/contiguous/);});
test('trace failure or insufficient duration remains a failure',()=>{const f=fixture();f.rows.at(-1).completed=false;assert.throws(()=>check(f),/incomplete/);f.rows.at(-1).completed=true;f.rows.at(-1).endedQpc='1122999';assert.throws(()=>check(f),/shorter/);});
test('geometry and page pixel ratio must agree with the native region',()=>{const f=fixture();f.options.pageMarker.devicePixelRatio=1.25;assert.throws(()=>check(f),/DPR/);f.options.pageMarker.devicePixelRatio=1;f.rows.find(row=>row.event==='desktop.frame').width=1099;assert.throws(()=>check(f),/geometry/);});
test('QPC beyond uint64 or malformed strings cannot be coerced',()=>{const f=fixture();f.rows[0].startedQpc='18446744073709551616';assert.throws(()=>check(f),/exact/);f.rows[0].startedQpc='1e6';assert.throws(()=>check(f),/exact/);});
test('latency retains negative lower bound when ACK occurs during SendInput',()=>{const f=fixture();f.input[1].qpcAfter=1001021;f.input[1].desktopQpcAfter='1001021';const result=check(f);assert.equal(result.samples[0].lowerMs,-1);assert.equal(result.samples[0].upperMs,20);});
test('quantile tail includes every slow observation',()=>{assert.deepEqual(distribution([1,2,3,100]),{samples:4,p50:2,p95:100,p99:100,min:1,max:100});assert.throws(()=>distribution([]),/Missing/);});
test('output LUID and region cannot silently move to another adapter',()=>{const f=fixture();f.rows[1].adapterLow=78;assert.throws(()=>check(f),/adapter/);});
test('capture probe has an explicit finite result and never becomes a full round',()=>{const f=fixture();f.rows[0].probe=true;f.input[0].mode='probe';f.options.probe=true;const result=check(f);assert.equal(result.desktopProbeAccepted,true);assert.equal(result.desktopObservationAccepted,false);assert.equal(result.observationScope,'probe');});
