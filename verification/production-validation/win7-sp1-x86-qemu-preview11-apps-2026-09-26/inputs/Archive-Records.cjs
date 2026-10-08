'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process');
const root=__dirname;
const files=[
  'README.md','input.json','build-Release.json','Prepare-Payload.cjs','Verify-AppSerial.cjs','SelfTest-Serial.cjs',
  'Freeze-Preparation.cjs','Verify-Frozen.cjs','Archive-Records.cjs','self-test-results.json','self-test-only-v2-results.json',
  'SouiStarter-zip-create.log','SouiStarter-zip-test.log','SouiWorkbench-zip-create.log','SouiWorkbench-zip-test.log',
  ...['run-preview11.cmd','run-apps.ps1','verify-apps.ps1','export-serial.ps1'].flatMap(n=>['harness/'+n,'transfer/'+n]),
  ...['SouiStarter-payload.tsv','SouiWorkbench-payload.tsv','transfer-files.tsv'].map(n=>'transfer/'+n),
  ...['souistarter-payload-pre.tsv','souiworkbench-payload-pre.tsv','exitcode.txt'].map(n=>'host-payload-check/'+n)
].sort();
const archive=path.join(root,'preview11-win7-preparation-records.zip');
if(fs.existsSync(archive)) throw new Error('Records archive exists');
fs.writeFileSync(path.join(root,'records-file-list.txt'),files.join('\r\n')+'\r\n',{flag:'wx'});
const result=cp.spawnSync(path.join(root,'transfer/7za.exe'),['a','-tzip','-mx=1','-mmt=1',archive,'@records-file-list.txt'],
  {cwd:root,windowsHide:true,encoding:'utf8',timeout:60000});
fs.writeFileSync(path.join(root,'records-zip-create.log'),(result.stdout||'')+(result.stderr||''),{flag:'wx'});
if(result.error || result.status!==0) throw new Error('Records archive creation failed');
const checked=cp.spawnSync(path.join(root,'transfer/7za.exe'),['t',archive],{cwd:root,windowsHide:true,encoding:'utf8',timeout:60000});
fs.writeFileSync(path.join(root,'records-zip-test.log'),(checked.stdout||'')+(checked.stderr||''),{flag:'wx'});
if(checked.error || checked.status!==0) throw new Error('Records archive test failed');
process.stdout.write(JSON.stringify({archive,files:files.length,bytes:fs.statSync(archive).size,guestExecuted:false})+'\n');
