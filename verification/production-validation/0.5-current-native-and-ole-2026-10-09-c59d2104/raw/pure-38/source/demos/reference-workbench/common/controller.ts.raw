import {connectSouiNative, createSouiGui, type SouiCoreMethodMap, type SouiJson} from '@soui-sdk';
import {callAction, authorizedActionTools, type ActionMethods, type SubmitFormInput} from '../web/src/generated/actions';
import fixtureJson from './fixture.json';
import {makeRows, validateFixture, readState, defaultState, restoreState, saveState, selectRow, deriveRows,
  summarize, utf8Bytes, type UiState} from './model.mjs';
import {ReferenceTask, taskControls, type TaskView} from './task-controller.mjs';
import {initializeReference} from './initialization.mjs';

const fixture = validateFixture(fixtureJson);
const storageKey = 'soui.reference-ui/v1';
const errorCode = (error: unknown) => {
  const value = error as {code?: unknown};
  return typeof value?.code === 'string' && /^[A-Z0-9_]{1,64}$/.test(value.code) ? value.code : 'E_REFERENCE_ACTION';
};
const record = (value: unknown): Record<string, unknown> => {
  if (!value || typeof value !== 'object' || Array.isArray(value)) throw new Error('E_REFERENCE_INPUT');
  return value as Record<string, unknown>;
};
const keys = (value:Record<string,unknown>,allowed:string[]) => {
  if(Object.keys(value).some(key=>!allowed.includes(key)))throw new Error('E_REFERENCE_INPUT');
};
export function createReferenceController(profile: string) {
  const gui = createSouiGui();
  const rows = makeRows(fixture), chart = summarize(rows, fixture).groups;
  let state: UiState = defaultState(fixture), restoreStatus = 'not-restored', taskRecovery = 'none', storageStatus = 'not-saved';
  try {
    const restored = restoreState(localStorage.getItem(storageKey), fixture);
    state = restored.state; restoreStatus = restored.status; taskRecovery = restored.taskRecovery;
  } catch {restoreStatus = 'storage-unavailable';}
  let filtered = deriveRows(rows, state, fixture), ready = false, stopped = false, initialized = false;
  let task: ReferenceTask | undefined, taskView: TaskView = {
    state:'idle', progress:0, taskId:'', operationId:'', message:'尚未启动', observationError:'', subscriptionError:'',
  };
  let client: Awaited<ReturnType<typeof connectSouiNative<SouiCoreMethodMap & ActionMethods>>> | undefined;
  let dialog = false, returnFocus: HTMLElement | null = null, error = '', message = '正在连接真实原生宿主…', formBusy = false;
  const listeners = new Set<() => void>();
  const publish = () => {if (!stopped) for (const listener of listeners) listener();};
  const persist = () => {
    try {localStorage.setItem(storageKey, saveState({...state, previousTask:task?.historicalReference() || state.previousTask}, fixture)); storageStatus='saved';}
    catch {storageStatus='storage-unavailable';}
  };
  const update = (patch: Partial<UiState>, refilter = false) => {
    state = readState({...state, ...patch}, fixture);
    if (refilter) filtered = deriveRows(rows, state, fixture);
    persist(); publish(); return {changed:true};
  };
  const fail = (failure: unknown) => {error=errorCode(failure); message='操作未确认。请查看错误码；不会自动重放任务。'; publish();};
  const openDialog = () => {returnFocus=document.activeElement as HTMLElement | null; dialog=true; publish(); return {open:true};};
  const closeDialog = () => {dialog=false; publish(); returnFocus?.focus(); return {open:false};};
  const actions: Record<string, Record<string, (input: SouiJson) => unknown | Promise<unknown>>> = {
    filter:{set:input=>{const value=record(input);keys(value,['query','category']);
      return update({query:value.query===undefined?state.query:value.query as string,category:value.category===undefined?state.category:value.category as string},true);}},
    sort:{set:input=>{const value=record(input);keys(value,['sort','descending']);return update({sort:value.sort as UiState['sort'],descending:value.descending as boolean},true);}},
    selection:{set:input=>{const value=record(input);keys(value,['id','selected']);state=selectRow(state,value.id as number,value.selected as boolean,fixture);persist();publish();return {selectedCount:state.selectedIds.length};}},
    form:{set:input=>{const value=record(input);keys(value,['name','notes']);return update({draftName:value.name as string,draftNotes:value.notes as string});},
      submit:async()=> {
        if (!client || !ready || formBusy) throw new Error('E_REFERENCE_NOT_READY');
        formBusy=true;publish();
        try {
          const result=await callAction(client,'reference.form.submit',{name:state.draftName,notes:state.draftNotes,
            category:(state.category==='all'?fixture.groups[0]:state.category) as SubmitFormInput['category']});
          if (!stopped) {message=result.message;error=result.accepted?'':'E_FORM_NAME';publish();}
          return result;
        } finally {formBusy=false;publish();}
      }},
    start:{activate:async input=>{if(!task)throw new Error('E_REFERENCE_NOT_READY');const value=record(input);keys(value,['durationMs']);
      await task.start(state.category,value.durationMs===undefined?5000:value.durationMs as number);return {state:task.snapshot().state};}},
    query:{activate:async()=>{if(!task)throw new Error('E_REFERENCE_NOT_READY');await task.query();return {state:task.snapshot().state};}},
    cancel:{activate:async()=>{if(!task)throw new Error('E_REFERENCE_NOT_READY');await task.cancel();return {state:task.snapshot().state};}},
    theme:{activate:()=>update({theme:state.theme==='dark'?'light':'dark'})},
    dialog:{open:openDialog,close:closeDialog},
    reload:{activate:()=>{persist();window.location.reload();return {requested:true};}},
    focus:{activate:()=>{document.getElementById('reference-name')?.focus();return {requested:true};}},
    status:{read:()=>({profile,ready,state:taskView.state,progress:taskView.progress,restoreStatus,taskRecovery,
      storageStatus,filteredRows:filtered.length,selectedCount:state.selectedIds.length})},
  };
  const publicState = (id: string) => ({
    enabled:id==='form'?ready:
      ['start','query','cancel'].includes(id)?taskControls(ready,taskView)[id as 'start'|'query'|'cancel'].enabled:true,
    busy:id==='form'?formBusy:
      ['start','query','cancel'].includes(id)?taskControls(ready,taskView)[id as 'start'|'query'|'cancel'].busy:false,
    state:{ready,taskState:taskView.state,progress:taskView.progress,filteredRows:filtered.length,
      selectedCount:state.selectedIds.length,theme:state.theme,dialogOpen:dialog,restoreStatus,taskRecovery,
      storageStatus,nameBytes:utf8Bytes(state.draftName),notesBytes:utf8Bytes(state.draftNotes),error},
  });
  async function initialize() {
    if (initialized) throw new Error('E_REFERENCE_ALREADY_MOUNTED');
    if (stopped) return;
    initialized=true;
    for (const id of Object.keys(actions)) {
      const element=document.getElementById('reference-'+id);
      if (!element) throw new Error('E_REFERENCE_ELEMENT');
      gui.register({id:'reference.'+id,role:['filter','form'].includes(id)?'form':'button',
        name:'Reference '+id,element,actions:actions[id],state:()=>publicState(id)});
    }
    try {
      await initializeReference({
        alive:()=>!stopped,
        connect:()=>connectSouiNative<SouiCoreMethodMap & ActionMethods>({maxPendingRequests:16,maxSubscriptions:2,
          onError:failure=>{if(!stopped)fail(failure);}}),
        adoptClient:connected=>{client=connected;},
        discover:connected=>connected.discover(),
        validateTools:catalog=>{
          const tools=authorizedActionTools(catalog);
          if(!['reference.fixture.describe','reference.form.submit'].every(name=>tools.some(tool=>tool.name===name)))
            throw new Error('E_REFERENCE_CONTRACT');
        },
        describe:connected=>callAction(connected,'reference.fixture.describe',{}),
        createTask:(connected,description)=>{
          validateFixture(description);
          task=new ReferenceTask(connected,fixture);
          task.observe(next=>{taskView=next;persist();publish();});
        },
        probe:connected=>connected.call('bridge.probe.report',{ready:true}),
        ready:()=>{ready=true;message='已连接 C++；表单和任务使用同一公开 Bridge。';publish();},
      });
    } catch (failure) {if(!stopped)fail(failure);}
  }
  async function perform(id: string, action: string, input: SouiJson = {}) {
    try {return await gui.perform({id:'reference.'+id,action,input});}
    catch (failure) {fail(failure);throw failure;}
  }
  const shutdown = () => {
    if (stopped) return;
    persist();stopped=true;ready=false;task?.dispose();gui.dispose();listeners.clear();
    window.removeEventListener('pagehide',shutdown);window.removeEventListener('beforeunload',shutdown);
    // connectSouiNative owns document client disposal; native navigation owns grant revocation.
  };
  window.addEventListener('pagehide',shutdown);window.addEventListener('beforeunload',shutdown);
  return {
    fixture,gui,initialize,perform,shutdown,
    snapshot:()=>({profile,state:{...state,selectedIds:state.selectedIds.slice()},rows:filtered,chart,ready,task:{...taskView},controls:taskControls(ready,taskView),
      dialog,error,message,formBusy,restoreStatus,taskRecovery,storageStatus}),
    observe:(listener:()=>void)=>{if(listeners.size>=8)throw new Error('E_REFERENCE_OBSERVERS');listeners.add(listener);return()=>listeners.delete(listener);},
  };
}
export type ReferenceController = ReturnType<typeof createReferenceController>;
