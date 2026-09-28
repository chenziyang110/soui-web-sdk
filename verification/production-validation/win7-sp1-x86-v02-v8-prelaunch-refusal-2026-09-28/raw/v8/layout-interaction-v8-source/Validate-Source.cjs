'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const cmake = fs.readFileSync(path.join(root, 'CMakeLists.txt'), 'utf8');
const probe = fs.readFileSync(path.join(root, 'V02InputProbe.cpp'), 'utf8');
const guest = fs.readFileSync(path.join(root, 'run-layout-interaction.ps1'), 'utf8');
const actions = [...guest.matchAll(/Run-Action\s+\$pidValue\s+\$creationMatch\.Groups\[1\]\.Value\s+'([^']+)'/g)]
  .map(match => match[1]);
assert.deepEqual([...new Set(actions)].sort(),
  ['click', 'move-drag', 'pinyin', 'resize-drag', 'wheel', 'wheel-up']);
assert.match(cmake, /MSVC_RUNTIME_LIBRARY\s+"MultiThreaded\$<\$<CONFIG:Debug>:Debug>"/);
for (const action of actions)
  assert(probe.includes(`action == L"${action}"`), `Probe does not support ${action}`);
assert.match(probe, /action == L"wheel-up" \? WHEEL_DELTA : -WHEEL_DELTA/);
assert.match(probe, /key\(VK_SPACE\);/);
assert.doesNotMatch(guest, /'key-space'/);
assert.match(guest, /C:\\soui-v02-current-interaction-v8-results/);
assert.match(guest, /'move-drag' '-80,-80' 'move'/);
assert.match(guest, /'wheel' '30,150' \('outer-down-'/);
assert.match(guest, /'wheel-up' '30,150' \('input-reveal-'/);
assert.match(guest, /'wheel' '30,150' \('task-return-'/);
assert.match(guest, /'click' '635,600' 'start-task'/);
assert.match(guest, /'click' '350,125' 'focus-input'/);
assert.match(guest, /'resize-drag' '80,60' 'resize'/);
assert(guest.indexOf("'move-drag' '-80,-80' 'move'") <
  guest.indexOf("'resize-drag' '80,60' 'resize'"));
console.log(JSON.stringify({status: 'static-source-valid', actionKinds: [...new Set(actions)].sort(),
  wheelUpSupported: true, pinyinCommitsOnce: true, guestExecuted: false,
  sourceCompiled: false, releaseReady: false}));
