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
for (const source of ['content/browser/HostVisibilityPolicy.h',
  'cc/trees/DrawFrameWakePolicy.h', 'content/common/PaintLifetime.h',
  'content/common/InputFrameTrace.h']) save(source, source);
const page = 'content/browser/WebPageImpl.cpp';
save(page, 'visibility_state.inc', 'struct HostVisibilityWakeState {', 'int64_t WebPageImpl::m_firstFrameId');
save(page, 'auto_record.inc', 'class AutoRecordActions {', '#if (defined ENABLE_WKE)');
save(page, 'commit_task.inc', 'class CommitTask :', 'class VisibilityRestoreTask final');
save(page, 'visibility_owner.inc', 'class VisibilityRestoreTask final', 'void WebPageImpl::setNeedsCommitAndNotLayout()');
save(page, 'schedule_commit.inc', 'void WebPageImpl::setNeedsCommitAndNotLayout()', 'void WebPageImpl::setNeedsCommit()');
save(page, 'main_frame.inc', 'void WebPageImpl::setNeedsCommit()', 'void WebPageImpl::onLayerTreeDirty()');
save('cc/trees/LayerTreeHost.cpp', 'host_visible.inc', 'void LayerTreeHost::setVisible(bool visible)', '// Sets the current page scale factor');
fs.writeFileSync(path.join(output, 'source-binding.json'), JSON.stringify({
  schema: 'host-visibility-source/1', dependencyMode: 'actual-owner-and-record-with-fake-blink-resource-scheduler', entries
}, null, 2) + '\n');
