import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import legacy from '@vitejs/plugin-legacy';
import path from 'node:path';
export default defineConfig({
  base: './',
  resolve:{alias:{'@soui-sdk':path.resolve(__dirname,'../sdk/web/src/index.ts')}},
  plugins: [react(), legacy({targets:['Chrome 49'],renderLegacyChunks:true})],
  build: {manifest:true,cssTarget:'chrome49',sourcemap:false},
});
