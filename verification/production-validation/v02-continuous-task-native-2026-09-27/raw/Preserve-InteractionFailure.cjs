'use strict';
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const id = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
const attempt = 'green-x64-attempt2';
const read = name => id.readJson(path.join(__dirname, name));
const before = read(`${attempt}-before-input.json`);
const source = id.source(repo);
assert.equal(source.sha256, before.source.sha256);
assert.equal(source.files.length, 718);
for (const name of ['configure', 'build']) {
  const result = read(`command-logs/${attempt}-${name}/result.json`);
  assert.equal(result.exitCode, 0);
  assert.equal(result.timedOut, false);
  assert.equal(result.error, null);
}
const driver = read(`command-logs/${attempt}-driver/result.json`);
const test = read(`command-logs/${attempt}-interaction/result.json`);
assert.equal(driver.exitCode, 1);
assert.equal(test.exitCode, 1);
assert.equal(test.timedOut, false);
assert.equal(test.error, null);
assert(!fs.existsSync(path.join(__dirname, 'command-logs', `${attempt}-renewal`)));
for (const stage of ['before', 'compile', 'test'])
  assert.equal(read(`${attempt}-${stage}-gate.json`).processes.length, 0);
const stderr = fs.readFileSync(path.join(__dirname, 'command-logs', `${attempt}-interaction`, 'stderr.log'), 'utf8');
const stdout = fs.readFileSync(path.join(__dirname, 'command-logs', `${attempt}-interaction`, 'stdout.log'), 'utf8');
assert(stdout.includes('duration completion, real elapsed time and public progress passed'));
assert(stderr.includes('Demo bootstrap rejected'));

const frozen = path.join(__dirname, `${attempt}-failed`);
fs.mkdirSync(frozen);
const rels = ['tests/native/workbench_interaction_tests.cpp',
  'demos/workbench/integration/demo_bridge_adapter.cpp',
  'out/x64/tests/native/Release/soui_workbench_interaction_tests.exe',
  'out/x64/tests/native/Release/soui_demo_session_renewal_tests.exe'];
for (const rel of rels) {
  const to = path.join(frozen, rel);
  fs.mkdirSync(path.dirname(to), {recursive: true});
  fs.writeFileSync(to, fs.readFileSync(path.join(repo, rel)), {flag: 'wx'});
}
const names = ['soui_workbench_interaction_tests', 'soui_demo_session_renewal_tests'];
const commands = id.walk(path.join(repo, 'out/x64/tests/native'))
  .filter(name => names.some(target => name.startsWith(`${target}.dir/Release/`)) &&
    /(?:CL|link)\.command\.1\.tlog$/.test(name));
assert(commands.length >= 4);
for (const name of commands) {
  const bytes = fs.readFileSync(path.join(repo, 'out/x64/tests/native', name));
  const content = bytes[0] === 255 && bytes[1] === 254 ? bytes.toString('utf16le') : bytes.toString('utf8');
  assert(!/[\\/]vcpkg[\\/]/i.test(content));
  const to = path.join(frozen, 'commands', name);
  fs.mkdirSync(path.dirname(to), {recursive: true});
  fs.writeFileSync(to, bytes, {flag: 'wx'});
}
const result = {schema: 'soui.v02-interaction-failed-test/v1', at: new Date().toISOString(),
  attempt, sourceSha256: source.sha256, sourceFiles: source.files.length,
  configureExitCode: 0, buildExitCode: 0, interactionExitCode: 1, driverExitCode: 1,
  renewalExecuted: false, guiExecuted: false, releaseReady: false,
  observed: 'First duration/progress case passed; second case rejected its second bridge.bootstrap.',
  frozen: id.snapshot(frozen, id.walk(frozen)),
  commandFiles: commands.length};
fs.writeFileSync(path.join(__dirname, `${attempt}-failure-review.json`), id.encoded(result), {flag: 'wx'});
console.log(JSON.stringify({attempt, sourceSha256: source.sha256, interactionExitCode: 1,
  frozenFiles: result.frozen.files.length, commandFiles: commands.length}));
