'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const main = fs.readFileSync(path.resolve(__dirname, '..', 'main.cpp'), 'utf8');

test('failed benchmark WM_CLOSE post enters the same slot close lifecycle', () => {
  const helper = main.match(/bool\s+post_close_or_fallback\s*\([\s\S]*?\n\}/);
  assert.ok(helper, 'Host must expose the bounded post/fallback decision used by its close action');
  assert.match(helper[0], /if\s*\(\s*post\(\)\s*\)\s*return\s+true\s*;/);
  assert.match(helper[0], /fallback\(\)\s*;\s*return\s+false\s*;/);

  const start = main.indexOf('if(next.name=="close")');
  const end = main.indexOf('else if(next.name=="second-host")', start);
  assert.ok(start >= 0 && end > start, 'Native action queue must retain its receipt-claimed close branch');
  const closeBranch = main.slice(start, end);
  assert.match(closeBranch, /post_close_or_fallback/);
  assert.match(closeBranch, /exit_code\s*=\s*3/);
  assert.match(closeBranch, /action-failed/);
  assert.match(closeBranch, /item\.request_close\(\)/,
    'A failed PostMessage must request normal Host teardown instead of waiting for an external close');
  assert.match(closeBranch, /action-close-posted/,
    'The successful PostMessage path must keep its existing normal WM_CLOSE behavior');

  assert.match(main, /post_close_or_fallback\(\[\]\s*\{\s*return\s+false\s*;\s*\},\s*\[&\]\s*\{\s*fallback_called\s*=\s*true\s*;\s*\}\s*\)/,
    'Native self-test must exercise a failed post and confirm fallback invocation');
  assert.match(main, /post_close_or_fallback\(\[\]\s*\{\s*return\s+true\s*;\s*\},\s*\[&\]\s*\{\s*fallback_called\s*=\s*true\s*;\s*\}\s*\)/,
    'Native self-test must confirm the successful post does not invoke fallback');
});
