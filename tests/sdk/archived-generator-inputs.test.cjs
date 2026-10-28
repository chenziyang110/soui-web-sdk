'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const archiveRoot = path.resolve(__dirname,
  '../../verification/production-validation/preview11-upgrade-interruption-2026-09-26');
const manifest = JSON.parse(fs.readFileSync(path.join(archiveRoot, 'raw-manifest.json'), 'utf8'));
const inputs = [
  {
    path: 'raw/old-generator/templates/desktop-app/web/package.json',
    bytes: 578,
    sha256: '2f4faea44f215cb0106d228b777847e30152f2971432ece0453a27d68b332dff',
  },
  {
    path: 'raw/old-generator/templates/desktop-app/web/package-lock.json',
    bytes: 130655,
    sha256: '8fe6b1e6386cccfb071b26ee5fd7ab3ba426b3a2efb14365c20b5e37f91e47d1',
  },
];

for (const expected of inputs) {
  test(`archived generator input retains its original bytes: ${expected.path}`, () => {
    assert.equal(manifest.schema, 'soui.raw-evidence/v1');
    const rows = manifest.files.filter(row => row.path === expected.path);
    assert.equal(rows.length, 1, 'the original raw manifest must identify exactly one input');
    assert.equal(rows[0].bytes, expected.bytes, 'the historical byte pin must remain fixed');
    assert.equal(rows[0].sha256, expected.sha256, 'the historical SHA-256 pin must remain fixed');

    const bytes = fs.readFileSync(path.join(archiveRoot, expected.path));
    assert.equal(bytes.length, expected.bytes, 'archived input byte length changed');
    assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), expected.sha256,
      'archived input bytes changed; update active templates instead of historical evidence');
  });
}
