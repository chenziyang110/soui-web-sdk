'use strict';
const crypto=require('node:crypto'),{isUtf8}=require('node:buffer');
const {analyzeAddressSpace}=require('./process-address-census.cjs');
const POLICY=Object.freeze({schema:'soui.software-cache-budget-policy/v1',revision:'0.5.0-committed-envelope-v1',
 maximumCommittedBytes:'1073741824',maximumCompletedAgeMs:180000,
 scope:'software-single-process-complete-committed-address-range',
 components:'MEM_PRIVATE+MEM_MAPPED+MEM_IMAGE',allSamplePhases:true,subtractNonCacheBytes:false,
 atomic:false,physicalMemory:false,allocatorHardLimit:false,perCacheOwnershipObserved:false});
const hash=raw=>crypto.createHash('sha256').update(raw).digest('hex');
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
function initial(binding){return {schema:'soui.software-cache-budget-analysis/v1',policy:POLICY,binding,
 budgetGate:'not-evaluated',bindingGate:'not-evaluated',points:[],peakCommittedBytes:null,
 allocatorHardLimit:false,perCacheOwnershipObserved:false,releaseReady:false};}
function analyzeSoftwareCacheBudget(raw,binding){
 const out=initial(binding);
 if(!Buffer.isBuffer(raw)||raw.length===0||raw.length>256*1024*1024||!isUtf8(raw)||hash(raw)!==binding?.rawLogSha256)
  return {...out,budgetGate:'failed',failure:'original-cache-budget-log-differs'};
 try{
  const text=raw.toString('utf8');if(!text.endsWith('\n'))throw Error('truncated-cache-budget-log');
  const records=text==='\n'?[]:text.slice(0,-1).split('\n').map(line=>JSON.parse(line));
  return recheckSoftwareCacheBudget(records,binding);
 }catch(error){return {...out,budgetGate:'failed',failure:error.message};}
}
// Pure aggregate replay; byte authentication is performed by the entry above
// and the public file reader. Passing this subgate never certifies an 8h run.
function recheckSoftwareCacheBudget(records,binding){
 const out=initial(binding),owners=require('./long-run-owners.cjs'),policy=require('./long-run-policy.cjs');
 if(!owners.privateBindingValid(binding)||binding?.schema!=='soui.software-cache-budget-binding/v1')
  return {...out,failure:'complete-cache-budget-binding-missing'};
 const subject=binding.subject;
 if(!['x86','x64'].includes(binding.architecture)||!equal(binding.scope,{kernel:'miniblink',renderer:'software',processModel:'single-process'})||
  subject.entireJobProcesses!==1||subject.naturalClosure!==true||subject.forcedClosure!==false||subject.nativeExitCode!==0||subject.supervisorExitCode!==0)
  return {...out,failure:'exact-software-single-process-scope-missing'};
 try{
  if(!equal(binding.expectedPolicy,POLICY)||!['mixed','session'].includes(binding.expectedRun?.mode)||
    !policy.sameConfig(binding.expectedRun,policy.fixedConfig(binding.expectedRun.mode)))throw Error('frozen-cache-budget-policy-differs');
 }catch(error){return {...out,budgetGate:'failed',failure:error.message};}
 out.bindingGate='matched';
 if(!Array.isArray(records))return {...out,budgetGate:'failed',failure:'invalid-cache-budget-records'};
 let previous,previousCaller,peak=0n,complete=0,failed=false;
 for(const [eventIndex,row]of records.entries()){
  if(row?.event!=='sample')continue;
  const native=row.nativeSdk,space=native?.addressSpace,clock=native?.captureClock;
  const observed=analyzeAddressSpace(space,{subject,nativeIdentity:native,captureClock:clock,
   pointerBytes:binding.architecture==='x86'?4:8,previous});
  const point={eventIndex,phase:row.phase??null,cycle:row.cycle??null,elapsedMs:row.elapsedMs??null,
   dataGate:observed.dataGate,budgetGate:'not-evaluated',committedBytes:null,failure:observed.failure??null};
  if(observed.dataGate==='invalid'){failed=true;point.budgetGate='failed';}
  if(observed.dataGate==='matched'){
   const total=Object.values(observed.committed).reduce((sum,value)=>sum+BigInt(value),0n);
   point.committedBytes=total.toString();if(total>peak)peak=total;
   const age=BigInt(clock.qpc)-BigInt(space.endQpc);
   if(native.producer!=='native-sdk-owner')point.failure='native-cache-budget-producer-differs';
   else if(previousCaller!==undefined&&(BigInt(clock.qpc)<previousCaller||BigInt(space.beginQpc)<previousCaller))point.failure='address-scan-predates-prior-caller';
   else if(previous&&BigInt(space.beginQpc)<=BigInt(previous.beginQpc))point.failure='completed-address-scan-replayed';
   else if(age*1000n>BigInt(POLICY.maximumCompletedAgeMs)*BigInt(space.qpcFrequency))point.failure='completed-address-scan-stale';
   else if(total>BigInt(POLICY.maximumCommittedBytes))point.failure='committed-envelope-exceeded';
   else point.failure=null;
   point.budgetGate=point.failure?'failed':'passed';if(point.failure)failed=true;else ++complete;
   previous=space;
   previousCaller=BigInt(clock.qpc);
  }
  out.points.push(point);
 }
 out.peakCommittedBytes=out.points.some(point=>point.committedBytes!==null)?peak.toString():null;
 out.budgetGate=failed?'failed':out.points.length&&complete===out.points.length?'passed':'not-evaluated';
 return out;
}
module.exports={POLICY,analyzeSoftwareCacheBudget,recheckSoftwareCacheBudget};
