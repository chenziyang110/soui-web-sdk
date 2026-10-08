'use strict';
const fs=require('node:fs'),path=require('node:path');
const [project,packageRoot,boundary]=process.argv.slice(2);
if(!project||!packageRoot||!boundary)throw Error('Explicit paths required');
const target=path.resolve(project,'sdk/include/soui/version.h');
const rename=fs.renameSync;
fs.renameSync=(from,to)=>{
 const value=rename(from,to);
 if(path.resolve(from).toLowerCase()===target.toLowerCase() && path.resolve(to).includes(path.sep+'old'+path.sep)){
  const pending=boundary+'.pending';
  const fd=fs.openSync(pending,'wx');
  fs.writeSync(fd,JSON.stringify({pid:process.pid,at:new Date().toISOString(),from,to,phase:'after-old-file-backed-up-before-next-file-publish'}));
  fs.fsyncSync(fd);fs.closeSync(fd);rename(pending,boundary);
  // Pause only this dedicated test child, then the parent terminates it. No
  // exception is injected into the installer and its catch block cannot run.
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0);
 }
 return value;
};
const sdk=require(path.join(packageRoot,'payload/tools/sdk/soui-sdk.cjs'));
sdk.install(packageRoot,project);
throw Error('Expected interruption boundary was not reached');
