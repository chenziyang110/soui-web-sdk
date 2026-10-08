'use strict';

// Local evidence consistency, not authentication against a user who can rewrite
// the complete bundle. Absolute paths in context are metadata, never read targets.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {TextDecoder} = require('node:util');
const {isDeepStrictEqual} = require('node:util');
const limit = 32 * 1024 * 1024;
const roles = ['host', 'sampler', 'input'];
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const insist = (condition, message) => { if (!condition) throw new Error(message); };
const digest = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const pid = value => Number.isSafeInteger(value) && value > 0 && value <= 4294967295;
const filetime = value => typeof value === 'string' && /^[1-9][0-9]{0,19}$/.test(value) &&
    BigInt(value) <= 18446744073709551615n;
const normalized = value => process.platform === 'win32' ? path.resolve(value).toLowerCase() : path.resolve(value);
const samePath = (left, right) => typeof left === 'string' && typeof right === 'string' &&
    normalized(left) === normalized(right);

function runPath(root, relative) {
  insist(typeof relative === 'string' && relative.length > 0 && !relative.includes('\\') &&
    !relative.includes(':') && !relative.includes('\0') && !path.isAbsolute(relative) &&
    relative.split('/').every(part => part && part !== '.' && part !== '..'), 'Unsafe run evidence path');
  const base = fs.realpathSync(root);
  let target = base;
  for (const part of relative.split('/')) {
    target = path.join(target, part);
    insist(!fs.lstatSync(target).isSymbolicLink(), 'Linked run evidence is not allowed');
    const actual = fs.realpathSync(target);
    const inside = path.relative(base, actual);
    insist(inside && !inside.startsWith('..' + path.sep) && inside !== '..' && !path.isAbsolute(inside),
      'Run evidence escapes its directory');
  }
  return target;
}
function runDirectory(root, relative) {
  const target = runPath(root, relative);
  insist(fs.lstatSync(target).isDirectory(), 'Run evidence directory has the wrong type');
  return target;
}
function runFile(root, relative) {
  const target = runPath(root, relative);
  const stat = fs.lstatSync(target);
  insist(stat.isFile() && stat.nlink === 1 && stat.size <= limit, 'Run evidence must be a bounded unlinked regular file');
  return target;
}

