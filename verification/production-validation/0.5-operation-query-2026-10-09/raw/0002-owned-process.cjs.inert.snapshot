'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');

const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function parseRecords(bytes) {
  const complete = bytes.slice(0, bytes.lastIndexOf('\n') + 1);
  return complete.split('\n').filter(Boolean).map(line => JSON.parse(line));
}
function records(file) {
  return fs.existsSync(file) ? parseRecords(fs.readFileSync(file, 'utf8')) : [];
}
function identity(rows) {
  const start = rows[0];
  if (!start || start.event !== 'start') return null;
  if (start.schema !== 'soui.owned-process-run/v1' ||
      !Number.isSafeInteger(start.pid) || start.pid <= 0 ||
      typeof start.creationFileTime !== 'string' || !/^[1-9]\d*$/.test(start.creationFileTime) ||
      start.jobAssignedBeforeResume !== true || start.breakawayAllowed !== false)
    throw new Error('Invalid owned process start identity');
  return {pid: start.pid, creationFileTime: start.creationFileTime};
}

function launchOwned(executable, args, {runner, cwd, report, timeoutMs, drainMs = 2000,
  env = process.env, stdout = 'ignore', stderr = 'ignore'} = {}) {
  if (process.platform !== 'win32') throw new Error('Owned process supervision requires Windows');
  for (const [name, value] of Object.entries({executable, runner, cwd, report}))
    if (typeof value !== 'string' || !path.isAbsolute(value)) throw new Error(`${name} must be absolute`);
  for (const [name, value, min, max] of [
    ['timeoutMs', timeoutMs, 1, 172800000], ['drainMs', drainMs, 0, 60000]])
    if (!Number.isSafeInteger(value) || value < min || value > max) throw new Error(`Invalid ${name}`);
  if (!Array.isArray(args) || args.some(arg => typeof arg !== 'string')) throw new Error('Arguments must be strings');
  if (fs.existsSync(report)) throw new Error('Owned report already exists');
  const runnerSha256 = sha(runner);
  const executableSha256 = sha(executable);
  const context = {schema: 'soui.owned-process-context/v1', runnerSha256, executableSha256,
    executable, args, cwd, timeoutMs, drainMs};
  fs.writeFileSync(report + '.context.json', JSON.stringify(context, null, 2) + '\n', {flag: 'wx'});
  const child = spawn(runner, ['--timeout-ms', String(timeoutMs), '--drain-ms', String(drainMs),
    '--stop-on-stdin', '--report', report, '--cwd', cwd, '--', executable, ...args],
    {cwd, env, windowsHide: true, stdio: ['pipe', stdout, stderr]});
  let root = null;
  let readySettled = false;
  let settleReady;
  let rejectReady;
  const ready = new Promise((resolve, reject) => { settleReady = resolve; rejectReady = reject; });
  // A later caller may await ready after setup; retain rejection without an
  // unhandled promise, while preserving its failure for that caller.
  void ready.catch(() => {});
  let stopRequested = false;
  let spawnError = null;
  let observationError = null;
  let fallbackUsed = false;
  child.stdin.on('error', error => { spawnError ??= 'Stop control: ' + error.message; });
  function stop() {
    if (stopRequested || child.exitCode !== null || child.signalCode !== null) return false;
    stopRequested = true;
    if (!child.stdin.destroyed && child.stdin.writable) child.stdin.end('STOP\n');
    return true;
  }
  const observeStart = () => {
    if (readySettled) return;
    try {
      root = identity(records(report));
      if (root) { readySettled = true; clearInterval(poll); settleReady({...root}); }
    } catch (error) {
      observationError = error.message;
      readySettled = true; clearInterval(poll); rejectReady(error); stop();
    }
  };
  const poll = setInterval(observeStart, 20);
  const hardTimer = setTimeout(() => {
    fallbackUsed = true;
    // Killing only this held supervisor closes its non-inherited Job handle.
    // Without the native terminal record, closure remains unverified.
    child.kill();
  }, timeoutMs + drainMs + 15000);
  const finished = new Promise(resolve => {
    child.once('error', error => { spawnError = error.message; });
    child.once('close', (supervisorExitCode, signal) => {
      clearInterval(poll); clearTimeout(hardTimer);
      let rows = [];
      let reportError = observationError;
      let terminal = null;
      let closureConfirmed = false;
      let passed = false;
      let reportSha256 = null;
      try {
        const reportBytes = fs.readFileSync(report);
        const reportText = reportBytes.toString('utf8');
        reportSha256 = crypto.createHash('sha256').update(reportBytes).digest('hex');
        rows = parseRecords(reportText);
        const finalRoot = identity(rows);
        if (root && (!finalRoot || root.pid !== finalRoot.pid ||
            root.creationFileTime !== finalRoot.creationFileTime))
          throw new Error('Owned process identity differs from the published subject');
        root ??= finalRoot;
        terminal = rows.at(-1);
        if (terminal?.event !== 'terminal') throw new Error('Missing native Job terminal record');
        if (reportText.slice(-1) !== '\n') throw new Error('Truncated owned report');
        if (sha(runner) !== runnerSha256 || sha(executable) !== executableSha256)
          throw new Error('Owned tool or subject changed during execution');
        closureConfirmed = root !== null && terminal.closureConfirmed === true &&
          terminal.activeProcesses === 0 && Number.isSafeInteger(terminal.totalProcesses) &&
          terminal.totalProcesses >= 1 && !fallbackUsed;
        passed = !spawnError && !reportError && !signal && supervisorExitCode === 0 && !stopRequested &&
          terminal.reason === 'completed' && terminal.rootExited === true &&
          terminal.rootExitCode === 0 && terminal.passed === true && closureConfirmed;
      } catch (error) { reportError = error.message; }
      if (!readySettled) {
        readySettled = true;
        if (root) settleReady({...root});
        else rejectReady(new Error(reportError || terminal?.reason || spawnError || 'Owned process did not start'));
      }
      resolve({pid: root?.pid ?? null, creationFileTime: root?.creationFileTime ?? null,
        exitCode: terminal?.rootExitCode ?? null, supervisorExitCode, signal,
        spawnError, reportError, stopRequested, fallbackUsed, closureConfirmed, passed,
        terminal, report, reportSha256,
        runnerSha256, executableSha256});
    });
  });
  return {child, ready, finished, stop, get pid() { return root?.pid ?? null; }};
}

module.exports = {launchOwned};
