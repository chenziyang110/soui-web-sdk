import {readFile} from 'node:fs/promises';
import {inflateRawSync} from 'node:zlib';
import {createHash} from 'node:crypto';

// Deliberately narrow: this gate accepts ordinary single-volume ZIPs produced
// by the frozen packager. Unknown/ZIP64/encrypted archives need a reviewed
// reader, never a guessed size pass. No extraction or external executable.
export async function zipInventory(file, rootPrefix='', expectedFiles=null) {
  const bytes=await readFile(file);
  const bad=message=>{throw new Error('Unbound ZIP: '+message);};
  if(bytes.length<22) bad('truncated archive');
  const view=(at,count)=>{if(!Number.isSafeInteger(at)||at<0||at+count>bytes.length) bad('record bounds');};
  const u16=at=>{view(at,2);return bytes.readUInt16LE(at);};
  const u32=at=>{view(at,4);return bytes.readUInt32LE(at);};
  let end=-1;
  for(let at=bytes.length-22;at>=Math.max(0,bytes.length-65557);--at)
    if(u32(at)===0x06054b50 && at+22+u16(at+20)===bytes.length){end=at;break;}
  if(end<0 || u16(end+4)!==0 || u16(end+6)!==0 || u16(end+8)!==u16(end+10)) bad('not a single-volume ZIP');
  const count=u16(end+10),size=u32(end+12),start=u32(end+16);
  if(count===0xffff || size===0xffffffff || start===0xffffffff || start+size!==end) bad('ZIP64 or invalid directory');
  const decode=(raw,flags)=> {
    if(!(flags&0x800) && raw.some(byte=>byte>127)) bad('non-UTF8 package name');
    return new TextDecoder('utf-8',{fatal:true}).decode(raw);
  };
  const normalize=value=> {
    if(!value || value.startsWith('/') || value.includes('\\') || value.includes(':') || /[\x00-\x1f]/.test(value) ||
        value.split('/').some(part=>!part || part==='.' || part==='..')) bad('unsafe entry path');
    return value;
  };
  const prefix=rootPrefix ? normalize(rootPrefix)+'/' : '';
  const expected=expectedFiles && new Map(expectedFiles.map(item=>[item.path,item.bytes]));
  const crcTable=Uint32Array.from({length:256},(_,index)=> {
    let value=index;for(let bit=0;bit<8;++bit)value=value&1 ? 0xedb88320^(value>>>1) : value>>>1;
    return value>>>0;
  });
  const crc32=plain=> {let value=0xffffffff;for(const byte of plain)value=crcTable[(value^byte)&255]^(value>>>8);return (value^0xffffffff)>>>0;};
  const files=[],seen=new Set(),ranges=[];
  let at=start;
  for(let n=0;n<count;++n) {
    if(u32(at)!==0x02014b50) bad('central directory signature');
    view(at,46);
    const flags=u16(at+8),method=u16(at+10),crc=u32(at+16),packed=u32(at+20),unpacked=u32(at+24);
    const names=u16(at+28),extra=u16(at+30),comment=u16(at+32),offset=u32(at+42);
    if(flags&~0x80e || u16(at+6)>20 || packed===0xffffffff || unpacked===0xffffffff || offset===0xffffffff || u16(at+34)!==0)
      bad('encrypted, split or ZIP64 entry');
    view(at+46,names+extra+comment);
    const raw=bytes.subarray(at+46,at+46+names),name=decode(raw,flags);
    const directory=name.endsWith('/');
    const safe=normalize(directory ? name.slice(0,-1) : name);
    const mode=u32(at+38)>>>16;
    if((mode&0xf000)!==0 && (mode&0xf000)!==(directory ? 0x4000 : 0x8000)) bad('non-regular Unix entry');
    if(!name.startsWith(prefix) || (prefix && safe===prefix.slice(0,-1) && !directory)) bad('entry outside declared package root');
    if(u32(offset)!==0x04034b50 || u16(offset+4)>20 || u16(offset+6)!==flags || u16(offset+8)!==method) bad('local header mismatch');
    const localNames=u16(offset+26),localExtra=u16(offset+28),data=offset+30+localNames+localExtra;
    view(offset+30,localNames+localExtra);view(data,packed);
    if(!bytes.subarray(offset+30,offset+30+localNames).equals(raw) || data+packed>start) bad('local entry bounds/name');
    let finish=data+packed;
    if(flags&8) {
      const descriptor=finish+(u32(finish)===0x08074b50 ? 4 : 0);
      if(u32(descriptor)!==crc || u32(descriptor+4)!==packed || u32(descriptor+8)!==unpacked)
        bad('data descriptor mismatch');
      finish=descriptor+12;
    } else if(u32(offset+14)!==crc || u32(offset+18)!==packed || u32(offset+22)!==unpacked)
      bad('local CRC/size mismatch');
    if(finish>start) bad('local entry overlaps directory');
    ranges.push([offset,finish]);
    if(!directory) {
      const relative=normalize(name.slice(prefix.length));
      const folded=relative.toLowerCase();
      if(seen.has(folded)) bad('duplicate package entry');
      seen.add(folded);
      if(expected && expected.get(relative)!==unpacked) bad('ZIP entry not in frozen runtime inventory');
      // Bound uncompressed allocation to the declared package file size. A
      // malformed compressed stream cannot inflate beyond that declaration.
      if(unpacked>1024*1024*1024) bad('individual file exceeds gate budget');
      const compressed=bytes.subarray(data,data+packed);
      const plain=method===0 ? compressed : method===8 ? inflateRawSync(compressed,{maxOutputLength:Math.max(1,unpacked)}) : bad('unsupported compression method');
      if(plain.length!==unpacked) bad('uncompressed size mismatch');
      if(crc32(plain)!==crc) bad('CRC mismatch');
      files.push({path:relative,bytes:plain.length,sha256:createHash('sha256').update(plain).digest('hex')});
    } else {
      if(unpacked!==0 || crc!==0) bad('directory contains payload');
      const compressed=bytes.subarray(data,data+packed);
      if(method===0 ? packed!==0 : method===8 ? inflateRawSync(compressed,{maxOutputLength:1}).length!==0 : true)
        bad('invalid directory compression');
    }
    at+=46+names+extra+comment;
  }
  if(at!==end) bad('directory count mismatch');
  ranges.sort((a,b)=>a[0]-b[0]);
  for(let n=1;n<ranges.length;++n) if(ranges[n][0]<ranges[n-1][1]) bad('overlapping local entries');
  return files.sort((a,b)=>a.path.localeCompare(b.path,'en'));
}
