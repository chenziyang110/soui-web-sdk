'use strict';

// Runs paired scenes in alternating order. A clean preflight is still not a
// physical-presentation, idle, startup, or product-workload verdict.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn, execFileSync} = require('node:child_process');
const {compare, loadRun} = require('./check-pair-runs.cjs');

const prohibited = new Set([
  'qemu-system-i386.exe', 'qemu-system-x86_64.exe',
  'souiacceptance.exe', 'souibenchmark.exe', 'electron.exe',
]);

function requireFile(value, label) {
  if (!path.isAbsolute(value) || !fs.statSync(value).isFile())
    throw new Error(`${label} must be an existing absolute file`);
  return fs.realpathSync(value);
}

function requireDirectory(value, label) {
  if (!path.isAbsolute(value) || !fs.statSync(value).isDirectory())
    throw new Error(`${label} must be an existing absolute directory`);
  return fs.realpathSync(value);
}

function bundleDigest(root) {
  const entries = [];
  const walk = (directory, prefix = '') => {
    for (const item of fs.readdirSync(directory, {withFileTypes: true})) {
      const relative = prefix ? `${prefix}/${item.name}` : item.name;
      const file = path.join(directory, item.name);
      if (item.isDirectory()) walk(file, relative);
      else if (item.isFile()) entries.push({path: relative,
        bytes: fs.statSync(file).size,
        sha256: crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')});
      else throw new Error(`Unsupported page bundle entry: ${relative}`);
    }
  };
  walk(root);
  entries.sort((left, right) => left.path.localeCompare(right.path));
  return crypto.createHash('sha256').update(JSON.stringify(entries)).digest('hex');
}

function competingProcesses() {
  if (process.platform !== 'win32') throw new Error('The series runner requires Windows');
  const output = execFileSync('tasklist', ['/fo', 'csv', '/nh'], {
    encoding: 'utf8', windowsHide: true, maxBuffer: 4 * 1024 * 1024,
  });
  return output.split(/\r?\n/).map(row => {
    const fields = /^"([^"]+)","(\d+)"/.exec(row);
    return fields ? {name: fields[1].toLowerCase(), pid: Number(fields[2])} : null;
  }).filter(process => process && prohibited.has(process.name));
}

function interferenceCheck() {
  return {checkedAt: new Date().toISOString(), competingProcesses: competingProcesses()};
}

function assertIdle(check) {
  if (check.competingProcesses.length)
    throw new Error(`Competing process is running: ${check.competingProcesses
      .map(process => `${process.name} (PID ${process.pid})`).join(', ')}`);
}

function runOne(args) {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, [path.join(__dirname, 'run-one.cjs'), ...args], {
      stdio: 'inherit', windowsHide: false,
    });
    child.once('error', reject);
    child.once('exit', (code, signal) => {
      if (code === 0) resolve();
      else reject(new Error(`Single run failed: code=${code}, signal=${signal}`));
    });
  });
}

function frozenIdentity(run) {
  const record = run.record;
  return {machine: record.machine, pageSha256: record.pageSha256,
    pageFiles: record.pageFiles, hostExeSha256: record.hostExeSha256,
    dependency: record.dependency, identity: record.identity, tools: record.tools};
}

