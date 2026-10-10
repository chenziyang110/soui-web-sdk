'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');

function sha256(file) {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha256');
    const input = fs.createReadStream(file);
    input.on('data', bytes => hash.update(bytes));
    input.once('error', reject);
    input.once('end', () => resolve(hash.digest('hex')));
  });
}

function appendedTail(file, observedBytes) {
  if (!fs.existsSync(file)) return {fileBytes: 0, unreadBytes: 0, tail: ''};
  const size = fs.statSync(file).size;
  const start = Math.max(0, size - 8192);
  const bytes = Buffer.alloc(size - start);
  const input = fs.openSync(file, 'r');
  let read = 0;
  try {
    while (read < bytes.length) {
      const count = fs.readSync(input, bytes, read, bytes.length - read, start + read);
      if (!count) break;
      read += count;
    }
  }
  finally { fs.closeSync(input); }
  return {fileBytes: size, unreadBytes: Math.max(0, size - observedBytes), tail: bytes.subarray(0, read).toString('utf8')};
}

async function debuggerCapture(pid, directory, executable) {
  if (!executable) return {status: 'disabled'};
  if (!Number.isInteger(pid) || pid <= 0 || !path.isAbsolute(executable) || !fs.statSync(executable).isFile())
    return {status: 'invalid-debugger'};
  if (/[";\r\n]/.test(directory)) return {status: 'invalid-output-path'};
  fs.mkdirSync(directory, {recursive: true});
  const dump = path.join(directory, 'timeout.dmp');
  const log = path.join(directory, 'cdb.log');
  if (fs.existsSync(dump) || fs.existsSync(log)) return {status: 'existing-diagnostic'};
  const command = `.dump /ma "${dump}"; ~* k; qd`;
  const args = ['-y', process.env.SystemRoot || 'C:\\Windows', '-p', String(pid), '-logo', log, '-c', command];
  const result = await new Promise(resolve => {
    const debuggerProcess = spawn(executable, args, {windowsHide: true, stdio: 'ignore'});
    let error = null;
    let expired = false;
    const timer = setTimeout(() => { expired = true; debuggerProcess.kill(); }, 20000);
    debuggerProcess.once('error', failure => { error = failure.message; });
    debuggerProcess.once('close', (exitCode, signal) => {
      clearTimeout(timer);
      resolve({exitCode, signal, expired, error});
    });
  });
  const files = {};
  for (const [name, file] of [['dump', dump], ['log', log]]) {
    if (fs.existsSync(file)) files[name] = {bytes: fs.statSync(file).size, sha256: await sha256(file)};
  }
  const logText = fs.existsSync(log) ? fs.readFileSync(log, 'utf8') : '';
  return {status: !result.expired && result.exitCode === 0 &&
    files.dump?.bytes > 1024 && logText.includes('Dump successfully written') && logText.includes('Id:')
    ? 'captured' : 'failed', ...result, files};
}

async function captureTimeout({pid, output, nativeLog, snapshot, debuggerExe, dumpRoot}) {
  const observation = {...snapshot, ...appendedTail(nativeLog, snapshot.observedBytes)};
  const observationFile = path.join(output, 'timeout-observation.json');
  fs.writeFileSync(observationFile, JSON.stringify(observation, null, 2) + '\n', {flag: 'wx'});
  const observationSha256 = await sha256(observationFile);
  if (!debuggerExe) return {observationSha256, debugger: {status: 'disabled'}};
  if (!dumpRoot || !path.isAbsolute(dumpRoot))
    return {observationSha256, debugger: {status: 'invalid-dump-root'}};
  try {
    return {observationSha256, debugger: await debuggerCapture(pid, path.join(dumpRoot, snapshot.runId), debuggerExe)};
  } catch (error) {
    return {observationSha256, debugger: {status: 'failed', error: error.message}};
  }
}

module.exports = {captureTimeout};
