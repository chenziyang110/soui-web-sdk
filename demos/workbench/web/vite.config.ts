import { defineConfig } from 'vite';
import path from 'node:path';
import vue from '@vitejs/plugin-vue';
import legacy from '@vitejs/plugin-legacy';
import react from '@vitejs/plugin-react';
import { vueMiniblinkCompat } from './build/vue-miniblink-compat';
import {createRequire} from 'node:module';
const require = createRequire(import.meta.url);
const profiles = require('../../../tools/internal/web-build-profile.cjs');
const compatibility = require('../../../tools/internal/compat-polyfills.cjs').prepareCompatibilityPolyfills(__dirname);
const webProfile = profiles.buildProfile(undefined, {appSourceDirectories: [
  'apps/demo/src', 'apps/react', 'apps/fileinsight/src', 'packages/sdk',
  'benchmarks/electron-smoothness',
]});

export default defineConfig(({ mode }) => ({
  base: './',
  // Native FileInsight samples in public/fixtures are not HTTP assets.
  publicDir: 'http-public',
  plugins: [
    vueMiniblinkCompat(),
    vue(),
    react(),
    legacy({
      // The isolated Miniblink source identifies itself as the Chrome 49 era
      // (Blink build 98096 / V8 5.7). Keep the emitted classic bundle inside
      // that parser/runtime envelope; developers can still write modern Vue
      // and TypeScript because this target is applied after bundling.
      targets: ['Chrome 49'],
      modernPolyfills: true,
      renderLegacyChunks: true,
    }),
    compatibility.plugin,
    webProfile.plugin,
  ],
  build: {
    outDir: mode === 'legacy' ? 'dist-legacy' : 'dist',
    emptyOutDir: true,
    manifest: true,
    cssCodeSplit: true,
    // plugin-legacy owns the legacy target; setting build.target here causes
    // Vite to emit a warning that the unified validation entry treats as a failure.
    ...webProfile.build,
    rollupOptions: {
      input: {
        index: path.resolve(__dirname, 'index.html'),
        react: path.resolve(__dirname, 'react.html'),
      },
      output: {
        entryFileNames: 'assets/[name]-[hash].js',
        chunkFileNames: 'assets/[name]-[hash].js',
        assetFileNames: 'assets/[name]-[hash][extname]',
      },
    },
  },
  css: {
    preprocessorOptions: { scss: { api: 'modern-compiler' } },
  },
}));
