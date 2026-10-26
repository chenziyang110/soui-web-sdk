'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const [root, output] = process.argv.slice(2);
if (!root || !output) throw new Error('usage: generate.cjs source-root output');
fs.mkdirSync(output, { recursive: true });
const base = 'third_party/sources/miniblink/';
const entries = [];
{
  const source = 'content/common/PaintLifetime.h';
  const bytes = fs.readFileSync(path.join(root, base + source));
  fs.writeFileSync(path.join(output, 'PaintLifetime.h'), bytes);
  const digest = crypto.createHash('sha256').update(bytes).digest('hex');
  entries.push({ source: base + source, target: 'PaintLifetime.h', startByte: 0,
    endByteExclusive: bytes.length, sourceSha256: digest, fragmentSha256: digest });
}
function extract(source, target, begin, end) {
  const bytes = fs.readFileSync(path.join(root, base + source));
  const first = bytes.indexOf(Buffer.from(begin));
  const last = bytes.indexOf(Buffer.from(end), first + begin.length);
  if (first < 0 || last <= first || bytes.indexOf(Buffer.from(begin), first + 1) !== -1)
    throw new Error('ambiguous production source boundary: ' + source);
  const fragment = bytes.subarray(first, last);
  fs.writeFileSync(path.join(output, target), fragment);
  entries.push({ source: base + source, target, startByte: first, endByteExclusive: last,
    sourceSha256: crypto.createHash('sha256').update(bytes).digest('hex'),
    fragmentSha256: crypto.createHash('sha256').update(fragment).digest('hex') });
}
extract('wke/wkeWebWindow.cpp', 'window_helpers.inc',
  'namespace {\nstatic bool isOwnedCloseWindow', 'CWebWindow::CWebWindow(COLORREF c)');
extract('wke/wkeWebWindow.cpp', 'native_close.inc',
  'bool CWebWindow::_destroyWindow()', 'void CWebWindow::_initCallbacks()');
extract('wke/wkeWebWindow.cpp', 'native_receipt.inc',
  '    case WM_NCDESTROY:', '    case WM_TIMER:');
extract('wke/wkeWebView.cpp', 'callback_once.inc',
  'bool CWebView::takeWindowDestroyCallback()', 'void CWebView::quiescePaintCallbacks()');
fs.writeFileSync(path.join(output, 'source-binding.json'), JSON.stringify({
  schema: 'window-close-lifetime-source/1', dependencyMode: 'deterministic-fakes-no-native-window', entries
}, null, 2) + '\n');
