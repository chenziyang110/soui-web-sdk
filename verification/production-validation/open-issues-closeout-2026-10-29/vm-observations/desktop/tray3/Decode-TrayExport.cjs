'use strict';
// Offline transport decode only. Never executes guest/product/controller code.
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const {TextDecoder}=require('node:util');
const root=__dirname;
const tag='ISSUESHORT832C1A66DBB049618D74D90C38D9E717';
const serial='C:/Users/11034/.codex/task-vms/soui-devtools-qemu-stable-e89cf96f2d1e4685bbdac347cb9eea3d/private-fe2e6175cf5a4a78b23d418330506ee3/postsettings-cpu-nehalem-3f486e90/runs/166925c3-9fd3-4751-9a17-c45d880af0a6/serial.log';
const producer='F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-build/Guest-Export.ps1';
const limit=8388608;
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function need(ok,reason){if(!ok)throw Error(reason);}
function plain(p){const s=fs.lstatSync(p);need(s.isFile()&&!s.isSymbolicLink(),'Expected plain file');return s;}
function pin(p){plain(p);const b=fs.readFileSync(p);return {path:p,bytes:b.length,sha256:hash(b)};}
function utf8(b){const text=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(b);need(Buffer.from(text,'utf8').equals(b),'UTF-8 bytes did not round-trip');return text;}
function from64(s,label){need(typeof s==='string'&&s.length%4===0&&/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(s),label+' Base64 syntax invalid');const b=Buffer.from(s,'base64');need(b.toString('base64')===s,label+' Base64 is not canonical');return b;}
function parse(b){const s=utf8(b);return JSON.parse(s.charCodeAt(0)===0xfeff?s.slice(1):s);}
function relFromOriginal(p,expectedRun){
  need(typeof p==='string'&&p.length<1024,'Invalid original path');
  const match=/^C:\\issue-closeout-evidence\\([a-z0-9-]{1,80})\\(.+)$/i.exec(p);
  need(match,'Original path outside task evidence root');
  need(!expectedRun||match[1].toLowerCase()===expectedRun.toLowerCase(),'Multiple original evidence roots');
  need(path.win32.normalize(p)===p,'Original path has noncanonical navigation');
  const segments=match[2].split('\\');
  need(segments.length>0&&segments.every(s=>s&&s!=='.'&&s!=='..'&&!/[<>:"/\\|?*\x00-\x1f]/.test(s)&&!/[. ]$/.test(s)&&! /^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)/i.test(s)),'Original path has unsafe or aliased components');
  const rel=segments.join('/');
  const target=path.resolve(root,'raw',...segments);
  need(target.startsWith(path.resolve(root,'raw')+path.sep),'Decoded path escapes destination');
  return {run:match[1],relative:rel,target};
}
function write(name,bytes){const target=path.resolve(root,...name.split('/'));need(target.startsWith(root+path.sep),'Output path escapes root');fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});}
function walk(d,prefix=''){const files=[];for(const e of fs.readdirSync(d,{withFileTypes:true})){need(!e.isSymbolicLink(),'Output link rejected');const name=prefix+e.name;if(e.isDirectory())files.push(...walk(path.join(d,e.name),name+'/'));else{need(e.isFile(),'Non-file output rejected');files.push(name);}}return files.sort();}
async function main(){
  need(process.argv.length===2,'No parameters supported; exact selected input is pinned in this decoder');
  need(JSON.stringify(fs.readdirSync(root).sort())===JSON.stringify(['Decode-TrayExport.cjs']),'Refuse output reuse or overwrite');
  const startedAtUtc=new Date().toISOString();
  const decoder=pin(__filename),node=pin(process.execPath),producerBefore=pin(producer);
  const fd=fs.openSync(serial,'r');let initial,final,offset=0,lineStart=0,buffer=Buffer.alloc(0),state=null,frame=null,frameCount=0,encoded=null;
  const begin=Buffer.from(tag+'BEGIN'),end=Buffer.from(tag+'END');
  function line(raw){
    const start=lineStart;offset+=raw.length;lineStart=offset;
    let bytes=raw;if(bytes.at(-1)===10)bytes=bytes.subarray(0,-1);if(bytes.at(-1)===13)bytes=bytes.subarray(0,-1);
    if(bytes.equals(begin)){need(state===null&&frameCount===0,'Duplicate or nested selected BEGIN');state='data';frame={start,parts:[Buffer.from(raw)]};return;}
    if(bytes.equals(end)){need(state==='end','Unmatched or premature selected END');frame.parts.push(Buffer.from(raw));frame.end=offset;frameCount++;state=null;return;}
    if(state!==null){need(state==='data','Unexpected extra line inside selected frame');need(bytes.length>0&&bytes.length<=24*1024*1024&&[...bytes].every(c=>c>=0x20&&c<=0x7e),'Selected envelope line exceeds ASCII/size boundary');encoded=bytes.toString('ascii');frame.parts.push(Buffer.from(raw));state='end';}
  }
  try{
    initial=fs.fstatSync(fd);need(initial.isFile()&&initial.size>0&&Number.isSafeInteger(initial.size),'Invalid source serial metadata');
    const stream=fs.createReadStream(serial,{fd,autoClose:false,start:0,end:initial.size-1,highWaterMark:65536});
    for await(const chunk of stream){buffer=Buffer.concat([buffer,chunk]);let n;while((n=buffer.indexOf(10))>=0){line(buffer.subarray(0,n+1));buffer=buffer.subarray(n+1);}need(buffer.length<=24*1024*1024,'Unbounded serial line refused');}
    if(buffer.length)line(buffer);final=fs.fstatSync(fd);
  }finally{fs.closeSync(fd);}
  need(state===null&&frameCount===1&&frame&&encoded!==null,'Missing complete unique selected frame');
  need(offset===initial.size,'Serial snapshot read did not cover initial boundary');
  const frameRaw=Buffer.concat(frame.parts),envelopeBytes=from64(encoded,'Envelope');
  need(envelopeBytes.length<=16*1024*1024,'Envelope too large');
  const envelopeText=utf8(envelopeBytes),envelope=JSON.parse(envelopeText);
  need(envelope&&typeof envelope==='object'&&!Array.isArray(envelope),'Envelope must be an object');
  const catalogBytes=from64(envelope['ExportFiles.actual.json'],'Catalog');need(catalogBytes.length<=limit,'Catalog too large');
  const catalog=parse(catalogBytes);
  need(catalog&&catalog.schema==='soui.issue-short-export-files/v1'&&Array.isArray(catalog.files)&&catalog.files.length>0&&catalog.files.length<=512,'Catalog schema/count mismatch');
  const expectedKeys=['ExportFiles.actual.json',...Array.from({length:catalog.files.length},(_,i)=>'file'+String(i).padStart(4,'0'))];
  need(JSON.stringify(Object.keys(envelope))===JSON.stringify(expectedKeys),'Envelope key set/order differs from catalog and complete declared file set');
  need(JSON.stringify(envelope)===envelopeText,'Envelope is not the exact compact ordered producer JSON');
  need(Number.isSafeInteger(catalog.totalBytes)&&catalog.totalBytes>=0,'Invalid declared catalog byte total');
  let run=null,sum=0;const relativeNames=new Set(['exportfiles.actual.json']);const files=[];
  for(let i=0;i<catalog.files.length;i++){
    const row=catalog.files[i];need(row&&Number.isSafeInteger(row.bytes)&&row.bytes>=0&&row.bytes<=16777216&&typeof row.sha256==='string'&&/^[a-f0-9]{64}$/.test(row.sha256),'Invalid catalog pin');
    const target=relFromOriginal(row.path,run);run??=target.run;
    need(!relativeNames.has(target.relative.toLowerCase()),'Duplicate or aliased catalog path');relativeNames.add(target.relative.toLowerCase());
    const bytes=from64(envelope[expectedKeys[i+1]],'Payload '+i);
    need(bytes.length===row.bytes&&hash(bytes)===row.sha256,'Payload bytes/SHA differ from original catalog');
    sum+=bytes.length;need(sum+catalogBytes.length<=limit,'Original total export limit exceeded');
    files.push({originalPath:row.path,path:target.relative,bytes:bytes.length,sha256:row.sha256,data:bytes});
  }
  need(sum===catalog.totalBytes,'Catalog aggregate bytes mismatch');
  const summaryFile=files.find(f=>f.path==='Summary.actual.json');need(summaryFile,'Tray summary missing');const summary=parse(summaryFile.data);need(summary.schema==='soui.current-public-desktop-short-execution/v1'&&Array.isArray(summary.selected)&&summary.selected.length===1&&summary.selected[0]==='tray-x86','Selected export is not the exact tray-x86 controller run');
  need(JSON.stringify(pin(producer))===JSON.stringify(producerBefore),'Producer changed during decode');
  need(JSON.stringify(pin(__filename))===JSON.stringify(decoder),'Decoder changed during decode');
  write('Serial.frame.raw',frameRaw);write('Envelope.json.raw',envelopeBytes);write('raw/ExportFiles.actual.json',catalogBytes);
  for(const file of files)write('raw/'+file.path,file.data);
  const expectedRaw=['ExportFiles.actual.json',...files.map(f=>f.path)].sort();need(JSON.stringify(walk(root+'/raw'))===JSON.stringify(expectedRaw),'Decoded raw file set is not closed');
  for(const f of [{path:'ExportFiles.actual.json',bytes:catalogBytes.length,sha256:hash(catalogBytes)},...files]){const b=fs.readFileSync(path.join(root,'raw',...f.path.split('/')));need(b.length===f.bytes&&hash(b)===f.sha256,'Written raw file differs from decoded original');}
  const actual={derivative:{"schema":"soui.offline-decoder-derivative/v1","original":{"path":"F:/work/SOUI_WEB/out/open-issues-closeout-20261029/tray-build/decoded-resident1/Decode-TrayExport.cjs","bytes":11249,"sha256":"40865d2c33e4e1e4ade854ca9fed71086a7867816ce74b474ebffce5a20d9d20"},"edits":["Select exact new tray3 tag","Selection description only","Require exact tray-x86 selected run","Selection description only"],"unchangedGuards":["canonical Base64","round-trip fatal UTF-8","unique complete selected frame","bounded exact envelope/catalog order","safe original evidence paths","case-insensitive closed output set","all declared file sizes and SHA-256","aggregate byte total"],"productOrVmExecution":false},schema:'soui.issue-short-serial-decoded/v1',startedAtUtc,completedAtUtc:new Date().toISOString(),scope:'Offline decode of one selected tag only; no application/VM/test execution or product qualification',tag,
    sourceSerial:{path:serial,bytesAtOpen:initial.size,bytesAtClose:final.size,readBoundaryBytes:initial.size,wholeSerialCopied:false,otherTagsCopied:false},
    originalFrame:{byteOffset:frame.start,bytes:frameRaw.length,sha256:hash(frameRaw),path:'Serial.frame.raw'},
    envelope:{path:'Envelope.json.raw',bytes:envelopeBytes.length,sha256:hash(envelopeBytes),canonicalBase64:true,exactCompactOrderedJson:true},
    catalog:{path:'raw/ExportFiles.actual.json',originalPath:'C:\\issue-closeout-evidence\\'+run+'\\ExportFiles.actual.json',bytes:catalogBytes.length,sha256:hash(catalogBytes),declaredFiles:catalog.files.length,declaredFileBytes:catalog.totalBytes},
    decodedSummarySelected:summary.selected,decodedSummaryNativeExecutionAndNaturalClosePassed:summary.nativeExecutionAndNaturalClosePassed,payloadFileCount:files.length,rawFileCountIncludingCatalog:files.length+1,payloadFileBytes:sum,rawBytesIncludingCatalog:sum+catalogBytes.length,
    files:files.map(({data,...f})=>f),base64CanonicalForAll:true,originalPathsConfined:true,caseInsensitiveClosedSetVerified:true,allOriginalBytesAndSha256Verified:true,
    producer:producerBefore,decoder,node,argv:process.argv,cwd:process.cwd(),pid:process.pid,testPassed:null,browserPassClaimed:false,uiInteractionQualified:false,releaseReady:false,productionQualificationComplete:false,fullDevtoolsGoalComplete:false};
  write('Decode.actual.json',Buffer.from(JSON.stringify(actual,null,2)+'\n'));
  const indexFiles=walk(root).map(name=>{const b=fs.readFileSync(path.join(root,...name.split('/')));return {path:name,bytes:b.length,sha256:hash(b)};});
  write('Files.index.json',Buffer.from(JSON.stringify({schema:'soui.issue-short-decoded-files/v1',tag,policy:'All files except this index to avoid self-pin; includes decoder and exact selected frame/envelope/catalog/payload bytes',files:indexFiles},null,2)+'\n'));
  console.log(JSON.stringify({tag,payloadFiles:files.length,rawFileCountIncludingCatalog:files.length+1,payloadFileBytes:sum,rawBytesIncludingCatalog:sum+catalogBytes.length,catalogSha256:hash(catalogBytes),frameSha256:hash(frameRaw),decode:root+'/Decode.actual.json',allOriginalBytesAndSha256Verified:true,testPassed:null}));
}
main().catch(error=>{console.error('Decode refused: '+error.message);process.exitCode=1;});
