import type {SouiGuiCommandContext, SouiJson} from '@soui-sdk';
import type {Fixture} from './model.mjs';
import type {SubmitFormInput, SubmitFormOutput} from '../web/src/generated/actions';
export function submitReferenceForm(input:SouiJson,context:SouiGuiCommandContext|undefined,
  draft:SubmitFormInput,manual:(draft:SubmitFormInput)=>Promise<SubmitFormOutput>):Promise<SubmitFormOutput>;
export interface AgentTaskView {state:string;progress:number;taskId:string;sequence:string;cancelRequested:boolean;error:string}
export class AgentTaskObservation {
  constructor(fixture:Fixture,publish?:()=>void);
  snapshot():AgentTaskView;
  start(input:SouiJson,context?:SouiGuiCommandContext):Promise<SouiJson>;
  query(input:SouiJson,context?:SouiGuiCommandContext):Promise<SouiJson>;
  cancel(input:SouiJson,context?:SouiGuiCommandContext):Promise<SouiJson>;
  dispose():void;
}
