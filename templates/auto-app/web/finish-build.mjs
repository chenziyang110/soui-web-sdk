import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
const targets = createRequire(import.meta.url)('../../../tools/internal/web-targets.cjs');
const selected = targets.selection(path.resolve(process.cwd(), '..'), process.env.SOUI_WEB_TARGET);
if (selected.name === 'compatibility') {
  const script = fileURLToPath(new URL('../../../tools/internal/create-miniblink-entry.mjs', import.meta.url));
  const result = spawnSync(process.execPath, [script, 'dist', 'SouiAutoStarter'], {stdio: 'inherit'});
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error('Compatibility entry generation failed.');
}
