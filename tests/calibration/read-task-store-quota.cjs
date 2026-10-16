'use strict';
// Pure quantitative reader. Caller must independently bind actual build/payload,
// native process capture and complete original stdout; this cannot grant release.
const {isUtf8}=require('node:buffer');
const {parseStrictJson}=require('../../tools/internal/owned-tail-evidence.cjs');
const U64=(1n<<64n)-1n,I64=(1n<<63n)-1n;
function need(ok,message){if(!ok)throw new Error('TaskStore calibration: '+message);}
function uint(value){need(typeof value==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(value),'canonical uint64 required');const n=BigInt(value);need(n<=U64,'uint64 overflow');return n;}
function shape(row,keys){need(row&&typeof row==='object'&&!Array.isArray(row)&&Object.keys(row).length===keys.length&&keys.every(k=>Object.hasOwn(row,k)),'record fields differ');}
const phases=['empty','full-history','full-inflight','released-inflight','retired-history'];
const fields=['schema','phase','pid','creationFileTime','qpcBegin','qpcEnd','qpcFrequency','poolEpoch','configuredLimit',
 'chargedLive','chargedPeak','successfulLive','successfulPeak','quotaDenials','storeEntries','inflight','queued','handled','privateBytes','handles','productionDefaultQualified'];
function read(raw,subject,limit=null){
 need(Buffer.isBuffer(raw)&&raw.length>0&&raw.length<=32768&&isUtf8(raw),'bounded original UTF-8 stdout required');
 const text=raw.toString('utf8');need(text.endsWith('\n')&&!text.startsWith('\uFEFF'),'complete framing required');
 const lines=text.replaceAll('\r\n','\n').slice(0,-1).split('\n');need(lines.length===6&&lines.every(s=>s.length>0&&s.length<=4096),'complete six-record workload required');
 const rows=lines.map(parseStrictJson);
 need(Number.isSafeInteger(subject?.pid)&&subject.pid>0&&subject.pid<=0xffffffff&&uint(subject.creationFileTime)>0n,'actual native subject required');
 const cap=limit===null?null:uint(limit);let epoch=null,frequency=null,lastEnd=0n,lastPeak=0n,lastSuccessfulPeak=0n,maxPrivate=0n;
 for(let i=0;i<phases.length;++i){
  const row=rows[i];shape(row,fields);need(row.schema==='soui.native-task-store-calibration/1'&&row.phase===phases[i]&&row.productionDefaultQualified===false,'phase or scope differs');
  need(row.pid===subject.pid&&row.creationFileTime===subject.creationFileTime,'native process identity differs');
  const currentEpoch=uint(row.poolEpoch),currentFrequency=uint(row.qpcFrequency),begin=uint(row.qpcBegin),end=uint(row.qpcEnd);
  need(currentEpoch>0n&&currentEpoch<U64&&currentFrequency>0n&&currentFrequency<=I64&&begin>0n&&begin<=end&&end<=I64&&begin>=lastEnd,'owner or monotonic envelope invalid');
  if(i){need(currentEpoch===epoch&&currentFrequency===frequency,'owner or clock changed');}epoch=currentEpoch;frequency=currentFrequency;lastEnd=end;
  need(row.configuredLimit===limit,'configured policy differs');
  const live=uint(row.chargedLive),peak=uint(row.chargedPeak),successful=uint(row.successfulLive),successfulPeak=uint(row.successfulPeak);
  need(successful<=live&&live<=peak&&successful<=successfulPeak&&successfulPeak<=peak&&peak>=lastPeak&&successfulPeak>=lastSuccessfulPeak,'credit/peak invariant failed');
  need(uint(row.quotaDenials)===0n&&(cap===null||peak<=cap),'quota refused work or exceeded fixed cap');lastPeak=peak;lastSuccessfulPeak=successfulPeak;
  for(const key of ['storeEntries','inflight','queued','handled','handles'])need(Number.isSafeInteger(row[key])&&row[key]>=0&&row[key]<=0xffffffff,'bounded integer differs');
  need(row.storeEntries===(i?1024:0)&&row.inflight===(i===2?32:0)&&row.handled===[0,1024,1024,1056,1120][i],'required history/inflight/completed workload differs');
  need(i===2?(row.queued>=30&&row.queued<=32):row.queued===0,'queue occupancy differs');
  const memory=uint(row.privateBytes);need(memory>0n,'process memory unavailable');if(memory>maxPrivate)maxPrivate=memory;
 }
 const final=rows[5];shape(final,['schema','phase','postDestructionQuotaRead','productionDefaultQualified']);
 need(final.schema==='soui.native-task-store-calibration/1'&&final.phase==='destructor-returned'&&final.postDestructionQuotaRead===false&&final.productionDefaultQualified===false,'destructor return scope differs');
 return {schema:'soui.native-task-store-calibration-summary/1',subject,limit,poolEpoch:epoch.toString(),chargedPeak:lastPeak.toString(),successfulPeak:lastSuccessfulPeak.toString(),
  maxObservedProcessPrivateBytes:maxPrivate.toString(),workloadComplete:true,nativeProcessClosureVerified:false,productionDefaultQualified:false};
}
module.exports={read};
