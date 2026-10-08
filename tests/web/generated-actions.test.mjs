import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import ts from '../../sdk/web/node_modules/typescript/lib/typescript.js';
import { SouiNativeClientImpl, SouiBridgeError } from '../../sdk/web/dist/index.js';
import * as core from '../../sdk/web/src/action-contract-core.js';
import { readSouiActionContract, validateSouiActionInput, validateSouiActionOutput } from '../../sdk/web/dist/index.js';

const repository = fileURLToPath(new URL('../../', import.meta.url));
const generated = path.join(repository, 'tests/action-consumer/web/src/generated/actions.ts');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-action-client-'));
process.once('exit', () => fs.rmSync(temporary, { recursive: true, force: true }));
const code = fs.readFileSync(generated, 'utf8').replace("'@soui-sdk'", JSON.stringify(new URL('../../sdk/web/dist/index.js', import.meta.url).href));
const module = path.join(temporary, 'actions.mjs');
fs.writeFileSync(module, ts.transpileModule(code, { compilerOptions: { target: ts.ScriptTarget.ES2015, module: ts.ModuleKind.ES2020 } }).outputText);
const { callAction, actionContracts, authorizedActionTools } = await import(pathToFileURL(module).href);
const contract = actionContracts['app.greet'];
function client(t, handler) {
  const sent = [];
  const value = new SouiNativeClientImpl({ onEvent: () => () => {}, request: packet => {
    const request = JSON.parse(packet); sent.push(request);
    const result = handler(request);
    return Promise.resolve(JSON.stringify({ requestId: request.requestId, ...result }));
  } }, { sessionId: 'session', origin: 'app://generated', token: 'native-token' });
  t.after(() => value.dispose());
  return { value, sent };
}

test('typed generated calls enter native synchronously and validate success results', async t => {
  const f = client(t, request => ({ ok: true, result: { message: `Hello, ${request.params.name}` } }));
  const pending = callAction(f.value, 'app.greet', { name: '开发者😀', attempts: 9007199254740991,
    ratio: 0.5, enabled: true, nothing: null, meta: { tags: ['one', 'two'] } });
  assert.equal(f.sent.length, 1, 'An async wrapper would lose the native input gesture');
  assert.equal((await pending).message, 'Hello, 开发者😀');
  assert.equal(f.sent[0].token, 'native-token');
});

test('invalid bounded inputs never reach the native client', t => {
  const f = client(t, () => ({ ok: true, result: { message: 'should not run' } }));
  for (const input of [{ name: '' }, { name: 'x', actor: 'admin' }, { name: 'x', attempts: 1.5 },
    { name: 'x', attempts: 9007199254740992 }, { name: 'x', meta: { tags: ['bad'] } },
    { name: '\ud800' }, { name: '😀'.repeat(33) }, { name: 'x', nothing: undefined }]) {
    assert.throws(() => callAction(f.value, 'app.greet', input), error => error instanceof SouiBridgeError && error.code === 'E_INVALID_PARAMS');
  }
  assert.equal(f.sent.length, 0);
});

test('bad native output and native denial fail without retry', async t => {
  const bad = client(t, () => ({ ok: true, result: { message: 42 } }));
  await assert.rejects(callAction(bad.value, 'app.greet', { name: 'valid' }), error => error.code === 'E_SCHEMA_INVALID');
  assert.equal(bad.sent.length, 1);
  const denied = client(t, () => ({ ok: false, error: { code: 'E_UNAUTHORIZED', message: 'Not granted' } }));
  await assert.rejects(callAction(denied.value, 'app.greet', { name: 'valid' }), error => error.code === 'E_UNAUTHORIZED');
  assert.equal(denied.sent.length, 1);
});

test('runtime Agent tools follow the authorized catalog and exclude unrelated host actions', () => {
  const catalog = methods => ({ protocol: 'soui.capabilities/1', sdkVersion: 'preview', methods });
  const own = { name: 'app.greet', capability: 'app.greet', action: contract };
  assert.deepEqual(authorizedActionTools(catalog([])), []);
  const tools = authorizedActionTools(catalog([own, { ...own, name: 'other.action', capability: 'other.action' }]));
  assert.deepEqual(tools.map(tool => tool.name), ['app.greet']);
  assert.deepEqual(authorizedActionTools(catalog([{ name: 'bridge.describe', capability: 'bridge.describe' }])), []);
  assert.throws(() => authorizedActionTools(catalog([{ ...own, action: { ...contract, protocol: 'bad' } }])), error => error.code === 'E_INVALID_RESPONSE');
});

test('browser wrapper and source CLI core preserve Unicode, safe integers and schema errors', () => {
  assert.deepEqual(core.readSouiActionContract(contract), readSouiActionContract(contract));
  for (const value of [{ name: '😀'.repeat(32) }, { name: 'x', nothing: null }, { name: 'x' }]) {
    assert.doesNotThrow(() => core.validateSouiActionInput(contract, value));
    assert.doesNotThrow(() => validateSouiActionInput(contract, value));
  }
  for (const value of [{ name: '😀'.repeat(33) }, { name: '\ud800' }, { name: 'x', attempts: 1.5 }]) {
    assert.throws(() => core.validateSouiActionInput(contract, value), error => error.code === 'E_INVALID_PARAMS');
    assert.throws(() => validateSouiActionInput(contract, value), error => error instanceof SouiBridgeError && error.code === 'E_INVALID_PARAMS');
  }
  assert.throws(() => validateSouiActionOutput(contract, { message: null }), error => error.code === 'E_SCHEMA_INVALID');
  assert.throws(() => core.readSouiActionContract({ ...contract, unknown: 1 }), error => error.code === 'E_INVALID_RESPONSE');
});

test('generated MethodMap rejects wrong inputs, methods and output types in a source consumer', () => {
  const file = path.join(temporary, 'consumer.ts');
  const importPath = generated.replaceAll('\\', '/').replace(/\.ts$/, '');
  fs.writeFileSync(file, `import { callAction, type ActionClient, type GreetInput } from ${JSON.stringify(importPath)};
declare const client: ActionClient;
const input: GreetInput = { name: 'valid', meta: { tags: ['one'] }, nothing: null };
const valid: Promise<{ message: string }> = callAction(client, 'app.greet', input);
// @ts-expect-error wrong input
callAction(client, 'app.greet', { name: 123 });
// @ts-expect-error unregistered method
callAction(client, 'app.unknown', { name: 'x' });
// @ts-expect-error undeclared parameter
callAction(client, 'app.greet', { name: 'x', actor: 'admin' });
// @ts-expect-error enum is limited
callAction(client, 'app.greet', { name: 'x', meta: { tags: ['invalid'] } });
// @ts-expect-error wrong result
const bad: Promise<{ message: number }> = callAction(client, 'app.greet', input);
`);
  const program = ts.createProgram([file], { strict: true, noEmit: true, skipLibCheck: true,
    target: ts.ScriptTarget.ES2015, module: ts.ModuleKind.ES2020, moduleResolution: ts.ModuleResolutionKind.Bundler,
    baseUrl: repository, paths: { '@soui-sdk': ['sdk/web/src/index.ts'] } });
  const diagnostics = ts.getPreEmitDiagnostics(program);
  assert.equal(diagnostics.length, 0, ts.formatDiagnosticsWithColorAndContext(diagnostics,
    { getCurrentDirectory: () => repository, getCanonicalFileName: value => value, getNewLine: () => '\n' }));
});
