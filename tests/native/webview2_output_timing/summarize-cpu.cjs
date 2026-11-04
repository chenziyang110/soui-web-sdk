'use strict';
const fs=require('node:fs'),assert=require('node:assert/strict');
function summarize(rows){
  assert.equal(rows.length,72,'Expected all 18 payload/kind cases in ABBA order');const cases=new Map();
  const comparison=rows[0].comparison||'prepare-only';assert.ok(['prepare-only','guard-only'].includes(comparison));
  for(const row of rows){
    assert.equal(row.schema,'soui.webview2-output-cpu-projection/v1');assert.equal(row.iterations,128);
    for(const key of ['wire_equal','single_cpu_thread'])assert.equal(row[key],true);
    for(const key of ['com_measured','input_measured','fps_measured'])assert.equal(row[key],false);
    for(const key of ['preparation_us','simulated_owner_us','total_us'])assert.ok(Number.isFinite(row[key])&&row[key]>=0);
    assert.ok(row.peak_charged_bytes<=65536&&row.raw_bytes<=60000&&row.raw_bytes>=60);
    assert.equal(row.comparison||'prepare-only',comparison);
    assert.equal(comparison==='guard-only'?row.guarded:row.prepare,[false,true,true,false][row.trial-1]);
    if(comparison==='guard-only')assert.equal(row.prepare,true);
    assert.equal(row.cached+row.fallback,row.prepare?row.iterations:0);
    assert.ok(Math.abs(row.preparation_us+row.simulated_owner_us-row.total_us)<0.01);
    const key=[row.kind,row.pattern,row.raw_bytes].join(':');if(!cases.has(key))cases.set(key,[]);cases.get(key).push(row);
  }
  assert.equal(cases.size,18);
  const results=[];for(const [key,values]of cases){assert.deepEqual(values.map(row=>row.trial),[1,2,3,4]);
    const group=enabled=>values.filter(row=>(comparison==='guard-only'?row.guarded:row.prepare)===enabled),sum=(list,key)=>list.reduce((total,row)=>total+row[key],0);
    const off=group(false),on=group(true),operations=sum(on,'iterations');
    results.push({case:key,operations_per_mode:operations,cached:sum(on,'cached'),fallback:sum(on,'fallback'),
      off_total_us_per_operation:sum(off,'total_us')/operations,on_total_us_per_operation:sum(on,'total_us')/operations,
      off_owner_us_per_operation:sum(off,'simulated_owner_us')/operations,on_owner_us_per_operation:sum(on,'simulated_owner_us')/operations,
      cpu_total_ratio:sum(on,'total_us')/sum(off,'total_us')});
  }
  return {schema:'soui.webview2-output-cpu-summary/v1',comparison,rows:rows.length,cases:results,
    interpretation:'Single-thread CPU phase projection only; cached work moves labels, cache misses may serialize twice. No default-policy decision from this probe.',
    physicalPerformanceAccepted:false,inputLatencyMeasured:false,framesMeasured:false,comMeasured:false};
}
if(require.main===module){try{assert.equal(process.argv.length,3);const bytes=fs.readFileSync(process.argv[2]);assert.ok(bytes.length<1048576);
  console.log(JSON.stringify(summarize(bytes.toString('utf8').trim().split(/\r?\n/).map(JSON.parse)),null,2));
}catch(error){console.error(error.message);process.exitCode=1;}}
module.exports={summarize};
