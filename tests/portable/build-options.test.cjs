'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '../..');
const cmake = process.env.CMAKE_COMMAND || 'cmake';

// Real configure calls, not text matching or synthetic CMake projects. Keep failed
// configure output for diagnosis; no SDK/application build or execution is requested.
for (const { entry, option } of [
  { entry: '', option: 'AUTO_HOST' },
  { entry: 'sdk', option: 'AUTO_HOST' },
  { entry: '', option: 'INPUT_UI_BENCHMARKS' },
]) {
  for (const mode of ['PORTABLE', 'DESKTOP']) {
    test(`${entry || 'root'} rejects ${option} with ${mode}`, t => {
      const build = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-build-options-'));
      t.diagnostic(`configure evidence: ${build}`);
      const args = ['-S', path.join(root, entry), '-B', build,
        `-DSOUI_BUILD_PORTABLE=${mode === 'PORTABLE' ? 'ON' : 'OFF'}`,
        `-DSOUI_BUILD_DESKTOP=${mode === 'DESKTOP' ? 'ON' : 'OFF'}`,
        `-DSOUI_BUILD_AUTO_HOST=${option === 'AUTO_HOST' ? 'ON' : 'OFF'}`,
        `-DSOUI_BUILD_INPUT_UI_BENCHMARKS=${option === 'INPUT_UI_BENCHMARKS' ? 'ON' : 'OFF'}`,
        '-DSOUI_BUILD_WEBVIEW2=OFF',
        '-DSOUI_BUILD_UIA=OFF', '-DSOUI_BUILD_TEMPLATE=OFF',
        '-DSOUI_BUILD_DEMOS=OFF', '-DSOUI_BUILD_VENDOR_TESTS=OFF',
        '-DSOUI_BUILD_TESTS=OFF'];
      const result = spawnSync(cmake, args, { encoding: 'utf8', timeout: 120000 });
      const output = `${result.stdout || ''}\n${result.stderr || ''}`;
      fs.writeFileSync(path.join(build, 'configure-result.json'), JSON.stringify({
        command: cmake, args, status: result.status, signal: result.signal,
        error: result.error?.message, output,
      }, null, 2));
      assert.ifError(result.error);
      assert.equal(result.signal, null, output);
      assert.equal(result.status, 1, output);
      assert.match(output.replace(/\s+/g, ' '),
        new RegExp(`SOUI_BUILD_${option} cannot be combined with SOUI_BUILD_PORTABLE or SOUI_BUILD_DESKTOP\\.`));
    });
  }
}
