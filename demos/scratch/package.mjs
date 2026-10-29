import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';

const demo = path.dirname(fileURLToPath(import.meta.url));
const sdk = path.resolve(demo, '../..');
const arch = process.argv[2];
if (!['x64','x86'].includes(arch)) throw new Error('Architecture required: x64 or x86');
const version = (await fs.readFile(path.join(sdk,'VERSION'),'utf8')).trim();
if (!/^0\.6\.0(?:-[A-Za-z0-9.-]+)?$/.test(version)) throw new Error('Expected 0.6.0 SDK version');
const built = path.join(sdk, `out/scratch-${arch}-package/bin/Release/SouiScratch`);
const deliveries = path.resolve(sdk, '../deliverables');
await fs.mkdir(deliveries,{recursive:true});
let folder;
for(let number=1;number<=100;number++) {
  const candidate=path.join(deliveries,`SouiScratch-${version}-windows-${arch}-r${number}`);
  try {await fs.mkdir(candidate);folder=candidate;break;} catch(error){if(error.code!=='EEXIST')throw error;}
}
if(!folder) throw new Error('No unused package revision available');
const name=path.basename(folder);
const hash=data=>createHash('sha256').update(data).digest('hex');
async function copyTree(source,destination,{sourceOnly=false}={}) {
  const info=await fs.lstat(source);
  if(info.isSymbolicLink())throw new Error('Link refused: '+source);
  if(info.isDirectory()) {
    await fs.mkdir(destination,{recursive:true});
    for(const entry of await fs.readdir(source)) {
      if(sourceOnly && ['.scratch-cache','dist','node_modules','out','.git'].includes(entry))continue;
      await copyTree(path.join(source,entry),path.join(destination,entry),{sourceOnly});
    }
  } else if(info.isFile()) {
    const before=await fs.readFile(source);
    await fs.mkdir(path.dirname(destination),{recursive:true});
    await fs.writeFile(destination,before,{flag:'wx'});
    if(hash(before)!==hash(await fs.readFile(destination)) || hash(before)!==hash(await fs.readFile(source)))throw new Error('Source changed while packaging: '+source);
  } else throw new Error('Unsupported file type: '+source);
}
await copyTree(built,folder);
const resources=JSON.parse(await fs.readFile(path.join(folder,'resources/.scratch-resources.json'),'utf8'));
for(const entry of resources.files) {
  const file=path.resolve(folder,'resources',entry.path);
  if(!file.startsWith(path.join(folder,'resources')+path.sep))throw new Error('Invalid resource path');
  const data=await fs.readFile(file);
  if(data.length!==entry.bytes || hash(data)!==entry.sha256)throw new Error('Resource identity mismatch: '+entry.path);
}
const webview2=path.join(sdk,'out/webview2-deps/1.0.4258.31/package');
for(const file of ['LICENSE.txt','NOTICE.txt'])await copyTree(path.join(webview2,file),path.join(folder,'resources/scratch-legal',`WebView2-${file}`));
const materials=path.join(folder,'source-materials');
await copyTree(path.join(demo,'.scratch-cache/scratch-scratch-gui-standalone-15.3.0.tgz'),path.join(materials,'scratch-gui-standalone-15.3.0.tgz'));
await copyTree(demo,path.join(materials,'soui-sdk/demos/scratch'),{sourceOnly:true});
for(const directory of ['sdk','cmake','tools','third_party/include/json','third_party/include/miniblink','third_party/sources/zlib','third_party/manifests','third_party/licenses',`third_party/prebuilt/${arch}/runtime`])
  await copyTree(path.join(sdk,directory),path.join(materials,'soui-sdk',directory),{sourceOnly:true});
for(const file of ['VERSION','LICENSE.md','THIRD_PARTY_NOTICES.md','README.md'])
  await copyTree(path.join(sdk,file),path.join(materials,'soui-sdk',file));
