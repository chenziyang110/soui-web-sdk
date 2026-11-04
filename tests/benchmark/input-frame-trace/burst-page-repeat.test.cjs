'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const pageSource = fs.readFileSync(
  path.join(__dirname, '../../../benchmarks/protocol/burst-page.js'), 'utf8');
const runId = '0123456789abcdef0123456789abcdef';

function harness(kind) {
  const listeners = Object.create(null);
  const animationFrames = [];
  let paintOperations = 0;
  let prevented = 0;
  const drawing = {
    fillRect() { ++paintOperations; },
    fillText() { ++paintOperations; }
  };
  const canvas = {
    getContext(name) { assert.equal(name, '2d'); return drawing; },
    addEventListener(name, callback) { listeners[name] = callback; }
  };
  const window = {devicePixelRatio: 1, souiBurstConfig: {run: runId, kind}};
  vm.runInNewContext(pageSource, {
    window,
    location: {search: ''},
    document: {getElementById(id) { assert.equal(id, 'feedback'); return canvas; }},
    requestAnimationFrame(callback) { animationFrames.push(callback); }
  }, {filename: 'burst-page.js', timeout: 1000});
  return {
    result: () => window.souiBurstResult(),
    dispatch(name, event) {
      assert.equal(typeof listeners[name], 'function');
      listeners[name](event);
    },
    pointer(x, y) {
      return {type: 'mousemove', clientX: x, clientY: y, screenX: x + 88, screenY: y + 111,
        timeStamp: 10, isTrusted: true, preventDefault() { ++prevented; }};
    },
    wheel(delta) {
      return {type: 'wheel', clientX: 100, clientY: 100, screenX: 188, screenY: 211,
        timeStamp: 10, isTrusted: true, wheelDelta: delta, preventDefault() { ++prevented; }};
    },
    frameCount: () => animationFrames.length,
    paintCount: () => paintOperations,
    preventedCount: () => prevented,
    render() {
      assert.ok(animationFrames.length > 0);
      animationFrames.shift()();
    }
  };
}

test('same-position pointer callback is counted without a new ACK or render', () => {
  const page = harness('pointer_move');
  page.dispatch('mousemove', page.pointer(100, 100));
  assert.equal(page.result().latest, 1);
  assert.equal(page.result().observed, 1);
  page.render();
  page.dispatch('mousemove', page.pointer(101, 100));
  assert.equal(page.result().latest, 2);
  assert.equal(page.result().observed, 2);
  page.render();

  const before = {frames: page.frameCount(), paints: page.paintCount()};
  page.dispatch('mousemove', page.pointer(101, 100));
  const repeated = page.result();
  assert.equal(repeated.latest, 2);
  assert.equal(repeated.observed, 2);
  assert.equal(repeated.rejected, 0);
  assert.equal(repeated.invalid, false);
  assert.equal(repeated.same_position_repeats, 1);
  assert.deepEqual(Array.from(repeated.same_position_repeat_range), [0, repeated.same_position_repeat_limit]);
  assert.equal(repeated.same_position_repeat_overflow, false);
  assert.match(repeated.same_position_repeat_scope, /not injection IDs, acknowledgements, or presentations/);
  assert.equal(page.frameCount(), before.frames);
  assert.equal(page.paintCount(), before.paints);

  page.dispatch('mousemove', page.pointer(102, 100));
  assert.equal(page.result().latest, 3);
  assert.equal(page.result().observed, 3);
  assert.equal(page.result().same_position_repeats, 1);
  assert.equal(page.result().invalid, false);
});

test('strictly backward pointer sequence remains sticky-invalid', () => {
  const page = harness('pointer_move');
  for (const x of [100, 101, 102]) page.dispatch('mousemove', page.pointer(x, 100));
  const before = page.result();
  page.dispatch('mousemove', page.pointer(101, 100));
  const after = page.result();
  assert.equal(before.latest, 3);
  assert.equal(after.latest, 3);
  assert.equal(after.observed, 3);
  assert.equal(after.rejected, 1);
  assert.equal(after.invalid, true);
  assert.equal(after.same_position_repeats, 0);
});

test('wheel acceptance and invalid-delta rejection are unchanged', () => {
  const page = harness('wheel');
  page.dispatch('wheel', page.wheel(-120));
  page.dispatch('wheel', page.wheel(-120));
  assert.equal(page.result().latest, 2);
  assert.equal(page.result().observed, 2);
  assert.equal(page.result().rejected, 0);
  assert.equal(page.result().same_position_repeats, 0);
  assert.equal(page.preventedCount(), 4);

  const invalidWheel = harness('wheel');
  invalidWheel.dispatch('wheel', invalidWheel.wheel(0));
  assert.equal(invalidWheel.result().latest, 0);
  assert.equal(invalidWheel.result().observed, 0);
  assert.equal(invalidWheel.result().rejected, 1);
  assert.equal(invalidWheel.result().invalid, true);
  assert.equal(invalidWheel.result().same_position_repeats, 0);
  assert.equal(invalidWheel.preventedCount(), 1);
});

test('same-position repeat counter is bounded and overflow fails closed', () => {
  const page = harness('pointer_move');
  page.dispatch('mousemove', page.pointer(100, 100));
  page.dispatch('mousemove', page.pointer(101, 100));
  const repeated = page.pointer(101, 100);
  for (let index = 0; index < page.result().same_position_repeat_limit; ++index)
    page.dispatch('mousemove', repeated);
  const bounded = page.result();
  assert.equal(bounded.same_position_repeats, bounded.same_position_repeat_limit);
  assert.equal(bounded.same_position_repeat_overflow, false);
  assert.equal(bounded.invalid, false);

  page.dispatch('mousemove', repeated);
  const overflow = page.result();
  assert.equal(overflow.same_position_repeats, overflow.same_position_repeat_limit);
  assert.equal(overflow.same_position_repeat_overflow, true);
  assert.equal(overflow.invalid, true);
  assert.equal(overflow.rejected, 1);
  assert.equal(overflow.latest, 2);
  assert.equal(overflow.observed, 2);
});
