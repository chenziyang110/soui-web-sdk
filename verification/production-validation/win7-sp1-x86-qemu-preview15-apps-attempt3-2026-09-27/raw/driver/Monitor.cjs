'use strict';
const fs=require('node:fs'), path=require('node:path'), net=require('node:net');
const [arg, action, value]=process.argv.slice(2), attempt=path.resolve(arg||'.');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const launch=read(path.join(attempt,'launch.json'));
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(){
 const socket=net.connect({host:'127.0.0.1',port:launch.monitorPort});
 let transcript='', failure=null;
 socket.on('data',d=>{transcript+=d.toString('utf8')});
 socket.on('error',e=>{failure=e});
 const until=async predicate=>{const end=Date.now()+10000;while(!predicate()){if(failure)throw failure;if(Date.now()>end)throw Error('HMP response timeout');await pause(25)}};
 const send=async command=>{const before=transcript.length;socket.write(command+'\n');await until(()=>transcript.slice(before).endsWith('(qemu) '));};
 const log=path.join(attempt,`monitor-${action}-${Date.now()}.log`);
 try {
  await until(()=>transcript.endsWith('(qemu) '));
  if(action==='screen'){
   if(!/^[a-z0-9-]+$/.test(value||''))throw Error('Invalid screenshot name');
   const dest=path.join(attempt,value+'.ppm');if(fs.existsSync(dest))throw Error('Refuse overwrite screenshot');
   await send('screendump '+dest.replaceAll('\\','/'));
   if(!fs.existsSync(dest))throw Error('QEMU did not create screenshot');
  }else if(action==='explorer'){
   await send('sendkey meta_l-e 50');
  }else if(action==='run'){
   if(value!=='cmd.exe /c D:\\soui-input\\run-preview15.cmd')throw Error('Only frozen guest entry permitted');
   fs.writeFileSync(path.join(attempt,'command-send.json'),JSON.stringify({sentAt:new Date().toISOString(),pid:launch.pid,command:value},null,2)+'\n',{flag:'wx'});
   await send('sendkey meta_l-r 50');await pause(800);
   for(const c of value.toLowerCase()){
    const key={':':'shift-semicolon','\\':'backslash','-':'minus','.':'dot',' ':'spc','/':'slash'}[c]||c;
    if(!/^(?:[a-z0-9]|shift-semicolon|backslash|minus|dot|spc|slash)$/.test(key))throw Error('Unsupported key');
    await send('sendkey '+key+' 50');await pause(100);
   }
   await send('sendkey ret 50');
  }else if(action==='hmp'){
   if(!['info block','info status','system_powerdown','quit'].includes(value))throw Error('Unsupported command');
   if(value==='quit'){socket.write('quit\n');await pause(300)}else await send(value);
  }else throw Error('Unknown action');
  if(/Error:/.test(transcript))throw Error('QEMU reported an error');
  console.log(JSON.stringify({action,status:'sent',log,pid:launch.pid}));
 }finally {socket.destroy();fs.writeFileSync(log,transcript,{flag:'wx'});}
}
main().catch(e=>{console.error(e.stack||e);process.exitCode=1});
