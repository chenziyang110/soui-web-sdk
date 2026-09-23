import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {build} from 'vite';
import legacy from '@vitejs/plugin-legacy';
import identity from '../../tools/internal/delivery-identity.cjs';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const suite=identity.safePath(process.argv[2]);
if(path.dirname(suite)!==path.join(root,'out','acceptance'))throw Error('Invalid acceptance suite');
const pending=identity.readJson(path.join(suite,'pending.json'));
const source=path.join(suite,'consumer-source'),sdk=path.join(suite,'web-sdk'),output=path.join(suite,'web-output');
if(identity.snapshot(source,identity.walk(source)).sha256!==pending.consumerSource.sha256 ||
   identity.snapshot(sdk,identity.walk(sdk)).sha256!==pending.webSdk.sha256)throw Error('Acceptance Web inputs changed');
if(fs.existsSync(output))throw Error('Web output already exists; keep failed evidence and prepare a new suite');
// Vite's virtual legacy entry is resolved relative to the process directory.
// Match the standalone application's root so its manifest is relocatable.
process.chdir(path.join(source,'web'));
await build({configFile:false,root:path.join(source,'web'),base:'./',
  resolve:{alias:{'@soui-sdk':path.join(sdk,'dist','index.js')}},
  plugins:[legacy({targets:['Chrome 49'],renderLegacyChunks:true})],
  build:{outDir:output,emptyOutDir:false,manifest:true,cssTarget:'chrome49',sourcemap:false}});
const result=spawnSync(process.execPath,[path.join(root,'tools/internal/create-miniblink-entry.mjs'),output,'SDK 公共接口验证'],{cwd:root,stdio:'inherit'});
if(result.status!==0)throw Error('Classic acceptance entry generation failed');
if(identity.snapshot(source,identity.walk(source)).sha256!==pending.consumerSource.sha256 ||
   identity.snapshot(sdk,identity.walk(sdk)).sha256!==pending.webSdk.sha256)throw Error('Acceptance Web inputs changed during build');
fs.writeFileSync(path.join(suite,'web-build.json'),identity.encoded({schema:'soui.acceptance-web/v1',
  consumerSourceSha256:pending.consumerSource.sha256,webSdkSha256:pending.webSdk.sha256,
  output:identity.snapshot(output,identity.walk(output)),node:process.version,completedAt:new Date().toISOString()}),{flag:'wx'});
