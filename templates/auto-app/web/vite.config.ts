import {defineConfig} from 'vite';
import react from '@vitejs/plugin-react';
import legacy from '@vitejs/plugin-legacy';
import path from 'node:path';
import {createRequire} from 'node:module';

const require = createRequire(import.meta.url);
const profiles = require('../../../tools/internal/web-build-profile.cjs');
const targets = require('../../../tools/internal/web-targets.cjs');
const selected = targets.selection(path.resolve(__dirname, '..'), process.env.SOUI_WEB_TARGET);
const compatibility = selected.name === 'compatibility';
const polyfills = compatibility
  ? require('../../../tools/internal/compat-polyfills.cjs').prepareCompatibilityPolyfills(__dirname) : undefined;
const profile = profiles.buildProfile();
export default defineConfig({
  base: './',
  resolve: {alias: {'@soui-sdk': path.resolve(__dirname, '../../../sdk/web/src/index.ts')}},
  plugins: [react(), ...(compatibility ? [legacy({targets: ['Chrome 49'], renderLegacyChunks: true}), polyfills!.plugin] : []), profile.plugin],
  build: {manifest: true, ...(compatibility ? {} : {target: 'es2020'}),
    cssTarget: compatibility ? 'chrome49' : 'chrome90', ...profile.build},
});
