// Real browser objects only. No replacement queue, scheduler, GC call or worker join claim.
export type MessagePortReport = {
  sequence:number; caseId:'run'|'fifo-bidirectional'|'drained-refill'|'transfer'|'close-later-send'|
    'callback-reentry'|'worker-fifo'|'worker-cooperative-close'|'worker-terminate';
  status:'begin'|'pass'|'failed'|'unavailable'|'end';
  sent:number; received:number; quietMs:number; trace:string;
};
export type MessagePortReportAck = {accepted:true;documentSession:string};
type Report = (value:MessagePortReport)=>Promise<MessagePortReportAck>;
type Observation = {sent:number;received:number;quietMs:number;trace:string};
const caseIds:MessagePortReport['caseId'][]=['fifo-bidirectional','drained-refill','transfer',
  'close-later-send','callback-reentry','worker-fifo','worker-cooperative-close','worker-terminate'];
const pause=(milliseconds:number)=>new Promise<void>(resolve=>setTimeout(resolve,milliseconds));
function requireValue(value:unknown,message:string):asserts value {if(!value)throw new Error(message);}
function record(value:unknown):{[key:string]:unknown} {
  requireValue(value!==null&&typeof value==='object'&&!Array.isArray(value),'Message is not an object.');
  return value as {[key:string]:unknown};
}
function payload(tag:string,kind:string,sequence:number){return {tag,kind,sequence};}
function checked(value:unknown,tag:string,kind:string,sequence:number) {
  const message=record(value);
  requireValue(Object.keys(message).length===3&&message.tag===tag&&message.kind===kind&&
    message.sequence===sequence,'Message identity or FIFO differs.');
}
function deadline<T>(work:Promise<T>,milliseconds=2000):Promise<T> {
  return new Promise<T>((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error('Real browser case timed out.')),milliseconds);
    work.then(value=>{clearTimeout(timer);resolve(value);},error=>{clearTimeout(timer);reject(error);});
  });
}
async function ports(body:(channel:MessageChannel)=>Promise<Observation>):Promise<Observation> {
  const channel=new MessageChannel();
  try {
    return await deadline(body(channel));
  } finally {
    channel.port1.close();
    channel.port2.close();
  }
}
function fifo(tag:string):Promise<Observation> {
  return ports(async channel=>{
    let left=0,right=0,posting=true,error:unknown=null;
    await new Promise<void>((resolve,reject)=>{
    const fail=(problem:unknown)=>{error=problem;reject(problem);};
    const complete=()=>{if(left===24&&right===32)resolve();};
    channel.port1.onmessage=event=>{try{requireValue(!posting&&!error&&left<24,'Synchronous or extra left delivery.');
      checked(event.data,tag,'left',left++);complete();}catch(error){fail(error);}};
    channel.port2.onmessage=event=>{try{requireValue(!posting&&!error&&right<32,'Synchronous or extra right delivery.');
      checked(event.data,tag,'right',right++);complete();}catch(error){fail(error);}};
    channel.port1.start();channel.port2.start();
    for(let i=0;i<32;++i)channel.port1.postMessage(payload(tag,'right',i));
    for(let i=0;i<24;++i)channel.port2.postMessage(payload(tag,'left',i));
    posting=false;
    });
    await pause(120);if(error)throw error;
    requireValue(left===24&&right===32,'Late FIFO delivery differs.');
    return {sent:56,received:56,quietMs:120,trace:'right:0..31;left:0..23;asynchronous;120ms-quiet'};
  });
}
async function refill(tag:string):Promise<Observation> {
  return ports(async channel=>{
    let received=0,error:unknown=null,wake:(()=>void)|null=null;
    channel.port2.onmessage=event=>{try{checked(event.data,tag,'refill',received++);
      if(wake){const next=wake;wake=null;next();}}catch(problem){error=problem;if(wake){const next=wake;wake=null;next();}}};
    channel.port2.start();
    for(let i=0;i<6;++i){
      const delivered=new Promise<void>(resolve=>{wake=resolve;});
      channel.port1.postMessage(payload(tag,'refill',i));
      await deadline(delivered);if(error)throw error;
      requireValue(received===i+1,'Refill was missing or duplicated.');
      // Allow the previous dispatch to return before the next empty-to-nonempty send.
      await pause(20);if(error)throw error;
      requireValue(received===i+1,'Extra refill delivery.');
    }
    return {sent:6,received:6,quietMs:0,trace:'0,1,2,3,4,5;20ms-after-each-receipt'};
  });
}
async function transfer(tag:string):Promise<Observation> {
  let moved:MessagePort|null=null;
  try{return await ports(data=>ports(async courier=>{
    let forward=0,backward=0,error:unknown=null;
    await new Promise<void>((resolve,reject)=>{
    const finish=()=>{if(forward===4&&backward===4)resolve();};
    const fail=(problem:unknown)=>{error=problem;reject(problem);};
    data.port1.onmessage=event=>{try{requireValue(!error&&backward<4,'Transferred old port or extra reverse delivery.');
      checked(event.data,tag,'reverse',backward++);finish();}catch(error){fail(error);}};
    data.port1.start();
    courier.port2.onmessage=event=>{try{
      checked(event.data,tag,'transfer',0);
      requireValue(!moved&&event.ports&&event.ports.length===1,'Transferred port receipt differs.');
      moved=event.ports[0];
      moved.onmessage=message=>{try{requireValue(!error&&forward<4,'Extra transferred delivery.');
        checked(message.data,tag,'queued',forward++);finish();}catch(error){fail(error);}};
      moved.start();
      for(let i=0;i<4;++i)moved.postMessage(payload(tag,'reverse',i));
    }catch(error){fail(error);}};
    courier.port2.start();
    for(let i=0;i<4;++i)data.port1.postMessage(payload(tag,'queued',i));
    courier.port1.postMessage(payload(tag,'transfer',0),[data.port2]);
    let refused=false;
    try{courier.port1.postMessage(payload(tag,'duplicate-transfer',0),[data.port2]);}
    catch(error){refused=error instanceof DOMException&&error.name==='DataCloneError';}
    if(!refused){fail(new Error('Detached port could be transferred twice.'));return;}
    // A detached endpoint must not send to the still-entangled endpoint.
    data.port2.postMessage(payload(tag,'old-port',0));
    });
    await pause(120);if(error)throw error;
    requireValue(forward===4&&backward===4,'Late transferred delivery differs.');
    return {sent:9,received:9,quietMs:120,
      trace:'courier:one-port;queued:0..3;reverse:0..3;old-port-no-delivery;120ms-quiet'};
  }));}finally{if(moved)(moved as MessagePort).close();}
}
async function closed(tag:string):Promise<Observation> {
  return ports(async channel=>{
    let received=0,error:unknown=null;
    const first=new Promise<void>((resolve,reject)=>{
      channel.port2.onmessage=event=>{try{checked(event.data,tag,'first',0);++received;
        requireValue(received===1,'Extra first delivery.');channel.port2.close();resolve();
      }catch(problem){error=problem;reject(problem);}};
    });
    channel.port2.start();channel.port1.postMessage(payload(tag,'first',0));await deadline(first);
    await pause(20); // Callback and its original dispatch have returned.
    for(let i=0;i<16;++i)channel.port1.postMessage(payload(tag,'after-close',i));
    await pause(120);if(error)throw error;
    requireValue(received===1,'A later-turn send reached a closed port.');
    const alreadyClosed=new MessageChannel();
    try{
      alreadyClosed.port2.onmessage=()=>{++received;};alreadyClosed.port2.start();alreadyClosed.port2.close();
      for(let i=0;i<16;++i)alreadyClosed.port1.postMessage(payload(tag,'closed-before-send',i));
      await pause(120);requireValue(received===1,'Close-before-send still delivered.');
    }finally{alreadyClosed.port1.close();alreadyClosed.port2.close();}
    return {sent:33,received:1,quietMs:240,trace:'one-callback-close;later-turn16-sends;closed-before16-sends'};
  });
}
function reentry(tag:string):Promise<Observation> {
  return ports(async channel=>{
    let right=0,left=0,error:unknown=null;
    await new Promise<void>((resolve,reject)=>{
    const complete=()=>{if(right===12&&left===12)resolve();};
    channel.port2.onmessage=event=>{try{requireValue(!error&&right<12,'Extra reentrant ping.');
      checked(event.data,tag,'ping',right);channel.port2.postMessage(payload(tag,'pong',right++));complete();
    }catch(problem){error=problem;reject(problem);}};
    channel.port1.onmessage=event=>{try{requireValue(!error&&left<12,'Extra reentrant pong.');
      checked(event.data,tag,'pong',left++);if(left<12)channel.port1.postMessage(payload(tag,'ping',left));complete();
    }catch(problem){error=problem;reject(problem);}};
    channel.port1.start();channel.port2.start();channel.port1.postMessage(payload(tag,'ping',0));
    });
    await pause(120);if(error)throw error;
    requireValue(left===12&&right===12,'Late reentrant delivery differs.');
    return {sent:24,received:24,quietMs:120,trace:'ping/pong:0..11;callback-post;120ms-quiet'};
  });
}
async function workerCase(tag:string,kind:'fifo'|'close'|'terminate'):Promise<Observation> {
  requireValue(typeof Worker==='function','Dedicated Worker is unavailable.');
  const worker=new Worker('./message-worker.js');
  let stopped=false,terminationRequested=false,error:unknown=null,received=0,ticks=0;
  const inbox:{[key:string]:unknown}[]=[];
  let wake:(()=>void)|null=null;
  worker.onerror=event=>{error=new Error('Dedicated Worker error: '+event.message);if(wake){const next=wake;wake=null;next();}};
  worker.onmessage=event=>{try{
    requireValue(!stopped,'Worker message arrived after observed stop.');
    const value=record(event.data);requireValue(value.tag===tag,'Worker document identity differs.');
    ++received;inbox.push(value);if(wake){const next=wake;wake=null;next();}
  }catch(problem){error=problem;if(wake){const next=wake;wake=null;next();}}};
  const next=async()=>{
    if(!inbox.length&&!error)await deadline(new Promise<void>(resolve=>{wake=resolve;}));
    if(error)throw error;const value=inbox.shift();requireValue(value,'Worker receipt missing.');return value;
  };
  const timer=setInterval(()=>{++ticks;},10);
  const execute=async():Promise<Observation>=>{
    worker.postMessage({tag,kind:'initialize'});
    const ready=await next();requireValue(ready.kind==='ready'&&Object.keys(ready).length===2,'Worker initialization differs.');
    if(kind==='fifo'){
      for(let i=0;i<16;++i)worker.postMessage(payload(tag,'echo',i));
      for(let i=0;i<16;++i){const value=await next();requireValue(value.kind==='echo'&&value.sequence===i&&
        value.receiveOrdinal===i&&Object.keys(value).length===4,'Worker FIFO/ordinal differs.');}
      await pause(120);if(error)throw error;
      requireValue(inbox.length===0,'Unexpected extra Worker receipt.');
      requireValue(received===17,'Worker actual FIFO receipt count differs.');
      return {sent:17,received:17,quietMs:120,trace:'ready;echo:0..15;worker-receive-ordinal:0..15;120ms-quiet'};
    }
    worker.postMessage({tag,kind:'arm'});const armed=await next();
    requireValue(armed.kind==='armed'&&Object.keys(armed).length===2,'Worker active acknowledgement differs.');
    let pulses=0;
    const observePulse=(value:{[key:string]:unknown})=>{
      requireValue(pulses<32,'Worker pulse observation exceeded its bound.');
      checked(value,tag,'pulse',pulses++);
    };
    observePulse(await next()); // Prove that the continuing emitter ran before stopping it.
    if(kind==='close'){
      worker.postMessage({tag,kind:'close'});
      for(;;){const receipt=await next();
        if(receipt.kind==='closing'){
          requireValue(Object.keys(receipt).length===2,'Worker close acknowledgement differs.');break;
        }
        observePulse(receipt);
      }
    }else{
      // All receipts already dispatched before the synchronous terminate call retain their ordinal.
      while(inbox.length)observePulse(inbox.shift()!);
      worker.terminate();terminationRequested=true;
    }
    stopped=true;const before=ticks;await pause(120);if(error)throw error;
    requireValue(ticks>before&&inbox.length===0,'Page stopped or Worker emitted during bounded quiet interval.');
    const sent=kind==='close'?3:2;
    requireValue(received===sent+pulses,'Worker actual receipt count differs.');
    return {sent,received,quietMs:120,trace:'ready;armed;pulse:0..'+String(pulses-1)+(kind==='close'?
      ';closing;self.close-request;120ms-quiet;native-join-unobserved':
      ';terminate-request;120ms-quiet;native-join-unobserved')};
  };
  try{return await deadline(execute());}finally{clearInterval(timer);worker.onmessage=null;worker.onerror=null;
    if(!terminationRequested)worker.terminate();}
}
export async function runMessagePortBehavior(report:Report):Promise<void> {
  const start=await report({sequence:0,caseId:'run',status:'begin',sent:0,received:0,quietMs:0,trace:'explicit-browser-api'});
  requireValue(start.accepted===true&&typeof start.documentSession==='string'&&start.documentSession.length>0,
    'Native document observation was not admitted.');
  const tag=start.documentSession;
  requireValue(typeof MessageChannel==='function','MessageChannel is unavailable.');
  const bodies:(()=>Promise<Observation>)[]=[()=>fifo(tag),()=>refill(tag),()=>transfer(tag),()=>closed(tag),
    ()=>reentry(tag),()=>workerCase(tag,'fifo'),()=>workerCase(tag,'close'),()=>workerCase(tag,'terminate')];
  for(let i=0;i<bodies.length;++i){
    let result:Observation;
    try{result=await deadline(bodies[i](),4000);}catch(error){
      const status=i>=5&&typeof Worker!=='function'?'unavailable':'failed';
      try{await report({sequence:i+1,caseId:caseIds[i],status,sent:0,received:0,quietMs:0,
        trace:String(error).slice(0,2048)});}catch{/* Original native readiness/Owned failure remains authoritative. */}
      throw error;
    }
    const acknowledgement=await report({sequence:i+1,caseId:caseIds[i],status:'pass',...result});
    requireValue(acknowledgement.accepted&&acknowledgement.documentSession===tag,'Observation document changed.');
  }
  const end=await report({sequence:9,caseId:'run',status:'end',sent:0,received:0,quietMs:0,
    trace:'eight-finite-cases;worker-native-join/debugger-wait-unobserved'});
  requireValue(end.accepted&&end.documentSession===tag,'Final observation document changed.');
}
