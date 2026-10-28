'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),zlib=require('node:zlib'),assert=require('node:assert/strict');
const helper=path.join(__dirname,'StrictJsonReader.source.cjs');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
assert.equal(hash(fs.readFileSync(helper)),'61bceb766ecdfaE0552c26212b332c8d7dbd0c2f24833cf7d27d59932f2b23cd'.toLowerCase());
const {parseStrictJson}=require(helper);
const strict=bytes=>parseStrictJson(new TextDecoder('utf-8',{fatal:true}).decode(bytes));
const [input,output,wanted]=process.argv.slice(2);
assert.ok(input&&output,'usage: node Extract-Serial-v2.cjs SERIAL_LOG NEW_OUTPUT [TAG]');
assert.equal(fs.existsSync(output),false,'output must be new');
const raw=fs.readFileSync(input);assert.ok(raw.length<=32*1024*1024,'bounded32MiB serial input');
const inputPin={path:path.resolve(input),bytes:raw.length,sha256:hash(raw)};
const lines=[];
for(let start=0;start<raw.length;){const lf=raw.indexOf(10,start),end=lf<0?raw.length:lf+1;let textEnd=lf<0?end:lf;if(raw[textEnd-1]===13)textEnd--;
 lines.push({start,end,text:raw.subarray(start,textEnd).toString('ascii')});start=end;}
const begins=lines.map((line,index)=>({line,index,match:/^(R06OWNED[A-F0-9]{32})BEGIN$/.exec(line.text)})).filter(row=>row.match);
const selected=wanted||((assert.equal(begins.length,1,'select one unique frame'),begins[0].match[1]));
assert.match(selected,/^R06OWNED[A-F0-9]{32}$/);
const begin=begins.filter(row=>row.match[1]===selected);assert.equal(begin.length,1,'unique selected BEGIN');
const ends=lines.map((line,index)=>({line,index})).filter(row=>row.line.text===selected+'END');assert.equal(ends.length,1,'one complete exact END line');
assert.ok(ends[0].index>begin[0].index,'END must follow BEGIN');
const frame=raw.subarray(begin[0].line.start,ends[0].line.end);
const content=lines.slice(begin[0].index,ends[0].index).map(row=>row.text);
assert.ok(content[1]?.startsWith(selected+'META '),'metadata required');
function base64(value){assert.ok(typeof value==='string'&&/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value),'strict base64');const b=Buffer.from(value,'base64');assert.equal(b.toString('base64'),value);return b;}
const metaRaw=base64(content[1].slice((selected+'META ').length)),meta=strict(metaRaw);
assert.equal(meta.schema,'soui.release-vm-gzip-serial/v1');assert.equal(meta.tag,selected);
for(const [name,maximum] of [['files',512],['rawBytes',8388608],['envelopeBytes',12582912],['gzipBytes',12582912],['chunks',8192]])assert.ok(Number.isSafeInteger(meta[name])&&meta[name]>0&&meta[name]<=maximum,'bounded '+name);
assert.equal(meta.chunkChars,2048);assert.equal(content.length,meta.chunks+2,'exact chunk count');
const chunks=content.slice(2).map((line,i)=>{const prefix=selected+'DATA '+i+'/'+meta.chunks+' ';assert.ok(line.startsWith(prefix),'ordered exact chunks');const value=line.slice(prefix.length);assert.ok(value.length>0&&value.length<=2048);return value;});
const compressed=base64(chunks.join(''));assert.equal(compressed.length,meta.gzipBytes);assert.equal(hash(compressed),meta.gzipSha256);
const envelope=zlib.gunzipSync(compressed,{maxOutputLength:12582912});assert.equal(envelope.length,meta.envelopeBytes);assert.equal(hash(envelope),meta.envelopeSha256);
const payload=strict(envelope);assert.equal(Object.keys(payload).length,meta.files);
const catalog=base64(payload['ExportFiles.actual.json']);assert.equal(hash(catalog),meta.catalogSha256);
const parsed=strict(catalog);assert.equal(parsed.schema,'soui.release-vm-export-files/v1');assert.ok(Array.isArray(parsed.files));assert.equal(parsed.files.length+1,meta.files);
const expected=['ExportFiles.actual.json',...parsed.files.map((_,i)=>'file'+String(i).padStart(4,'0'))];assert.deepEqual(Object.keys(payload).sort(),expected.sort());
assert.ok(/^C:\\release06-control\\evidence\\[^\\/]+$/i.test(meta.evidenceDirectory),'bounded evidence root');
const rows=[{relative:'ExportFiles.actual.json',data:catalog,guestPath:meta.evidenceDirectory+'\\ExportFiles.actual.json'}];
const seen=new Set();let total=catalog.length;
for(let i=0;i<parsed.files.length;i++){
 const row=parsed.files[i];assert.equal(typeof row.path,'string');assert.ok(Number.isSafeInteger(row.bytes)&&row.bytes>=0&&row.bytes<=8388608);assert.match(row.sha256,/^[a-f0-9]{64}$/);
 const name=path.win32.normalize(row.path);assert.equal(path.win32.isAbsolute(name),true);assert.ok(!/[\x00-\x1f<>"|?*]/.test(name));
 assert.ok(name.toLowerCase().startsWith('c:\\sr06\\out\\')||name.toLowerCase().startsWith(meta.evidenceDirectory.toLowerCase()+'\\'),'guest root bounds');
 assert.equal(seen.has(name.toLowerCase()),false);seen.add(name.toLowerCase());
 const data=base64(payload['file'+String(i).padStart(4,'0')]);assert.equal(data.length,row.bytes);assert.equal(hash(data),row.sha256);
 const relative='guest/'+name.slice(3).replaceAll('\\','/');assert.ok(!relative.split('/').includes('..'));rows.push({relative,data,guestPath:name});total+=data.length;
}
assert.equal(total,meta.rawBytes);assert.ok(total<=8388608);fs.mkdirSync(output);
function save(relative,data){const file=path.resolve(output,relative);assert.ok(file.startsWith(path.resolve(output)+path.sep));fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,data,{flag:'wx'});return {path:relative,bytes:data.length,sha256:hash(data)};}
const framePin=save('SerialFrame.bin',frame),envelopePin=save('DecodedEnvelope.json',envelope),gzipPin=save('CompressedEnvelope.gz',compressed),metaPin=save('TransportMeta.json',metaRaw);
for(const row of rows)save(row.relative,row.data);
const report={schema:'soui.release-vm-serial-extraction/v2',tag:selected,inputObservedPin:inputPin,
 selectedFrame:{...framePin,rawStartOffset:begin[0].line.start,rawEndOffsetExclusive:ends[0].line.end,uniqueExactBeginAndEnd:true},
 decodedEnvelope:envelopePin,compressedEnvelope:gzipPin,transportMetadata:metaPin,meta,
 strictJsonReader:{source:'C:/sr06/tools/internal/owned-tail-evidence.cjs',copy:'StrictJsonReader.source.cjs',sha256:hash(fs.readFileSync(helper)),duplicateFree:true},
 rawFiles:rows.map(r=>({path:r.relative,guestPath:r.guestPath,bytes:r.data.length,sha256:hash(r.data)})),allRawMatched:true,testPassed:null};
save('Extraction.actual.json',Buffer.from(JSON.stringify(report,null,2)+'\n'));
console.log(JSON.stringify({tag:selected,files:rows.length,rawBytes:total,allRawMatched:true,testPassed:null}));