await fs.writeFile(path.join(folder,'打开演示.cmd'),'@echo off\r\nstart "" /D "%~dp0" "%~dp0SouiScratch.exe"\r\n');
await fs.writeFile(path.join(folder,'使用说明.txt'),
  `积木创作室 / SOUI SDK ${version} / Scratch 15.3.0\r\n\r\n`+
  '双击“打开演示.cmd”或 SouiScratch.exe。请完整解压后运行。\r\n'+
  '需要 Windows 10/11 和已安装的 Microsoft WebView2 Runtime；运行无需 Node.js、CMake 或 Visual Studio。\r\n'+
  '使用“小猫画圆”“按键漫游”开始体验，拖动积木后点击绿色旗帜运行。\r\n'+
  '打开/保存支持不超过 16 MiB 的 .sb3。联网教程、社区与硬件扩展不在范围内。\r\n\r\n'+
  '性能测试：右上角“性能实验”→选择场景和轻/中/重档→载入并运行→数量稳定后记录30秒→导出JSON报告。\r\n'+
  '场景包含40/120/240角色、60/150/280克隆粒子、20/60/120并发计算角色。保持窗口可见。\r\n'+
  'rAF统计反映页面调度，不等同于屏幕呈现FPS或物理输入延迟。\r\n\r\n'+
  `本包是未签名的本地 ${arch} 开发候选，不是公开正式发布。\r\n`+
  '声明材料在 resources/legal 和 resources/scratch-legal；原始 Scratch 包及本地接入源码在 source-materials。\r\n'+
  'Scratch 及角色商标属于 Scratch Foundation；本演示不表示官方背书。\r\n');
await fs.writeFile(path.join(folder,'resources/scratch-legal/README.txt'),
  'Scratch GUI 15.3.0: AGPL-3.0-only. No Scratch Foundation endorsement.\n'+
  'Original npm package with GUI sources and bundled source maps: ../../source-materials/scratch-gui-standalone-15.3.0.tgz\n'+
  'Local application and current SDK sources: ../../source-materials/soui-sdk/\n'+
  'The npm distribution includes GUI sources and bundled source maps; rebuilding upstream Scratch itself also needs its pinned repository build configuration.\n'+
  'Pinned upstream source: https://github.com/scratchfoundation/scratch-editor/tree/b4b934aaf6641a539325a971249234f4a50a2f03\n'+
  'Rebuilding local SDK/app inputs requires the documented compiler and first-run dependency downloads.\n'+
  'This local demonstration is not a licensing certification for public redistribution.\n');
// Package-only legal additions and their source-location note are part of the
// payload identity, so refresh the resource inventory before freezing the ZIP.
const resourceFiles=[];
async function collectResources(directory) {
  for(const entry of await fs.readdir(directory,{withFileTypes:true})) {
    const file=path.join(directory,entry.name);
    if(entry.isDirectory())await collectResources(file);
    else if(file!==path.join(folder,'resources/.scratch-resources.json')) {
      const data=await fs.readFile(file);
      resourceFiles.push({path:path.relative(path.join(folder,'resources'),file).replaceAll('\\','/'),bytes:data.length,sha256:hash(data)});
    }
  }
}
await collectResources(path.join(folder,'resources'));
resources.files=resourceFiles;
resources.totalBytes=resourceFiles.reduce((total,file)=>total+file.bytes,0);
if(resourceFiles.length>resources.limits.files || resources.totalBytes>resources.limits.totalBytes || resourceFiles.some(file=>file.bytes>resources.limits.fileBytes))
  throw new Error('Packaged resources exceed the declared demo budget');
await fs.writeFile(path.join(folder,'resources/.scratch-resources.json'),JSON.stringify(resources,null,2));
const inventory=[];
async function collect(directory) {
  for(const entry of await fs.readdir(directory,{withFileTypes:true})) {
    const file=path.join(directory,entry.name);
    if(entry.isDirectory())await collect(file);
    else {const data=await fs.readFile(file);inventory.push({path:path.relative(folder,file).replaceAll('\\','/'),bytes:data.length,sha256:hash(data)});}
  }
}
await collect(folder);
await fs.writeFile(path.join(folder,'package-manifest.json'),JSON.stringify({schema:'soui.scratch-local-package/v1',sdkVersion:version,scratchVersion:'15.3.0',architecture:arch,configuration:'Release',productionReady:false,files:inventory},null,2));
const zip=path.join(deliveries,name+'.zip');
const compressed=spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',path.join(demo,'Compress-Package.ps1'),'-Source',folder,'-Output',zip],{cwd:deliveries,stdio:'inherit'});
if(compressed.status!==0)throw new Error('ZIP creation failed');
const zipBytes=await fs.readFile(zip);
await fs.writeFile(zip+'.sha256',hash(zipBytes)+'  '+path.basename(zip)+'\n');
console.log(JSON.stringify({folder,zip,zipBytes:zipBytes.length,sha256:hash(zipBytes),files:inventory.length}));
