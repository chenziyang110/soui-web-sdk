import type {ReferenceController} from './controller';
import {summarize, makeRows} from './model.mjs';

// App-initiated, bounded business checks. Native states come from the real task client.
// No synthetic bearer, task storage, raw transport or fabricated user gesture.
export async function runReferenceParity(controller:ReferenceController,scroll:()=>void) {
  const steps:Record<string,unknown>[]=[];
  const check=(name:string,condition:boolean,detail:Record<string,unknown>={})=>{
    steps.push({name,passed:condition,...detail});if(!condition)throw new Error('E_PARITY_'+name);
  };
  const perform=controller.perform;
  const observeDom=()=>new Promise<void>((resolve,reject)=>{
    const limit=setTimeout(()=>reject(new Error('E_PARITY_DOM_TIMEOUT')),2000);
    requestAnimationFrame(()=>{clearTimeout(limit);resolve();});
  });
  const waitTerminal=async()=>{
    const began=Date.now();
    while(Date.now()-began<70000) {
      await perform('query','activate');
      const task=controller.snapshot().task;
      if(['succeeded','failed','cancelled','partial'].includes(task.state))return task;
      if(task.state==='unknown')throw new Error('E_PARITY_UNKNOWN');
      await new Promise(resolve=>setTimeout(resolve,100));
    }
    throw new Error('E_PARITY_TIMEOUT');
  };
  await perform('filter','set',{query:'',category:'all'});
  await perform('form','set',{name:'中文业务示例',notes:'同一 C++ 入口；未证明 IME'});
  const form=await perform('form','submit') as {accepted?:boolean};
  check('native-form',form.accepted===true);
  await perform('start','activate',{durationMs:5000});
  check('start-receipt',Boolean(controller.snapshot().task.taskId));
  await perform('sort','set',{sort:'sizeKb',descending:true});scroll();
  await perform('filter','set',{query:'fixture-0',category:'all'});
  const rows=controller.snapshot().rows;
  check('ten-thousand-operations',rows.length>1000&&rows[0].sizeKb>=rows[rows.length-1].sizeKb,{matched:rows.length});
  await perform('selection','set',{id:rows[0].id,selected:true});
  check('selection',controller.snapshot().state.selectedIds.includes(rows[0].id));
  await perform('query','activate');
  check('operations-during-native-task',['queued','running'].includes(controller.snapshot().task.state));
  await observeDom();
  check('bounded-dom-and-chart',document.querySelectorAll('#reference-selection .table-row').length>0&&
    document.querySelectorAll('#reference-selection .table-row').length<=80&&document.querySelectorAll('svg g').length===5);
  await perform('theme','activate');await perform('dialog','open');
  await observeDom();
  check('dialog',controller.snapshot().dialog&&Boolean(document.querySelector('[role=dialog]')));
  await perform('dialog','close');await perform('focus','activate');
  check('focus',document.activeElement?.id==='reference-name');
  const completed=await waitTerminal();
  const expected=summarize(makeRows(controller.fixture),controller.fixture);
  const result=completed.result;
  check('native-complete',completed.state==='succeeded'&&result?.processed===expected.processed&&
    result?.matched===expected.matched&&result?.totalSizeKb===expected.totalSizeKb&&
    JSON.stringify(result?.groups)===JSON.stringify(expected.groups),{state:completed.state,progress:completed.progress});
  await perform('start','activate',{durationMs:60000});await perform('cancel','activate');
  const cancelled=await waitTerminal();
  check('native-cancelled',cancelled.state==='cancelled',{state:cancelled.state});
  // Error handling must be visible, not transformed into success.
  let rejected=false;
  try {await perform('selection','set',{id:10001,selected:true});}catch{rejected=true;}
  check('bounded-error',rejected&&Boolean(controller.snapshot().error));
  return {schema:'soui.reference-business-parity/1',profile:controller.snapshot().profile,
    steps,passed:steps.every(step=>step.passed===true),reload:'requires-real-navigation-and-new-document-observation',
    nativeGesture:false,imeQualification:false,renderQualification:false,releaseReady:false};
}
