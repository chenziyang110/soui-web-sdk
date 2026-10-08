import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { createHash } from 'node:crypto';

const repository = fileURLToPath(new URL('../../', import.meta.url));
const tool = path.join(repository, 'tools/sdk/actiongen.cjs');
const object = properties => ({ type: 'object', properties, additionalProperties: false });
const source = () => ({ schema: 'soui.action-source/1', cppNamespace: 'consumer_actions', actions: [{
  method: 'app.greet', capability: 'app.greet', cppName: 'Greet', action: {
    protocol: 'soui.action/1', title: '问候', description: '返回问候。',
    inputSchema: { ...object({ name: { type: 'string', minLength: 1, maxLength: 32 } }), required: ['name'] },
    outputSchema: { ...object({ message: { type: 'string', maxLength: 200 } }), required: ['message'] }
  }
}] });
const outputs = ['app/generated/actions.hpp', 'web/src/generated/actions.ts',
  'contracts/generated/tools.json', 'docs/generated/actions.md', 'contracts/generated/actiongen-manifest.json'];
function fixture(t, value = source()) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-actiongen-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, 'contracts'));
  const input = path.join(root, 'contracts/actions.json');
  fs.writeFileSync(input, JSON.stringify(value, null, 2));
  return { root, input, run: (...extra) => spawnSync(process.execPath,
    [tool, '--source', input, '--root', root, ...extra], { encoding: 'utf8', timeout: 10000 }) };
}
function passed(result) { assert.equal(result.status, 0, result.stderr + result.stdout); }
function snapshot(root) { return outputs.map(file => fs.readFileSync(path.join(root, file), 'utf8')); }

test('generates relocatable deterministic typed bindings and traceable output hashes', t => {
  const first = fixture(t), second = fixture(t);
  passed(first.run()); passed(second.run());
  assert.deepEqual(snapshot(first.root), snapshot(second.root));
  const [cpp, ts, tools, docs, manifest] = snapshot(first.root);
  assert.match(cpp, /register_Greet/); assert.match(cpp, /bridge\.register_action/);
  assert.match(ts, /validateSouiActionInput/); assert.match(ts, /validateSouiActionOutput/);
  assert.match(ts, /authorizedActionTools/); assert.equal(JSON.parse(tools).tools[0].name, 'app.greet');
  assert.match(docs, /Unicode/); assert.match(manifest, /sourceSha256/);
  assert.equal(snapshot(first.root).some(text => text.includes(first.root)), false);
  const before = outputs.map(file => fs.statSync(path.join(first.root, file)).mtimeMs);
  passed(first.run('--check'));
  assert.deepEqual(outputs.map(file => fs.statSync(path.join(first.root, file)).mtimeMs), before);
  // Formatting of the source has no effect on its normalized identity.
  fs.writeFileSync(first.input, JSON.stringify(source()).replace(/,/g, ',\r\n'));
  passed(first.run('--check'));
});

test('portable generator runs offline from SDK source files without dist or npm dependencies', t => {
  const f = fixture(t), sdkRoot = path.join(f.root, 'sdk-delivery');
  for (const relative of ['tools/sdk/actiongen.cjs', 'tools/sdk/actiongen/generate.cjs',
    'tools/sdk/actiongen/emit.cjs', 'sdk/web/package.json', 'sdk/web/src/action-contract-core.js']) {
    const destination = path.join(sdkRoot, relative);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    fs.copyFileSync(path.join(repository, relative), destination);
  }
  passed(spawnSync(process.execPath, [path.join(sdkRoot, 'tools/sdk/actiongen.cjs'),
    '--source', f.input, '--root', f.root], { encoding: 'utf8', timeout: 10000 }));
  passed(f.run('--check'));
  assert.equal(fs.existsSync(path.join(sdkRoot, 'sdk/web/dist')), false);
  assert.equal(fs.existsSync(path.join(sdkRoot, 'sdk/web/node_modules')), false);
});

test('check rejects missing and tampered files without creating or rewriting anything', t => {
  const f = fixture(t);
  assert.notEqual(f.run('--check').status, 0);
  assert.equal(fs.existsSync(path.join(f.root, 'app')), false);
  passed(f.run());
  const file = path.join(f.root, outputs[0]);
  fs.appendFileSync(file, '// local edit\n');
  const before = snapshot(f.root);
  assert.notEqual(f.run('--check').status, 0);
  assert.deepEqual(snapshot(f.root), before);
  assert.notEqual(f.run().status, 0, 'Explicit generation must not overwrite a tampered owned output');
});

test('explicit regeneration accepts a changed source while preserving ownership evidence', t => {
  const f = fixture(t); passed(f.run());
  const value = source(); value.actions[0].action.title = '新问候';
  fs.writeFileSync(f.input, JSON.stringify(value));
  assert.notEqual(f.run('--check').status, 0);
  passed(f.run()); passed(f.run('--check'));
  assert.match(fs.readFileSync(path.join(f.root, outputs[3]), 'utf8'), /新问候/);
});

