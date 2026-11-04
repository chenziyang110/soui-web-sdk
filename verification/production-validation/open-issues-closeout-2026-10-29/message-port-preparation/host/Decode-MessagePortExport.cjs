'use strict';
// Offline only: decode one explicitly selected complete export, preserving original bytes.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),readline=require('node:readline'),assert=require('node:assert/strict');
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
async function main(){
 const [serial,output,exportId]=process.argv.slice(2);assert.equal(process.argv.length,5);assert(/^[A-F0-9]{32}$/.test(exportId));assert(!fs.existsSync(output));fs.mkdirSync(output,{recursive:true});fs.mkdirSync(output+'/raw');
 const byId=new Map(),paths=new Set();let ordinal=0,pending=null,completed=null;
 const prefix='MP77'+exportId+'C';
 function relative(name){assert(typeof name==='string'&&name&&!name.includes('\\')&&!name.includes(':')&&!name.split('/').some(x=>!x||x==='.'||x==='..'));return name;}
 for await(const line of readline.createInterface({input:fs.createReadStream(serial),crlfDelay:Infinity})){
  if(line.startsWith(prefix)&&line.endsWith('BEGIN')){assert.equal(pending,null);assert.equal(line,prefix+String(ordinal).padStart(6,'0')+'BEGIN');pending={tag:line.slice(0,-5),data:null};continue;}
  if(!pending)continue;
  if(line===pending.tag+'END'){
   assert(pending.data);assert(!completed);const raw=Buffer.from(pending.data,'base64');assert(raw.length<=8388608);assert.equal(raw.toString('base64'),pending.data);
   const row=JSON.parse(raw.toString('utf8'));assert.equal(row.exportId,exportId);
   if(row.schema==='soui.message-port-serial-completed/v1'){completed=row;assert.equal(row.rawChunkLimit,1048576);assert(row.sourceBytesBeforeAfterEqual);}
   else{
    assert.equal(row.schema,'soui.message-port-serial-chunk/v1');relative(row.path);assert(Number.isSafeInteger(row.fileId)&&row.fileId>=0&&row.fileId<=512);
    assert(Number.isSafeInteger(row.fileBytes)&&row.fileBytes>=0&&Number.isSafeInteger(row.offset)&&row.offset>=0);
    assert(/^[a-f0-9]{64}$/.test(row.fileSha256));const bytes=Buffer.from(row.dataBase64,'base64');assert.equal(bytes.toString('base64'),row.dataBase64);assert.equal(bytes.length,row.chunkBytes);assert(bytes.length<=1048576);assert.equal(digest(bytes),row.chunkSha256);
    let file=byId.get(row.fileId);
    if(!file){assert.equal(row.offset,0);assert(!paths.has(row.path.toLowerCase()));paths.add(row.path.toLowerCase());const target=path.join(output,'raw',row.path);fs.mkdirSync(path.dirname(target),{recursive:true});file={path:row.path,bytes:row.fileBytes,sha256:row.fileSha256,offset:0,chunks:0,target};fs.writeFileSync(target,Buffer.alloc(0),{flag:'wx'});byId.set(row.fileId,file);}
    if(file.chunks>0)assert(file.offset<file.bytes,'Duplicate chunk after file completion');assert.equal(file.path,row.path);assert.equal(file.bytes,row.fileBytes);assert.equal(file.sha256,row.fileSha256);assert.equal(file.offset,row.offset);assert(file.offset+bytes.length<=file.bytes);
    fs.appendFileSync(file.target,bytes);file.offset+=bytes.length;file.chunks++;
   }
   ordinal++;pending=null;continue;
  }
  assert.equal(pending.data,null,'Unexpected extra line inside frame');assert(line.length<=12000000&&/^[A-Za-z0-9+/]*={0,2}$/.test(line));pending.data=line;
 }
 assert.equal(pending,null);assert(completed,'No complete export receipt');assert.equal(completed.files,byId.size);
 const rows=[...byId.entries()].sort((a,b)=>a[0]-b[0]);assert.deepEqual(rows.map(row=>row[0]),Array.from({length:rows.length},(_,i)=>i));
 for(const [,file] of rows){assert.equal(file.offset,file.bytes);const actual=fs.readFileSync(file.target);assert.equal(actual.length,file.bytes);assert.equal(digest(actual),file.sha256);}
 assert.equal(rows.reduce((n,[,f])=>n+f.chunks,0),completed.chunks);assert.equal(rows.reduce((n,[,f])=>n+f.bytes,0),completed.totalOriginalBytes);
 const catalogFile=byId.get(0);assert.equal(catalogFile.path,'ExportFiles.actual.json');assert.equal(catalogFile.sha256,completed.catalogSha256);
 const catalog=JSON.parse(fs.readFileSync(catalogFile.target,'utf8'));assert.equal(catalog.schema,'soui.message-port-export-catalog/v1');
 assert.deepEqual(rows.slice(1).map(([,f])=>({path:f.path,bytes:f.bytes,sha256:f.sha256})),catalog.files);
 const result={schema:'soui.message-port-serial-decoded/v1',serial,exportId,files:byId.size,chunks:completed.chunks,
  rawBytes:completed.totalOriginalBytes,catalogSha256:completed.catalogSha256,allOriginalBytesVerified:true,testPassed:null};
 fs.writeFileSync(output+'/Decoded.actual.json',JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));
}
main().catch(error=>{console.error(error.stack);process.exitCode=1});
