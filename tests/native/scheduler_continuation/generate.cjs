'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const [root, output] = process.argv.slice(2);
if (!root || !output) throw new Error('usage: generate.cjs source-root output');
fs.mkdirSync(output, { recursive: true });
const base = 'third_party/sources/miniblink/';
const entries = [];
function save(relative, target, begin, end) {
  const bytes = fs.readFileSync(path.join(root, base + relative));
  let start = 0, finish = bytes.length;
  if (begin) {
    start = bytes.indexOf(Buffer.from(begin));
    finish = bytes.indexOf(Buffer.from(end), start + begin.length);
    if (start < 0 || finish <= start || bytes.indexOf(Buffer.from(begin), start + 1) !== -1)
      throw new Error('ambiguous production method boundary: ' + relative);
  }
  const fragment = bytes.subarray(start, finish);
  fs.writeFileSync(path.join(output, target), fragment);
  entries.push({ source: base + relative, target, startByte: start, endByteExclusive: finish,
    sourceSha256: crypto.createHash('sha256').update(bytes).digest('hex'),
    fragmentSha256: crypto.createHash('sha256').update(fragment).digest('hex') });
}
save('content/browser/SharedTimerWin.h', 'SharedTimerWin.h');
save('content/web_impl_win/TaskSchedulingPolicy.h', 'TaskSchedulingPolicy.h');
save('content/web_impl_win/TaskSchedulingIndex.h', 'TaskSchedulingIndex.h');
save('content/web_impl_win/WebThreadImpl.cpp', 'budgets.inc',
  'static const double maxDurationOfFiringTimers', 'namespace {');
save('content/web_impl_win/WebThreadImpl.cpp', 'scheduler.inc',
  'void WebThreadImpl::schedulerTasks()', 'std::vector<WebTimerBase*>& WebThreadImpl::timerHeap()');
save('content/web_impl_win/WebThreadImpl.cpp', 'enable.inc',
  'void WebThreadImpl::enableScheduler()', 'void WebThreadImpl::fire()');
save('content/web_impl_win/WebThreadImpl.cpp', 'retire.inc',
  'void WebThreadImpl::retireCompletedTasksForOwner()', 'void WebThreadImpl::deleteTimersOnExit()');
fs.writeFileSync(path.join(output, 'source-binding.json'), JSON.stringify({
  schema: 'scheduler-continuation-contract/1', dependencyMode: 'deterministic-fakes-no-host-ui', entries
}, null, 2) + '\n');