test('rejects unknown keywords, unsafe symbols, duplicate actions and reserved success error', t => {
  const mutate = [
    value => { value.extra = true; },
    value => { value.cppNamespace = '../escape'; },
    value => { value.cppNamespace = 'std'; },
    value => { value.actions[0].cppName = 'class'; },
    value => { value.actions.push(value.actions[0]); },
    value => { value.actions[0].action.inputSchema.properties.class = { type: 'boolean' }; },
    value => { value.actions[0].action.inputSchema.pattern = '.*'; },
    value => { value.actions[0].action.outputSchema.properties.error = { type: 'string', maxLength: 10 }; },
    value => { value.actions[0].action.inputSchema.properties.name.maxLength = 32769; },
    value => { value.actions[0].action.inputSchema.properties.count = { type: 'integer', maximum: 9007199254740992 }; },
    value => { value.actions[0].method = 'bridge.bootstrap'; },
    value => { value.actions[0].capability = ''; }
  ];
  for (const change of mutate) {
    const value = source(); change(value);
    const f = fixture(t, value), result = f.run();
    assert.notEqual(result.status, 0, result.stdout);
    assert.match(result.stderr, /^actiongen:/, 'Failure must come from source validation');
    assert.equal(fs.existsSync(path.join(f.root, 'app')), false);
  }
});

test('refuses generated C++ symbol collisions before writing output', t => {
  const value = source();
  value.actions[0].cppName = 'contract_A';
  value.actions.push({ ...value.actions[0], method: 'app.other', cppName: 'AInput' });
  const f = fixture(t, value), result = f.run();
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /symbol collision/);
  assert.equal(fs.existsSync(path.join(f.root, 'app')), false);
});

test('prototype-named methods use computed TS keys without prototype mutation', t => {
  const value = source(); value.actions[0].method = '__proto__';
  const f = fixture(t, value); passed(f.run());
  const ts = fs.readFileSync(path.join(f.root, outputs[1]), 'utf8');
  const literal = ts.match(/export const actionContracts:[^\n]+ = (\{[\s\S]*?\n\});/)[1];
  const contracts = vm.runInNewContext(`(${literal})`, {}, { timeout: 1000 });
  assert.equal(Object.hasOwn(contracts, '__proto__'), true);
  assert.equal(contracts.__proto__.protocol, 'soui.action/1');
  passed(f.run('--check'));
});

test('source encoding, byte bounds and argument errors fail before output', t => {
  const f = fixture(t);
  fs.writeFileSync(f.input, Buffer.from([0xff]));
  assert.match(f.run().stderr, /not valid UTF-8/);
  fs.writeFileSync(f.input, ' '.repeat(524289));
  assert.match(f.run().stderr, /bounded file/);
  assert.notEqual(f.run('--unknown').status, 0);
  assert.equal(fs.existsSync(path.join(f.root, 'app')), false);
});

test('rejects existing business files and output junctions without writing outside the project', t => {
  const f = fixture(t);
  fs.mkdirSync(path.join(f.root, 'app/generated'), { recursive: true });
  const file = path.join(f.root, outputs[0]);
  fs.writeFileSync(file, '// business-owned\n');
  assert.notEqual(f.run().status, 0);
  assert.equal(fs.readFileSync(file, 'utf8'), '// business-owned\n');
  assert.equal(fs.existsSync(path.join(f.root, outputs[1])), false);
  const junction = fixture(t), external = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-actiongen-outside-'));
  t.after(() => fs.rmSync(external, { recursive: true, force: true }));
  fs.symlinkSync(external, path.join(junction.root, 'app'), 'junction');
  assert.notEqual(junction.run().status, 0);
  assert.deepEqual(fs.readdirSync(external), []);
});

test('known standard and Windows macros are rejected as generated identifiers', t => {
  for (const name of ['NULL', 'EOF', 'INT64_MAX', 'UINT_FAST32_MAX', 'NAN', 'min', 'max', 'errno', 'interface', 'near', 'far', 'GetMessage']) {
    const value = source(); value.actions[0].action.inputSchema.properties[name] = { type: 'boolean' };
    const f = fixture(t, value), result = f.run();
    assert.notEqual(result.status, 0, `Accepted macro field ${name}`);
    assert.match(result.stderr, /macro/);
    assert.equal(fs.existsSync(path.join(f.root, 'app')), false);
  }
});

