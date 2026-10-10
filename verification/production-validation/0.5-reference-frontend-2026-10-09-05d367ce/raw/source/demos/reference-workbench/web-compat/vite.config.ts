import {defineConfig} from 'vite';
import react from '@vitejs/plugin-react';
import vue from '@vitejs/plugin-vue';
import legacy from '@vitejs/plugin-legacy';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {vueMiniblinkCompat} from '../tools/vue-miniblink-compat';
import {fixtureAsset} from '../common/fixture-asset';
const root=path.dirname(fileURLToPath(import.meta.url));
export default defineConfig(({mode})=>{
  if(!['react','vue'].includes(mode))throw new Error('Use --mode react or --mode vue explicitly.');
  return {base:'./',plugins:[vueMiniblinkCompat(),vue(),react(),fixtureAsset(path.resolve(root,'../common/fixture.json')),
    legacy({targets:['Chrome 49'],modernPolyfills:true,renderLegacyChunks:true})],
    resolve:{alias:{'@soui-sdk':path.resolve(root,'../../../sdk/web/src/index.ts'),
      'reference-view':path.resolve(root,'src',mode==='react'?'react.tsx':'vue.ts'),
      react:path.resolve(root,'node_modules/react'),'react-dom':path.resolve(root,'node_modules/react-dom'),
      vue:path.resolve(root,'node_modules/vue'),primereact:path.resolve(root,'node_modules/primereact'),
      'element-plus':path.resolve(root,'node_modules/element-plus')}},
    build:{outDir:'dist-'+mode,emptyOutDir:true,manifest:true,sourcemap:false,cssCodeSplit:true}};
});