function readRunFile(root, relative) {
  const bytes = fs.readFileSync(runFile(root, relative));
  insist(bytes.length <= limit, 'Run evidence grew past its byte limit');
  return bytes;
}
function decode(bytes) { return new TextDecoder('utf-8', {fatal:true}).decode(bytes); }
function readRunJson(root, relative) { return JSON.parse(decode(readRunFile(root, relative))); }
function readRunLines(root, relative) {
  const bytes = readRunFile(root, relative);
  insist(bytes.length > 0 && bytes.at(-1) === 10, 'Truncated run NDJSON evidence');
  return decode(bytes).trimEnd().split(/\r?\n/).map(line => JSON.parse(line));
}
function describe(root, relative) {
  const bytes = readRunFile(root, relative);
  return {path:relative, bytes:bytes.length, sha256:hash(bytes)};
}
function describeTree(root, relative) {
  const entries = [];
  const walk = prefix => {
    const directory = runDirectory(root, relative + (prefix ? '/' + prefix : ''));
    for (const entry of fs.readdirSync(directory, {withFileTypes:true})) {
      const name = prefix ? prefix + '/' + entry.name : entry.name;
      if (entry.isDirectory()) walk(name);
      else if (entry.isFile()) {
        insist(entries.length < 10000, 'Too many run bundle files');
        const item = describe(root, relative + '/' + name);
        entries.push({...item, path:name});
      } else throw new Error('Unsupported/linked run bundle entry');
    }
  };
  walk('');
  return entries.sort((left, right) => left.path.localeCompare(right.path));
}
function verifiedBytes(root, descriptor, expectedPath) {
  insist(descriptor && descriptor.path === expectedPath && Number.isSafeInteger(descriptor.bytes) &&
    descriptor.bytes >= 0 && descriptor.bytes <= limit && digest(descriptor.sha256), 'Invalid evidence descriptor/path');
  const bytes = readRunFile(root, expectedPath);
  insist(bytes.length === descriptor.bytes && hash(bytes) === descriptor.sha256, 'Run evidence bytes/hash differ');
  return bytes;
}
function startIdentity(start, context) {
  insist(start?.schema === 'soui.owned-process-run/v1' && start.event === 'start' &&
    pid(start.pid) && filetime(start.creationFileTime) && start.jobAssignedBeforeResume === true &&
    start.breakawayAllowed === false && start.timeoutMs === context.timeoutMs &&
    start.drainMs === context.drainMs, 'Invalid native owned start identity/context');
  return {pid:start.pid, creationFileTime:start.creationFileTime};
}
function checkContext(context, expected) {
  insist(context?.schema === 'soui.owned-process-context/v1' &&
    digest(context.runnerSha256) && digest(context.executableSha256) &&
    context.runnerSha256 === expected.runnerSha256 && context.executableSha256 === expected.executableSha256 &&
    typeof context.executable === 'string' && path.isAbsolute(context.executable) && !context.executable.includes('\0') &&
    typeof context.cwd === 'string' && path.isAbsolute(context.cwd) && !context.cwd.includes('\0') &&
    Array.isArray(context.args) && context.args.length <= 128 &&
    context.args.every(arg => typeof arg === 'string' && arg.length <= 32768 && !arg.includes('\0')) &&
    Number.isSafeInteger(context.timeoutMs) && context.timeoutMs > 0 && context.timeoutMs <= 172800000 &&
    Number.isSafeInteger(context.drainMs) && context.drainMs >= 0 && context.drainMs <= 60000,
  'Invalid native launch context/tool identity');
  if (expected.executable) insist(samePath(context.executable, expected.executable), 'Context executable differs from launch');
  if (expected.cwd) insist(samePath(context.cwd, expected.cwd), 'Context cwd differs from launch');
  if (expected.args) insist(isDeepStrictEqual(context.args, expected.args), 'Context arguments differ from launch');
}

function readPublishedSubject(root, name, ready, expected) {
  insist(roles.includes(name), 'Unknown owned role');
  const contextBytes = readRunFile(root, name + '.owned.ndjson.context.json');
  insist(hash(contextBytes) === expected.contextSha256, 'Launch context changed before ready');
  const context = JSON.parse(decode(contextBytes));
  checkContext(context, expected);
  const bytes = readRunFile(root, name + '.owned.ndjson');
  const newline = bytes.indexOf(10);
  insist(newline >= 0, 'Native start is truncated');
  const subject = startIdentity(JSON.parse(decode(bytes.subarray(0, newline))), context);
  insist(subject.pid === ready.pid && subject.creationFileTime === ready.creationFileTime,
    'Published native subject differs from ready');
  return subject;
}

function captureOwnedEvidence(root, processes) {
  return processes.map(item => {
    insist(roles.includes(item.name), 'Unknown owned role');
    const report = describe(root, item.name + '.owned.ndjson');
    const context = describe(root, item.name + '.owned.ndjson.context.json');
    insist(context.sha256 === item.contextSha256, 'Launch context changed during execution');
    return {name:item.name, report, context,
      stdout:describe(root, item.name + '.stdout.log'), stderr:describe(root, item.name + '.stderr.log')};
  });
}