test('known macros are rejected in namespace, action type and recursive input/output fields', t => {
  const values = [];
  let value = source(); value.cppNamespace = 'GetMessage'; values.push(value);
  value = source(); value.actions[0].cppName = 'EOF'; values.push(value);
  value = source(); value.actions[0].action.inputSchema.properties.nested = {
    type: 'array', maxItems: 2, items: { type: 'object', additionalProperties: false,
      properties: { interface: { type: 'boolean' } }, required: [] }
  }; values.push(value);
  value = source(); value.actions[0].action.outputSchema.properties.NULL = { type: 'boolean' }; values.push(value);
  for (const value of values) {
    const f = fixture(t, value), result = f.run();
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /known header macro/);
    assert.equal(fs.existsSync(path.join(f.root, 'app')), false);
  }
});

for (const relative of outputs) test(`hard-linked ${relative} is rejected before any output changes`, t => {
  const f = fixture(t); passed(f.run());
  const external = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-actiongen-hardlink-'));
  t.after(() => fs.rmSync(external, { recursive: true, force: true }));
  const target = path.join(f.root, relative), outside = path.join(external, 'unrelated.txt');
  const original = fs.readFileSync(target);
  fs.writeFileSync(outside, original);
  fs.unlinkSync(target); fs.linkSync(outside, target);
  assert.equal(fs.statSync(target).nlink, 2);
  const value = source(); value.actions[0].action.title = 'Changed';
  fs.writeFileSync(f.input, JSON.stringify(value));
  const before = outputs.map(file => fs.readFileSync(path.join(f.root, file)));
  const result = f.run();
  assert.notEqual(result.status, 0, 'A generator-owned path must not mutate another linked file');
  assert.match(result.stderr, /hard.link/);
  assert.equal(fs.readFileSync(outside).equals(original), true);
  assert.deepEqual(outputs.map(file => fs.readFileSync(path.join(f.root, file))), before);
});

test('check compares exact UTF-8 bytes and the manifest hashes without rewriting corruption', t => {
  const value = source(); value.actions[0].action.description = 'Replacement: \ufffd';
  const f = fixture(t, value); passed(f.run());
  const target = path.join(f.root, 'docs/generated/actions.md');
  const bytes = fs.readFileSync(target), index = bytes.indexOf(Buffer.from('\ufffd'));
  assert.ok(index >= 0);
  const damaged = Buffer.concat([bytes.subarray(0, index), Buffer.from([0xff]), bytes.subarray(index + 3)]);
  assert.equal(damaged.toString('utf8'), bytes.toString('utf8'), 'The old decoded comparison would accept this corruption');
  fs.writeFileSync(target, damaged);
  const result = f.run('--check');
  assert.notEqual(result.status, 0, 'Replacement decoding must not hide byte/hash corruption');
  assert.equal(fs.readFileSync(target).equals(damaged), true);
  // Even a manifest updated to the damaged bytes must not make decoding sufficient.
  const manifestFile = path.join(f.root, outputs[4]);
  const manifest = JSON.parse(fs.readFileSync(manifestFile, 'utf8'));
  manifest.outputs['docs/generated/actions.md'] = createHash('sha256').update(damaged).digest('hex');
  fs.writeFileSync(manifestFile, JSON.stringify(manifest, null, 2) + '\n');
  const consistentHash = f.run('--check');
  assert.notEqual(consistentHash.status, 0);
  assert.match(consistentHash.stderr, /Missing or stale generated output: docs\/generated\/actions\.md/);
  assert.equal(fs.readFileSync(target).equals(damaged), true);
});

for (const relative of [outputs[0], outputs[4]]) test(`atomic ${relative} replacement cannot modify a late external hard link`, t => {
  const f = fixture(t); passed(f.run());
  const outsideRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-actiongen-late-link-'));
  t.after(() => fs.rmSync(outsideRoot, { recursive: true, force: true }));
  const target = path.join(f.root, relative), outside = path.join(outsideRoot, 'unrelated.txt');
  const original = fs.readFileSync(target);
  const value = source(); value.actions[0].action.title = 'New title';
  fs.writeFileSync(f.input, JSON.stringify(value));
  // Inject a file hard link after preflight, at the filesystem publish operation.
  const script = `const fs = require('node:fs'); const rename = fs.renameSync;
    fs.renameSync = (from, to) => { if (to === ${JSON.stringify(target)}) fs.linkSync(to, ${JSON.stringify(outside)}); return rename(from, to); };
    require(${JSON.stringify(path.join(repository, 'tools/sdk/actiongen/generate.cjs'))})
      .run(['--source', ${JSON.stringify(f.input)}, '--root', ${JSON.stringify(f.root)}])
      .catch(error => { console.error(error.message); process.exitCode = 1; });`;
  const runner = path.join(f.root, 'publish-race.cjs'); fs.writeFileSync(runner, script);
  passed(spawnSync(process.execPath, [runner], { encoding: 'utf8', timeout: 10000 }));
  assert.equal(fs.existsSync(outside), true, 'Publishing must use a separate inode rather than an in-place write');
  assert.equal(fs.readFileSync(outside).equals(original), true);
  assert.equal(fs.statSync(target).nlink, 1);
  passed(f.run('--check'));
});
