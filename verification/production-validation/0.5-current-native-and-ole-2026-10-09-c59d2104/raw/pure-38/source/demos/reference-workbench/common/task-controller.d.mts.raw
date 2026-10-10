import type {SouiCoreMethodMap, SouiNativeClientImpl} from '@soui-sdk';
import type {Fixture, PreviousTask} from './model.mjs';
export interface TaskView {state:string;progress:number;taskId:string;operationId:string;message:string;observationError:string;subscriptionError:string;result?:Record<string,unknown>}
export function taskControls(ready:boolean,view:TaskView):Record<'start'|'query'|'cancel',{enabled:boolean;busy:boolean}>;
export class ReferenceTask {
  constructor(client:SouiNativeClientImpl<SouiCoreMethodMap>,fixture:Fixture,options?:{timers?:typeof globalThis;operationId?:()=>string});
  snapshot():TaskView;
  observe(listener:(value:TaskView)=>void):()=>void;
  start(category?:string,durationMs?:number):Promise<void>;
  query():Promise<void>;cancel():Promise<void>;
  historicalReference():PreviousTask|null;dispose():void;
}
