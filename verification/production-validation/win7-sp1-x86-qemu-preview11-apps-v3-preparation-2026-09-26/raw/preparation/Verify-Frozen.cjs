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
  if(input.diagnosticsVersion!==2 || manifest.diagnosticsVersion!==2 || input.parentFreezeSha256!==manifest.parentFreezeSha256 ||
    hash(fs.readFileSync(path.join(root,'parent-freeze.json')))!==manifest.parentFreezeSha256 ||
    input.parentFreezeSha256!=='20f5b68956e9483dfeff26349bd81d49bcd81e4f6aeb8bf0ee7d462588dc9d13') throw new Error('Diagnostic parent freeze differs');
  if(input.layoutVersion!==3 || manifest.layoutVersion!==3 || input.mountRoot!==path.join(root,'volume') ||
    manifest.mountRoot!==input.mountRoot || input.inputSubdir!=='soui-input' || manifest.inputSubdir!==input.inputSubdir) throw new Error('Input layout differs');
  const volumeEntries=fs.readdirSync(input.mountRoot,{withFileTypes:true});
  if(volumeEntries.length!==1 || volumeEntries[0].name!==input.inputSubdir || !volumeEntries[0].isDirectory() || volumeEntries[0].isSymbolicLink()) throw new Error('Host mount root must contain only input subdirectory');
  const transfer=path.join(input.mountRoot,input.inputSubdir),transferEntries=fs.readdirSync(transfer,{withFileTypes:true});
  if(transferEntries.some(e=>!e.isFile()||e.isSymbolicLink()) || JSON.stringify(transferEntries.map(e=>e.name).sort())!==JSON.stringify(input.transferFiles.map(f=>f.path))) throw new Error('Input subdirectory file set differs');
  for(const f of input.transferFiles){const b=fs.readFileSync(path.join(transfer,f.path));if(b.length!==f.bytes||hash(b)!==f.sha256)throw new Error('Transfer identity differs: '+f.path);}
  const buildBytes=fs.readFileSync(path.join(root,'build-Release.json'));
  const build=JSON.parse(buildBytes.toString('utf8').replace(/^\uFEFF/,''));
  if(hash(buildBytes)!==input.buildRecordSha256 || build.source.sha256!==manifest.sourceSha256 || build.buildId!==manifest.buildId) throw new Error('Build identity differs');
  for(const name of ['SouiStarter','SouiWorkbench']) {
    if(JSON.stringify(input.packages[name].files)!==JSON.stringify(build.payloads[name].files) || input.packages[name].payloadSha256!==build.payloads[name].sha256) throw new Error('Payload binding differs: '+name);
  }
  const parentInput=JSON.parse(fs.readFileSync(path.join(root,'input.parent.json'),'utf8'));
  if(input.parentInputSha256!==hash(fs.readFileSync(path.join(root,'input.parent.json'))) ||
    JSON.stringify(input.packages)!==JSON.stringify(parentInput.packages) ||
    JSON.stringify(input.transferFiles)!==JSON.stringify(parentInput.transferFiles)) throw new Error('Parent application/guest-script binding differs');
  for(const name of ['SouiStarter','SouiWorkbench']) {
    const p=input.packages[name],b=fs.readFileSync(path.join(transfer,p.zipName));
    if(b.length!==p.zipBytes || hash(b)!==p.zipSha256) throw new Error('Copied ZIP changed');
    for(const f of p.files){const data=fs.readFileSync(path.join(root,'payloads',name,f.path));if(data.length!==f.bytes||hash(data)!==f.sha256)throw new Error('Copied payload changed: '+name+'/'+f.path);}
  }
  const selfTest=JSON.parse(fs.readFileSync(path.join(root,'self-test-only-v4-results.json'),'utf8'));
  if(selfTest.inputSha256!==hash(fs.readFileSync(path.join(root,'input.json'))) || selfTest.acceptedFixtures!==1 || selfTest.rejectedFixtures!==27 || selfTest.syntheticOnly!==true || selfTest.guestExecuted!==false) throw new Error('Synthetic test binding differs');
  process.stdout.write(JSON.stringify({status:'frozen-preparation-intact',files:manifest.files.length,freezeSha256:hash(manifestBytes),
    inputSha256:hash(fs.readFileSync(path.join(root,'input.json'))),buildId:manifest.buildId,sourceSha256:manifest.sourceSha256,
    guestExecuted:false,releaseReady:false},null,2)+'\n');
} catch(e) {process.stderr.write(e.message+'\n');process.exitCode=1;}
