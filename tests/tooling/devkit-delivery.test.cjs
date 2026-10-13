'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const sdk=require('../../tools/sdk/soui-sdk.cjs');
const identity=require('../../tools/internal/delivery-identity.cjs');
const repo=path.resolve(__dirname,'../..');
test('the real SDK pack/install transaction delivers the entry without Demo/templates and refuses same-version replacement',()=>{
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'soui-devkit-delivery-'));
  const write=(root,name,bytes)=>{const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);};
  try {
    const source=path.join(temporary,'source'),first=path.join(temporary,'first'),second=path.join(temporary,'second'),project=path.join(temporary,'project');
    for(const name of sdk.managedRoots)write(source,name+'/fixture.txt','synthetic packaging input\n');
    for(const name of sdk.managedFiles)write(source,name,'synthetic packaging input\n');
    for(const name of ['tools/Soui.ps1','tools/Doctor.ps1','docs/developer-workflow.md','tools/internal/devkit-command.cjs'])
      write(source,name,fs.readFileSync(path.join(repo,name)));
    write(source,'templates/desktop-app/app/private.txt','must not enter Core');
    write(source,'demos/workbench/business.txt','must not enter Core');
    sdk.pack(source,first,'0.5.0');
    const manifest=sdk.readPackage(first);
    for(const name of ['tools/Soui.ps1','tools/Doctor.ps1','tools/internal/devkit-command.cjs','docs/developer-workflow.md'])
      assert(manifest.files.some(x=>x.path===name));
    assert.equal(manifest.files.some(x=>/^(templates|demos)\//.test(x.path)),false);
    write(project,'app/business.txt','business stays');sdk.install(first,project);
    const entry=manifest.files.find(file=>file.path==='tools/Soui.ps1');
    assert.equal(entry.sourceNormalization,'lf');
    assert.deepEqual(fs.readFileSync(path.join(project,'tools/Soui.ps1')),
      identity.normalize(fs.readFileSync(path.join(repo,'tools/Soui.ps1')),entry.sourceNormalization));
    write(source,'tools/Soui.ps1','different contents under same version');sdk.pack(source,second,'0.5.0');
    assert.throws(()=>sdk.install(second,project),/version cannot identify different contents/);
    assert.equal(fs.readFileSync(path.join(project,'app/business.txt'),'utf8'),'business stays');
    assert.equal(sdk.readLock(project).manifest.version,'0.5.0');
  } finally {fs.rmSync(temporary,{recursive:true,force:true});}
});
