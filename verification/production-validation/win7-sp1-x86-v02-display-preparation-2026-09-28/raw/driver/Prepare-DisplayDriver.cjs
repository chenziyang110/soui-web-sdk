'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
function create(sourceName, targetName, replacements) {
  const source = path.join(root, sourceName);
  const target = path.join(root, targetName);
  assert.equal(fs.existsSync(target), false, `Refuse overwrite: ${targetName}`);
  let content = fs.readFileSync(source, 'utf8');
  for (const [before, after] of replacements) {
    assert.equal(content.split(before).length, 2, `Expected one ${before} in ${sourceName}`);
    content = content.replace(before, after);
  }
  fs.writeFileSync(target, content, {flag: 'wx'});
  return targetName;
}
const visible = 'visible-explore-attempt1';
const display = 'display-explore-attempt1';
const freeze = create('Freeze-VisibleExplore.cjs', 'Freeze-DisplayExplore.cjs', [
  [visible, display],
  ['soui.win7-v02-current-visible-explore-input/v1', 'soui.win7-v02-current-display-explore-input/v1'],
  ['soui.win7-v02-current-visible-explore-freeze/v1', 'soui.win7-v02-current-display-explore-freeze/v1'],
]);
const verify = create('Verify-VisibleExplore.cjs', 'Verify-DisplayExplore.cjs', [
  [visible, display],
  ['soui.win7-v02-current-visible-explore-input/v1', 'soui.win7-v02-current-display-explore-input/v1'],
  ['soui.win7-v02-current-visible-explore-freeze/v1', 'soui.win7-v02-current-display-explore-freeze/v1'],
]);
console.log(JSON.stringify({status: 'display-freeze-tools-prepared', files: [freeze, verify]}));
