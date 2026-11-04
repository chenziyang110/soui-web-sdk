'use strict';
const fs=require('node:fs'),crypto=require('node:crypto'),path=require('node:path');
const [source,directory]=process.argv.slice(2);if(!source||!directory)throw Error('Expected host source and build directory');
const raw=fs.readFileSync(source),text=raw.toString('utf8').replace(/\r\n/g,'\n');
function extract(signature){const start=text.indexOf(signature);if(start<0||text.indexOf(signature,start+1)>=0)throw Error('Ambiguous projected function '+signature);
  const end=text.indexOf('\n}',start);if(end<0)throw Error('Function end missing');return text.slice(start,end+2);}
const chunks=['std::wstring wide(const std::string& text)','json parse_bounded(const std::string& raw)',
  'std::string serialize_output(const state::output& output)'].map(extract);
const output=chunks.join('\n\n')+'\n';fs.mkdirSync(directory,{recursive:true});
fs.writeFileSync(path.join(directory,'serialization.inc'),output);
// Preserve the complete production guard's bytes. The fixture supplies only
// state/output identities and a bounded fake note_output observation sink.
const observationNeedle=Buffer.from('struct output_observation final {','ascii');
const observationStart=raw.indexOf(observationNeedle);
const observationEnd=raw.indexOf(Buffer.from('void stop_bridge_admission(', 'ascii'),observationStart+observationNeedle.length);
if(observationStart<0||observationEnd<=observationStart||raw.indexOf(observationNeedle,observationStart+observationNeedle.length)>=0)
  throw Error('Ambiguous production output_observation projection');
const observation=raw.subarray(observationStart,observationEnd);
fs.writeFileSync(path.join(directory,'output-observation.inc'),observation);
fs.writeFileSync(path.join(directory,'projection.json'),JSON.stringify({source,sourceSha256:crypto.createHash('sha256').update(raw).digest('hex'),
  projectedSha256:crypto.createHash('sha256').update(output).digest('hex'),functions:3,
  observation:{startByte:observationStart,endByteExclusive:observationEnd,sha256:crypto.createHash('sha256').update(observation).digest('hex'),
    scope:'Exact production RAII bytes; fake state identities and note_output sink, no queue/COM execution'},
  scope:'CPU projection; no WebView2, COM, GPU or input measurement'},null,2)+'\n');
