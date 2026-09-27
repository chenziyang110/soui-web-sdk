'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const command = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/guest-display-explore-attempt2-driver';
const read = name => fs.readFileSync(path.join(command, name));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const result = JSON.parse(read('result.json').toString('utf8'));
const stdout = read('stdout.log');
const stderr = read('stderr.log');
assert.equal(result.exitCode, 1);
assert.equal(result.timedOut, false);
assert.equal(result.error, null);
assert.match(stdout.toString('utf8'), /guest-display-explore-attempt2/);
assert.match(stderr.toString('utf8'), /Other desktop or QEMU load is running/);
assert.equal(fs.existsSync(path.join(root, 'guest-display-explore-attempt2')), false);
const review = {
  schema: 'soui.win7-v02-display-prelaunch-refusal-review/v1',
  attempt: 'guest-display-explore-attempt2',
  startedAt: result.startedAt, finishedAt: result.finishedAt,
  driverExitCode: 1, timedOut: false,
  stdoutSha256: sha(stdout), stderrSha256: sha(stderr),
  gateReason: 'Other desktop or QEMU load is running',
  ownQemuStarted: false, guestCommandSent: false, serialBytes: 0,
  displayModeExecuted: false, applicationStarted: false,
  win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(root, 'display-prelaunch-refusal-review.json'),
  JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
