<script setup lang="ts">
import { computed } from 'vue';

const props = withDefaults(defineProps<{
  name: string;
  tone?: 'glossy' | 'line';
  size?: number;
}>(), { tone: 'line', size: 30 });

const paths: Record<string, string[]> = {
  video: ['M8 13h23v22H8z', 'M31 20l9-5v18l-9-5z', 'M14 19h8v8h-8z'],
  recorder: ['M13 16h22v17H13z', 'M18 21h7v7h-7z', 'M9 37h30'],
  screen: ['M8 12h32v23H8z', 'M19 19l12 7-12 7z', 'M14 40h20'],
  player: ['M24 9a15 15 0 1 0 0 30a15 15 0 1 0 0-30', 'M19 18l11 6-11 6z', 'M14 15a12 12 0 0 1 4-3', 'M14 33a12 12 0 0 0 4 3'],
  image: ['M9 12h30v24H9z', 'M14 30l7-8 5 5 4-4 6 7', 'M16 18h.1'],
  audio: ['M16 18a11 11 0 0 0 0 12', 'M32 18a11 11 0 0 1 0 12', 'M21 14v20', 'M27 18v12'],
  extract: ['M24 35V11', 'M16 19l8-8 8 8', 'M12 34v5h24v-5'],
  gif: ['M15 12h22v25H15z', 'M11 16V9h22', 'M20 24h6', 'M32 18a6 6 0 1 0 0 12h4v-5h-5'],
  m3u8: ['M24 10a14 14 0 1 0 0 28a14 14 0 1 0 0-28', 'M16 29V19l8 7 8-7v10'],
  webp: ['M15 13h22v23H15z', 'M11 17v-5h20', 'M19 30l6-7 4 4 3-3 5 6', 'M20 18h.1'],
  screenshot: ['M14 12h22v22H14z', 'M20 19l10 4-10 7z', 'M10 16v-6h6', 'M32 38h6v-6'],
  reverse: ['M37 18a14 14 0 1 0 1 10', 'M37 11v8h-8'],
  crop: ['M16 9v23h23', 'M9 16h23v23', 'M16 16h16v16'],
  intro: ['M14 13h24v21H14z', 'M10 17v-5h24', 'M20 20h9', 'M20 26h9', 'M20 32h6'],
  shape: ['M13 13h18v18H13z', 'M34 29a7 7 0 1 0 0 14a7 7 0 1 0 0-14', 'M17 17l10 10'],
  virtual: ['M10 14h28v21H10z', 'M15 31l7-9 5 6 4-4 5 7', 'M17 19h.1'],
  text: ['M14 12h20', 'M24 12v24', 'M17 36h14'],
  split: ['M24 11v26', 'M13 17l8 7-8 7', 'M35 17l-8 7 8 7'],
  merge: ['M12 17l8 7-8 7', 'M36 17l-8 7 8 7', 'M20 24h8'],
  noise: ['M9 25c4-13 8 13 12 0s8 13 12 0 4 6 6 0', 'M12 35h24'],
  speed: ['M12 32a14 14 0 1 1 24 0', 'M24 25l6-7'],
  compress: ['M11 15h26v18H11z', 'M17 24h14', 'M20 20l-4 4 4 4', 'M28 20l4 4-4 4'],
  voice: ['M24 11a7 7 0 0 0-7 7v8a7 7 0 0 0 14 0v-8a7 7 0 0 0-7-7', 'M11 25a13 13 0 0 0 26 0', 'M24 38v-5'],
  menu: ['M10 15h28', 'M10 24h28', 'M10 33h28'],
  minimize: ['M10 31h28'],
  maximize: ['M12 12h24v24H12z'],
  close: ['M13 13l22 22', 'M35 13L13 35'],
  sparkle: ['M24 8l3.5 12.5L40 24l-12.5 3.5L24 40l-3.5-12.5L8 24l12.5-3.5z'],
  user: ['M24 25a7 7 0 1 0 0-14a7 7 0 0 0 0 14', 'M12 40c1-8 5-12 12-12s11 4 12 12'],
  headset: ['M10 27a14 14 0 0 1 28 0', 'M10 27v8h7v-9h-7', 'M38 27v8h-7v-9h7', 'M31 38c-2 3-5 4-8 4'],
};

const iconPaths = computed(() => paths[props.name] || paths.video);
</script>

<template>
  <svg class="reference-icon" :class="[`reference-icon-${tone}`, `reference-icon-name-${name}`]" :width="size" :height="size" viewBox="4 4 40 40" fill="none" aria-hidden="true" focusable="false" preserveAspectRatio="xMidYMid meet">
    <path v-for="(path, index) in iconPaths" :key="`${name}-${index}`" :d="path" />
  </svg>
</template>

<style scoped>
.reference-icon { display:block; overflow:visible; stroke-linecap:round; stroke-linejoin:round; shape-rendering:geometricPrecision; text-rendering:geometricPrecision; }
.reference-icon path { fill:none; stroke:currentColor; stroke-width:3.1; vector-effect:non-scaling-stroke; }
/* Keep the glow on the icon tile. A filter on the SVG itself softens every
   stroke in old Skia builds, which is particularly visible on small Chinese
   desktop labels. */
.reference-icon-glossy { color:#fff; stroke-width:3.1; filter:none; }
.reference-icon-glossy path { stroke-width:3.45; }
.reference-icon-line { color:currentColor; }
.reference-icon-name-player.reference-icon-glossy path:first-child { stroke-width:3.5; }
.reference-icon-name-menu.reference-icon-line path { stroke-width:3; }
</style>
