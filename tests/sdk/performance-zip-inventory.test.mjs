import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp, writeFile, mkdir, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {zipInventory} from '../../tools/internal/performance-zip-inventory.mjs';
import {recordSize} from '../../tools/performance-size-gate.mjs';

function zip(name='engine.dll', crc=0x352441c2) {
  const filename=Buffer.from(name),data=Buffer.from('abc');
  const local=Buffer.alloc(30);local.writeUInt32LE(0x04034b50);local.writeUInt16LE(20,4);
  local.writeUInt16LE(0x800,6);local.writeUInt32LE(crc,14);local.writeUInt32LE(3,18);
  local.writeUInt32LE(3,22);local.writeUInt16LE(filename.length,26);
  const central=Buffer.alloc(46);central.writeUInt32LE(0x02014b50);central.writeUInt16LE(20,4);
  central.writeUInt16LE(20,6);central.writeUInt16LE(0x800,8);central.writeUInt32LE(crc,16);
  central.writeUInt32LE(3,20);central.writeUInt32LE(3,24);central.writeUInt16LE(filename.length,28);
  const end=Buffer.alloc(22);end.writeUInt32LE(0x06054b50);end.writeUInt16LE(1,8);end.writeUInt16LE(1,10);
  end.writeUInt32LE(central.length+filename.length,12);end.writeUInt32LE(local.length+filename.length+3,16);
  return Buffer.concat([local,filename,data,central,filename,end]);
}
const profile={backend:'miniblink',architecture:'x64',configuration:'Release',toolchain:'fixed',
  featureSet:'fixed',resourceSet:'fixed',packaging:'zip',requiredRuntimeDependencies:[]};
test('ZIP bytes bind to the measured runtime; an old small archive cannot substitute',async()=>{
  const root=await mkdtemp(path.join(tmpdir(),'soui-size-'));
  try {
    await mkdir(path.join(root,'runtime'));
    await writeFile(path.join(root,'runtime/engine.dll'),'abc');
    await writeFile(path.join(root,'app.zip'),zip());
    const config={schema:'soui.performance-size-config/v1',profile,runtimeRoot:'runtime',archive:'app.zip',kernelPaths:['engine.dll']};
    const valid=await recordSize(config,root);
    assert.equal(valid.totals.runtimeBytes,3);assert.match(valid.archive.inventorySha256,/^[a-f0-9]{64}$/);
    await writeFile(path.join(root,'runtime/engine.dll'),'xyz');
    await assert.rejects(recordSize(config,root),/ZIP contents/);
  } finally {await rm(root,{recursive:true,force:true});}
});
test('malformed CRC, traversal and truncated ZIP cannot produce bound evidence',async()=>{
  const root=await mkdtemp(path.join(tmpdir(),'soui-zip-'));
  try {
    const file=path.join(root,'case.zip');
    await writeFile(file,zip('engine.dll',0));await assert.rejects(zipInventory(file),/CRC/);
    await writeFile(file,zip('../engine.dll'));await assert.rejects(zipInventory(file),/unsafe/);
    await writeFile(file,zip().subarray(0,10));await assert.rejects(zipInventory(file),/truncated/);
  } finally {await rm(root,{recursive:true,force:true});}
});
