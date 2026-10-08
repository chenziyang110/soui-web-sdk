'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const inventoryBytes = fs.readFileSync(path.join(root, 'inventory.json'));
const inventory = JSON.parse(inventoryBytes.toString('utf8'));
const recorded = JSON.parse(fs.readFileSync(path.join(root, 'verification.json'), 'utf8'));
const explicit = JSON.parse(fs.readFileSync(path.join(root, '..',
  'license-runtime-notices-2026-09-28', 'current-link-command-extraction-v3.json'), 'utf8'));

assert.equal(inventory.schema, 'soui.current-link-trace-inventory/v1');
assert.equal(recorded.schema, 'soui.current-link-trace-verification/v1');
assert.equal(digest(inventoryBytes),
  '802cabd09ee32e9c2a859e154e541d2f07b69e0cc3aa8682aa272158a72629b0');
assert.equal(recorded.inventorySha256, digest(inventoryBytes));
assert.equal(recorded.passed, true);
assert.deepEqual(recorded.architectures, ['x86', 'x64']);
assert.equal(recorded.libraryReadCountPerArchitecture, 44);
assert.equal(inventory.sourceHead, '5df8e3f0a5ef5a45098cb08466d444158c7fef5e');
assert.equal(inventory.linkedArchiveMembersVerified, false);
assert.equal(inventory.applicableRedistributionTermsVerified, false);
assert.equal(inventory.binaryLicenseReviewComplete, false);
assert.equal(inventory.releaseReady, false);

const summary = [];
for (const arch of ['x86', 'x64']) {
  const item = inventory.architectures.find(value => value.architecture === arch);
  const earlier = explicit.binaries.find(value => value.architecture === arch);
  assert(item && earlier);
  assert.equal(item.currentBinarySha256, earlier.artifact.sha256);
  const traces = {};
  for (const kind of ['command', 'read']) {
    const meta = item.linkTraces[kind];
    const file = path.join(root, 'raw', meta.path);
    const bytes = fs.readFileSync(file);
    assert.equal(bytes.length, meta.bytes);
    assert.equal(digest(bytes), meta.sha256);
    assert.equal(bytes[0], 0xff);
    assert.equal(bytes[1], 0xfe);
    traces[kind] = bytes.toString('utf16le');
  }
  assert(traces.command.toLowerCase().includes(
    path.win32.basename(earlier.artifact.path).toLowerCase()));
  const opened = [...new Map(traces.read.split(/\r?\n/)
    .map(line => line.trim()).filter(line => line.toLowerCase().endsWith('.lib'))
    .map(file => [file.toLowerCase(), {
      name: path.win32.basename(file).toLowerCase(), path: file,
    }])).values()];
  const byName = values => [...values].sort((a, b) => a.name.localeCompare(b.name));
  assert.deepEqual(byName(opened), byName(item.libraries));
  assert.equal(opened.length, item.libraryReadCount);
  const names = opened.map(value => value.name);
  const explicitNames = earlier.linkCommand.explicitInputLibraryNames;
  assert(explicitNames.every(name => names.includes(name)));
  const readNotExplicit = names.filter(name => !explicitNames.includes(name)).sort();
  summary.push({architecture: arch, openedLibraries: names.length,
    explicitLibraries: explicitNames.length, readNotExplicit,
    opensslRead: names.includes('openssl.lib'),
    opensslExplicit: explicitNames.includes('openssl.lib')});
}
assert.deepEqual(summary.map(item => item.openedLibraries), [44, 44]);
assert(summary.every(item => item.opensslRead));
assert.equal(summary[0].opensslExplicit, true);
assert.equal(summary[1].opensslExplicit, false);
process.stdout.write(JSON.stringify({passed: true, scope: 'archived-link-read-traces',
  linkedArchiveMembersVerified: false, releaseReady: false, summary}, null, 2) + '\n');
