'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, 'win7-v9-source');
const script = fs.readFileSync(path.join(root, 'run-layout-interaction.ps1'));
const text = script.toString('utf8');
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
assert.equal(sha(script),
  '206c8f86a481e44ee7392cb5a112fe28157840e9868efb2a0e8701a8d2c391df');
for (const marker of [
  "$Target='C:\\soui-v02-work-area-v9'",
  "$Results='C:\\soui-v02-work-area-v9-results'",
  "Wait-AppDelay 8000",
  "$wheelIndex -le 12",
  "$wheelIndex -le 2",
  "$wheelIndex -le 10",
  "'wheel' '30,150' ('outer-down-'",
  "'resize-drag' '80,60' 'resize'",
  "'click' '635,600' 'start-task'",
  "'click' '350,125' 'focus-input'",
  "'pinyin' 'zhongwen' 'pinyin'",
  "'wheel-up' '30,150' ('input-reveal-'",
  "'wheel-up' '30,150' ('list-reveal-'",
  "'wheel' '450,450' 'inner-list-wheel'",
  "'wheel' '30,150' ('task-return-'",
  '--width=850 --height=640',
  'TimeoutMs=120000',
  'buildId=b6caec6c-117e-49eb-86fa-4182e64d5b55',
  'source=60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640',
  'scope=1024x768-v02-current-work-area-initial-resize-scroll-task-ime-exploration-not-production',
]) assert(text.includes(marker), `Missing V9 marker: ${marker}`);
assert(!text.includes('move-drag'), 'V9 must test initial placement without a move workaround');
assert(!text.includes('f0c38056-058a-456e-be99-17446127f7ec'));
assert(!text.includes('15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d'));
assert(text.indexOf("'resize-drag' '80,60' 'resize'") <
  text.indexOf("'click' '635,600' 'start-task'"));
assert(text.indexOf("'click' '635,600' 'start-task'") <
  text.indexOf("'pinyin' 'zhongwen' 'pinyin'"));
const result = {schema: 'soui.win7-v02-v9-source-static/v1',
  scriptSha256: sha(script), expectedActions: 39, expectedSerialFrames: 116,
  sourceValidated: true, guestExecuted: false,
  win7CurrentV02Verified: false, releaseReady: false};
console.log(JSON.stringify(result));
