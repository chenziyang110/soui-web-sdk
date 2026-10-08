'use strict';

// Read the captured PE/PDB tool output. A matching CodeView identity and PDB
// compiland list establish linked archive-member evidence for this exact DLL.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const read = name => fs.readFileSync(path.join(root, name));
const text = name => read(name).toString('utf8').replace(/^\uFEFF/, '');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const capture = JSON.parse(text('capture.json'));
assert.equal(capture.schema, 'soui.license-pdb-members-capture/v1');
assert.equal(capture.dllSha256,
  '2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3');
assert.equal(capture.debugExit, 0);
assert.equal(capture.summaryExit, 0);
assert.equal(capture.modulesExit, 0);
for (const name of ['x86-dll-debug.stderr.txt', 'x86-pdb-summary.stderr.txt',
  'x86-pdb-modules.stderr.txt']) assert.equal(read(name).length, 0, name);

const debug = text('x86-dll-debug.stdout.txt');
const summary = text('x86-pdb-summary.stdout.txt');
const modules = text('x86-pdb-modules.stdout.txt');
const dllGuid = /PDBGUID:\s*\{([0-9A-F-]+)\}/i.exec(debug)?.[1];
const dllAge = Number(/PDBAge:\s*([0-9]+)/i.exec(debug)?.[1]);
const pdbGuid = /^\s*GUID:\s*\{([0-9A-F-]+)\}/im.exec(summary)?.[1];
const pdbAge = Number(/^\s*Age:\s*([0-9]+)/im.exec(summary)?.[1]);
assert(dllGuid && pdbGuid);
assert.equal(dllGuid.toLowerCase(), pdbGuid.toLowerCase());
assert(Number.isSafeInteger(dllAge) && dllAge > 0);
assert.equal(dllAge, pdbAge);
assert.match(debug, /Format:\s*COFF-i386/);
assert.match(summary, /Has Debug Info:\s*true/);

const compilands = [...modules.matchAll(/^\s*Mod\s+([0-9]+)\s*\|\s*`([^`]+)`:\s*\r?\n\s*Obj:\s*`([^`]+)`:/gm)]
  .map(match => ({index: Number(match[1]), module: match[2].replace(/[\r\n]/g, '').trim(),
    archive: match[3].replace(/[\r\n]/g, '').trim()}));
assert(compilands.length > 1000, 'PDB compiland directory is incomplete');
assert.equal(new Set(compilands.map(item => item.index)).size, compilands.length);
const openssl = compilands.filter(item => /[\\/]openssl\.lib$/i.test(item.archive));
assert(openssl.length > 0, 'No OpenSSL archive members in matching PDB');
assert(openssl.every(item => /[\\/]openssl-x86[\\/]/i.test(item.module)));
const libcurl = compilands.filter(item => /[\\/]libcurl\.lib$/i.test(item.archive));

const rawNames = ['capture.json', 'x86-dll-debug.stdout.txt', 'x86-dll-debug.stderr.txt',
  'x86-pdb-summary.stdout.txt', 'x86-pdb-summary.stderr.txt',
  'x86-pdb-modules.stdout.txt', 'x86-pdb-modules.stderr.txt'];
const analysis = {
  schema: 'soui.license-pdb-members-analysis/v1',
  dllSha256: capture.dllSha256,
  pdbSha256: capture.pdbSha256,
  pdbGuid: dllGuid,
  pdbAge: dllAge,
  pdbMatchesDll: true,
  compilandCount: compilands.length,
  opensslArchiveMemberCount: openssl.length,
  opensslModuleIndices: openssl.map(item => item.index),
  opensslModuleExamples: openssl.slice(0, 12).map(item => path.basename(item.module)),
  libcurlArchiveMemberCount: libcurl.length,
  raw: Object.fromEntries(rawNames.map(name => [name, {
    bytes: read(name).length, sha256: hash(read(name)),
  }])),
  x86OpenSslLinkedMemberEvidence: true,
  x64OpenSslLinkedMemberEvidence: false,
  licenseReviewVerified: false,
  releaseReady: false,
};
const saved = JSON.parse(text('analysis.json'));
assert.deepEqual(saved, analysis, 'Archived analysis does not match raw tool outputs');
console.log(JSON.stringify({pdbMatchesDll: true, compilandCount: compilands.length,
  opensslArchiveMemberCount: openssl.length, licenseReviewVerified: false,
  releaseReady: false}));
