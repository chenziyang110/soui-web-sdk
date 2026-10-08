import {defineConfig} from 'vite';
import react from '@vitejs/plugin-react';
import path from 'node:path';
export default defineConfig({
  base: './',
  plugins: [react()],
  resolve: {alias: {'@soui-sdk': path.resolve(import.meta.dirname, '../../../sdk/web/src/index.ts')}},
  build: {target: 'chrome120', cssTarget: 'chrome120', manifest: true, sourcemap: false},
});