function verifyOwnedProcess(root, name, item, evidence, expected, captureRoot = root) {
  insist(roles.includes(name) && item?.name === name && evidence?.name === name, 'Owned role differs');
  const reportBytes = verifiedBytes(root, evidence.report, name + '.owned.ndjson');
  const contextBytes = verifiedBytes(root, evidence.context, name + '.owned.ndjson.context.json');
  verifiedBytes(root, evidence.stdout, name + '.stdout.log');
  verifiedBytes(root, evidence.stderr, name + '.stderr.log');
  insist(reportBytes.length > 0 && reportBytes.at(-1) === 10, 'Truncated native owned report');
  const rows = decode(reportBytes).slice(0, -1).split('\n').map(line => JSON.parse(line));
  const context = JSON.parse(decode(contextBytes));
  checkContext(context, expected);
  const subject = startIdentity(rows[0], context);
  const terminal = rows.at(-1);
  insist(rows.filter(row => row.event === 'start').length === 1 &&
    rows.filter(row => row.event === 'terminal').length === 1 && terminal?.event === 'terminal',
  'Duplicate identity/terminal or missing native terminal');
  const exits = rows.filter(row => row.event === 'root-exit');
  insist(exits.length <= 1 && typeof terminal.rootExited === 'boolean' &&
    Number.isSafeInteger(terminal.elapsedMs) && terminal.elapsedMs >= 0 &&
    (terminal.rootExited ? exits.length === 1 && Number.isSafeInteger(terminal.rootExitCode) &&
      terminal.rootExitCode >= 0 && terminal.rootExitCode <= 4294967295 &&
      exits[0].exitCode === terminal.rootExitCode :
      exits.length === 0 && terminal.rootExitCode === null), 'Native root exit identity differs');
  for (const member of rows.filter(row => row.event === 'owned-member')) {
    insist(pid(member.pid) && filetime(member.creationFileTime), 'Invalid native member identity');
    if (member.pid === subject.pid) insist(member.creationFileTime === subject.creationFileTime, 'Native root member identity differs');
  }
  const result = item.ownedProcess;
  insist(result && item.pid === subject.pid && result.pid === subject.pid &&
    result.creationFileTime === subject.creationFileTime && item.code === result.exitCode &&
    result.exitCode === terminal.rootExitCode && isDeepStrictEqual(result.terminal, terminal) &&
    result.reportSha256 === evidence.report.sha256 && item.contextSha256 === evidence.context.sha256 &&
    result.runnerSha256 === context.runnerSha256 && result.executableSha256 === context.executableSha256 &&
    typeof result.report === 'string' && path.isAbsolute(result.report) &&
    samePath(result.report, path.join(captureRoot, name + '.owned.ndjson')),
  'Native report/context differs from recorded process identity');
  insist(result.spawnError === null && result.reportError === null && result.signal === null &&
    item.signal === null && result.fallbackUsed === false && result.closureConfirmed === true &&
    terminal.closureConfirmed === true && terminal.activeProcesses === 0 &&
    Number.isSafeInteger(terminal.totalProcesses) && terminal.totalProcesses >= 1 &&
    Number.isSafeInteger(terminal.activeBeforeCleanup) && terminal.activeBeforeCleanup >= 0 &&
    terminal.activeBeforeCleanup <= terminal.totalProcesses &&
    terminal.limitTerminatedProcesses === 0 && typeof terminal.terminatedOwnedJob === 'boolean' &&
    (terminal.activeBeforeCleanup !== 0 || terminal.terminatedOwnedJob === false) &&
    typeof result.stopRequested === 'boolean' && typeof result.passed === 'boolean' &&
    result.passed === terminal.passed, 'Native Job closure or supervision failed');
  const naturalCompletion = terminal.reason === 'completed' && terminal.rootExited === true &&
    terminal.rootExitCode === 0 && terminal.activeBeforeCleanup === 0 && terminal.terminatedOwnedJob === false &&
    terminal.passed === true && result.supervisorExitCode === 0 && result.stopRequested === false && item.error === null;
  const controlledStopClosed = terminal.reason === 'supervisor-stop' && terminal.passed === false &&
    result.stopRequested === true && result.supervisorExitCode === 124 && item.error === 'supervisor-stop';
  insist(naturalCompletion || controlledStopClosed, 'Owned process did not naturally complete or close by explicit STOP');
  return {subject, context, naturalCompletion, controlledStopClosed, genericPassed:result.passed};
}

