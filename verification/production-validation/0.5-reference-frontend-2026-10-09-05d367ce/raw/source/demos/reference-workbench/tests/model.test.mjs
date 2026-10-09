import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {makeRows,rowAt,defaultState,readState,saveState,restoreState,selectRow,deriveRows,visibleRows,summarize,utf8Bytes} from '../common/model.mjs';
const fixture=JSON.parse(fs.readFileSync(new URL('../common/fixture.json',import.meta.url),'utf8'));
const rows=makeRows(fixture);
test('fixed dataset has exact identities and deterministic endpoints',()=>{
  assert.equal(rows.length,10000);assert.equal(new Set(rows.map(row=>row.id)).size,10000);
  assert.deepEqual(rows[0],rowAt(fixture,0));assert.deepEqual(rows[9999],rowAt(fixture,9999));
  assert.equal(rows[0].name,'fixture-00001.asset');assert.equal(rows[9999].name,'fixture-10000.asset');
});
test('wrong fixture and row bounds reject',()=>{
  assert.throws(()=>makeRows({...fixture,seed:1}));for(const index of [-1,10000,0.5,NaN])assert.throws(()=>rowAt(fixture,index));
});
test('UTF-8 limits count Chinese and supplementary characters without TextEncoder',()=>{
  assert.equal(utf8Bytes('中文'),6);assert.equal(utf8Bytes('😀'),4);
  for(const value of ['\ud800','\udc00','\ud800x'])assert.throws(()=>utf8Bytes(value));
});
test('full fixed summary conserves every row and filtered group',()=>{
  const all=summarize(rows,fixture);assert.equal(all.processed,10000);assert.equal(all.matched,10000);
  assert.equal(all.groups.reduce((sum,group)=>sum+group.count,0),10000);
  const group=summarize(rows,fixture,'素材');const actual=rows.filter(row=>row.group==='素材');
  assert.equal(group.matched,actual.length);assert.equal(group.totalSizeKb,actual.reduce((sum,row)=>sum+row.sizeKb,0));
  assert.equal(group.groups.filter(item=>item.group!=='素材').every(item=>item.count===0),true);
});
test('sort/filter uses same data and does not mutate original order',()=>{
  const state={...defaultState(fixture),query:'fixture-0',sort:'sizeKb',descending:true,category:'项目'};
  const filtered=deriveRows(rows,state,fixture);
  assert.ok(filtered.length>0);assert.ok(filtered.every(row=>row.group==='项目'&&row.name.includes('fixture-0')));
  assert.ok(filtered.every((row,i)=>i===0||filtered[i-1].sizeKb>=row.sizeKb));assert.equal(rows[0].id,1);
});
test('selection is bounded, unique and copied',()=>{
  let state=defaultState(fixture);
  for(let id=1;id<=100;id++)state=selectRow(state,id,true,fixture);
  assert.throws(()=>selectRow(state,101,true,fixture));assert.equal(selectRow(state,100,true,fixture).selectedIds.length,100);
  const next=selectRow(state,1,false,fixture);assert.equal(next.selectedIds.length,99);assert.equal(state.selectedIds.length,100);
});
test('selection and viewport reject non-integer/out-of-range inputs',()=>{
  for(const id of [0,10001,1.5,NaN])assert.throws(()=>selectRow(defaultState(fixture),id,true,fixture));
  for(const top of [-1,NaN,Infinity])assert.throws(()=>visibleRows(rows,top,320));
  assert.throws(()=>visibleRows(rows,0,320,31));
});
test('virtual window caps actual DOM rows to 80 even for an oversized viewport',()=>{
  assert.ok(visibleRows(rows,0,100000).rows.length<=80);
  const tail=visibleRows(rows,320000,320);assert.equal(tail.rows.at(-1).id,10000);
  assert.deepEqual(visibleRows([],0,320),{first:0,rows:[]});
});
test('reload restores only bounded UI state and explains old task as unknown',()=>{
  const state={...defaultState(fixture),theme:'dark',draftName:'中文表单',selectedIds:[1,10000],
    previousTask:{operationId:'previous-op',taskId:'sdk-task-old',lastKnownState:'running'}};
  const result=restoreState(saveState(state,fixture),fixture);
  assert.equal(result.status,'restored');assert.equal(result.taskRecovery,'previous-document-unknown');
  assert.deepEqual(result.state.selectedIds,[1,10000]);assert.equal(result.state.draftName,'中文表单');
});
test('credential/extra fields cannot be persisted as UI state',()=>{
  for(const key of ['token','sessionId','scope','userGesture'])assert.throws(()=>saveState({...defaultState(fixture),[key]:'forged'},fixture));
  assert.equal(restoreState('{"token":"forged"}',fixture).status,'rejected');
});
test('wrong fixture, duplicate selections, oversized drafts and malformed bytes reject',()=>{
  for(const patch of [{fixtureId:'wrong'},{selectedIds:[1,1]},{draftName:'中'.repeat(22)},
    {draftNotes:'中'.repeat(86)},{query:'x'.repeat(129)},{draftName:'\ud800'},{previousTask:{taskId:'old',operationId:'old',lastKnownState:'fake'}}])
    assert.throws(()=>readState({...defaultState(fixture),...patch},fixture));
  assert.equal(restoreState('x'.repeat(8193),fixture).status,'rejected');
  assert.equal(restoreState('{',fixture).status,'rejected');
});
test('missing storage is explicit empty rather than recovered',()=>{
  const result=restoreState(null,fixture);assert.equal(result.status,'empty');assert.equal(result.taskRecovery,'none');
});
