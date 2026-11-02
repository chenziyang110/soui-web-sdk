'use strict';
// Archive integrity and recorded scope only; never product or platform qualification.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=__dirname;
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const read=name=>JSON.parse(fs.readFileSync(path.join(root,name),'utf8'));
const index=read('FILES.json');
if(index.schema!=='soui.issue-evidence-files/v1'||!Array.isArray(index.files)||!index.files.length)
  throw new Error('Invalid evidence index');
const names=new Set();
for(const row of index.files){
  if(typeof row.path!=='string'||row.path.includes('\\')||row.path.includes(':')||
    row.path.split('/').some(part=>!part||part==='.'||part==='..')||names.has(row.path))
    throw new Error('Invalid or duplicate evidence path');
  names.add(row.path);
  const file=path.join(root,row.path),stat=fs.lstatSync(file);
  if(!stat.isFile()||stat.isSymbolicLink())throw new Error('Evidence must remain a regular file');
  const bytes=fs.readFileSync(file);
  if(bytes.length!==row.bytes||sha(bytes)!==row.sha256)throw new Error('Evidence changed: '+row.path);
}
const facts=read('RESULTS.json');
if(facts.version!=='0.6.1-preview.1'||facts.releaseReady!==false||
  facts.productionQualificationComplete!==false||facts.fullDevtoolsGoalComplete!==false||
  facts.fullCollectionExecuted!==false||facts.guiExecuted!==false)
  throw new Error('Recorded non-qualification scope changed');
if(facts.issueOrder.join(',')!=='23,27,29,31,39,42,77,96,133')throw new Error('Issue order changed');
console.log(JSON.stringify({archiveVerified:true,files:index.files.length,version:facts.version,
  productReexecuted:false,win7Verified:false,releaseReady:false}));
