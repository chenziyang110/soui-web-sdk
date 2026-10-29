import {createHash} from 'node:crypto';
import {readdir, lstat, readFile} from 'node:fs/promises';
import path from 'node:path';

export async function distInventory(root, relative='') {
  const files=[];
  for(const item of (await readdir(path.join(root,relative),{withFileTypes:true})).sort((a,b)=>a.name.localeCompare(b.name,'en'))) {
    const name=relative ? relative+'/'+item.name : item.name;
    const info=await lstat(path.join(root,name));
    if(info.isSymbolicLink()) throw new Error('Source vendor links are not allowed.');
    if(info.isDirectory()) files.push(...await distInventory(root,name));
    else if(info.isFile()) files.push({path:name,bytes:info.size,
      sha256:createHash('sha256').update(await readFile(path.join(root,name))).digest('hex')});
    else throw new Error('Unexpected source vendor entry.');
  }
  return files;
}
