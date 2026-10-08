'use strict';
const fs=require('node:fs'),path=require('node:path');
const [installer,project,pkg,boundary,report]=process.argv.slice(2),rename=fs.renameSync;
fs.renameSync=function(from,to){
 const result=rename.apply(this,arguments);
 const target=boundary==='commit'?path.join(project,'sdk-lock.json'):path.join(project,'sdk/include/soui/version.h');
 if(path.resolve(to)===target){
  const fd=fs.openSync(report,'wx');try{fs.writeFileSync(fd,JSON.stringify({pid:process.pid,boundary,from,to,time:new Date().toISOString()})+'\n');fs.fsyncSync(fd);}finally{fs.closeSync(fd);}
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0);
 }
 return result;
};
require(installer).install(pkg,project);
throw Error('Expected real package boundary not reached');
