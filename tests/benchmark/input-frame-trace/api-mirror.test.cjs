'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../../..');
// Static source gate only. This authored regression has not been executed.
function normalize(text) {
  return text.replaceAll('[[nodiscard]] ', '')
    .replace('namespace soui::sdk::diagnostics::input_frame_trace {', 'namespace content { namespace input_frame_trace {')
    .replace('} // namespace soui::sdk::diagnostics::input_frame_trace', '} } // namespace content::input_frame_trace')
    .replaceAll('\r\n', '\n').trim();
}
test('SDK/vendor v1 POD, stage, counter and recorder declarations remain mirrored', () => {
  const sdk = fs.readFileSync(path.join(root, 'sdk/include/soui/diagnostics/input_frame_trace.h'), 'utf8');
  const vendor = fs.readFileSync(path.join(root, 'third_party/sources/miniblink/content/common/InputFrameTrace.h'), 'utf8');
  assert.equal(normalize(sdk), normalize(vendor));
});
test('vendor declaration retains its existing language baseline and dependency direction', () => {
  const vendor = fs.readFileSync(path.join(root, 'third_party/sources/miniblink/content/common/InputFrameTrace.h'), 'utf8');
  assert.doesNotMatch(vendor, /#include\s*[<"]soui\//);
  assert.doesNotMatch(vendor, /^namespace\s+content::|\[\[nodiscard\]\]/m);
});
