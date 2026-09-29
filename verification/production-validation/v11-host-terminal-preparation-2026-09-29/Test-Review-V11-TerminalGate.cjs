'use strict';

// Execute the actual terminal-gate assertions with synthetic host records.
// These cases do not run QEMU or validate the Win7 application.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const reviewer = fs.readFileSync(path.join(__dirname, 'Review-V11.cjs'), 'utf8');
const start = reviewer.indexOf('const gates = Array.isArray(gateRows)');
const end = reviewer.indexOf('\nconst serial =', start);
assert(start >= 0 && end > start, 'Cannot locate the real terminal-gate checks');
const checks = reviewer.slice(start, end);
const exitAt = '2026-09-29T00:00:00.000Z';
const earlier = {at: '2026-09-28T23:59:55.000Z', processes: [], error: null};
const terminal = {at: '2026-09-29T00:00:01.000Z', phase: 'after-qemu-exit',
  processes: [], error: null};

function review(lastGate, qemuExitedAt = exitAt) {
  vm.runInNewContext(checks, {
    assert, Date, gateRows: [earlier, lastGate], result: {qemuExitedAt},
  }, {filename: 'Review-V11.cjs:terminal-gate'});
}

test('one clean host sample just after QEMU exit is accepted', () => {
  assert.doesNotThrow(() => review(terminal));
});
test('a missing terminal phase cannot close the isolation gate', () => {
  assert.throws(() => review({...terminal, phase: undefined}), /after-qemu-exit/);
});
test('a sample before QEMU exit cannot close the isolation gate', () => {
  assert.throws(() => review({...terminal, at: '2026-09-28T23:59:59.000Z'}),
    /Missing timely terminal/);
});
test('a stale sample cannot close the isolation gate', () => {
  assert.throws(() => review({...terminal, at: '2026-09-29T00:00:16.000Z'}),
    /Missing timely terminal/);
});
test('an external process at the terminal sample rejects isolation', () => {
  assert.throws(() => review({...terminal, processes: [{ProcessId: 9000}]}));
});
test('a failed terminal process query rejects isolation', () => {
  assert.throws(() => review({...terminal, error: 'CIM query failed'}));
});
