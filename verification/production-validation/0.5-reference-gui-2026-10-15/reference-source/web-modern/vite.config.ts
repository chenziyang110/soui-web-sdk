import {defineConfig} from 'vite';
import react from '@vitejs/plugin-react';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {fixtureAsset} from '../common/fixture-asset';
const root=path.dirname(fileURLToPath(import.meta.url));
export default defineConfig({
  base:'./',plugins:[react(),fixtureAsset(path.resolve(root,'../common/fixture.json'))],
  resolve:{alias:{'@soui-sdk':path.resolve(root,'../../../sdk/web/src/index.ts'),
    react:path.resolve(root,'node_modules/react'),'react-dom':path.resolve(root,'node_modules/react-dom')}},
  build:{target:'chrome120',cssTarget:'chrome120',manifest:true,sourcemap:false},
});
