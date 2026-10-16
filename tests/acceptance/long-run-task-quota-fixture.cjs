'use strict';
const policy=require('./long-run-policy.cjs');
const cap='134217728';
function fixture(mode='session'){
  const config=policy.fixedConfig(mode),subject={pid:71,creationFileTime:'134000000000000001'};
  const records=[],live=new Map();let tick=9007199254740993n,epoch=0n;
  const clock=()=>({valid:true,qpc:String(tick+=100n),frequency:'10000000',captureThreadId:'42',phase:'synchronous-native-read-complete'});
  const view=(id,retired=false)=>{
    const life=live.get(id),begin=tick+=100n,end=tick+=10n;
    return {window:String(id),ownerAlive:true,hostClosed:retired,lifetime:retired?'after-host-close-before-owner-destruction':'owner-alive',captureThreadId:'42',
      bridge:{task_service:{observation:{state:'ready',owner_epoch:String(life.epoch+100n)}}},
      taskStoreRequestQuota:{schema:'soui.acceptance-task-store-request-quota/1',scope:'task-store-private-request-byte-quota',state:'available',getterStatus:'available',captureState:'valid',maxLiveRequestedBytes:cap,poolEpoch:String(life.epoch),poolEpochValid:true,creditValid:true,chargedLiveRequestedBytes:'4',chargedPeakRequestedBytes:'16',successfulLiveRequestedBytes:'4',successfulPeakRequestedBytes:'8',deniedRequests:'0',deniedRequestsExact:true,diagnosticsServiceEpoch:String(life.epoch+100n),diagnosticsServiceState:'ready',capture:{windowId:String(id),windowHandle:String(id+12300),pid:String(subject.pid),creationFileTime:subject.creationFileTime,captureThreadId:'42',windowThreadId:'42',identityValid:true,clock:{source:'QueryPerformanceCounter',unit:'ticks',frequency:'10000000',begin:String(begin),end:String(end),valid:true}},productionDefaultGate:'not-evaluated',fullCacheBudgetGate:'not-evaluated',calibrationPin:null}};
  };
  const sample=(phase,cycle)=>{const views=[...live.keys()].map(id=>view(id));records.push({event:'sample',phase,cycle,nativeSdk:{producer:'native-sdk-owner',pid:String(subject.pid),creationFileTime:subject.creationFileTime,captureThreadId:'42',views,captureClock:clock()}});};
  const mount=(id,cycle)=>{live.set(id,{epoch:++epoch});records.push({event:'step.complete',action:'mount',window:id,cycle,nativeRenderer:{captureClock:clock()}});};
  const close=(id,cycle)=>{const retiredOwner=view(id,true);live.delete(id);records.push({event:'step.complete',action:'close',window:id,cycle,retiredOwner,nativeRenderer:{captureClock:clock()}});};
  records.push({event:'run.begin',...config,...subject});sample('process',0);
  if(mode==='session'){mount(1,0);sample('anchor',0);}
  for(let cycle=1;cycle<=2;cycle++){
    records.push({event:'cycle.begin',cycle});if(mode==='mixed')mount(1,cycle);
    mount(2,cycle);mount(3,cycle);close(3,cycle);close(2,cycle);
    if(mode==='mixed'){mount(2,cycle);close(2,cycle);close(1,cycle);}
    sample(mode==='session'?'anchor':'cycle',cycle);records.push({event:'cycle.complete',cycle});
  }
  if(mode==='session'){records.push({event:'session.end'});sample('anchor-finished',3);close(1,3);}
  sample('closed',3);sample('runtime-closed',3);records.push({event:'run.complete'});
  const binding={schema:'soui.long-task-store-quota-binding/1',inputsMatched:true,inputBindingScope:'complete-acceptance-inputs',sourceSha256:'a'.repeat(64),payloadSha256:'b'.repeat(64),consumerBuildId:'SYNTHETIC-MODEL-NOT-NATIVE',rawLogSha256:'0'.repeat(64),ownedLogSha256:'c'.repeat(64),contextSha256:'d'.repeat(64),subject,executablePin:{path:'C:/model/SouiAcceptance.exe',bytes:1,sha256:'e'.repeat(64)},expectedPolicy:cap,expectedRun:config};
  return {records,binding};
}
module.exports={fixture};
