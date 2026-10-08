'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {spawn, spawnSync} = require('node:child_process');
const {once} = require('node:events');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const sampler = process.env.PROCESS_TREE_SAMPLER_EXE;
const available = process.platform === 'win32' && sampler && fs.existsSync(sampler);

function startTree() {
  const source = `const {spawn}=require('node:child_process');
    spawn(process.execPath,['-e','setTimeout(()=>process.exit(0),1500)'],{windowsHide:true,stdio:'ignore'});
    setTimeout(()=>process.exit(0),2000);`;
  return spawn(process.execPath, ['-e', source], {windowsHide: true, stdio: 'ignore'});
}

function startOrphanTree(folder) {
  const leaf = path.join(folder, 'leaf.cjs');
  const middle = path.join(folder, 'middle.cjs');
  const root = path.join(folder, 'root.cjs');
  fs.writeFileSync(leaf, 'setTimeout(()=>process.exit(0),2000);\n');
  fs.writeFileSync(middle, `const {spawn}=require('node:child_process');
    const fs=require('node:fs');
    const child=spawn(process.execPath,[${JSON.stringify(leaf)}],{detached:true,windowsHide:true,stdio:'ignore'});
    fs.writeFileSync(${JSON.stringify(path.join(folder, 'leaf.pid'))},String(child.pid));
    child.unref();
    setTimeout(()=>process.exit(0),900);\n`);
  fs.writeFileSync(root, `const {spawn}=require('node:child_process');
    spawn(process.execPath,[${JSON.stringify(middle)}],{windowsHide:true,stdio:'ignore'});
    setTimeout(()=>process.exit(0),2600);\n`);
  return spawn(process.execPath, [root], {windowsHide: true, stdio: 'ignore'});
}

function rows(filename) {
  return fs.readFileSync(filename, 'utf8').trim().split('\n').map(line => JSON.parse(line));
}

test('process sampler includes descendants and records terminal and timeout paths',
    {skip: !available}, async () => {
      const folder = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-sampler-'));
      try {
        const normal = startTree();
        const normalExit = once(normal, 'exit');
        const normalFile = path.join(folder, 'normal.ndjson');
        const normalRun = spawnSync(sampler, [String(normal.pid), '100', '5000', normalFile],
                                    {windowsHide: true, timeout: 8000});
        await normalExit;
        assert.equal(normalRun.status, 0, normalRun.stderr?.toString());
        const normalRows = rows(normalFile);
        const samples = normalRows.filter(item => item.event === 'sample');
        assert.equal(normalRows[0].schema, 'soui.process-tree-samples/v1');
        assert.ok(samples.length >= 5);
        assert.ok(samples.every(item => item.complete));
        assert.ok(samples.some(item => item.processes.length >= 2));
        assert.ok(samples.some(item => Number.isFinite(item.cpuPercent)));
        assert.equal(normalRows.at(-1).rootExitCode, 0);
        assert.equal(normalRows.at(-1).durationExpired, false);

        const timeout = startTree();
        const timeoutExit = once(timeout, 'exit');
        const timeoutFile = path.join(folder, 'timeout.ndjson');
        const timeoutRun = spawnSync(sampler, [String(timeout.pid), '100', '300', timeoutFile],
                                     {windowsHide: true, timeout: 3000});
        await timeoutExit;
        assert.equal(timeoutRun.status, 2, timeoutRun.stderr?.toString());
        assert.equal(rows(timeoutFile).at(-1).durationExpired, true);

        const orphan = startOrphanTree(folder);
        const orphanExit = once(orphan, 'exit');
        const orphanFile = path.join(folder, 'orphan.ndjson');
        const orphanRun = spawnSync(sampler, [String(orphan.pid), '50', '5000', orphanFile],
                                    {windowsHide: true, timeout: 8000});
        await orphanExit;
        const orphanRows = rows(orphanFile);
        assert.equal(orphanRun.status, 0,
          `${orphanRun.stderr?.toString()} terminal=${JSON.stringify(orphanRows.at(-1))}`);
        const leafPid = Number(fs.readFileSync(path.join(folder, 'leaf.pid'), 'utf8'));
        const orphanSamples = orphanRows.filter(item => item.event === 'sample');
        const leafSeenAt = orphanSamples.filter(item => item.processes.some(process => process.pid === leafPid))
          .map(item => item.elapsedMs);
        assert.ok(leafSeenAt.some(elapsed => elapsed > 1200),
          `observed grandchild ${leafPid} disappeared after its parent exited; seenAt=${leafSeenAt}`);

        const duplicateRun = spawnSync(sampler, [String(process.pid), '100', '300', normalFile],
                                       {windowsHide: true, timeout: 3000});
        assert.equal(duplicateRun.status, 1);
      } finally {
        const intended = path.resolve(os.tmpdir()) + path.sep;
        if (!path.resolve(folder).startsWith(intended)) throw new Error('Unsafe sampler test output path');
        fs.rmSync(folder, {recursive: true, force: true});
      }
    });
