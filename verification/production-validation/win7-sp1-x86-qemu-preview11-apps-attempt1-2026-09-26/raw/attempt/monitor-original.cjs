'use strict';
const fs=require('node:fs'),path=require('node:path'),net=require('node:net');
const [attempt,command,...args]=process.argv.slice(2);
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const launch=read(path.join(attempt,'launch.json'));
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function main(){
 const socket=net.connect({host:'127.0.0.1',port:launch.monitorPort});
 socket.setTimeout(10000); socket.on('timeout',()=>socket.destroy(new Error('Monitor timeout')));
 let transcript='';socket.on('data',d=>{transcript+=d.toString('utf8')});
 await new Promise((resolve,reject)=>{socket.once('connect',resolve);socket.once('error',reject)});
 socket.on('error',e=>{console.error(e.message);process.exitCode=1});
 await pause(150);
 const send=async line=>{socket.write(line+'\n');await pause(100)};
 if(command==='screen'){
  const name=args[0];if(!/^[a-z0-9-]+$/.test(name))throw Error('Invalid screenshot name');
  const dest=path.join(attempt,name+'.ppm');if(fs.existsSync(dest))throw Error('Refuse overwrite screenshot');
  await send('screendump '+dest.replaceAll('\\','/'));await pause(500);
 }else if(command==='run'){
  const text=args[0];if(text!=='cmd.exe /c D:\\run-preview11.cmd')throw Error('Only the frozen one-shot guest entry is allowed');
  fs.writeFileSync(path.join(attempt,'command-send.json'),JSON.stringify({sentAt:new Date().toISOString(),pid:launch.pid,command:text},null,2),{flag:'wx'});
  await send('sendkey meta_l-r');await pause(800);
  for(const c of text.toLowerCase()){
   const key={':':'shift-semicolon','\\':'backslash','-':'minus','.':'dot',' ':'spc','/':'slash'}[c]||c;
   if(!/^(?:[a-z0-9]|shift-semicolon|backslash|minus|dot|spc|slash)$/.test(key))throw Error('Unsupported key');
   await send('sendkey '+key);
  }
  await send('sendkey ret');await pause(800);
 }else if(command==='hmp'){
  if(!['info block','info status','system_powerdown','quit'].includes(args[0]))throw Error('Unsupported read/lifecycle monitor command');
  await send(args[0]);await pause(300);
 }else throw Error('Unknown monitor action');
 socket.end();process.stdout.write(transcript);
}
main().catch(e=>{console.error(e);process.exitCode=1});
