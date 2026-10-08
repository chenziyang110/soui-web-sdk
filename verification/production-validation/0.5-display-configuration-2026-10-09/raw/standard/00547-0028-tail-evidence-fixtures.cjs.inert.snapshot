'use strict';
// Synthetic protocol data only. No fixture below asserts an actual MSVC build.
const crypto = require('node:crypto');
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encode = value => Buffer.from(JSON.stringify(value) + '\n');
const image = 'C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x86\\vctip.exe';
function fixture() {
  const pinOption = {image, sha256: '1c1ca5f9c04171c02caca6863e29eca3550333219486d23e65bf01fb02951311', nonce: '1234567890abcdef1234567890abcdef'};
  const context = {schema: 'soui.owned-process-context/v1', runnerSha256: 'a'.repeat(64), executableSha256: 'b'.repeat(64),
    executable: 'C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe',
    args: ['-NoProfile','-ExecutionPolicy','Bypass','-File','F:\\sdk\\tools\\Build.ps1','-Architecture','x86','-Configuration','Release','-SkipWeb'],
    cwd: 'F:\\sdk', timeoutMs: 10000, drainMs: 100, tailImagePin: pinOption};
  context.report = 'F:\\sdk\\owned.ndjson';
  context.runnerArgs = ['--timeout-ms','10000','--drain-ms','100','--stop-on-stdin','--report',context.report,'--cwd',context.cwd,
    '--pin-tail-image',pinOption.image,'--pin-tail-sha256',pinOption.sha256,'--snapshot-nonce',pinOption.nonce,'--',context.executable,...context.args];
  const pin = {canonicalPath: '\\\\?\\' + image,
    volumePath: '\\\\?\\Volume{11111111-1111-1111-1111-111111111111}\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x86\\vctip.exe',
    volumeSerialNumber: '4294967295', fileId: '18446744073709551615', size: '335560', sha256: pinOption.sha256};
  const accounting = active => ({activeProcesses: active, totalProcesses: 4, limitTerminatedProcesses: 0});
  const rows = [{schema: 'soui.owned-process-run/v1', event: 'start', pid: 41, creationFileTime: '134359391934214888',
    timeoutMs: 10000, drainMs: 100, jobAssignedBeforeResume: true, breakawayAllowed: false, snapshotNonce: pinOption.nonce}];
  let sequence = 0;
  const add = (event, phase, attempt, fields) => rows.push({schema: 'soui.owned-tail-snapshot/v1', event,
    nonce: pinOption.nonce, sequence: ++sequence, phase, attempt, ...fields});
  add('pin', 'prelaunch', 0, {...pin, observedBeforeLaunchUtcFileTime: '134359391934089970', observedBeforeLaunchTickMs: '123',
    pinBudgetMs: 2000, snapshotBudgetMs: 1000, maximumFileBytes: '67108864', diskBytesOnly: true});
  rows.push({event: 'owned-member', pid: 42, creationFileTime: '134359391934583654', image});
  rows.push({event: 'root-exit', elapsedMs: 100, exitCode: 0});
  add('snapshot.begin', 'pre-cleanup', 1, {accountingBefore: accounting(1)});
  const list = pass => add('snapshot.list', pass === 'final' ? 'pre-cleanup-confirm' : 'pre-cleanup', 1,
    {pass, growth: 0, capacity: 4, assigned: 1, listed: 1, win32Error: 0});
  list('first');
  add('snapshot.member', 'pre-cleanup', 1, {index: 0, pid: 42, creationFileTime: '134359391934583654', inHeldJob: true,
    alive: true, pinMatched: true, image, diskObject: {...pin}});
  list('second');
  add('snapshot.end', 'pre-cleanup', 1, {accountingAfter: accounting(1), assigned: 1, listed: 1, uniquePids: 1, members: 1,
    pids: [42], secondPids: [42], complete: true, eligible: true, stickyFailure: false});
  const decisive = sequence;
  list('final');
  add('pre-cleanup-confirm', 'pre-cleanup-confirm', 1, {snapshotSequence: decisive, accounting: accounting(1), pids: [42], identitiesMatch: true, confirmed: true});
  add('cleanup.member', 'post-cleanup', 0, {index: 0, pid: 42, creationFileTime: '134359391934583654', waitResult: 0, identityMatches: true, signaled: true});
  for (const attempt of [1, 2]) add('cleanup.confirm', 'post-cleanup', attempt,
    {snapshotSequence: decisive, accounting: accounting(0), fixedTotalProcesses: 4, allHeldSignaled: true, confirmed: true});
  add('tail-evidence', 'post-cleanup', 0, {preCleanupSnapshotSequence: decisive, tailEvidenceComplete: true, failure: null, diskBytesOnly: true});
  rows.push({event: 'terminal', reason: 'owned-descendants-remain', elapsedMs: 500, rootExited: true, rootExitCode: 0,
    activeBeforeCleanup: 1, terminatedOwnedJob: true, closureConfirmed: true, activeProcesses: 0, totalProcesses: 4,
    limitTerminatedProcesses: 0, passed: false, preCleanupSnapshotSequence: decisive, tailEvidenceComplete: true});
  return {rows, context, pin, raw: () => Buffer.from(rows.map(row => JSON.stringify(row)).join('\n') + '\n')};
}
module.exports = {fixture, digest, encode};
