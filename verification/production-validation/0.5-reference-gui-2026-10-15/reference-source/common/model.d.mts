export interface Fixture {fixtureId:string;seed:number;rowCount:number;rowHeightPx:number;groups:string[];statuses:string[]}
export interface Row {id:number;name:string;group:string;sizeKb:number;status:string}
export interface PreviousTask {operationId:string;taskId:string;lastKnownState:string}
export interface UiState {schema:string;fixtureId:string;theme:'light'|'dark';query:string;sort:'id'|'name'|'sizeKb';descending:boolean;category:string;selectedIds:number[];draftName:string;draftNotes:string;previousTask:PreviousTask|null}
export const MAX_SELECTION:number;export const MAX_SAVED_BYTES:number;
export function utf8Bytes(value:string):number;
export function validateFixture(value:Fixture):Fixture;
export function rowAt(fixture:Fixture,index:number):Row;
export function makeRows(fixture:Fixture):Row[];
export function defaultState(fixture:Fixture):UiState;
export function readState(value:unknown,fixture:Fixture):UiState;
export function saveState(state:UiState,fixture:Fixture):string;
export function restoreState(raw:string|null,fixture:Fixture):{status:'empty'|'restored'|'rejected';state:UiState;taskRecovery:'none'|'previous-document-unknown'};
export function selectRow(state:UiState,id:number,selected:boolean,fixture:Fixture):UiState;
export function deriveRows(rows:Row[],state:UiState,fixture:Fixture):Row[];
export function visibleRows(rows:Row[],scrollTop:number,viewportHeight:number,rowHeight?:number):{first:number;rows:Row[]};
export function summarize(rows:Row[],fixture:Fixture,category?:string):{processed:number;matched:number;totalSizeKb:number;groups:{group:string;count:number}[]};
