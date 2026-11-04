'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {spawnSync}=require('node:child_process');
const {requireFormalHarness}=require('../../../benchmarks/protocol/input-ui-harness-policy.cjs');
const frozen=require('../../../benchmarks/protocol/freeze-input-ui-request.cjs');
const {compareRuns}=require('../../../benchmarks/protocol/same-product-input-pair.cjs');
function formal(){return {measurement_path:'public-sdk',diagnostics:{suppress_focus:false,borderless:false,foreground:false,logging:false}};}
test('frozen inputs detect same-length mutation before a measurement launch',()=>{
  const directory=fs.mkdtempSync(path.join(os.tmpdir(),'soui-input-ui-pins-'));
  try{
    const filename=path.join(directory,'tool');fs.writeFileSync(filename,'old');
    const prior=frozen.pin(filename);frozen.verifyPins(frozen.pin(filename),prior);
    fs.writeFileSync(filename,'new');assert.throws(()=>frozen.verifyPins(frozen.pin(filename),prior),/changed/);
    assert.throws(()=>frozen.verify({schema:'soui.input-ui-run-request/v1'}),/v2/);
  }finally{fs.rmSync(directory,{recursive:true,force:true});}
});
test('matching commit labels do not allow changed measurement source bytes into a pair',()=>{
  assert.throws(()=>compareRuns({record:{measurement_sources_sha256:'a'.repeat(64)}},
    {record:{measurement_sources_sha256:'b'.repeat(64)}}),/source bytes differ/);
});
test('formal reader rejects historical unknown path and every diagnostic intervention',()=>{
  requireFormalHarness(formal());
  assert.throws(()=>requireFormalHarness({}),/public SDK path/);
  for(const key of Object.keys(formal().diagnostics)){
    const value=formal();value.diagnostics[key]=true;assert.throws(()=>requireFormalHarness(value),/diagnostic/);
    value.diagnostics[key]='false';assert.throws(()=>requireFormalHarness(value));
  }
  const unknown=formal();unknown.diagnostics.new_intervention=false;assert.throws(()=>requireFormalHarness(unknown));
  const diagnostic=formal();diagnostic.measurement_path='diagnostic';assert.throws(()=>requireFormalHarness(diagnostic));
});
test('raw SDK release identity maps to its exact package tuple and rejects malformed versions',()=>{
  const root=fs.mkdtempSync(path.join(os.tmpdir(),'soui-input-ui-version-'));
  try{
    const directory=path.join(root,'benchmarks/input-ui-host');fs.mkdirSync(directory,{recursive:true});
    fs.copyFileSync(path.resolve(__dirname,'../../../benchmarks/input-ui-host/sdk_version.cmake'),path.join(directory,'sdk_version.cmake'));
    // The actual imported-target/default/source-cache lifecycle is configured by
    // benchmarks/input-ui-host/tests/sdk-version, not simulated in script mode.
    fs.writeFileSync(path.join(root,'check.cmake'),'include("${CMAKE_CURRENT_LIST_DIR}/benchmarks/input-ui-host/sdk_version.cmake")\nsoui_input_ui_package_version("${SELECTED_VERSION}" actual)\nif(NOT actual STREQUAL EXPECTED_VERSION)\n message(FATAL_ERROR "Version mismatch")\nendif()\n');
    const run=(expected,selected)=>spawnSync('cmake',[`-DEXPECTED_VERSION=${expected}`,`-DSELECTED_VERSION=${selected}`,'-P',path.join(root,'check.cmake')],{encoding:'utf8'});
    for(const [expected,selected]of [['0.6.0','0.6.0'],['0.7.0','0.7.0-preview.1'],['0.7.0','0.7.0-rc.0']]){
      const result=run(expected,selected);assert.ifError(result.error);assert.equal(result.status,0,result.stderr);
    }
    for(const selected of ['0.6','0.6.0;anything','0.07.0','0.7.0-preview.1+local'])
      assert.notEqual(run('ignored',selected).status,0);
  }finally{fs.rmSync(root,{recursive:true,force:true});}
});
