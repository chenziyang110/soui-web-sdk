'use strict';
const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const name = process.argv[2];
assert.match(name || '', /^guest-layout-interaction-v8-attempt[0-9]+$/);
const root = __dirname;
const attempt = path.join(root, name);
const launch = JSON.parse(fs.readFileSync(path.join(attempt, 'launch.json'), 'utf8').replace(/^\uFEFF/, ''));
const sent = JSON.parse(fs.readFileSync(path.join(attempt, 'command-send.json'), 'utf8'));
assert.equal(sent.pid, launch.pid);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const frames = [];
const startedAt = new Date().toISOString();
async function main() {
  let stopReason = 'timeline-limit';
  for (let index = 1; index <= 60; ++index) {
    const label = `timeline-${String(index).padStart(3, '0')}`;
    const raw = path.join(attempt, `${label}.ppm`);
    const png = path.join(attempt, `${label}.png`);
    if (fs.existsSync(raw) || fs.existsSync(png)) throw Error(`Refuse overwrite ${label}`);
    const captureAt = new Date().toISOString();
    const monitor = cp.spawnSync(process.execPath,
      [path.join(root, 'Monitor-DisplayExplore.cjs'), attempt, 'screen', label],
      {encoding: 'utf8', windowsHide: true, timeout: 15000});
    if (monitor.status !== 0) {
      frames.push({label, captureAt, monitorExitCode: monitor.status,
        monitorError: monitor.error?.message || null, monitorStderr: monitor.stderr});
      stopReason = 'monitor-failed';
      break;
    }
    const convert = cp.spawnSync('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-n', '-i', raw, png],
      {encoding: 'utf8', windowsHide: true, timeout: 15000});
    if (convert.status !== 0 || !fs.existsSync(png)) throw Error(`Screenshot conversion failed: ${label}`);
    const rawBytes = fs.readFileSync(raw), pngBytes = fs.readFileSync(png);
    frames.push({label, captureAt, monitorExitCode: 0,
      rawBytes: rawBytes.length, rawSha256: sha(rawBytes),
      pngBytes: pngBytes.length, pngSha256: sha(pngBytes)});
    const serialPath = path.join(attempt, 'serial-export.txt');
    const serial = fs.existsSync(serialPath) ? fs.readFileSync(serialPath, 'ascii') : '';
    if (serial.includes('SOUI_SERIAL_END')) { stopReason = 'serial-ended'; break; }
    if (fs.existsSync(path.join(attempt, 'qemu-result.json'))) { stopReason = 'qemu-ended'; break; }
    await sleep(3000);
  }
  const result = {schema: 'soui.win7-v02-layout-timeline/v8', attempt: name,
    qemuPid: launch.pid, commandSentAt: sent.sentAt, startedAt,
    finishedAt: new Date().toISOString(), stopReason, frames,
    layoutWheelVerified: false, v02RealInputVerified: false, releaseReady: false};
  fs.writeFileSync(path.join(attempt, 'timeline-result.json'), JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
  console.log(JSON.stringify({attempt: name, frames: frames.length, stopReason,
    layoutWheelVerified: false, releaseReady: false}));
}
main().catch(error => {console.error(error.stack || error); process.exitCode = 1});
