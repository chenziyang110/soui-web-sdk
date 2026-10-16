// Command-specific effects only. This is a view of actual Agent child receipts,
// not a second task executor, permission store, page client, timer or replay loop.
const terminal=['succeeded','failed','cancelled','partial'];
const states=['queued','running','timeout','cancel-pending',...terminal];
const id=value=>typeof value==='string'&&/^[A-Za-z0-9_.:-]{1,128}$/.test(value);
const object=value=>value&&typeof value==='object'&&!Array.isArray(value);
const exact=(value,names)=>object(value)&&Object.keys(value).length===names.length&&names.every(name=>Object.prototype.hasOwnProperty.call(value,name));
const active=context=>context&&context.active===true&&typeof context.effect==='function';
const failure=code=>Object.assign(new Error(code),{code});
const errorCode=(error,fallback)=>typeof error?.code==='string'&&/^[A-Z0-9_]{1,64}$/.test(error.code)?error.code:fallback;
const sequence=value=>typeof value==='string'&&/^(0|[1-9]\d{0,19})$/.test(value)&&
  (value.length<20||value<='18446744073709551615');
export async function submitReferenceForm(input,context,draft,manual) {
  if(!context)return manual(draft);
  if(!active(context))throw failure('E_GUI_EFFECT_STALE');
  if(!exact(input,['name','notes','category'])||input.name!==draft.name||input.notes!==draft.notes||input.category!==draft.category)
    throw failure('E_REFERENCE_FORM_CHANGED');
  const result=await context.effect(input); // Exactly one attempt; a lost result is not replayed.
  if(!active(context))throw failure('E_GUI_EFFECT_STALE');
  if(!exact(result,['accepted','message','receiptId'])||typeof result.accepted!=='boolean'||
      typeof result.message!=='string'||result.message.length>200||!id(result.receiptId))throw failure('E_REFERENCE_FORM_RECEIPT');
  return result;
}
export class AgentTaskObservation {
  constructor(fixture,publish=()=>{}) {
    this.fixture=fixture;this.publish=publish;this.closed=false;this.generation=0;this.busy=false;
    this.view={state:'idle',progress:0,taskId:'',sequence:'0',cancelRequested:false,error:''};
  }
  snapshot(){return {...this.view};}
  update(patch){if(!this.closed){this.view={...this.view,...patch};this.publish();}}
  current(generation,context){return !this.closed&&this.generation===generation&&active(context);}
  async start(input,context) {
    if(this.closed||this.busy||!['idle',...terminal].includes(this.view.state))throw failure('E_REFERENCE_AGENT_BUSY');
    if(!active(context))throw failure('E_GUI_NATIVE_REQUIRED');
    if(!exact(input,['fixtureId','category','durationMs'])||input.fixtureId!==this.fixture.fixtureId||
      !['all',...this.fixture.groups].includes(input.category)||!Number.isSafeInteger(input.durationMs)||
      input.durationMs<500||input.durationMs>60000)throw failure('E_REFERENCE_AGENT_INPUT');
    const generation=++this.generation;this.busy=true;
    this.update({state:'admitting',progress:0,taskId:'',sequence:'0',cancelRequested:false,error:''});
    try {
      if(!this.current(generation,context))throw failure('E_GUI_EFFECT_STALE');
      const result=await context.effect(input);
      if(!exact(result,['taskId','accepted','duplicate'])||!id(result.taskId)||result.accepted!==true||
        typeof result.duplicate!=='boolean')throw failure('E_REFERENCE_AGENT_START_RECEIPT');
      if(!this.current(generation,context))throw failure('E_GUI_EFFECT_STALE');
      this.update({state:'admitted',taskId:result.taskId}); // No invented queued/progress/terminal observation.
      return {admitted:true,taskId:result.taskId,duplicate:result.duplicate};
    } catch(error) {
      if(this.current(generation,context))this.update({state:'unknown',error:errorCode(error,'E_REFERENCE_AGENT_START')});
      throw error;
    } finally {if(this.generation===generation)this.busy=false;}
  }
  async query(input,context) {
    const taskId=this.view.taskId,generation=this.generation;
    if(this.closed||this.busy||!taskId||!exact(input,['taskId'])||input.taskId!==taskId)throw failure('E_REFERENCE_AGENT_SELECTOR');
    if(!active(context))throw failure('E_GUI_NATIVE_REQUIRED');
    this.busy=true;
    try {
      const result=await context.effect(input);
      if(!object(result)||Object.keys(result).some(name=>!['taskId','state','progress','sequence','result','projectionError'].includes(name))||
        result.taskId!==taskId||!states.includes(result.state)||!Number.isSafeInteger(result.progress)||result.progress<0||result.progress>100||
        !sequence(result.sequence)||
        (result.projectionError!==undefined&&result.projectionError!=='E_RESULT_PROJECTION'))throw failure('E_REFERENCE_AGENT_QUERY_RECEIPT');
      if(!this.current(generation,context)||this.view.taskId!==taskId)throw failure('E_GUI_EFFECT_STALE');
      // Only the real authenticated family query supplies a terminal state.
      // Fixed summary data remains in the native effect receipt, not copied
      // into an ambient page task or persisted as an authority reference.
      this.update({state:result.state,progress:result.progress,sequence:result.sequence,error:result.projectionError||''});
      return {observed:true,taskId,state:result.state,progress:result.progress,sequence:result.sequence};
    } catch(error) {
      if(this.current(generation,context)&&this.view.taskId===taskId)this.update({error:errorCode(error,'E_REFERENCE_AGENT_QUERY')});
      throw error;
    } finally {if(this.generation===generation)this.busy=false;}
  }
  async cancel(input,context) {
    const taskId=this.view.taskId,generation=this.generation;
    if(this.closed||this.busy||!taskId||terminal.includes(this.view.state)||!exact(input,['taskId'])||input.taskId!==taskId)
      throw failure('E_REFERENCE_AGENT_SELECTOR');
    if(!active(context))throw failure('E_GUI_NATIVE_REQUIRED');
    this.busy=true;
    try {
      const result=await context.effect(input);
      if(!exact(result,['taskId','state','cancelRequested'])||result.taskId!==taskId||result.state!=='cancel-pending'||
          typeof result.cancelRequested!=='boolean')throw failure('E_REFERENCE_AGENT_CANCEL_RECEIPT');
      if(!this.current(generation,context)||this.view.taskId!==taskId)throw failure('E_GUI_EFFECT_STALE');
      this.update({cancelRequested:result.cancelRequested,error:''}); // A request is not an actual terminal.
      return {requestAcknowledged:true,taskId,cancelRequested:result.cancelRequested};
    } catch(error) {
      if(this.current(generation,context)&&this.view.taskId===taskId)this.update({error:errorCode(error,'E_REFERENCE_AGENT_CANCEL')});
      throw error;
    } finally {if(this.generation===generation)this.busy=false;}
  }
  dispose(){if(this.closed)return;this.closed=true;this.generation++;this.busy=false;this.publish=()=>{};}
}
