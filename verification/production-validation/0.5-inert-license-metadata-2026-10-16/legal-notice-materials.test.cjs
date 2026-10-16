'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const root=path.resolve(__dirname,'../..');
const id=require('../../tools/internal/delivery-identity.cjs');
const deploy=require('../../tools/internal/deploy-resources.cjs');
const inventory=JSON.parse(fs.readFileSync(path.join(root,'third_party/licenses/npm/index.json'),'utf8'));

test('upstream package declarations are inert text with their original bytes and identity',()=>{
  assert(inventory.packages.length>0);
  const declarations=new Set();
  for(const entry of inventory.packages){
    const file=entry.declaration;
    assert.match(file.path,/^third_party\/licenses\/npm\/[^/]+\/upstream-package\.json\.txt$/);
    assert(!declarations.has(file.path));declarations.add(file.path);
    const bytes=fs.readFileSync(id.safePath(root,file.path));
    assert.equal(bytes.length,file.bytes);assert.equal(id.hash(bytes),file.sha256);
    const original=JSON.parse(bytes.toString('utf8'));
    assert.equal(original.name,entry.name);assert.equal(original.version,entry.version);
    assert(!fs.existsSync(id.safePath(root,file.path.slice(0,-4))),'Executable-resource JSON alias remains');
    for(const notice of entry.notices){
      const text=fs.readFileSync(id.safePath(root,notice.path));
      assert.equal(text.length,notice.bytes);assert.equal(id.hash(text),notice.sha256);
    }
  }
});

test('runtime deployment preserves every notice and provenance byte without creating JSON aliases',t=>{
  const parent=fs.realpathSync(os.tmpdir()),folder=fs.mkdtempSync(path.join(parent,'soui-legal-materials-'));
  t.after(()=>{
    const actual=fs.realpathSync(folder);assert(actual.startsWith(parent+path.sep));
    fs.rmSync(actual,{recursive:true,force:true});
  });
  const web=path.join(folder,'web'),app=path.join(folder,'app');
  fs.mkdirSync(web);fs.writeFileSync(path.join(web,'index.html'),'<div>local page</div>');
  deploy.replace(web,app,root);
  const legal=path.join(app,'resources/legal');
  for(const name of ['THIRD_PARTY_NOTICES.md',...id.walk(root,'third_party/licenses')])
    assert.deepEqual(fs.readFileSync(id.safePath(legal,name)),fs.readFileSync(id.safePath(root,name)));
  for(const entry of inventory.packages){
    assert.match(entry.declaration.path,/\.json\.txt$/);
    assert(!fs.existsSync(id.safePath(legal,entry.declaration.path.slice(0,-4))));
  }
});
