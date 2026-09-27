'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const freezeSha = '3dcc7c9eb1dbf71fd79f1626e32192a3e4bcab020612b3a27ab6ab557782474b';
function create(sourceName, targetName, replacements) {
  const target = path.join(root, targetName);
  let content = fs.readFileSync(path.join(root, sourceName), 'utf8');
  for (const [before, after, expected] of replacements) {
    const count = content.split(before).length - 1;
    assert.equal(count, expected, `${sourceName}: expected ${expected} occurrences of ${before}, got ${count}`);
    content = content.replaceAll(before, after);
  }
  if (fs.existsSync(target)) {
    assert.equal(fs.readFileSync(target, 'utf8'), content, `Existing file differs: ${targetName}`);
  } else {
    fs.writeFileSync(target, content, {flag: 'wx'});
  }
  return targetName;
}
const wrapper = create('Run-VisibleExplore.ps1', 'Run-DisplayExplore.ps1', [
  ['guest-visible-explore-attempt', 'guest-display-explore-attempt', 1],
  ['VisibleExplore', 'DisplayExplore', 1],
]);
const launcher = create('Launch-VisibleExplore.ps1', 'Launch-DisplayExplore.ps1', [
  ['guest-visible-explore-attempt', 'guest-display-explore-attempt', 1],
  ['visible-explore-attempt1', 'display-explore-attempt1', 1],
  ['VisibleExplore', 'DisplayExplore', 4],
  ['soui-v02-current-win7-visible-explore', 'soui-v02-current-win7-display-explore', 1],
  ['eab0500f4bf79d6204529549f924f9cb59bf573ab2b5c6e6ab30e55a8a1d0bdd', freezeSha, 1],
]);
const monitor = create('Monitor-VisibleExplore.cjs', 'Monitor-DisplayExplore.cjs', [
  ['run-visible-explore.cmd', 'run-display-explore.cmd', 1],
]);
console.log(JSON.stringify({status: 'display-launch-prepared', files: [wrapper, launcher, monitor], freezeSha}));
