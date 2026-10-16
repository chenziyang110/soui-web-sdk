import fs from 'node:fs';
import path from 'node:path';
const root=path.resolve(process.argv[2]||'');
if(process.argv.length!==3||!['dist-react','dist-vue'].includes(path.basename(root)))throw new Error('Exact reference dist required');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'.vite/manifest.json'),'utf8'));
const entry=manifest['index-legacy.html'],polyfill=manifest['vite/legacy-polyfills-legacy'];
if(!entry||!polyfill)throw new Error('Actual Vite legacy manifest entries required');
const local=value=>{
  if(typeof value!=='string'||!/^assets\/[A-Za-z0-9_.-]+\.js$/.test(value)||!fs.statSync(path.join(root,value)).isFile())
    throw new Error('Expected actual local emitted JS asset');
  return './'+value;
};
const styles=(manifest['index.html']?.css||[]).map(value=>{
  if(typeof value!=='string'||!/^assets\/[A-Za-z0-9_.-]+\.css$/.test(value)||!fs.statSync(path.join(root,value)).isFile())throw new Error('Expected actual local CSS asset');
  return '<link rel="stylesheet" href="./'+value+'">';
}).join('\n');
// Preserve generated graph ownership. Prefer the concrete main runtime over an empty HTML facade.
const main=(entry.imports||[]).find(key=>/main-legacy/.test(key));
const runtime=main&&manifest[main]?.file?manifest[main].file:entry.file;
const prelude="(function(){try{var D=window.DOMException;new D(1,2);}catch(e){var P=D&&D.prototype;var S=function(message,name){this.message=String(message||'');this.name=String(name||'Error');};if(P)S.prototype=P;window.DOMException=S;}})();";
fs.writeFileSync(path.join(root,'index.html'),'<!doctype html><html lang="zh-CN"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SOUI Reference Compatibility</title>'+styles+
  '</head><body><div id="app"></div><script>'+prelude+'</script><script src="'+local(polyfill.file)+'"></script><script>System.import('+JSON.stringify(local(runtime))+
  ').catch(function(){document.getElementById("app").textContent="E_COMPAT_BOOTSTRAP";});</script></body></html>\n','utf8');
