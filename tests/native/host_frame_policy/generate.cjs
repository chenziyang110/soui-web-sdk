'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const [root, output] = process.argv.slice(2);
if (!root || !output) throw new Error('usage: generate.cjs source-root output');
fs.mkdirSync(output, {recursive: true});
const entries = [];
function project(source, target, begin, end) {
  const bytes = fs.readFileSync(path.join(root, source));
  const needle = Buffer.from(begin, 'ascii');
  const start = bytes.indexOf(needle);
  const stop = bytes.indexOf(Buffer.from(end, 'ascii'), start + needle.length);
  if (start < 0 || stop <= start || bytes.indexOf(needle, start + needle.length) >= 0)
    throw new Error('ambiguous source projection: ' + source);
  const fragment = bytes.subarray(start, stop);
  if (fragment.some(value => value > 127)) throw new Error('expected exact ASCII method bytes');
  fs.writeFileSync(path.join(output, target), fragment);
  entries.push({source, target, startByte: start, endByteExclusive: stop,
    sourceSha256: crypto.createHash('sha256').update(bytes).digest('hex'),
    fragmentSha256: crypto.createHash('sha256').update(fragment).digest('hex')});
}
const vendor = 'third_party/sources/miniblink/';
const publicAbi = fs.readFileSync(path.join(root, 'third_party/include/miniblink/input_ui_api.h'));
if (!publicAbi.equals(fs.readFileSync(path.join(root, vendor + 'wke/InputUiApi.h'))))
  throw new Error('host/provider ABI copies differ');
project(vendor + 'content/browser/WebPageImpl.cpp', 'frame-policy-methods.inc',
  'void WebPageImpl::setMainFrameRate(int rate)', 'void WebPageImpl::setDrawMinInterval(double drawMinInterval)');
project(vendor + 'content/browser/WebPage.cpp', 'frame-policy-wrapper.inc',
  'bool WebPage::applyMainFrameRate(int rate)', 'void WebPage::setNeedsCommit()');
project(vendor + 'wke/InputUiApi.cpp', 'frame-policy-provider.inc',
  'wke::CWebView* liveOwnerView(void* opaque)', 'void WKE_INPUT_UI_CALL serviceOwner()');
project('sdk/src/host/soui_miniblink_host.cpp', 'frame-display-reference.inc',
  'const auto display_window = static_cast<HWND>(owner_window_ ? owner_window_ : webview_window_);',
  'const detail::frame_policy_key key{');
project('sdk/src/host/soui_miniblink_host.cpp', 'frame-display-report-inputs.inc',
  'lifetime->frame_report_pulse = pulse_rate; lifetime->frame_report_desired = requested;',
  'lifetime->frame_report_effective =');
fs.writeFileSync(path.join(output, 'source-binding.json'), JSON.stringify({
  schema: 'frame-policy-applied-contract/1',
  dependencyMode: 'actual-method-and-display-fragment-bytes-with-explicit-native-and-display-query-fakes', entries
}, null, 2) + '\n');
