const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '..', '..');

function iteratorReturns(relativePath) {
  const source = fs.readFileSync(path.join(root, relativePath), 'utf8');
  const result = new Map();
  for (const line of source.split(/\r?\n/)) {
    const match = line.match(/\bITERATOR\d+\(([^,]+),\s*([^,]+),/);
    if (match) result.set(match[2].trim(), match[1].trim().replace(/\s+/g, ' '));
  }
  return result;
}

test('the delivered Miniblink binding uses the source export return ABI', () => {
  const delivered = iteratorReturns('third_party/include/miniblink/wke.h');
  const exported = iteratorReturns('third_party/sources/miniblink/wke/wkedefine.h');
  const mismatches = [];

  for (const [name, returnType] of exported) {
    if (delivered.has(name) && delivered.get(name) !== returnType) {
      mismatches.push({
        name,
        delivered: delivered.get(name),
        exported: returnType,
      });
    }
  }

  assert.deepEqual(mismatches, [],
    'Dynamic function-pointer return types must match the compiled Miniblink exports, especially on x86.');
});

test('the host classifies pending resolvers through the validated jsIsString ABI', () => {
  const host = fs.readFileSync(path.join(root, 'sdk/src/host/soui_miniblink_host.cpp'), 'utf8');
  assert.match(host, /const bool waiting_is_string = jsIsString\(waiting\);/);
  assert.doesNotMatch(host, /const bool waiting_is_string = jsTypeOf\(waiting\)/);
});
