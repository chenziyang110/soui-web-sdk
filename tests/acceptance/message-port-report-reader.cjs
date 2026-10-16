'use strict';
// Supplemental only: Root must first verify the original acceptance log and actual Owned result.
const cases=[
  ['fifo-bidirectional',56,56,120,'right:0..31;left:0..23;asynchronous;120ms-quiet'],
  ['drained-refill',6,6,0,'0,1,2,3,4,5;20ms-after-each-receipt'],
  ['transfer',9,9,120,'courier:one-port;queued:0..3;reverse:0..3;old-port-no-delivery;120ms-quiet'],
  ['close-later-send',33,1,240,'one-callback-close;later-turn16-sends;closed-before16-sends'],
  ['callback-reentry',24,24,120,'ping/pong:0..11;callback-post;120ms-quiet'],
  ['worker-fifo',17,17,120,'ready;echo:0..15;worker-receive-ordinal:0..15;120ms-quiet'],
  ['worker-cooperative-close',3,null,120,null],
  ['worker-terminate',2,null,120,null],
];
function insist(value,message){if(!value)throw new Error(message);}
function exactKeys(value,keys){return value&&typeof value==='object'&&!Array.isArray(value)&&
  JSON.stringify(Object.keys(value).sort())===JSON.stringify([...keys].sort());}
function validateMessagePortReports(rows,ownedIdentity,expectedDocumentCounts){
  insist(Number.isSafeInteger(ownedIdentity?.pid)&&ownedIdentity.pid>0&&ownedIdentity.pid<=0xffffffff&&
    typeof ownedIdentity.creationFileTime==='string'&&/^[1-9][0-9]{0,19}$/.test(ownedIdentity.creationFileTime)&&
    BigInt(ownedIdentity.creationFileTime)<=18446744073709551615n,'Actual held subject identity is required.');
  insist(exactKeys(expectedDocumentCounts,['1','2','3'])&&Object.values(expectedDocumentCounts).every(value=>
    Number.isSafeInteger(value)&&value>=0)&&Object.values(expectedDocumentCounts).reduce((a,b)=>a+b,0)>0,
    'Expected actual document counts must come from original mount/reload steps.');
  insist(Array.isArray(rows)&&rows.length>0,'Actual sidecar is missing.');
  const sessions=new Map(),counts={'1':0,'2':0,'3':0};let elapsed=-1;
  for(const row of rows){
    insist(exactKeys(row,['event','elapsedMs','window','pid','creationFileTime','sessionId','scopeId','data'])&&
      row.event==='message-port.report'&&row.pid===ownedIdentity.pid&&row.creationFileTime===ownedIdentity.creationFileTime&&
      Number.isSafeInteger(row.elapsedMs)&&row.elapsedMs>=0&&row.elapsedMs>=elapsed&&[1,2,3].includes(row.window)&&
      typeof row.sessionId==='string'&&row.sessionId.length>0&&row.sessionId.length<=256&&
      typeof row.scopeId==='string'&&row.scopeId.length>0&&row.scopeId.length<=256,'Native sidecar identity/schema/timing differs.');
    elapsed=row.elapsedMs;
    const data=row.data;
    insist(exactKeys(data,['sequence','caseId','status','sent','received','quietMs','trace']), 'Observation schema differs.');
    insist(Number.isSafeInteger(data.sequence)&&data.sequence>=0&&data.sequence<=9&&
      Number.isSafeInteger(data.sent)&&data.sent>=0&&data.sent<=100&&
      Number.isSafeInteger(data.received)&&data.received>=0&&data.received<=100&&
      Number.isSafeInteger(data.quietMs)&&data.quietMs>=0&&data.quietMs<=1000&&
      typeof data.trace==='string'&&data.trace.length<=2048,'Observation scalar types or bounds differ.');
    if(!sessions.has(row.sessionId)){
      insist(data.sequence===0,'Document does not start with sequence zero.');
      sessions.set(row.sessionId,{window:row.window,scopeId:row.scopeId,rows:[]});counts[String(row.window)]++;
    }
    const session=sessions.get(row.sessionId);
    insist(row.window===session.window&&row.scopeId===session.scopeId&&data.sequence===session.rows.length,
      'Document identity or sequential observation changed.');
    const index=session.rows.length;
    const expected=index===0?['run',0,0,0,'explicit-browser-api','begin']:
      index===9?['run',0,0,0,'eight-finite-cases;worker-native-join/debugger-wait-unobserved','end']:
      index>=1&&index<=8?[...cases[index-1],'pass']:null;
    const pulses=index===7||index===8?data.received-data.sent:null;
    const workerStopValid=pulses!==null&&Number.isSafeInteger(pulses)&&pulses>=1&&pulses<=32&&
      data.trace==='ready;armed;pulse:0..'+String(pulses-1)+(index===7?
        ';closing;self.close-request;120ms-quiet;native-join-unobserved':
        ';terminate-request;120ms-quiet;native-join-unobserved');
    insist(expected&&data.caseId===expected[0]&&data.sent===expected[1]&&
      (index===7||index===8?workerStopValid:data.received===expected[2]&&data.trace===expected[4])&&
      data.quietMs===expected[3]&&data.status===expected[5],
      'Browser case failed, unavailable, missing, duplicated, or different.');
    session.rows.push(row);
  }
  insist([...sessions.values()].every(session=>session.rows.length===10)&&
    JSON.stringify(counts)===JSON.stringify(expectedDocumentCounts),'Missing actual document/end or extra epoch.');
  return {schema:'soui.message-port-browser-behavior/v1',observedDocuments:sessions.size,
    observedCases:sessions.size*8,messageChannelBehaviorObserved:true,dedicatedWorkerMessagingObserved:true,
    boundedWorkerMessageSuppressionObserved:true,workerNativeJoinObserved:false,genericDebuggerWaitKillQualified:false,
    polyfillSchedulerBranchObserved:false,resourcePassed:false,osQualified:false,releaseReady:false};
}
module.exports={validateMessagePortReports};
