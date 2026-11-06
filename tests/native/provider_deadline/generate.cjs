'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const [root, output] = process.argv.slice(2);
if (!root || !output) throw new Error('usage: generate.cjs source-root output');
fs.mkdirSync(output, { recursive: true });
const entries = [];
const publicAbi = fs.readFileSync(path.join(root, 'third_party/include/miniblink/input_ui_api.h'));
const providerAbi = fs.readFileSync(path.join(root, 'third_party/sources/miniblink/wke/InputUiApi.h'));
if (!publicAbi.equals(providerAbi)) throw new Error('SDK and provider ABI bytes differ');
function save(source, target, begin, end) {
  const bytes = fs.readFileSync(path.join(root, source));
  const start = begin ? bytes.indexOf(Buffer.from(begin, 'ascii')) : 0;
  const finish = end ? bytes.indexOf(Buffer.from(end, 'ascii'), start + begin.length) : bytes.length;
  if (start < 0 || finish <= start || (begin && bytes.indexOf(Buffer.from(begin, 'ascii'), start + 1) >= 0))
    throw new Error('ambiguous production boundary: ' + source);
  const fragment = bytes.subarray(start, finish);
  if (fragment.some(value => value > 127)) throw new Error('projection must remain exact ASCII: ' + target);
  fs.writeFileSync(path.join(output, target), fragment);
  entries.push({ source, target, startByte: start, endByteExclusive: finish,
    sourceSha256: crypto.createHash('sha256').update(bytes).digest('hex'),
    fragmentSha256: crypto.createHash('sha256').update(fragment).digest('hex') });
}
const base = 'third_party/sources/miniblink/';
save(base + 'content/common/PaintLifetime.h', 'cursor.inc',
  'template<class Key> class PaintCloseServiceCursor {', 'inline PaintLifetimeRuntime& paintLifetimeRuntime()');
save(base + 'wke/wkeWebView.cpp', 'sweep.inc',
  'bool CWebView::serviceDeferredPaintCloses(', 'bool CWebView::resumePaintRuntime()');
save(base + 'wke/InputUiApi.cpp', 'provider.inc',
  'void WKE_INPUT_UI_CALL serviceOwner()', 'int WKE_INPUT_UI_CALL cancelInput(void* opaque)');
save(base + 'wke/InputUiApi.cpp', 'api.inc',
  'const wkeInputUiApi* WKE_CALL_TYPE wkeGetInputUiApi(unsigned requestedVersion)');
// Both consumers compile the same private C ABI. Preserve the production
// provider header identity while independently checking the SDK's copy.
if (!fs.readFileSync(path.join(root, base + 'wke/InputUiApi.h')).equals(
    fs.readFileSync(path.join(root, 'third_party/include/miniblink/input_ui_api.h'))))
  throw new Error('SDK and provider input UI headers differ');
save(base + 'wke/InputUiApi.h', 'input_ui_api.h');
fs.writeFileSync(path.join(output, 'source-binding.json'), JSON.stringify({
  schema: 'provider-deadline-contract/1',
  dependencyMode: 'actual-method-bytes-with-explicit-platform-clock-and-view-fakes', entries
}, null, 2) + '\n');
