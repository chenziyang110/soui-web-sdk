'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const executable = process.env.BENCHMARK_INPUT_TRACE_EXE;
const available = process.platform === 'win32' && executable && fs.existsSync(executable);

test('input driver rejects invalid targets before creating a trace or injecting input',
    {skip: !available}, () => {
      const output = path.join(os.tmpdir(), `soui-input-rejected-${process.pid}.ndjson`);
      assert.equal(fs.existsSync(output), false);
      const attempts = [
        ['0', 'S01', output, 'probe'],
        [String(process.pid), 'S99', output, 'probe'],
        [String(process.pid), 'S01', 'relative.ndjson', 'probe'],
        [String(process.pid), 'S01', output, 'unknown'],
      ];
      for (const args of attempts) {
        const run = spawnSync(executable, args, {windowsHide: true, timeout: 3000});
        assert.equal(run.status, 1, `${args.join(' ')}: ${run.stderr?.toString()}`);
        assert.equal(fs.existsSync(output), false);
      }
    });
