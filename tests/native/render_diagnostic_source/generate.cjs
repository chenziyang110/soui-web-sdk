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
save('content/common/InputFrameTrace.h', 'content/common/InputFrameTrace.h');
save('cc/trees/RenderDiagnosticTrace.h', 'cc/trees/RenderDiagnosticTrace.h');
save('cc/trees/GpuSkiaSurface.cpp', 'gpu_present.inc',
  'bool GpuSkiaSurface::present()', 'void GpuSkiaSurface::recordPresented()');
fs.writeFileSync(path.join(output, 'source-binding.json'), JSON.stringify({
  schema: 'render-diagnostic-source/1', dependencyMode: 'actual-present-with-fake-gl-and-recorder', entries
}, null, 2) + '\n');
