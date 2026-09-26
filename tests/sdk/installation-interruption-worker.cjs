'use strict';
const fs = require('node:fs');
const path = require('node:path');
const [installer, project, packageRoot, command, boundary, report] = process.argv.slice(2);
const root = path.resolve(project);
const target = name => path.join(root, name);
let stopped = false;
function pause(kind, name) {
  if (stopped || kind !== boundary) return;
  stopped = true;
  const fd = fs.openSync(report, 'wx');
  try { fs.writeFileSync(fd, JSON.stringify({boundary, name, pid: process.pid}) + '\n'); fs.fsyncSync(fd); }
  finally { fs.closeSync(fd); }
  // Parent really terminates this process; no exception can enter installer catch.
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0);
}
const rename = fs.renameSync;
fs.renameSync = function (from, to) {
  const result = rename.apply(this, arguments);
  const destination = path.resolve(to);
  if (destination === target('.soui-sdk-transaction')) pause('prepare', destination);
  if (destination === target('.soui-sdk-transaction/journal.json')) pause('journal', destination);
  if (destination === target('.soui-sdk-transaction/claims/00000001')) pause('claim', destination);
  if (destination === target('sdk/shared.txt')) pause('replace', destination);
  if (destination === target('sdk/added.txt')) pause('add', destination);
  if (destination === target('sdk-lock.json')) pause('commit', destination);
  if (path.dirname(destination) === root && path.basename(destination).startsWith('.soui-sdk-retired-')) pause('cleanup', destination);
  return result;
};
const unlink = fs.unlinkSync;
fs.unlinkSync = function (file) {
  const result = unlink.apply(this, arguments);
  if (path.resolve(file) === target('sdk/removed.txt')) pause('delete', path.resolve(file));
  return result;
};
const sdk = require(installer);
if (command === 'install') sdk.install(packageRoot, project);
else sdk.recover(project);
throw new Error('Expected interruption boundary was not reached');
