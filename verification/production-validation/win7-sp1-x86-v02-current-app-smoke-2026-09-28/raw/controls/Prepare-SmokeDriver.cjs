'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
function replaceOnce(source, before, after) {
  assert.equal(source.split(before).length, 2, `Expected exactly one ${before}`);
  return source.replace(before, after);
}
let launch = fs.readFileSync(path.join(root, 'Launch-Preflight.ps1'), 'utf8');
launch = replaceOnce(launch, "[string]$Attempt='guest-preflight-attempt1'", "[string]$Attempt='guest-app-smoke-attempt1'");
launch = replaceOnce(launch, "Join-Path $PSScriptRoot 'attempt1'", "Join-Path $PSScriptRoot 'app-smoke-attempt1'");
launch = launch.replaceAll('Monitor-Preflight.cjs', 'Monitor-CurrentSmoke.cjs');
launch = launch.replaceAll('Verify-Preflight.cjs', 'Verify-CurrentSmoke.cjs');
launch = replaceOnce(launch, 'b5137aa787b1885e2d11f7152292de15421ec8f0dfe1da5b72240041b794b4f0',
  '6b11fc16ae74e00e528d14ab7aa301b49034348b8f8446a192c32b28c57445a3');
launch = replaceOnce(launch, 'soui-v02-current-win7-preflight', 'soui-v02-current-win7-app-smoke');
fs.writeFileSync(path.join(root, 'Launch-CurrentSmoke.ps1'), launch, {flag: 'wx'});
let monitor = fs.readFileSync(path.join(root, 'Monitor-Preflight.cjs'), 'utf8');
monitor = replaceOnce(monitor, 'run-preflight.cmd', 'run-current-smoke.cmd');
fs.writeFileSync(path.join(root, 'Monitor-CurrentSmoke.cjs'), monitor, {flag: 'wx'});
let wrapper = fs.readFileSync(path.join(root, 'Run-Guest.ps1'), 'utf8');
wrapper = replaceOnce(wrapper, '^guest-preflight-attempt[0-9]+$', '^guest-app-smoke-attempt[0-9]+$');
wrapper = replaceOnce(wrapper, 'Launch-Preflight.ps1', 'Launch-CurrentSmoke.ps1');
fs.writeFileSync(path.join(root, 'Run-CurrentSmoke.ps1'), wrapper, {flag: 'wx'});
console.log(JSON.stringify({status: 'smoke-driver-prepared', inputFrozen: true, guestExecuted: false}));
