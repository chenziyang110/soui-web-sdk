'use strict';
// Freeze identities before any launch. Hashes establish consistency, not trust
// in a user-writable build receipt or proof of loaded process memory.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {isDeepStrictEqual}=require('node:util');
const measurement=require('../../tools/internal/measurement-validation.cjs');
const root=path.resolve(__dirname,'../..');
function need(value,message){if(!value)throw new Error(message);}
function pin(filename){
  need(typeof filename==='string'&&path.isAbsolute(filename),'Frozen input requires an absolute file path');
  const location=fs.realpathSync(filename),stat=fs.statSync(location);
  need(stat.isFile()&&stat.size<=1024*1024*1024,'Frozen input is not a bounded file');
  const descriptor=fs.openSync(location,'r'),digest=crypto.createHash('sha256'),buffer=Buffer.allocUnsafe(65536);
  try{let size=0,count;while((count=fs.readSync(descriptor,buffer,0,buffer.length,null))){size+=count;
    need(size<=stat.size,'Frozen input changed during hashing');digest.update(buffer.subarray(0,count));}
    need(size===stat.size,'Frozen input truncated');return {path:location,bytes:size,sha256:digest.digest('hex')};
  }finally{fs.closeSync(descriptor);}
}
function verifyPins(actual,expected){need(isDeepStrictEqual(actual,expected),'Frozen measurement input changed; create a new request and preserve old evidence');}
function sourceDigest(value){return crypto.createHash('sha256').update(JSON.stringify(value)).digest('hex');}
function sources(){
  const names=fs.readdirSync(__dirname).filter(name=>/\.(cjs|cpp|h|html|js)$/.test(name)).sort()
    .map(name=>'benchmarks/protocol/'+name);
  names.push(...['main.cpp','trace_capture.cpp','trace_capture.h','harness_options.h','profile_options.h','manual_report.h','CMakeLists.txt','sdk_version.cmake']
    .map(name=>'benchmarks/input-ui-host/'+name));
  return names.map(name=>{const identity=pin(path.join(root,name));return {path:name,bytes:identity.bytes,sha256:identity.sha256};});
}
function preparedTools(request){
  const options=request.measurement_toolchain_request?{toolchainRequest:request.measurement_toolchain_request}:{};
  const prepared=measurement.preparedAt(request.measurement_owner,request.architecture,options);
  need(fs.realpathSync(request.input_executable)===fs.realpathSync(prepared.tools.BurstInput)&&
    fs.realpathSync(request.owned_runner)===fs.realpathSync(prepared.tools.OwnedProcessRunner),
    'Run tools differ from their independent preparation owner');
  for(const item of prepared.report.source.files){
    const actual=pin(path.join(root,item.path));
    need(actual.bytes===item.bytes&&actual.sha256===item.sha256,'Prepared native tool source differs from current measurement source: '+item.path);
  }
  return pin(path.join(measurement.buildDirectory(request.architecture,request.measurement_owner),'measurement-tools.json'));
}
function inputs(request){
  const entries=['host_executable','input_executable','owned_runner','miniblink_binary','sdk_library',
    'host_build_evidence','frozen_environment','frozen_configuration','calibration_template'];
  const files=Object.fromEntries(entries.map(name=>[name,pin(request[name])]));
  files.page_html=pin(path.join(request.page_root,'burst-page.html'));
  files.page_script=pin(path.join(request.page_root,'burst-page.js'));
  return {files,measurement_preparation:preparedTools(request),measurement_sources:sources()};
}
function freeze(request){
  need(request?.schema==='soui.input-ui-run-request/v1','Expected a v1 request template');
  need(!Object.hasOwn(request,'frozen_inputs'),'Template already contains frozen inputs');
  return {...request,schema:'soui.input-ui-run-request/v2',frozen_inputs:inputs(request)};
}
function verify(request){
  need(request?.schema==='soui.input-ui-run-request/v2'&&request.frozen_inputs,'Run requires an independently prepared v2 frozen request');
  verifyPins(inputs(request),request.frozen_inputs);
}
if(require.main===module){try{
  need(process.argv.length===4,'Usage: node freeze-input-ui-request.cjs TEMPLATE_JSON NEW_FROZEN_JSON');
  const template=pin(path.resolve(process.argv[2]));need(template.bytes<=65536,'Request template exceeds bound');
  const output=path.resolve(process.argv[3]);
  fs.writeFileSync(output,JSON.stringify(freeze(JSON.parse(fs.readFileSync(template.path,'utf8'))),null,2)+'\n',{flag:'wx'});
  console.log(output);
}catch(error){console.error(error.message);process.exitCode=1;}}
module.exports={freeze,verify,pin,verifyPins,sourceDigest};
