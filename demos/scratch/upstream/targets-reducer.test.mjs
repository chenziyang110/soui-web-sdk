import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

const text=await readFile(new URL('./targets-reducer.js',import.meta.url),'utf8');
const {default:reducer,updateTargets}=await import('data:text/javascript;base64,'+Buffer.from(text).toString('base64'));
test('preserves full mutable target fields, stage, order and fresh records',()=>{
  const variables={score:{value:1}};
  const stage={id:'stage',isStage:true};
  const first={id:'first',isStage:false,x:0,variables,order:99};
  const second={id:'second',isStage:false};
  const a=reducer(undefined,updateTargets([first,stage,second],'first'));
  assert.equal(a.stage,stage);assert.equal(a.sprites.first.order,99);assert.equal(a.sprites.second.order,1);
  assert.equal(a.sprites.first.variables,variables);
  variables.score.value=2;
  const b=reducer(a,updateTargets([first,stage,second],'second'));
  assert.notEqual(b,a);assert.notEqual(b.sprites.first,a.sprites.first);
  assert.equal(b.sprites.first.variables.score.value,2);assert.equal(b.editingTarget,'second');
});
test('deletions, reorder, stage absence and duplicate id keep upstream results',()=>{
  const one={id:'a',isStage:false},two={id:'b',isStage:false};
  const a=reducer(undefined,updateTargets([one,two],'a'));
  const b=reducer(a,updateTargets([two,{...two,x:2}],'b'));
  assert.deepEqual(Object.keys(b.sprites),['b']);assert.equal(b.sprites.b.order,1);
  assert.deepEqual(b.stage,{});assert.equal(b.sprites.b.x,2);
});
