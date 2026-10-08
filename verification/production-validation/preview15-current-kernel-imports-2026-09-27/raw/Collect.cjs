'use strict';
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');

const root = __dirname;
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const dumpbin = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe';
const inputs = [
  { arch: 'x86', file: 'out/dependencies/x86/miniblink/bin/node.dll', expected: '2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3' },
  { arch: 'x64', file: 'out/dependencies/x64/miniblink/bin/miniblink.dll', expected: 'fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951' },
];
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const write = (name, bytes) => fs.writeFileSync(path.join(root, name), bytes, { flag: 'wx' });
const records = [];
for (const input of inputs) {
  const binary = path.join(repo, input.file);
  const binarySha256 = sha(fs.readFileSync(binary));
  if (binarySha256 !== input.expected) throw new Error(`${input.arch} current kernel identity mismatch`);
  const result = cp.spawnSync(dumpbin, ['/dependents', binary], {
    windowsHide: true, timeout: 60000, maxBuffer: 4 * 1024 * 1024,
  });
  const stdout = result.stdout || Buffer.alloc(0);
  const stderr = result.stderr || Buffer.alloc(0);
  write(`${input.arch}.stdout.log`, stdout);
  write(`${input.arch}.stderr.log`, stderr);
  const dependencies = [...stdout.toString('utf8').matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gmi)].map(match => match[1]);
  const record = {
    arch: input.arch, input: input.file, binarySha256, binaryBytes: fs.statSync(binary).size,
    tool: dumpbin, toolSha256: sha(fs.readFileSync(dumpbin)), command: ['/dependents', binary],
    exitCode: result.status, signal: result.signal, error: result.error?.message || null,
    stdoutSha256: sha(stdout), stderrSha256: sha(stderr), dependencies,
  };
  write(`${input.arch}.result.json`, JSON.stringify(record, null, 2) + '\n');
  records.push(record);
  if (result.status !== 0 || result.error) throw new Error(`${input.arch} dumpbin failed; raw output preserved`);
}
write('inventory.json', JSON.stringify({
  schema: 'soui.kernel-direct-dependents/v1', scope: 'Direct PE dependencies of the current exact binaries only; static archive members and redistribution terms are not proven',
  observedAt: new Date().toISOString(), releaseReady: false, binaries: records,
}, null, 2) + '\n');
console.log(JSON.stringify({status: 'observed', binaries: records.map(({arch, binarySha256, dependencies}) => ({arch, binarySha256, dependencies}))}));
