import path from 'node:path';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import legacy from '@vitejs/plugin-legacy';

const benchmarkRoot = __dirname;

export default defineConfig({
  root: benchmarkRoot,
  base: './',
  plugins: [
    react(),
    legacy({ targets: ['Chrome >= 49'], modernPolyfills: true, renderLegacyChunks: true }),
  ],
  build: {
    outDir: path.resolve(benchmarkRoot, 'dist-legacy'),
    emptyOutDir: true,
    manifest: true,
    sourcemap: false,
    rollupOptions: {
      input: path.resolve(benchmarkRoot, 'benchmark.html'),
      output: {
        entryFileNames: 'assets/[name]-[hash].js',
        chunkFileNames: 'assets/[name]-[hash].js',
        assetFileNames: 'assets/[name]-[hash][extname]',
      },
    },
  },
});
