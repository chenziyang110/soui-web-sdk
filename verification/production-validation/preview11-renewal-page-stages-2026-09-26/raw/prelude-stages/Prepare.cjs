'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict'),vm=require('node:vm');
const root=__dirname,parent='C:/SOUI-SDK-production-validation/preview11-real-renewal-preparation-v2-20260926';
const expectedFreeze='80d19d4e2008a505bbb8a9293849f3756d4bf07ad7838ad38aa5bb054ac0ee3a';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const write=(p,b)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'});};
const encode=v=>JSON.stringify(v,null,2)+'\n';
function walk(p){return fs.readdirSync(p,{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name)).flatMap(e=>{assert(!e.isSymbolicLink());return e.isDirectory()?walk(path.join(p,e.name)):[path.join(p,e.name)];});}
function snapshot(p){return walk(p).map(f=>({path:path.relative(p,f).replaceAll('\\','/'),bytes:fs.statSync(f).size,sha256:hash(fs.readFileSync(f))})).sort((a,b)=>a.path.localeCompare(b.path));}
function verify(p,files){const names=snapshot(p).filter(f=>f.path!=='source-navigation.log');assert.deepEqual(names.map(f=>f.path).sort(),files.map(f=>f.path).sort());for(const f of files){const b=fs.readFileSync(path.join(p,f.path));assert.equal(b.length,f.bytes,f.path);assert.equal(hash(b),f.sha256,f.path);}}
function main(){
 const freezeBytes=fs.readFileSync(path.join(parent,'freeze.json'));assert.equal(hash(freezeBytes),expectedFreeze);const freeze=JSON.parse(freezeBytes),frozen=freeze.runtimes.x64;
 assert.equal(freeze.version,'0.3.0-preview.11');assert.equal(frozen.payload.length,70);verify(frozen.runtimePath,frozen.payload);
 const runtime=path.join(root,'input/runtime');assert(!fs.existsSync(runtime));
 for(const f of frozen.payload)write(path.join(runtime,f.path),fs.readFileSync(path.join(frozen.runtimePath,f.path)));
 const index=path.join(runtime,'resources/index.html'),before=fs.readFileSync(index,'utf8');write(path.join(root,'index-before.html'),before);
 const observer=`(function(){
  var messages=[],start=new Date().getTime();
  function safe(value){return String(value).replace(/[\\r\\n]/g," ").slice(0,220);}
  function show(){var box=document.getElementById("soui-diagnostic-stages");if(box)box.textContent=messages.join("\\n");}
  window.__souiStage=function(label){if(messages.length<40)messages.push((new Date().getTime()-start)+"ms "+safe(label));window.__souiStageText=messages.join("\\n");show();};
  window.__souiStage("head-observer");
  window.addEventListener("error",function(e){window.__souiStage("error: "+safe(e.message||"resource-error")+" file="+safe(e.filename||e.target&&e.target.src||""));},true);
  document.addEventListener("DOMContentLoaded",function(){window.__souiStage("dom-content-loaded");});
  window.addEventListener("load",function(){window.__souiStage("window-load System="+typeof window.System+" native="+typeof window.__souiNativePostMessage);});
  setTimeout(function(){window.__souiStage("timer-1000 state="+document.readyState);},1000);
})();`;
 new vm.Script(observer);
 const marker=label=>'<script>window.__souiStage('+JSON.stringify(label)+');</script>';
 const prelude='<script>(function(){try{var D=window.DOMException;new D(1,2);}catch(e){var P=D&&D.prototype;var S=function(message,name){this.message=String(message||"");this.name=String(name||"Error");};if(P)S.prototype=P;try{window.DOMException=S;}catch(ignore){}}})();</script>';
 const polyfill='<script src="./assets/polyfills-legacy-BUHvaia5.js"></script>';
 const entry='<script>System.import("./assets/index-legacy-C1PHiGhZ.js").catch(function(error){window.__souiBootstrapError=String(error&&error.message||error);var s=document.getElementById("status");if(s)s.textContent="页面启动失败："+window.__souiBootstrapError;});</script>';
 const replaceOnce=(text,from,to)=>{assert.equal(text.split(from).length,2,from);return text.replace(from,to);};
 let html=replaceOnce(before,'<head>','<head>\n<script>'+observer+'</script>');
 html=replaceOnce(html,'<body>','<body>\n<pre id="soui-diagnostic-stages" style="position:fixed;left:8px;right:8px;bottom:8px;z-index:2147483647;height:230px;min-height:0;overflow:auto;background:#fff2b3;color:#101010;border:2px solid #8a6200;padding:8px;font:13px/1.25 Consolas,monospace">DIAGNOSTIC OBSERVER HAS NOT PAINTED</pre>\n'+marker('body-marker'));
 const instrumented=prelude
  .replace('try{var D=window.DOMException;new D(1,2);}', 'try{window.__souiStage("exception-read-before");var D=window.DOMException;window.__souiStage("exception-read-after "+typeof D);window.__souiStage("exception-new-before");new D(1,2);window.__souiStage("exception-new-after");}')
  .replace('catch(e){var P=D&&D.prototype;', 'catch(e){window.__souiStage("exception-catch");var P=D&&D.prototype;window.__souiStage("exception-prototype-read");')
  .replace('if(P)S.prototype=P;try{window.DOMException=S;}catch(ignore){}', 'window.__souiStage("exception-prototype-set-before");if(P)S.prototype=P;window.__souiStage("exception-prototype-set-after");try{window.__souiStage("exception-global-set-before");window.DOMException=S;window.__souiStage("exception-global-set-after");}catch(ignore){window.__souiStage("exception-global-set-catch");}');
 html=replaceOnce(html,prelude,marker('prelude-before')+instrumented+marker('prelude-after'));
 html=replaceOnce(html,polyfill,marker('polyfill-before')+'<script src="./assets/polyfills-legacy-BUHvaia5.js" onload="window.__souiStage(\'polyfill-load\')" onerror="window.__souiStage(\'polyfill-resource-error\')"></script>\n<script>window.__souiStage("polyfill-after System="+typeof window.System);</script>');
 html=replaceOnce(html,entry,'<script>window.__souiStage("system-import-before");System.import("./assets/index-legacy-C1PHiGhZ.js").then(function(){window.__souiStage("system-import-resolved");}).catch(function(error){window.__souiStage("system-import-rejected: "+String(error&&error.message||error));window.__souiBootstrapError=String(error&&error.message||error);var s=document.getElementById("status");if(s)s.textContent="页面启动失败："+window.__souiBootstrapError;});window.__souiStage("system-import-returned");</script>');
 const scripts=[...html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)].map(m=>m[1]).filter(Boolean);scripts.forEach(script=>new vm.Script(script));
 fs.writeFileSync(index,html,'utf8');write(path.join(root,'observer.js'),observer);write(path.join(root,'index-diagnostic.html'),html);
 write(path.join(root,'Capture-OwnedProbe.ps1'),fs.readFileSync('C:/SOUI-SDK-production-validation/Capture-OwnedProbe.ps1'));
 const payload=snapshot(runtime),changes=payload.filter(f=>f.sha256!==frozen.payload.find(x=>x.path===f.path).sha256).map(f=>f.path);assert.deepEqual(changes,['resources/index.html']);
 verify(frozen.runtimePath,frozen.payload);assert.equal(hash(fs.readFileSync(path.join(parent,'freeze.json'))),expectedFreeze);
 const controls=['Prepare.cjs','Run.cjs','Capture-OwnedProbe.ps1','observer.js'].map(name=>{const b=fs.readFileSync(path.join(root,name));return{path:name,bytes:b.length,sha256:hash(b)};});
 write(path.join(root,'preparation.json'),encode({schema:'soui.renewal-page-stages/v1',preparedAt:new Date().toISOString(),parent,expectedFreeze,version:freeze.version,architecture:'x64',parentRuntime:frozen.runtimePath,parentPayload:frozen.payload,parentPayloadSha256:frozen.payloadSha256,buildId:frozen.buildId,payload,changedFiles:changes,controls,inlineScriptsParsed:scripts.length,parserScope:'Node syntax check only; not a Chrome 49 or GUI proof',guiExecuted:false,realRenewalVerified:false,releaseReady:false}));
 console.log(encode({prepared:true,files:payload.length,changes,parentUnchanged:true,guiExecuted:false,releaseReady:false}));
}
if(require.main===module)main();
module.exports={hash,write,encode,snapshot,verify};