function readOwnedRun(root, record, {requireNatural = true} = {}) {
  insist(record?.schema === 'soui.benchmark-single-run/v2' && typeof record.captureRoot === 'string' &&
    path.isAbsolute(record.captureRoot) && Array.isArray(record.processes) && record.processes.length === 3 &&
    Array.isArray(record.ownedEvidence) && record.ownedEvidence.length === 3, 'Missing v2 owned evidence');
  verifiedBytes(root, record.traceEvidence?.input, 'input.ndjson');
  verifiedBytes(root, record.traceEvidence?.processTree, 'process-tree.ndjson');
  const select = (array, name) => {
    const matches = array.filter(item => item?.name === name);
    insist(matches.length === 1, 'Duplicate or missing owned role');
    return matches[0];
  };
  const expectedHashes = {host:record.hostExeSha256, sampler:record.tools?.samplerSha256, input:record.tools?.inputSha256};
  const verified = {};
  for (const name of roles) {
    verified[name] = verifyOwnedProcess(root, name, select(record.processes, name), select(record.ownedEvidence, name),
      {runnerSha256:record.tools?.ownedRunnerSha256, executableSha256:expectedHashes[name]}, record.captureRoot);
    if (requireNatural) insist(verified[name].naturalCompletion, 'Formal sample requires three naturally completed Jobs');
  }
  const subject = verified.host.subject;
  const pidText = String(subject.pid), creation = subject.creationFileTime;
  const hostContext = verified.host.context;
  const dependencyPath = record.host === 'soui' ? record.dependency?.miniblinkPath : record.dependency?.appDirectory;
  insist(typeof dependencyPath === 'string' && path.isAbsolute(dependencyPath), 'Invalid captured host dependency path');
  const expectedHostArgs = record.host === 'soui' ?
    ['--resources=' + path.join(record.captureRoot, 'page-bundle'), '--miniblink=' + record.dependency?.miniblinkPath,
      '--result-dir=' + path.join(record.captureRoot, 'page-results'), '--exit-on-result'] :
    [record.dependency?.appDirectory, '--benchmark-page=' + path.join(record.captureRoot, 'page-bundle/benchmark.html'),
      '--result-dir=' + path.join(record.captureRoot, 'page-results'), '--exit-on-result'];
  insist(['soui', 'electron'].includes(record.host) &&
    isDeepStrictEqual(hostContext.args, expectedHostArgs) &&
    samePath(hostContext.cwd, record.host === 'soui' ? path.dirname(hostContext.executable) : record.dependency?.appDirectory) &&
    samePath(verified.sampler.context.cwd, path.dirname(verified.sampler.context.executable)) &&
    samePath(verified.input.context.cwd, path.dirname(verified.input.context.executable)),
  'Host/collector context cwd or arguments differ from the captured run');
  insist(record.maxDurationMs === (record.scene === 'S05' ? 250000 : 195000), 'Invalid benchmark collection duration');
  insist(isDeepStrictEqual(verified.sampler.context.args,
    [pidText, '1000', String(record.maxDurationMs), path.join(record.captureRoot, 'process-tree.ndjson'), creation]) &&
    isDeepStrictEqual(verified.input.context.args,
      [pidText, record.scene, path.join(record.captureRoot, 'input.ndjson'), 'full', creation]),
  'Collector context does not bind the native owned subject');
  return {subject, processes:verified, naturalCompletion:roles.every(name => verified[name].naturalCompletion),
    controlledStopClosed:roles.some(name => verified[name].controlledStopClosed)};
}

module.exports = {runFile, runDirectory, readRunFile, readRunJson, readRunLines, describe, describeTree, readPublishedSubject,
  captureOwnedEvidence, verifyOwnedProcess, readOwnedRun};
