import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import legacy from '@vitejs/plugin-legacy';
import path from 'node:path';
import {createRequire} from 'node:module';
const profiles = createRequire(import.meta.url)('../../../tools/internal/web-build-profile.cjs');
const webProfile = profiles.buildProfile();
export default defineConfig({
  base: './',
  resolve:{alias:{'@soui-sdk':path.resolve(__dirname,'../../../sdk/web/src/index.ts')}},
  plugins: [react(), legacy({targets:['Chrome 49'],renderLegacyChunks:true}), webProfile.plugin],
  build: {manifest:true,cssTarget:'chrome49',...webProfile.build},
});