async function main() {
  const [scene, pageArgument, souiArgument, nodeArgument, electronArgument,
    appArgument, samplerArgument, inputArgument, outputArgument,
    libraryArgument, lockArgument, roundsArgument] = process.argv.slice(2);
  if (![13, 14].includes(process.argv.length) ||
      !['S01', 'S02', 'S04', 'S05'].includes(scene))
    throw new Error('Usage: node run-series.cjs S01|S02|S04|S05 ABS_PAGE_DIR '
      + 'ABS_SOUI_EXE ABS_NODE_DLL ABS_ELECTRON_EXE ABS_ELECTRON_APP_DIR '
      + 'ABS_SAMPLER_EXE ABS_INPUT_EXE ABS_NEW_OUTPUT_DIR ABS_SDK_LIB '
      + 'ABS_ELECTRON_LOCKFILE [ROUNDS=5]');
  const rounds = roundsArgument === undefined ? 5 : Number(roundsArgument);
  if (!Number.isSafeInteger(rounds) || rounds < 1 || rounds > 20)
    throw new Error('ROUNDS must be an integer from 1 to 20');
  const page = requireDirectory(pageArgument, 'Shared page');
  const souiExe = requireFile(souiArgument, 'SOUI host');
  const nodeDll = requireFile(nodeArgument, 'Miniblink DLL');
  const electronExe = requireFile(electronArgument, 'Electron host');
  const app = requireDirectory(appArgument, 'Electron app');
  const sampler = requireFile(samplerArgument, 'Process sampler');
  const input = requireFile(inputArgument, 'External input driver');
  const library = requireFile(libraryArgument, 'Installed SDK library');
  const lock = requireFile(lockArgument, 'Electron lockfile');
  if (!path.isAbsolute(outputArgument) || fs.existsSync(outputArgument))
    throw new Error('Output must be a new absolute directory');
  const firstCheck = interferenceCheck();
  assertIdle(firstCheck);
  const root = path.resolve(outputArgument);
  fs.mkdirSync(root, {recursive: true});
  const record = {schema: 'soui.benchmark-series/v1', seriesId: crypto.randomUUID(),
    scene, plannedPairs: rounds, startedAt: new Date().toISOString(),
    order: 'alternating-soui-electron-and-electron-soui', checks: [firstCheck],
    pairs: [], benchmarkValid: false, releaseReady: false};
  try {
    const frozenPage = path.join(root, 'frozen-page');
    fs.cpSync(page, frozenPage, {recursive: true, errorOnExist: true, force: false});
    record.pageBundle = 'frozen-page';
    record.pageBundleSha256 = bundleDigest(frozenPage);
    for (let index = 0; index < rounds; ++index) {
      const order = index % 2 === 0 ? ['soui', 'electron'] : ['electron', 'soui'];
      const pair = {number: index + 1, order, runs: {}};
      record.pairs.push(pair);
      for (const host of order) {
        const before = interferenceCheck();
        record.checks.push(before);
        assertIdle(before);
        const runPath = path.join(root, `pair-${index + 1}-${host}`);
        const args = host === 'soui'
          ? ['soui', scene, frozenPage, souiExe, nodeDll, sampler, input, runPath, library]
          : ['electron', scene, frozenPage, electronExe, app, sampler, input, runPath, lock];
        await runOne(args);
        pair.runs[host] = path.basename(runPath);
        if (bundleDigest(frozenPage) !== record.pageBundleSha256)
          throw new Error('Frozen series page bundle changed during a run');
        const after = interferenceCheck();
        record.checks.push(after);
        assertIdle(after);
      }
      const soui = loadRun(path.join(root, pair.runs.soui), 'soui');
      const electron = loadRun(path.join(root, pair.runs.electron), 'electron');
      const identities = {soui: frozenIdentity(soui), electron: frozenIdentity(electron)};
      if (record.fixedIdentities &&
          JSON.stringify(identities) !== JSON.stringify(record.fixedIdentities))
        throw new Error(`Pair ${index + 1} changed its frozen machine, page, host or tool identity`);
      if (!record.fixedIdentities) record.fixedIdentities = identities;
      const report = compare(soui, electron);
      pair.preflightAccepted = report.preflightAccepted;
      const reportPath = path.join(root, `pair-${index + 1}-preflight.json`);
      fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`, {flag: 'wx'});
      if (!report.preflightAccepted) throw new Error(`Pair ${index + 1} failed preflight`);
    }
    if (bundleDigest(frozenPage) !== record.pageBundleSha256)
      throw new Error('Frozen series page bundle changed before completion');
    record.status = 'preflight-completed';
  } catch (error) {
    record.status = 'stopped';
    record.error = error.message;
    process.exitCode = 1;
  } finally {
    record.finishedAt = new Date().toISOString();
    record.completedPairs = record.pairs.filter(pair => pair.preflightAccepted).length;
    fs.writeFileSync(path.join(root, 'series.json'), `${JSON.stringify(record, null, 2)}\n`,
      {flag: 'wx'});
    process.stdout.write(`${JSON.stringify({root, status: record.status,
      completedPairs: record.completedPairs,
      error: record.error ?? null, benchmarkValid: false, releaseReady: false})}\n`);
  }
}

if (require.main === module)
  main().catch(error => {console.error(error.message); process.exitCode = 1;});

module.exports = {competingProcesses, frozenIdentity};
