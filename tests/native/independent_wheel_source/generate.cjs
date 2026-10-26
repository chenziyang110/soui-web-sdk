'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const [root, output] = process.argv.slice(2);
if (!root || !output) throw new Error('usage: generate.cjs source-root output');
const base = 'third_party/sources/miniblink/';
const entries = [];
function save(source, target, begin, end) {
  const bytes = fs.readFileSync(path.join(root, base + source));
  let first = 0, last = bytes.length;
  if (begin) {
    first = bytes.indexOf(Buffer.from(begin));
    last = bytes.indexOf(Buffer.from(end), first + begin.length);
    if (first < 0 || last <= first || bytes.indexOf(Buffer.from(begin), first + 1) !== -1)
      throw new Error('ambiguous production boundary: ' + source);
  }
  const fragment = bytes.subarray(first, last);
  fs.mkdirSync(path.dirname(path.join(output, target)), { recursive: true });
  fs.writeFileSync(path.join(output, target), fragment);
  entries.push({ source: base + source, target, startByte: first, endByteExclusive: last,
    sourceSha256: crypto.createHash('sha256').update(bytes).digest('hex'),
    fragmentSha256: crypto.createHash('sha256').update(fragment).digest('hex') });
}
for (const source of [
  'content/common/InputFrameTrace.h', 'content/common/PaintLifetime.h',
  'cc/input/ScrollController.h', 'cc/input/ScrollController.cpp',
  'cc/trees/ScrollTreeSnapshot.h', 'cc/trees/ScrollTreeSnapshot.cpp',
  'cc/trees/InputFrameCoverage.h', 'cc/trees/IndependentInputTrace.h',
  'cc/trees/DrawFrameWakePolicy.h', 'cc/trees/LegacyPaintRetryPolicy.h',
  'cc/trees/ResourceNotificationPolicy.h']) save(source, source);
const host = 'cc/trees/LayerTreeHost.cpp';
save(host, 'retry_state.inc', 'struct UiPaintRetryState {', '// The queue owns this object.');
save(host, 'draw_task.inc', 'class DrawFrameTask final', 'static void queueResourceProgress(');
save(host, 'input_payload.inc', 'struct CompositorScrollInput {', 'static CompositorScrollState* findCompositorScrollState(');
save(host, 'draw_wake.inc', 'void LayerTreeHost::requestDrawFrameToRunIntoCompositeThread()', 'bool LayerTreeHost::addCompositorAnimation(');
save(host, 'owner_retry.inc', 'void LayerTreeHost::retryPendingOwnerWork()', 'void LayerTreeHost::beginRecordActions(');
save(host, 'wheel.inc', 'bool LayerTreeHost::handleCompositorWheelInput(', 'bool LayerTreeHost::captureCompositorScrollBarrier(');
fs.writeFileSync(path.join(output, 'source-binding.json'), JSON.stringify({
  schema: 'independent-wheel-source/1', dependencyMode: 'actual-admission-and-wake-with-fake-composite-owner', entries
}, null, 2) + '\n');
