import {defineConfig} from 'vite';
import react from '@vitejs/plugin-react';
import path from 'node:path';
import {createRequire} from 'node:module';
const profiles = createRequire(import.meta.url)('../../../tools/internal/web-build-profile.cjs');
const webProfile = profiles.buildProfile();
export default defineConfig({
  base: './',
  plugins: [react(), webProfile.plugin],
  resolve: {alias: {'@soui-sdk': path.resolve(import.meta.dirname, '../../../sdk/web/src/index.ts')}},
  build: {target: 'chrome120', cssTarget: 'chrome120', manifest: true, ...webProfile.build},
});
