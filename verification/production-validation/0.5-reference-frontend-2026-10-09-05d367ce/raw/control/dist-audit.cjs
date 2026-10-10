'use strict';
// Root executes this against genuine Vite outputs; it never creates substitute assets.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const [mode,rootArg,fixtureArg]=process.argv.slice(2);
if(process.argv.length!==5||!['modern','react','vue'].includes(mode)||!path.isAbsolute(rootArg)||!path.isAbsolute(fixtureArg))throw Error('Exact audit arguments required');
const root=path.resolve(rootArg),fixture=fs.readFileSync(fixtureArg);
function list(directory){return fs.readdirSync(directory,{withFileTypes:true}).flatMap(e=>{const file=path.join(directory,e.name);if(e.isSymbolicLink())throw Error('Output symlink refused');if(e.isDirectory())return list(file);if(!e.isFile())throw Error('Nonregular output refused');return[file];}).sort();}
function local(value){
 if(typeof value!=='string'||!value||value.includes('\\')||value.includes('\0')||value.includes('%')||value.startsWith('/')||/^[a-z][a-z0-9+.-]*:/i.test(value))throw Error('Local asset reference required');
 const relative=value.replace(/^\.\//,'').split(/[?#]/,1)[0];
 if(relative.split('/').some(p=>!p||p==='.'||p==='..')||!path.resolve(root,relative).startsWith(root+path.sep))throw Error('Asset path escapes output');
 const file=path.join(root,relative);if(!fs.lstatSync(file).isFile()||!fs.statSync(file).size)throw Error('Actual referenced asset missing');return relative;
}
const files=list(root),manifestFile=path.join(root,'.vite/manifest.json'),manifest=JSON.parse(fs.readFileSync(manifestFile,'utf8'));
if(!manifest||typeof manifest!=='object'||Array.isArray(manifest)||!manifest['index.html']?.isEntry)throw Error('Actual Vite index manifest required');
for(const [key,entry]of Object.entries(manifest)){
 if(!entry||typeof entry!=='object'||Array.isArray(entry)||typeof entry.file!=='string')throw Error('Invalid manifest entry');local(entry.file);
 for(const field of ['imports','dynamicImports'])if(entry[field]!==undefined){if(!Array.isArray(entry[field])||entry[field].some(p=>typeof p!=='string'||!Object.hasOwn(manifest,p)))throw Error('Manifest graph reference missing');}
 for(const field of ['css','assets'])if(entry[field]!==undefined){if(!Array.isArray(entry[field]))throw Error('Invalid manifest assets');entry[field].forEach(local);}
}
const html=fs.readFileSync(path.join(root,'index.html'),'utf8'),references=[...html.matchAll(/(?:src|href)=["']([^"']+)["']/g)].map(p=>local(p[1]));
if(!html.includes('<div id="app"></div>')||!references.length||files.some(file=>/\.map$/i.test(file)))throw Error('Actual entry/output contract changed');
let classic=null;
if(mode==='modern'){
 if(!/<script\b[^>]*type=["']module["'][^>]*src=/.test(html)||!references.includes(local(manifest['index.html'].file)))throw Error('Actual Modern module entry missing');
}else{
 const entry=manifest['index-legacy.html'],polyfill=manifest['vite/legacy-polyfills-legacy'];if(!entry||!polyfill)throw Error('Actual legacy manifest required');
 const main=(entry.imports||[]).find(key=>/main-legacy/.test(key)),runtime=main&&manifest[main]?.file?manifest[main].file:entry.file;
 const polyfillPath=local(polyfill.file),runtimePath=local(runtime);
 if(/type=["']module["']/.test(html)||!references.includes(polyfillPath)||!html.includes('System.import('+JSON.stringify('./'+runtimePath)+')')||!fs.readFileSync(path.join(root,runtimePath),'utf8').includes('System.register('))throw Error('Actual classic graph/entry mismatch');
 classic={polyfill:polyfillPath,runtime:runtimePath};
}
for(const file of files.filter(p=>/\.css$/i.test(p)))for(const match of fs.readFileSync(file,'utf8').matchAll(/url\(\s*(["']?)(.*?)\1\s*\)/g)){
 const value=match[2].trim();if(value.startsWith('data:')||value.startsWith('#'))continue;
 // CSS references are relative to the emitting stylesheet, not to the HTML entry.
 const relative=path.relative(root,path.resolve(path.dirname(file),value.split(/[?#]/,1)[0])).replaceAll('\\','/');
 if(/^[a-z][a-z0-9+.-]*:/i.test(value)||value.startsWith('/')||value.includes('\\'))throw Error('External CSS asset refused');local(relative);
}
const emittedFixture=fs.readFileSync(path.join(root,'reference-fixture.json'));if(!fixture.equals(emittedFixture))throw Error('Emitted deterministic fixture changed');
const pins=files.map(file=>{const bytes=fs.readFileSync(file);return{relative:path.relative(root,file).replaceAll('\\','/'),bytes:bytes.length,sha256:sha(bytes)};});
console.log(JSON.stringify({schema:'soui.reference-dist-audit/1',mode,root,manifestSha256:sha(fs.readFileSync(manifestFile)),fixtureSha256:sha(fixture),classic,pins,sourceGraphChecked:true,rendererExercised:false,qualificationPassed:false,releaseReady:false}));
