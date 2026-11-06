'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { TextDecoder } = require('node:util');

const [root, output] = process.argv.slice(2);
if (!root || !output) throw new Error('usage: generate.cjs source-root output');
const base = 'third_party/sources/miniblink/';
const entries = [];
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

// Slice and write raw bytes. Decode only to reject malformed compiler inputs;
// never normalize newlines, replace characters, or rewrite production statements.
function save(source, target, begin, end) {
  const sourcePath = base + source;
  const bytes = fs.readFileSync(path.join(root, sourcePath));
  let first = 0;
  let last = bytes.length;
  if (begin) {
    const beginBytes = Buffer.from(begin, 'utf8');
    const endBytes = Buffer.from(end, 'utf8');
    first = bytes.indexOf(beginBytes);
    last = bytes.indexOf(endBytes, first + beginBytes.length);
    if (first < 0 || last <= first
        || bytes.indexOf(beginBytes, first + 1) !== -1
        || bytes.indexOf(endBytes, last + 1) !== -1)
      throw new Error('missing or ambiguous complete production range: ' + sourcePath);
  }
  const fragment = bytes.subarray(first, last);
  new TextDecoder('utf-8', { fatal: true }).decode(fragment);
  const destination = path.join(output, target);
  fs.mkdirSync(path.dirname(destination), { recursive: true });
  fs.writeFileSync(destination, fragment);
  entries.push({ source: sourcePath, target, startByte: first, endByteExclusive: last,
    sourceSha256: sha256(bytes), fragmentSha256: sha256(fragment),
    begin: begin || null, end: end || null });
}

save('cc/trees/FrameBufferLeasePolicy.h', 'cc/trees/FrameBufferLeasePolicy.h');
const host = 'cc/trees/LayerTreeHost.cpp';
save(host, 'frame_buffer_constructor.inc',
  'LayerTreeHost::FrameBuffer::FrameBuffer(',
  'LayerTreeHost::FrameBufferLease::FrameBufferLease() = default;');
save(host, 'clear_canvas.inc',
  'void LayerTreeHost::clearCanvas(',
  'void LayerTreeHost::setDrawMinInterval(');
save(host, 'prepare_writable_frame_buffer.inc',
  'bool LayerTreeHost::prepareWritableFrameBufferLocked()',
  'SkCanvas* LayerTreeHost::getMemoryCanvasLocked()');
fs.writeFileSync(path.join(output, 'source-binding.json'), JSON.stringify({
  schema: 'frame-buffer-copy-source/1',
  dependencyMode: 'complete-production-methods-with-fake-skia-platform-and-trace-counters',
  bytePreserving: true,
  entries
}, null, 2) + '\n');
