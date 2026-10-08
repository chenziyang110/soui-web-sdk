'use strict';

const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = path.join(__dirname, 'queued-smoke-attempt1');
fs.mkdirSync(root);
const started = Date.now();
const waitLimitMs = 20 * 60 * 1000;
const pollMs = 5000;
const idleRequiredMs = 10000;
const runLimitMs = 190000;
const script = "$ErrorActionPreference='Stop';$p=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$'} | Select-Object ProcessId,Name,CreationDate);[ordered]@{processes=$p}|ConvertTo-Json -Depth 4 -Compress";
const env = {...process.env, PSModulePath: path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/Modules')};
const record = {schema: 'soui.v02.queued-smoke/v1', startedAt: new Date(started).toISOString(),
  sourceSha256: '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d',
  architectures: [], status: 'waiting', releaseReady: false};
const trace = fs.openSync(path.join(root, 'gates.ndjson'), 'wx');
function persist() {
  fs.writeFileSync(path.join(root, 'result.json'), JSON.stringify({...record, updatedAt: new Date().toISOString()}, null, 2) + '\n');
}
function gate() {
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
      {env, encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const value = JSON.parse(result.stdout);
  const busy = Array.isArray(value.processes) ? value.processes : value.processes ? [value.processes] : [];
  const sample = {at: new Date().toISOString(), busy};
  fs.writeSync(trace, JSON.stringify(sample) + '\n');
  fs.fsyncSync(trace);
  return busy;
}
function delay(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }
async function waitForIdle() {
  let idleSince = null;
  while (Date.now() - started < waitLimitMs) {
    const busy = gate();
    if (busy.length) idleSince = null;
    else if (idleSince === null) idleSince = Date.now();
    else if (Date.now() - idleSince >= idleRequiredMs) return true;
    await delay(pollMs);
  }
  return false;
}
function run(architecture) {
  return new Promise(resolve => {
    const output = fs.openSync(path.join(root, `${architecture}.stdout.log`), 'wx');
    const error = fs.openSync(path.join(root, `${architecture}.stderr.log`), 'wx');
    const child = cp.spawn(process.execPath, [path.join(__dirname, 'Run-CurrentApps.cjs'), architecture, 'apps'],
      {cwd: __dirname, env, windowsHide: true, stdio: ['ignore', output, error]});
    const item = {architecture, pid: child.pid ?? null, startedAt: new Date().toISOString(),
      exitCode: null, signal: null, timedOut: false, error: null};
    record.architectures.push(item);
    persist();
    child.once('error', e => { item.error = String(e); });
    const timer = setTimeout(() => {
      item.timedOut = true;
      if (child.pid) {
        const killed = cp.spawnSync('taskkill.exe', ['/PID', String(child.pid), '/T', '/F'],
          {windowsHide: true, timeout: 15000});
        item.cleanupExitCode = killed.status;
        if (killed.error) item.error = String(killed.error);
      }
    }, runLimitMs);
    child.once('close', (code, signal) => {
      clearTimeout(timer);
      fs.closeSync(output);
      fs.closeSync(error);
      item.exitCode = code;
      item.signal = signal;
      item.finishedAt = new Date().toISOString();
      persist();
      resolve(item);
    });
  });
}
async function main() {
  persist();
  try {
    if (!(await waitForIdle())) { record.status = 'busy-timeout-no-launch'; process.exitCode = 2; return; }
    for (const architecture of ['x86', 'x64']) {
      // Recheck immediately before each architecture. Run-CurrentApps also
      // gates before launch and every five seconds, excluding its own tree.
      if (gate().length) { record.status = 'external-busy-before-launch'; process.exitCode = 2; return; }
      record.status = `running-${architecture}`;
      persist();
      const result = await run(architecture);
      if (result.exitCode !== 0 || result.timedOut || result.error) {
        record.status = `failed-${architecture}`;
        process.exitCode = 1;
        return;
      }
    }
    record.status = 'completed-two-architecture-smoke';
  } catch (error) {
    record.status = 'gate-or-driver-error';
    record.error = String(error.stack || error);
    process.exitCode = 1;
  } finally {
    record.finishedAt = new Date().toISOString();
    persist();
    fs.closeSync(trace);
    console.log(JSON.stringify({status: record.status, architectures: record.architectures.map(({architecture, exitCode}) => ({architecture, exitCode}))}));
  }
}
void main();
