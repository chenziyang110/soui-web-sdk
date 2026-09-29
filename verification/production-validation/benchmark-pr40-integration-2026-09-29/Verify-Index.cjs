'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const {execFileSync} = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const repo = path.resolve(__dirname, '../../..');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const batches = [
  ['copy-review.json', 'merge-summary-attempt1'],
  ['copy-review-classification.json', 'merge-classification-attempt1'],
  ['copy-review-live.json', 'merge-live-interference-attempt1'],
  ['copy-review-cleanup.json', 'merge-runone-cleanup-attempt1'],
  ['copy-review-traces.json', 'merge-runtraces-attempt1'],
  ['copy-review-preflight.json', 'merge-pair-preflight-attempt1'],
];
let count = 0;
for (const [reviewName, batchName] of batches) {
  const review = JSON.parse(fs.readFileSync(path.join(__dirname, reviewName), 'utf8'));
  assert.equal(review.schema, 'soui.benchmark-pr40-integration-copy/v1');
  assert.equal(review.allMatched, true);
  assert.equal(review.files.length, 3);
  assert.equal(review.releaseReady, false);
  for (const file of review.files) {
    const original = fs.readFileSync(path.join(review.sourceDirectory, file.name));
    const local = path.join(__dirname, 'raw', batchName, file.name);
    const workspace = fs.readFileSync(local);
    const relative = path.relative(repo, local).replaceAll('\\', '/');
    assert(!relative.startsWith('../'));
    const indexed = execFileSync('git', ['show', `:${relative}`], {
      cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024,
    });
    assert.equal(sha(original), file.sourceSha256, `Original changed: ${file.name}`);
    assert.equal(sha(workspace), file.copySha256, `Workspace changed: ${file.name}`);
    assert(original.equals(workspace), `Copy differs: ${file.name}`);
    assert(indexed.equals(workspace), `Git index differs: ${file.name}`);
    count++;
  }
}
process.stdout.write(JSON.stringify({status: 'verified', rawFiles: count,
  originalWorkspaceIndexMatched: true, benchmarkValid: false,
  releaseReady: false}) + '\n');
