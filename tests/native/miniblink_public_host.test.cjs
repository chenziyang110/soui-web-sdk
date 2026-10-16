const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '..', '..');

test('the host classifies pending resolvers through the validated jsIsString ABI', () => {
  const host = fs.readFileSync(path.join(root, 'sdk/src/host/soui_miniblink_host.cpp'), 'utf8');
  assert.match(host, /const bool waiting_is_string = jsIsString\(waiting\);/);
  assert.doesNotMatch(host, /const bool waiting_is_string = jsTypeOf\(waiting\)/);
});
