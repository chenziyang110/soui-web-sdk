'use strict';
const path=require('node:path');
const profiles=new Set(['MSVC143','LLVM22MSVC142']);
function parse(args){
  const value={scope:'full',withGuiBindings:false,withDisplayConfiguration:false,nativeToolchain:'MSVC143'},seen=new Set();
  for(let index=0;index<args.length;++index){
    const arg=args[index],key=arg==='full'||arg==='--foundation'?'scope':arg;
    if(seen.has(key))throw new Error('Duplicate collection option: '+arg);
    seen.add(key);
    if(key==='scope')value.scope=arg;
    else if(arg==='--with-gui-bindings')value.withGuiBindings=true;
    else if(arg==='--with-display-configuration')value.withDisplayConfiguration=true;
    else if(arg==='--native-toolchain'){
      const selected=args[++index];
      if(!profiles.has(selected))throw new Error('Collection toolchain must be MSVC143 or LLVM22MSVC142');
      value.nativeToolchain=selected;
    }else throw new Error('Usage: Collect-Verification.cjs [full|--foundation] [--with-gui-bindings] [--with-display-configuration] [--native-toolchain MSVC143|LLVM22MSVC142]');
  }
  return value;
}
function measurementArgs(root,command,architecture,nativeToolchain){
  if(!['protocol','native','tail-protocol','tail-native','display'].includes(command)||
    !['x64','x86'].includes(architecture)||!profiles.has(nativeToolchain)||!path.isAbsolute(root))
    throw new Error('Invalid collection measurement target or toolchain');
  const args=['tools/internal/measurement-validation.cjs',command,architecture];
  if(nativeToolchain==='LLVM22MSVC142')args.push('--toolchain-request',path.join(root,'out',
    architecture==='x64'?'benchmark-protocol':'benchmark-protocol-x86','llvm-profile-request-Release.json'));
  return args;
}
function buildRequest(root,architecture,configuration,sdkOnly,nativeToolchain){
  if(!path.isAbsolute(root)||!['x64','x86'].includes(architecture)||!['Debug','Release'].includes(configuration)||
    typeof sdkOnly!=='boolean'||!profiles.has(nativeToolchain))throw new Error('Invalid collection build target or toolchain');
  return {nativeToolchain,...(nativeToolchain==='LLVM22MSVC142'?{profileRequest:path.join(root,'out',
    (sdkOnly?'sdk-':'')+architecture,'llvm-profile-request-'+configuration+'.json')}:{})};
}
module.exports={parse,measurementArgs,buildRequest};
