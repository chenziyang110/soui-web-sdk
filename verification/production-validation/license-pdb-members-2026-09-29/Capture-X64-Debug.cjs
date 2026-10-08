'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');

const root = __dirname;
const dll = 'C:\\SOUI-SDK-development\\preview14-scheduler\\out\\dependencies\\x64\\miniblink\\bin\\miniblink.dll';
const pdb = path.join(path.dirname(dll), 'miniblink.pdb');
const linkLog = 'C:\\SOUI-SDK-development\\license-observation-full\\verification\\production-validation\\license-link-read-traces-2026-09-28\\raw\\x64\\link.command.1.tlog';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
assert.equal(fs.existsSync(path.join(root, 'x64-dll-debug.stdout.txt')), false);
const dllBytes = fs.readFileSync(dll);
const dllSha256 = sha(dllBytes);
assert.equal(dllSha256, 'fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951');
const command = spawnSync('llvm-readobj.exe', ['--coff-debug-directory', dll],
  {encoding: null, windowsHide: true, maxBuffer: 4 * 1024 * 1024});
assert.equal(command.status, 0, String(command.error || command.stderr));
fs.writeFileSync(path.join(root, 'x64-dll-debug.stdout.txt'), command.stdout, {flag: 'wx'});
fs.writeFileSync(path.join(root, 'x64-dll-debug.stderr.txt'), command.stderr, {flag: 'wx'});
const output = command.stdout.toString('utf8');
assert.match(output, /Format: COFF-x86-64/);
const link = fs.readFileSync(linkLog).toString('utf16le');
const result = {
  schema: 'soui.license-x64-debug-capture/v1',
  dll, dllBytes: dllBytes.length, dllSha256,
  debugExit: command.status,
  rawStdoutBytes: command.stdout.length,
  rawStdoutSha256: sha(command.stdout),
  rawStderrBytes: command.stderr.length,
  rawStderrSha256: sha(command.stderr),
  hasCodeView: /Type: CodeView/.test(output),
  pdbPathFromLinkCommand: /\/PDB:\"([^\"]+)\"/i.exec(link)?.[1] || null,
  linkCommandHasDebugSwitch: /(?:^|\s)\/DEBUG(?::\S+)?(?:\s|$)/i.test(link),
  pdbAtLinkedOutputPathExists: fs.existsSync(pdb),
  linkLogSha256: sha(fs.readFileSync(linkLog)),
  licenseReviewVerified: false, releaseReady: false,
};
fs.writeFileSync(path.join(root, 'x64-debug-capture.json'),
  JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({hasCodeView: result.hasCodeView,
  linkCommandHasDebugSwitch: result.linkCommandHasDebugSwitch,
  pdbAtLinkedOutputPathExists: result.pdbAtLinkedOutputPathExists,
  releaseReady: false}));
