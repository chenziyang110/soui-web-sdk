'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const cp = require('node:child_process');

const root = __dirname;
const input = JSON.parse(fs.readFileSync(path.join(root, 'input.json'), 'utf8'));
const work = path.join(os.tmpdir(), `soui-preview8-isolated-source-serial-selftest-${crypto.randomUUID()}`);
fs.mkdirSync(work);
fs.copyFileSync(path.join(root, 'input.json'), path.join(work, 'input.json'));
fs.copyFileSync(path.join(root, 'Verify-AppSerial.ps1'), path.join(work, 'Verify-AppSerial.ps1'));
const values = new Map();
const put = (name, value) => values.set(name, Buffer.from(value, 'utf8'));
const cert = value => `${value.match(/../g).join(' ')}\r\n`;
put('7za-sha256.txt', cert(input.sevenZipSha256));
put('starter-zip-sha256.txt', cert(input.packages.SouiStarter.zipSha256));
put('workbench-zip-sha256.txt', cert(input.packages.SouiWorkbench.zipSha256));
put('starter-exe-sha256.txt', cert(input.packages.SouiStarter.exeSha256));
put('workbench-exe-sha256.txt', cert(input.packages.SouiWorkbench.exeSha256));
put('engine-sha256.txt', cert(input.packages.SouiWorkbench.engineSha256));
put('sdk-sha256.txt', cert(input.packages.SouiWorkbench.souiDllSha256));
put('os-version.txt', 'Version=6.1.7601\r\nServicePackMajorVersion=1\r\nOSArchitecture=32-bit\r\n');
for (const name of ['starter-zip-test-exitcode.txt', 'workbench-zip-test-exitcode.txt',
  'starter-extract-exitcode.txt', 'workbench-extract-exitcode.txt',
  'verify-apps-exitcode.txt', 'starter-exitcode.txt',
  'workbench-default-exitcode.txt', 'workbench-react-exitcode.txt']) put(name, '0\r\n');
for (const name of ['starter-zip-test.log', 'workbench-zip-test.log',
  'starter-extract.log', 'workbench-extract.log']) put(name, 'Everything is Ok\r\n');
put('verify-apps.log', 'SouiStarter=71\r\nSouiWorkbench=475\r\nChinese fixture names preserved\r\nLegal files=5x2,hashes=matched\r\n');
put('starter-smoke.log', 'process-start\r\napplication-ready\r\nhost-close-complete\r\n');
for (const name of ['workbench-default-smoke.log', 'workbench-react-smoke.log']) {
  put(name, 'process-start\r\napplication-ready\r\ndocument=ready,bridge=ready,probe=ok\r\nwindow-destroyed\r\n');
}
if (values.size !== 24) throw new Error(`Expected 24 result files; got ${values.size}`);
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex').toUpperCase();
function serial(entries) {
  const lines = ['SOUI_SERIAL_V1'];
  for (const [name, bytes] of [...entries].sort(([a], [b]) => a.localeCompare(b))) {
    lines.push(`FILE|${name}|${bytes.length}|${sha256(bytes)}`, bytes.toString('base64'));
  }
  lines.push('SOUI_SERIAL_END');
  return lines.join('\n') + '\n';
}
function verify(file, expectedExit) {
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', path.join(work, 'Verify-AppSerial.ps1'), '-InputPath', file],
  {cwd: work, encoding: 'utf8', windowsHide: true});
  if ((result.status === 0) !== (expectedExit === 0)) {
    throw new Error(`Unexpected verifier result ${result.status}: ${result.stdout}\n${result.stderr}`);
  }
  return result;
}
const good = path.join(work, 'good-serial.txt');
fs.writeFileSync(good, serial(values), {flag: 'wx'});
verify(good, 0);
const broken = new Map(values);
broken.set('engine-sha256.txt', Buffer.from(cert('00'.repeat(32)), 'utf8'));
const bad = path.join(work, 'bad-serial.txt');
fs.writeFileSync(bad, serial(broken), {flag: 'wx'});
verify(bad, 1);
process.stdout.write(JSON.stringify({status: 'self-test-passed', goodFiles: values.size,
  badCase: 'wrong engine hash rejected', work, releaseReady: false}) + '\n');
