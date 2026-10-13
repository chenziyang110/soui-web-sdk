'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const identity = require('../../tools/internal/delivery-identity.cjs');

function fixture(t, generated = false) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-source-inventory-'));
  t.after(() => {
    const parent = fs.realpathSync(os.tmpdir()), target = fs.realpathSync(root);
    assert.ok(target.startsWith(parent + path.sep) && path.basename(target).startsWith('soui-source-inventory-'));
    fs.rmSync(target, {recursive:true, force:true});
  });
  const write = (name, text = name + '\r\n') => {
    const file = path.join(root, name);
    fs.mkdirSync(path.dirname(file), {recursive:true});
    fs.writeFileSync(file, text);
  };
  for (const name of ['VERSION','CMakeLists.txt','sdk/include/api.h','tools/build.cjs',
    'templates/desktop-app/app/main.cpp','tests/acceptance-app/main.cpp']) write(name);
  if (generated) for (const name of ['project.json','sdk-lock.json','app/main.cpp','web/main.ts','contracts/actions.json','resources/index.html']) write(name);
  return {root, write};
}

test('resource source snapshot retains the exact build identity and complete declared roots', t => {
  const {root} = fixture(t);
  const original = identity.source(root), frozen = identity.sourceSnapshot(root);
  assert.equal(frozen.sha256, original.sha256);
  assert.deepEqual(frozen.files, original.files);
  assert.equal(frozen.bytes, original.bytes);
  assert.equal(frozen.inventoryMode, 'sdk-source');
  assert.deepEqual(frozen.inventoryRoots, ['sdk','tools','templates','tests','VERSION','CMakeLists.txt']);
  assert.deepEqual(frozen.files.map(file => file.path), ['CMakeLists.txt','VERSION','sdk/include/api.h',
    'templates/desktop-app/app/main.cpp','tests/acceptance-app/main.cpp','tools/build.cjs']);
});

test('React/Vue generated output and the fixed navigation log retain original exclusion semantics', t => {
  const {root, write} = fixture(t), initial = identity.source(root).sha256;
  for (const dir of ['out','dist','dist-legacy','dist-react','dist-vue','node_modules','.git','.vs'])
    write('templates/desktop-app/web/' + dir + '/generated.js');
  write('demos/workbench/web/benchmarks/electron-smoothness/source-navigation.log');
  assert.equal(identity.source(root).sha256, initial);
  assert.equal(identity.sourceSnapshot(root).sha256, initial);
  write('templates/desktop-app/web/new-source.ts');
  assert.notEqual(identity.sourceSnapshot(root).sha256, initial);
});

test('generated-project source roots preserve app/web/contracts/resources and exclude template/demo trees', t => {
  const {root} = fixture(t, true), frozen = identity.sourceSnapshot(root);
  assert.ok(['app','web','contracts','resources','project.json','sdk-lock.json'].every(name => frozen.inventoryRoots.includes(name)));
  assert.ok(!frozen.inventoryRoots.includes('templates') && !frozen.inventoryRoots.includes('tests'));
  assert.equal(frozen.sha256, identity.source(root).sha256);
  assert.ok(!frozen.files.some(file => file.path.startsWith('templates/')));
});
