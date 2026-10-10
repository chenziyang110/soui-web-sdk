// Observes the real public task client. No task execution, replay or permission store.
const terminalStates=['succeeded','failed','cancelled','partial'];
const taskStates=['queued','running','timeout','cancel-pending',...terminalStates];
const id=value=>typeof value==='string'&&/^[A-Za-z0-9_.:-]{1,128}$/.test(value);
const code=error=>typeof error?.code==='string'?error.code:'E_TASK_OBSERVATION';
export function taskControls(ready,view) {
  const canStart=['idle',...terminalStates].includes(view.state);
  return {
    start:{enabled:ready&&canStart,busy:ready&&!canStart},
    query:{enabled:ready&&Boolean(view.operationId),busy:view.state==='starting'},
    cancel:{enabled:ready&&Boolean(view.taskId)&&!terminalStates.includes(view.state),busy:view.state==='cancel-pending'},
  };
}
export class ReferenceTask {
  constructor(client,fixture,{timers=globalThis,operationId=()=>`reference-${Date.now()}-${Math.random().toString(36).slice(2)}`}={}) {
    this.client=client;this.fixture=fixture;this.timers=timers;this.nextOperationId=operationId;
    this.view={state:'idle',progress:0,taskId:'',operationId:'',message:'',observationError:'',subscriptionError:''};
    this.listeners=new Set();this.disposed=false;this.starting=false;this.querying=false;
    this.poll=null;this.deadline=null;this.subscription=null;this.generation=0;
  }
  snapshot() {return{...this.view,result:this.view.result&&JSON.parse(JSON.stringify(this.view.result))};}
  observe(listener) {
    if(this.disposed||typeof listener!=='function'||this.listeners.size>=8)throw new Error('E_OBSERVER_CAPACITY');
    this.listeners.add(listener);listener(this.snapshot());return()=>this.listeners.delete(listener);
  }
  publish(patch) {
    if(this.disposed)return;
    this.view={...this.view,...patch};
    for(const listener of this.listeners) {try{listener(this.snapshot());}catch{/* One view cannot suppress other observers. */}}
  }
  stopWatching() {
    if(this.poll!==null)this.timers.clearInterval(this.poll);if(this.deadline!==null)this.timers.clearTimeout(this.deadline);
    this.poll=this.deadline=null;
    const subscription=this.subscription,generation=this.generation,taskId=this.view.taskId;this.subscription=null;
    if(subscription) {
      const report=error=>{
        if(this.disposed||this.generation!==generation||this.view.taskId!==taskId||this.subscription!==null)return;
        this.publish({subscriptionError:code(error)});
      };
      try {void Promise.resolve(subscription.unsubscribe()).catch(report);}
      catch(error) {report(error);}
    }
  }
  async start(category='all',durationMs=5000) {
    if(this.disposed||this.starting||!['idle',...terminalStates].includes(this.view.state))throw new Error('E_TASK_BUSY');
    if(!['all',...this.fixture.groups].includes(category)||!Number.isSafeInteger(durationMs)||durationMs<500||durationMs>60000)
      throw new RangeError('E_TASK_INPUT');
    const operationId=this.nextOperationId();if(!id(operationId))throw new TypeError('E_OPERATION_ID');
    this.stopWatching();this.starting=true;const generation=++this.generation;
    this.publish({state:'starting',operationId,taskId:'',progress:0,result:undefined,message:'原生任务正在入队',observationError:'',subscriptionError:''});
    try {
      if(this.disposed||this.generation!==generation)return;
      const started=await this.client.call('task.start',{kind:'reference.summary',operationId,
        input:{fixtureId:this.fixture.fixtureId,category,durationMs}});
      if(!started||started.accepted!==true||!id(started.taskId))throw new Error('E_START_RECEIPT');
      if(this.disposed||this.generation!==generation) {
        // Best effort only; this does not confirm cancellation or roll back work.
        void this.client.call('task.cancel',{taskId:started.taskId}).catch(()=>undefined);return;
      }
      this.publish({state:'queued',taskId:started.taskId,message:'任务已由原生接纳'});
      if(this.disposed||this.generation!==generation)return;
      let subscription;
      subscription=this.client.subscribe(started.taskId,'task.progress',()=>{
        if(!this.disposed&&this.generation===generation&&this.subscription===subscription)void this.query();
      });
      this.subscription=subscription;
      void subscription.ready.catch(error=>{
        if(!this.disposed&&this.generation===generation&&this.subscription===subscription)
          this.publish({subscriptionError:code(error)});
      });
      this.poll=this.timers.setInterval(()=>{if(!this.disposed&&this.generation===generation)void this.query();},150);
      this.deadline=this.timers.setTimeout(()=>{
        if(this.disposed||this.generation!==generation)return;
        this.stopWatching();this.publish({state:'unknown',observationError:'E_OBSERVATION_TIMEOUT',message:'观察超时，原生结果未知；未自动取消或重放'});
      },90000);
      await this.query();
    } catch(error) {
      if(this.disposed||this.generation!==generation)return;
      this.stopWatching();this.publish({state:'unknown',observationError:code(error),message:'启动确认未知；只能查询同文档 operationId，不自动重放'});
    } finally {this.starting=false;}
  }
  async query() {
    if(this.disposed||this.querying||!this.view.operationId)return;
    this.querying=true;const generation=this.generation;
    const selector=this.view.taskId?{taskId:this.view.taskId}:{operationId:this.view.operationId};
    try {
      const next=await this.client.call('task.query',selector);
      if(this.disposed||generation!==this.generation)return;
      if(!next||!id(next.taskId)||(this.view.taskId&&next.taskId!==this.view.taskId)||
        !taskStates.includes(next.state)||!Number.isInteger(next.progress)||next.progress<0||next.progress>100)
        throw new Error('E_QUERY_RECEIPT');
      this.publish({taskId:next.taskId,state:next.state,progress:next.progress,message:next.message||'',
        result:next.result,observationError:''});
      if(this.disposed||generation!==this.generation)return;
      if(terminalStates.includes(next.state))this.stopWatching();
    } catch(error) {
      if(this.disposed||generation!==this.generation)return;
      this.stopWatching();this.publish({state:'unknown',observationError:code(error),message:'任务状态未知；not-found 不证明未执行，也不允许自动重放'});
    } finally {this.querying=false;}
  }
  async cancel() {
    if(this.disposed||!this.view.taskId||terminalStates.includes(this.view.state))throw new Error('E_NO_ACTIVE_TASK');
    const generation=this.generation,taskId=this.view.taskId;
    try {
      const receipt=await this.client.call('task.cancel',{taskId});
      if(this.disposed||this.generation!==generation||this.view.taskId!==taskId)return;
      if(!receipt||receipt.taskId!==taskId||typeof receipt.cancelRequested!=='boolean')throw new Error('E_CANCEL_RECEIPT');
      // Acknowledged request is not the final task result.
      this.publish({message:receipt.cancelRequested?'原生收到取消请求，等待实际终态':'原生未接纳新的取消请求，继续查询'});
      if(!this.disposed&&this.generation===generation&&this.view.taskId===taskId)await this.query();
    } catch(error) {
      if(!this.disposed&&this.generation===generation&&this.view.taskId===taskId)
        this.publish({observationError:code(error),message:'取消请求未确认；原生结果仍须查询'});
    }
  }
  historicalReference() {
    return this.view.taskId?{operationId:this.view.operationId,taskId:this.view.taskId,
      lastKnownState:taskStates.includes(this.view.state)?this.view.state:'unknown'}:null;
  }
  dispose() {if(this.disposed)return;this.disposed=true;this.generation++;this.stopWatching();this.listeners.clear();}
}
