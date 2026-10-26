'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {nextAction,tailUncertainty}=require('../../../benchmarks/protocol/same-product-input-series.cjs');
test('two identical environmental invalidations stop the scenario without automatic retries',()=>{
  const first={scenario:'pointer-1000',status:'invalid_environment',reason:'desktop-locked'};
  assert.equal(nextAction([first],'pointer-1000').action,'run');
  assert.deepEqual(nextAction([first,{...first}],'pointer-1000'),{action:'stop',reason:'desktop-locked'});
  assert.equal(nextAction([first,{...first,reason:'foreground-changed'}],'pointer-1000').action,'run');
});
test('a product correctness failure stops its candidate immediately',()=>{
  assert.equal(nextAction([{scenario:'wheel',status:'product_failure'}],'wheel').action,'stop');
});
test('sparse tail uncertainty never uses the sample maximum as a population upper bound',()=>{
  const result=tailUncertainty([1,2,3,100]);assert.equal(result.p99,100);
  assert.equal(result.interval.upper_ms,null);assert.equal(result.interval.upper_unbounded,true);
  assert.equal(result.scope,'exploratory');
});
test('P99 is calculated from individual event values, never five round means',()=>{
  const result=tailUncertainty([...Array(99).fill(1),100]);
  assert.equal(result.samples,100);assert.equal(result.p99,1);assert.equal(result.max,100);
  assert.equal(tailUncertainty([]).scope,'unavailable');
});
