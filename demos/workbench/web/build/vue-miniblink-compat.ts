import { createHash } from 'node:crypto';
import type { Plugin } from 'vite';

// Vue 3.5.43 retains the same CSSStyleDeclaration cache read/write pair;
// its declaration now has a PURE annotation. Keep all three sites checked.
const expectedSha256 = 'cf2baff9153828bc18222ea7a9df9fd550bffdc2c7c00bf5aa5bcdfb639b3791';
const edits = [
  ['const CSS_VAR_TEXT = /* @__PURE__ */ Symbol(!!(process.env.NODE_ENV !== "production") ? "CSS_VAR_TEXT" : "");', 'const CSS_VAR_TEXT = new WeakMap();'],
  ['style[CSS_VAR_TEXT] = cssText;', 'CSS_VAR_TEXT.set(style, cssText);'],
  ['const cssVarText = style[CSS_VAR_TEXT];', 'const cssVarText = CSS_VAR_TEXT.get(style);'],
] as const;

// The pinned x86 kernel mishandles Symbol keys on CSSStyleDeclaration. Keep
// Vue's CSS-variable cache semantics without sending such keys into the DOM.
// This does not replace Symbol globally, disable optimization, or patch the DLL.
export function vueMiniblinkCompat(): Plugin {
  return {
    name: 'soui-vue-css-cache-compat',
    apply: 'build',
    enforce: 'pre',
    transform(code, id) {
      if (!id.split('?')[0].replace(/\\/g, '/').endsWith('/@vue/runtime-dom/dist/runtime-dom.esm-bundler.js')) return null;
      if (createHash('sha256').update(code).digest('hex') !== expectedSha256) {
        this.error('Vue runtime-dom fingerprint changed: review the Miniblink CSS cache compatibility patch before building.');
      }
      for (const [before, after] of edits) {
        if (code.split(before).length !== 2) this.error('Unexpected Vue CSS cache patch site.');
        code = code.replace(before, after);
      }
      return { code, map: null };
    },
  };
}
