'use strict';
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const root=__dirname;
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function walk(dir,rel='') {
  return fs.readdirSync(path.join(dir,rel),{withFileTypes:true}).flatMap(e=>{
    const name=rel?`${rel}/${e.name}`:e.name;
    if(e.isSymbolicLink()) throw new Error('Linked frozen path: '+name);
    return e.isDirectory()?walk(dir,name):[name];
  }).sort();
}
try {
  const manifestBytes=fs.readFileSync(path.join(root,'freeze.json'));
  const manifest=JSON.parse(manifestBytes);
  if(manifest.schema!=='soui.win7-app-preparation-freeze/v1' || manifest.guestExecuted!==false || manifest.releaseReady!==false ||
    manifest.buildId!=='7e000158-98f0-4349-854e-15c581663737' || manifest.sourceSha256!=='a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550') throw new Error('Invalid freeze identity');
  if(hash(JSON.stringify(manifest.files,null,2)+'\n')!==manifest.filesSha256) throw new Error('Invalid freeze file list');
  const names=walk(root).filter(n=>n!=='freeze.json');
  if(JSON.stringify(names)!==JSON.stringify(manifest.files.map(f=>f.path))) throw new Error('Frozen file set changed');
  for(const f of manifest.files) {
    if(f.path.includes('\\') || f.path.split('/').some(p=>!p || p==='.' || p==='..' || /[:\x00-\x1f]/.test(p))) throw new Error('Unsafe freeze path');
    const b=fs.readFileSync(path.join(root,f.path));
    if(b.length!==f.bytes || hash(b)!==f.sha256) throw new Error('Frozen file changed: '+f.path);
  }
  const input=JSON.parse(fs.readFileSync(path.join(root,'input.json'),'utf8'));
  const buildBytes=fs.readFileSync(path.join(root,'build-Release.json'));
  const build=JSON.parse(buildBytes.toString('utf8').replace(/^\uFEFF/,''));
  if(hash(buildBytes)!==input.buildRecordSha256 || build.source.sha256!==manifest.sourceSha256 || build.buildId!==manifest.buildId) throw new Error('Build identity differs');
  for(const name of ['SouiStarter','SouiWorkbench']) {
    if(JSON.stringify(input.packages[name].files)!==JSON.stringify(build.payloads[name].files) || input.packages[name].payloadSha256!==build.payloads[name].sha256) throw new Error('Payload binding differs: '+name);
  }
  const selfTest=JSON.parse(fs.readFileSync(path.join(root,'self-test-only-v2-results.json'),'utf8'));
  if(selfTest.inputSha256!==hash(fs.readFileSync(path.join(root,'input.json'))) || selfTest.acceptedFixtures!==1 || selfTest.rejectedFixtures!==17 || selfTest.guestExecuted!==false) throw new Error('Synthetic test binding differs');
  process.stdout.write(JSON.stringify({status:'frozen-preparation-intact',files:manifest.files.length,freezeSha256:hash(manifestBytes),
    inputSha256:hash(fs.readFileSync(path.join(root,'input.json'))),buildId:manifest.buildId,sourceSha256:manifest.sourceSha256,
    guestExecuted:false,releaseReady:false},null,2)+'\n');
} catch(e) {process.stderr.write(e.message+'\n');process.exitCode=1;}
