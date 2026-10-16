'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isVersion}=require('./internal/sdk-version.cjs');
const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const encode=value=>Buffer.from(JSON.stringify(value,null,2)+'\n');
const parse=bytes=>JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));

function safePath(root,relative){
  if(typeof relative!=='string'||!relative||relative.includes('\\')||relative.includes(':')||
    relative.split('/').some(part=>!part||part==='.'||part==='..'||/[<>"|?*\x00-\x1f]|[. ]$/.test(part)||
      /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part)))throw new Error('Invalid status reference path');
  let current=root;
  for(const part of relative.split('/')){
    current=path.join(current,part);
    try{if(fs.lstatSync(current).isSymbolicLink())throw new Error('Linked status path: '+relative);}
    catch(error){if(error.code!=='ENOENT')throw error;}
  }
  return current;
}
function readRegular(root,relative){
  const file=safePath(root,relative),stat=fs.statSync(file);
  if(!stat.isFile()||stat.size>16*1024*1024)throw new Error('Status reference must be a bounded regular file: '+relative);
  return fs.readFileSync(file);
}
function recordUnverified(root,args){
  if(args.length!==2||args[0]!=='--evidence-reference')
    throw new Error('Usage: Collect-Verification.cjs --record-unverified --evidence-reference verification/production-validation/<report>.json');
  root=fs.realpathSync(root);
  const relative=args[1];
  if(!relative.startsWith('verification/production-validation/')||!relative.endsWith('.json'))
    throw new Error('Status reference must be a production-validation JSON record');
  const version=readRegular(root,'VERSION').toString('utf8').replace(/^\uFEFF/,'').trim();
  if(!isVersion(version))throw new Error('Invalid current SDK version');
  const referenceBytes=readRegular(root,relative),reference=parse(referenceBytes);
  if(!reference||reference.version!==version||reference.releaseReady!==false||reference.productionQualificationComplete!==false)
    throw new Error('Status reference version or non-qualification flags do not match');
  const previousBytes=readRegular(root,'verification/acceptance.json'),previous=parse(previousBytes);
  if(!previous||typeof previous!=='object'||Array.isArray(previous))throw new Error('Invalid previous acceptance record');
  const previousHash=hash(previousBytes),archiveRelative='verification/history/acceptance-'+previousHash+'.json';
  const history=safePath(root,'verification/history');
  fs.mkdirSync(history,{recursive:true});
  const archive=safePath(root,archiveRelative);
  try{fs.writeFileSync(archive,previousBytes,{flag:'wx'});}
  catch(error){
    if(error.code!=='EEXIST')throw error;
    if(!readRegular(root,archiveRelative).equals(previousBytes))throw new Error('Conflicting immutable acceptance history');
  }
  const report={schema:'soui.source-delivery-acceptance/v2',version,scope:'full',status:'unverified',
    sourceDeliveryVerified:false,releaseReady:false,collectionExecuted:false,
    recordedAt:new Date().toISOString(),generator:'Collect-Verification.cjs --record-unverified',
    evidenceReference:{path:relative,bytes:referenceBytes.length,sha256:hash(referenceBytes),referenceOnly:true},
    previousAcceptance:{path:archiveRelative,bytes:previousBytes.length,sha256:previousHash},
    limitations:[
      'Full acceptance has not been collected for this version; this command records status only.',
      'The referenced report is checked only for version and non-qualification flags; its external evidence is not revalidated.',
      'Limited-scope results and historical passes do not establish current full source delivery or production qualification.',
      'Long-run, target-platform, performance, upgrade/rollback and licensing gates remain open; see docs/known-issues.md.'
    ]};
  const destination=safePath(root,'verification/acceptance.json');
  const temporary=safePath(root,'verification/acceptance-'+crypto.randomUUID()+'.tmp');
  try{
    fs.writeFileSync(temporary,encode(report),{flag:'wx'});
    // Refuse an intervening writer observed before publication. Collectors must run serially.
    if(!readRegular(root,'verification/acceptance.json').equals(previousBytes))throw new Error('Acceptance changed during status recording');
    if(!readRegular(root,relative).equals(referenceBytes))throw new Error('Status reference changed during recording');
    fs.renameSync(temporary,destination);
  }finally{if(fs.existsSync(temporary))fs.unlinkSync(temporary);}
  return {version,statusRecorded:true,acceptanceVerified:false,sourceDeliveryVerified:false,releaseReady:false,
    output:'verification/acceptance.json'};
}
module.exports={recordUnverified};
