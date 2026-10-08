'use strict';
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const {pipeline} = require('node:stream/promises');

const [name, limitText, program, ...args] = process.argv.slice(2);
const limit = Number(limitText);
if (!/^[a-z0-9-]+$/.test(name || '') || !Number.isSafeInteger(limit) ||
    limit < 1000 || limit > 1200000 || !program)
  throw Error('Usage: Capture-IsolatedBuild.cjs NAME TIMEOUT_MS PROGRAM ARGS...');
const root = path.join(__dirname, 'command-logs', name);
fs.mkdirSync(path.dirname(root), {recursive: true});
fs.mkdirSync(root);
const out = path.join(root, 'stdout.log');
const err = path.join(root, 'stderr.log');
const gates = path.join(root, 'external-gates.ndjson');
const record = {program, args, cwd: __dirname, startedAt: new Date().toISOString(), pid: null,
  exitCode: null, signal: null, timedOut: false, interruptedByExternal: null, gateError: null, error: null};
const child = cp.spawn(program, args, {cwd: __dirname, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']});
record.pid = child.pid ?? null;
const fail = error => {record.error ??= String(error.stack || error);};
child.on('error', fail);
const copies = [pipeline(child.stdout, fs.createWriteStream(out, {flags: 'wx'})).catch(fail),
  pipeline(child.stderr, fs.createWriteStream(err, {flags: 'wx'})).catch(fail)];
fs.writeFileSync(gates, '', {flag: 'wx'});
let stopRequested = false;
function stopOwnTree(reason) {
  if (stopRequested || child.exitCode !== null || child.signalCode !== null || !child.pid) return;
  stopRequested = true;
  const killed = cp.spawnSync(path.join(process.env.SystemRoot, 'System32/taskkill.exe'),
    ['/PID', String(child.pid), '/T', '/F'], {windowsHide: true, timeout: 15000});
  record.cleanupExitCode = killed.status;
  record.cleanupReason = reason;
  if (killed.error) fail(killed.error);
}
const query = "$ErrorActionPreference='Stop'; $p=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|cdb)\\.exe$'} | Select-Object ProcessId,Name,CreationDate); [ordered]@{processes=$p}|ConvertTo-Json -Depth 5 -Compress";
function poll() {
  if (stopRequested || child.exitCode !== null || child.signalCode !== null) return;
  const scan = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', query],
    {encoding: 'utf8', windowsHide: true, timeout: 15000,
      env: {...process.env, PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')}});
  if (scan.error || scan.status !== 0 || scan.stderr.trim()) {
    record.gateError = String(scan.error || scan.stderr || `Process query exited ${scan.status}`);
    fs.appendFileSync(gates, JSON.stringify({at: new Date().toISOString(), error: record.gateError}) + '\n');
    stopOwnTree('gate-query-error');
    return;
  }
  try {
    const state = JSON.parse(scan.stdout);
    const processes = Array.isArray(state.processes) ? state.processes :
      state.processes ? [state.processes] : [];
    fs.appendFileSync(gates, JSON.stringify({at: new Date().toISOString(), processes}) + '\n');
    if (processes.length) {
      record.interruptedByExternal = processes;
      stopOwnTree('external-workload');
    }
  } catch (error) {
    record.gateError = String(error);
    fs.appendFileSync(gates, JSON.stringify({at: new Date().toISOString(), error: record.gateError}) + '\n');
    stopOwnTree('gate-json-error');
  }
}
const watch = setInterval(poll, 5000);
const deadline = setTimeout(() => {record.timedOut = true; stopOwnTree('timeout');}, limit);
child.on('close', async (code, signal) => {
  clearInterval(watch);
  clearTimeout(deadline);
  record.exitCode = code;
  record.signal = signal;
  await Promise.all(copies);
  record.finishedAt = new Date().toISOString();
  for (const [key, file] of [['stdout', out], ['stderr', err], ['gates', gates]]) {
    try {
      const fd = fs.openSync(file, 'r+');
      try {fs.fsyncSync(fd);} finally {fs.closeSync(fd);}
      const bytes = fs.readFileSync(file);
      record[key] = {bytes: bytes.length, sha256: crypto.createHash('sha256').update(bytes).digest('hex')};
    } catch (error) {fail(error);}
  }
  fs.writeFileSync(path.join(root, 'result.json'), JSON.stringify(record, null, 2) + '\n', {flag: 'wx'});
  console.log(JSON.stringify({name, root, exitCode: code, timedOut: record.timedOut,
    interruptedByExternal: Boolean(record.interruptedByExternal), gateError: record.gateError, error: record.error}));
  process.exitCode = code === 0 && !record.timedOut && !record.interruptedByExternal &&
    !record.gateError && !record.error ? 0 : 1;
});
