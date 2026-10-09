// Pure bounded reference business. No Bridge, storage or renderer side effects.
export const MAX_SELECTION = 100;
export const MAX_SAVED_BYTES = 8192;
export function utf8Bytes(value) {
  if (typeof value !== 'string') throw new TypeError('Expected text');
  let bytes=0;
  for (let i=0;i<value.length;i++) {
    const code=value.charCodeAt(i);
    if(code<128)bytes++;
    else if(code<2048)bytes+=2;
    else if(code>=0xd800&&code<=0xdbff) {
      if(++i>=value.length||value.charCodeAt(i)<0xdc00||value.charCodeAt(i)>0xdfff)throw new TypeError('Invalid text');
      bytes+=4;
    } else { if(code>=0xdc00&&code<=0xdfff)throw new TypeError('Invalid text');bytes+=3; }
  }
  return bytes;
}
export function validateFixture(value) {
  if(!value||value.fixtureId!=='electron-smoothness-10k-v1'||value.seed!==20260915||
    value.rowCount!==10000||value.rowHeightPx!==32||
    JSON.stringify(value.groups)!==JSON.stringify(['素材','项目','缓存','导出','归档'])||
    JSON.stringify(value.statuses)!==JSON.stringify(['ready','indexed','queued','review']))throw new TypeError('Unexpected fixed fixture');
  return value;
}
function hash32(value) {
  let next=value>>>0;next^=next>>>16;next=Math.imul(next,0x7feb352d)>>>0;
  next^=next>>>15;next=Math.imul(next,0x846ca68b)>>>0;return(next^(next>>>16))>>>0;
}
function indexedRow(fixture,index) {
  const value=hash32(fixture.seed+index*2654435761),number=index+1;
  return{id:number,name:'fixture-'+('00000'+number).slice(-5)+'.asset',
    group:fixture.groups[value%fixture.groups.length],sizeKb:16+value%4096,
    status:fixture.statuses[(value>>>8)%fixture.statuses.length]};
}
export function rowAt(fixture,index) {
  validateFixture(fixture);
  if(!Number.isSafeInteger(index)||index<0||index>=fixture.rowCount)throw new RangeError('Row outside fixed fixture');
  return indexedRow(fixture,index);
}
export function makeRows(fixture) {validateFixture(fixture);return Array.from({length:fixture.rowCount},(_,i)=>indexedRow(fixture,i));}
export function defaultState(fixture) {
  validateFixture(fixture);return{schema:'soui.reference-ui/v1',fixtureId:fixture.fixtureId,theme:'light',
    query:'',sort:'id',descending:false,category:'all',selectedIds:[],draftName:'开发者',draftNotes:'',previousTask:null};
}
const plain=value=>value&&typeof value==='object'&&!Array.isArray(value);
const identifier=value=>typeof value==='string'&&/^[A-Za-z0-9_.:-]{1,128}$/.test(value);
export function readState(value,fixture) {
  const defaults=defaultState(fixture),names=Object.keys(defaults);
  if(!plain(value)||Object.keys(value).length!==names.length||Object.keys(value).some(name=>!names.includes(name))||
    value.schema!==defaults.schema||value.fixtureId!==defaults.fixtureId||!['light','dark'].includes(value.theme)||
    !['id','name','sizeKb'].includes(value.sort)||typeof value.descending!=='boolean'||
    !['all',...fixture.groups].includes(value.category)||utf8Bytes(value.query)>128||
    utf8Bytes(value.draftName)>64||utf8Bytes(value.draftNotes)>256||
    !Array.isArray(value.selectedIds)||value.selectedIds.length>MAX_SELECTION||
    new Set(value.selectedIds).size!==value.selectedIds.length||
    value.selectedIds.some(id=>!Number.isSafeInteger(id)||id<1||id>fixture.rowCount))throw new TypeError('Invalid bounded UI state');
  if(value.previousTask!==null&&(!plain(value.previousTask)||Object.keys(value.previousTask).length!==3||
    !identifier(value.previousTask.operationId)||!identifier(value.previousTask.taskId)||
    !['queued','running','timeout','cancel-pending','succeeded','failed','partial','cancelled','unknown'].includes(value.previousTask.lastKnownState)))
    throw new TypeError('Invalid historical task reference');
  return{...value,selectedIds:value.selectedIds.slice(),previousTask:value.previousTask&&{...value.previousTask}};
}
export function saveState(state,fixture) {
  const text=JSON.stringify(readState(state,fixture));
  if(utf8Bytes(text)>MAX_SAVED_BYTES)throw new RangeError('UI state exceeds fixed storage budget');return text;
}
export function restoreState(raw,fixture) {
  if(raw===null)return{status:'empty',state:defaultState(fixture),taskRecovery:'none'};
  try {
    if(typeof raw!=='string'||utf8Bytes(raw)>MAX_SAVED_BYTES)throw new TypeError('Invalid saved bytes');
    const state=readState(JSON.parse(raw),fixture);
    return{status:'restored',state,taskRecovery:state.previousTask?'previous-document-unknown':'none'};
  } catch {return{status:'rejected',state:defaultState(fixture),taskRecovery:'none'};}
}
export function selectRow(state,id,selected,fixture) {
  readState(state,fixture);
  if(!Number.isSafeInteger(id)||id<1||id>fixture.rowCount||typeof selected!=='boolean')throw new RangeError('Invalid row selection');
  const ids=state.selectedIds.filter(value=>value!==id);
  if(selected){if(ids.length>=MAX_SELECTION)throw new RangeError('At most100 selected rows');ids.push(id);}
  return{...state,selectedIds:ids.sort((a,b)=>a-b)};
}
export function deriveRows(rows,state,fixture) {
  readState(state,fixture);if(!Array.isArray(rows)||rows.length!==fixture.rowCount)throw new TypeError('Incomplete fixed dataset');
  const query=state.query.toLowerCase(),direction=state.descending?-1:1;
  return rows.filter(row=>(state.category==='all'||row.group===state.category)&&row.name.toLowerCase().includes(query))
    .sort((a,b)=>{const x=a[state.sort],y=b[state.sort];return direction*(x<y?-1:x>y?1:0)||(a.id-b.id);});
}
export function visibleRows(rows,scrollTop,viewportHeight,rowHeight=32) {
  if(!Array.isArray(rows)||rows.length>10000||!Number.isFinite(scrollTop)||scrollTop<0||
    !Number.isFinite(viewportHeight)||viewportHeight<0||rowHeight!==32)throw new TypeError('Invalid bounded viewport');
  const first=Math.min(Math.max(0,Math.floor(scrollTop/rowHeight)-8),Math.max(0,rows.length-1));
  const count=Math.min(80,Math.ceil(viewportHeight/rowHeight)+16);
  return{first,rows:rows.slice(first,first+count)};
}
export function summarize(rows,fixture,category='all') {
  validateFixture(fixture);if(!Array.isArray(rows)||rows.length!==fixture.rowCount||!['all',...fixture.groups].includes(category))throw new TypeError('Invalid summary');
  const counts=fixture.groups.map(group=>({group,count:0}));let totalSizeKb=0,matched=0;
  for(const row of rows)if(category==='all'||row.group===category){matched++;totalSizeKb+=row.sizeKb;counts[fixture.groups.indexOf(row.group)].count++;}
  return{processed:rows.length,matched,totalSizeKb,groups:counts};
}
