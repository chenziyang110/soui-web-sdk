'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const path=require('node:path');
const kit=require('../../tools/internal/devkit-command.cjs');
const root=path.resolve(__dirname,'../..');
const powershell='C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe';
const preamble=['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'tools/Package-Applications.ps1')];
const bad=args=>assert.throws(()=>kit.parseArgs(args),error=>error.code==='E_ARGUMENT');

test('omitted compression and bundle retain exact prior default and modern application argv',()=>{
  for(const architecture of ['x86','x64'])for(const profile of ['compatibility','modern']){
    const value=kit.parseArgs(['package','--kind','applications','--architecture',architecture,'--profile',profile]);
    const plan=kit.planCommand(value,root,powershell);
    assert.equal(value.compressionProfile,undefined);assert.equal(value.bundle,undefined);
    assert.equal(value.profile,profile);assert.equal(plan.executable,powershell);
    assert.deepEqual(plan.args,[...preamble,'-Architecture',architecture,...(profile==='modern'?['-Profile','modern']:[])]);
  }
  const normal=kit.parseArgs(['package','--kind','applications']);
  assert.equal(normal.profile,'compatibility');
  assert.deepEqual(kit.planCommand(normal,root,powershell).args,[...preamble,'-Architecture','x64']);
});

test('all explicit compression selections and bundle combinations forward literal application arguments',()=>{
  for(const compression of ['zip','7z-balanced','7z-small'])for(const bundle of [false,true]){
    for(const profile of ['compatibility','modern'])for(const architecture of ['x86','x64']){
      const value=kit.parseArgs(['package','--kind','applications','--architecture',architecture,'--profile',profile,
        '--compression-profile',compression,...(bundle?['--bundle']:[])]);
      const plan=kit.planCommand(value,root,powershell);
      assert.equal(value.compressionProfile,compression);assert.equal(value.bundle,bundle||undefined);
      assert.equal(value.profile,profile);
      assert.deepEqual(plan.args,[...preamble,'-Architecture',architecture,...(profile==='modern'?['-Profile','modern']:[]),
        '-CompressionProfile',compression,...(bundle?['-Bundle']:[])]);
      assert.equal(plan.args.includes('-Command'),false);
    }
  }
});

test('bundle alone leaves renderer and implicit compression unchanged',()=>{
  const value=kit.parseArgs(['package','--kind','applications','--bundle']);
  assert.equal(value.profile,'compatibility');assert.equal(value.compressionProfile,undefined);
  assert.deepEqual(kit.planCommand(value,root,powershell).args,[...preamble,'-Architecture','x64','-Bundle']);
});

test('source and SDK commands reject application-only compression and bundle flags explicitly',()=>{
  for(const args of [['package'],['package','--kind','source'],['package','--kind','sdk','--output','C:/package']]){
    for(const flags of [['--compression-profile','zip'],['--compression-profile','7z-balanced'],['--compression-profile','7z-small'],['--bundle']])bad([...args,...flags]);
  }
  for(const command of ['doctor','build','test','create','upgrade','dev']){
    bad([command,'--compression-profile','zip']);bad([command,'--bundle']);
  }
});

test('invalid profiles, repeated options, option-as-value and unknown options are rejected',()=>{
  const base=['package','--kind','applications'];
  for(const flags of [
    ['--compression-profile','ZIP'],['--compression-profile','7z'],['--compression-profile','compatibility'],
    ['--compression-profile','zip; Write-Host bad'],['--compression-profile',''],['--compression-profile'],
    ['--compression-profile','--bundle'],['--compression-profile','zip','--compression-profile','zip'],
    ['--compression-profile','zip','--compression-profile','7z-small'],['--bundle','--bundle'],
    ['--bundle','true'],['--bundle','false'],['--compress-profile','zip'],['--bundle=true'],
    ['--compression-profile','7z-small\n'],['--compression-profile','zip','--arbitrary-7z-arg','-mx=9'],
  ])bad([...base,...flags]);
});

test('existing source and SDK package plans retain their original command routes',()=>{
  const source=kit.planCommand(kit.parseArgs(['package','--output','C:/Archive/source.zip']),root,powershell);
  assert.deepEqual(source.args,['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(root,'tools/Package.ps1'),'-OutputFile','C:/Archive/source.zip']);
  const sdk=kit.planCommand(kit.parseArgs(['package','--kind','sdk','--output','C:/Archive/sdk','--with-gui-bindings']),root,powershell);
  assert.equal(sdk.executable,process.execPath);
  assert.equal(sdk.args[0],path.join(root,'tools/sdk/soui-sdk.cjs'));
  assert.equal(sdk.args.includes('--with-gui-bindings'),true);
  assert.equal(sdk.args.some(value=>/compression|bundle/.test(value)),false);
});
