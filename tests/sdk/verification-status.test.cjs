'use strict';
const test=require('node:test'),assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),crypto=require('node:crypto'),cp=require('node:child_process');
const repository=path.resolve(__dirname,'../..');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const reference='verification/production-validation/current/RELEASE-FACTS.json';
function fixture(t){
  const parent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(parent,'soui-status-'));
  t.after(()=>{const resolved=fs.realpathSync(root);assert.equal(path.dirname(resolved),parent);
    assert(path.basename(resolved).startsWith('soui-status-'));fs.rmSync(resolved,{recursive:true,force:true});});
  fs.mkdirSync(path.join(root,'tools'));
  for(const name of ['Collect-Verification.cjs','verification-status.cjs']){
    const source=path.join(repository,'tools',name);
    if(fs.existsSync(source))fs.copyFileSync(source,path.join(root,'tools',name));
  }
  // The old collector must reach its real argument parser, not fail on a missing import.
  for(const dir of ['internal','sdk'])fs.cpSync(path.join(repository,'tools',dir),path.join(root,'tools',dir),{recursive:true});
  fs.writeFileSync(path.join(root,'VERSION'),'0.5.0\n');
  fs.mkdirSync(path.dirname(path.join(root,reference)),{recursive:true});
  fs.writeFileSync(path.join(root,reference),JSON.stringify({version:'0.5.0',scopedReleaseChecksPassed:true,
    releaseReady:false,productionQualificationComplete:false,licenseReviewComplete:false}));
  const old=Buffer.from('\ufeff'+JSON.stringify({version:'0.4.0-preview.3',status:'passed',sourceDeliveryVerified:true,releaseReady:false},null,2).replaceAll('\n','\r\n')+'\r\n');
  fs.writeFileSync(path.join(root,'verification/acceptance.json'),old);
  const run=(args=['--record-unverified','--evidence-reference',reference])=>cp.spawnSync(process.execPath,
    [path.join(root,'tools/Collect-Verification.cjs'),...args],{cwd:root,encoding:'utf8',windowsHide:true});
  return {root,old,run,acceptance:path.join(root,'verification/acceptance.json'),report:path.join(root,reference)};
}
test('actual collector refreshes current unverified status without requiring builds or inheriting success',t=>{
  const f=fixture(t),before=fs.readFileSync(f.report),result=f.run();
  assert.equal(result.status,0,result.stderr);
  const current=JSON.parse(fs.readFileSync(f.acceptance,'utf8'));
  assert.equal(current.version,'0.5.0');assert.equal(current.scope,'full');assert.equal(current.status,'unverified');
  assert.equal(current.sourceDeliveryVerified,false);assert.equal(current.releaseReady,false);
  assert.equal(current.collectionExecuted,false);assert.equal(current.evidenceReference.referenceOnly,true);
  assert.equal(current.evidenceReference.path,reference);assert.equal(current.evidenceReference.sha256,hash(before));
  assert.equal(current.scopedReleaseChecksPassed,undefined);assert.equal(current.nativeApplicationMatrix,undefined);
  assert.deepEqual(fs.readFileSync(path.join(f.root,current.previousAcceptance.path)),f.old);
  assert.equal(current.previousAcceptance.sha256,hash(f.old));
  assert.deepEqual(fs.readFileSync(f.report),before);assert(!fs.existsSync(path.join(f.root,'out')));
  assert.equal(JSON.parse(result.stdout).acceptanceVerified,false);
});
test('a repeated status refresh preserves the earlier report chain',t=>{
  const f=fixture(t);assert.equal(f.run().status,0);
  const first=fs.readFileSync(f.acceptance),report=JSON.parse(first);
  assert.equal(f.run().status,0);
  const second=JSON.parse(fs.readFileSync(f.acceptance,'utf8'));
  assert.deepEqual(fs.readFileSync(path.join(f.root,second.previousAcceptance.path)),first);
  assert.deepEqual(fs.readFileSync(path.join(f.root,report.previousAcceptance.path)),f.old);
});
test('the archived original remains byte-exact in the Git index',t=>{
  const f=fixture(t);assert.equal(f.run().status,0);
  fs.copyFileSync(path.join(repository,'.gitattributes'),path.join(f.root,'.gitattributes'));
  const git=args=>cp.spawnSync('git',args,{cwd:f.root,windowsHide:true});
  assert.equal(git(['init','--quiet']).status,0);
  const record=JSON.parse(fs.readFileSync(f.acceptance,'utf8'));
  const added=git(['add','--',record.previousAcceptance.path]);assert.equal(added.status,0,String(added.stderr));
  const blob=git(['show',':'+record.previousAcceptance.path]);assert.equal(blob.status,0,String(blob.stderr));
  assert.deepEqual(blob.stdout,f.old);
});
test('wrong-version or qualification-claiming references leave the old acceptance untouched',t=>{
  const f=fixture(t),base=JSON.parse(fs.readFileSync(f.report,'utf8'));
  for(const change of [{version:'0.4.0-preview.3'},{releaseReady:true},{releaseReady:null},{productionQualificationComplete:true}]){
    fs.writeFileSync(f.report,JSON.stringify({...base,...change}));const result=f.run();
    assert.notEqual(result.status,0);assert.match(result.stderr,/reference|version|qualification/i);
    assert.deepEqual(fs.readFileSync(f.acceptance),f.old);
  }
});
test('missing, malformed and non-file references fail without changing acceptance',t=>{
  const f=fixture(t);fs.unlinkSync(f.report);
  for(const setup of [()=>{},()=>fs.writeFileSync(f.report,'{'),()=>{fs.unlinkSync(f.report);fs.mkdirSync(f.report);}]){
    setup();assert.notEqual(f.run().status,0);assert.deepEqual(fs.readFileSync(f.acceptance),f.old);
  }
});
test('mixed, duplicate, unknown and incomplete options do not write status',t=>{
  const f=fixture(t);
  for(const args of [['--record-unverified'],['--record-unverified','--evidence-reference'],
    ['--record-unverified','--evidence-reference',reference,'full'],
    ['--record-unverified','--evidence-reference',reference,'--native-toolchain','LLVM22MSVC142'],
    ['--record-unverified','--evidence-reference',reference,'--evidence-reference',reference],
    ['--foundation','--record-unverified'],['--record-unverified','--unknown',reference]]){
    const result=f.run(args);assert.notEqual(result.status,0);assert.match(result.stderr,/Usage|option/);
    assert.deepEqual(fs.readFileSync(f.acceptance),f.old);
  }
});
test('references cannot escape the evidence directory or point at the output itself',t=>{
  const f=fixture(t);
  for(const name of [path.join(f.root,reference),'../outside.json','verification/production-validation/../acceptance.json',
    'verification/acceptance.json','verification/production-validation/current/../current/RELEASE-FACTS.json']){
    assert.notEqual(f.run(['--record-unverified','--evidence-reference',name]).status,0);
    assert.deepEqual(fs.readFileSync(f.acceptance),f.old);
  }
});
test('a linked reference directory cannot redirect evidence reads',t=>{
  const f=fixture(t),alias=path.join(f.root,'verification/production-validation/alias');
  fs.symlinkSync(path.dirname(f.report),alias,'junction');
  assert.notEqual(f.run(['--record-unverified','--evidence-reference','verification/production-validation/alias/RELEASE-FACTS.json']).status,0);
  assert.deepEqual(fs.readFileSync(f.acceptance),f.old);
});
test('a linked history directory cannot redirect archival writes',t=>{
  const f=fixture(t),target=path.join(f.root,'elsewhere');fs.mkdirSync(target);
  fs.symlinkSync(target,path.join(f.root,'verification/history'),'junction');
  assert.notEqual(f.run().status,0);assert.deepEqual(fs.readFileSync(f.acceptance),f.old);
  assert.deepEqual(fs.readdirSync(target),[]);
});
test('conflicting immutable history is refused before replacing acceptance',t=>{
  const f=fixture(t),archive=path.join(f.root,'verification/history','acceptance-'+hash(f.old)+'.json');
  fs.mkdirSync(path.dirname(archive));fs.writeFileSync(archive,'different');
  assert.notEqual(f.run().status,0);assert.deepEqual(fs.readFileSync(f.acceptance),f.old);
  assert.equal(fs.readFileSync(archive,'utf8'),'different');
});
test('a missing or malformed previous acceptance is never silently replaced',t=>{
  const f=fixture(t);fs.writeFileSync(f.acceptance,'{');assert.notEqual(f.run().status,0);
  assert.equal(fs.readFileSync(f.acceptance,'utf8'),'{');
  fs.unlinkSync(f.acceptance);assert.notEqual(f.run().status,0);assert(!fs.existsSync(f.acceptance));
});
