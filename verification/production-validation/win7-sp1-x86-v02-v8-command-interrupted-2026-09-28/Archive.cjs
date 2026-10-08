'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const root = __dirname;
const external = 'C:\\SOUI-SDK-production-validation\\preview15-v02-win7-current-20260927';
const commandLogs = 'C:\\SOUI-SDK-production-validation\\preview15-v02-interaction-20260927\\command-logs';
const attempt = 'guest-layout-interaction-v8-attempt2';
const manifestPath = path.join(root, 'raw-manifest.json');
const attemptFiles = [
  'command-send.json', 'desktop-before-command.png', 'desktop-before-command.ppm',
  'frozen-check-before.log', 'frozen-check-after.log', 'gate.json',
  'gate-before-launch.json', 'gates-during.json', 'launch-script.ps1',
  'launch.json', 'monitor-script.cjs', 'monitor-run-1790587877011.log',
  'monitor-screen-1790587795650.log', 'qemu-result.json',
  'qemu.stderr.log', 'qemu.stdout.log', 'serial-export.txt',
];
const files = [
  ...attemptFiles.map(name => ({
    original: path.join(external, attempt, name),
    archived: path.join('raw', 'guest', name),
  })),
  ...['result.json', 'stdout.log', 'stderr.log'].map(name => ({
    original: path.join(commandLogs, `${attempt}-driver`, name),
    archived: path.join('raw', 'driver', name),
  })),
];

function sha256(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}
function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
}
function verify() {
  const manifest = readJson(manifestPath);
  if (manifest.schema !== 'soui.win7-v02-v8-command-interrupted-archive/v1' ||
      manifest.files.length !== files.length) throw new Error('Manifest schema or count mismatch');
  for (let i = 0; i < files.length; i++) {
    const entry = manifest.files[i];
    if (entry.path !== files[i].archived.replaceAll('\\', '/') ||
        entry.original !== files[i].original) throw new Error('Manifest selection changed');
    const archived = path.resolve(root, entry.path);
    if (!archived.startsWith(path.join(root, 'raw') + path.sep)) throw new Error('Archive path escaped');
    if (fs.statSync(archived).size !== entry.bytes || sha256(archived) !== entry.sha256) {
      throw new Error(`Archive bytes differ: ${entry.path}`);
    }
    if (fs.existsSync(entry.original) && sha256(entry.original) !== entry.sha256) {
      throw new Error(`Original bytes differ: ${entry.original}`);
    }
  }
  const driver = readJson(path.join(root, 'raw/driver/result.json'));
  const launch = readJson(path.join(root, 'raw/guest/launch.json'));
  const sent = readJson(path.join(root, 'raw/guest/command-send.json'));
  const qemu = readJson(path.join(root, 'raw/guest/qemu-result.json'));
  const gates = readJson(path.join(root, 'raw/guest/gates-during.json'));
  const monitor = fs.readFileSync(path.join(root, 'raw/guest/monitor-run-1790587877011.log'), 'utf8');
  const serial = fs.statSync(path.join(root, 'raw/guest/serial-export.txt'));
  const freezeFile = path.join(root, '../win7-sp1-x86-v02-v8-prelaunch-refusal-2026-09-28/raw/v8/layout-interaction-v8-attempt1/freeze.json');
  if (driver.exitCode !== 1 || driver.timedOut || launch.pid !== 79304 ||
      sent.pid !== launch.pid ||
      sent.command !== 'cmd.exe /c D:\\soui-input\\run-display-explore.cmd' ||
      qemu.pid !== launch.pid || qemu.exitCode !== -1 || !qemu.externalLoadObserved ||
      !qemu.diskUnchanged || qemu.frozenCheckExitCode !== 0 ||
      qemu.diskSha256Before !== qemu.diskSha256After || serial.size !== 0 ||
      !monitor.includes('sendkey meta_l-r') || !monitor.includes('sendkey ret') ||
      gates.length < 2 || gates.at(-2).processes.length !== 0 ||
      !gates.at(-1).processes.some(p => p.ProcessId === 45436 && p.Name === 'qemu-system-x86_64.exe') ||
      sha256(freezeFile) !== '764452636fd31dd5cac25611882f67fe578a7fe8357e36ce9d79a9826166502b') {
    throw new Error('Interrupted guest evidence does not match frozen attempt');
  }
  process.stdout.write(`verified ${files.length} raw files; command keys sent, guest result unavailable\n`);
}
function copy() {
  if (fs.existsSync(manifestPath)) throw new Error('Archive exists; refusing overwrite');
  for (const item of files) {
    if (!fs.existsSync(item.original)) throw new Error(`Missing original: ${item.original}`);
    if (fs.existsSync(path.join(root, item.archived))) throw new Error(`Archive destination exists: ${item.archived}`);
  }
  const entries = files.map(item => {
    const dest = path.join(root, item.archived);
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.copyFileSync(item.original, dest, fs.constants.COPYFILE_EXCL);
    return {path: item.archived.replaceAll('\\', '/'), original: item.original,
      bytes: fs.statSync(dest).size, sha256: sha256(dest)};
  });
  fs.writeFileSync(manifestPath, JSON.stringify({
    schema: 'soui.win7-v02-v8-command-interrupted-archive/v1',
    guestStatus: 'command-keys-sent-before-external-load; no-serial-result',
    win7CurrentV02Verified: false, benchmarkValid: false, releaseReady: false,
    files: entries,
  }, null, 2) + '\n', {flag: 'wx'});
  verify();
}
if (process.argv[2] === 'copy') copy();
else if (process.argv[2] === 'verify') verify();
else throw new Error('Usage: node Archive.cjs copy|verify');
