'use strict';
// Checked ZIP extraction for data preparation. Callers supply an authenticated
// archive pin and complete raw file manifest; this never runs extracted code.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const cp = require('node:child_process');
const {isDeepStrictEqual} = require('node:util');
const {performance} = require('node:perf_hooks');
const id = require('./delivery-identity.cjs');
const deadlineFile = require('./deadline-file-pin.cjs');
const profiles = require('./compression-profiles.cjs');
const LIMITS = Object.freeze({entries: 100000, archiveBytes: 32 * 1024 ** 3, rawBytes: 512 * 1024 ** 3,
  pathCharacters: 4 * 1024 ** 2, nativeOutputBytes: 32 * 1024 ** 2, timeoutMs: 1200000});
function need(value, message) {if (!value) throw new Error(message);}
function keys(value, expected, label) {
  need(value && typeof value === 'object' && !Array.isArray(value) &&
    isDeepStrictEqual(Object.keys(value).sort(), [...expected].sort()), 'Invalid ' + label + ' fields');
}
function fullPath(file, deadline) {
  const full = path.resolve(file), root = path.parse(full).root;
  need(!/^\\\\[?.]\\/.test(full), 'Device namespace path refused');
  return id.safePath(root, path.relative(root, full).split(path.sep).join('/'), deadline === undefined ? undefined : () => deadlineFile.check(deadline));
}
function ordinary(file, deadline) {
  const full = fullPath(file, deadline), stat = deadline === undefined ? fs.lstatSync(full) : deadlineFile.io(deadline, () => fs.lstatSync(full));
  need(stat.isFile() && stat.nlink === 1, 'Archive input/output must be a plain single-link file'); return full;
}
function archivePin(expected, deadline) {
  keys(expected, ['path', 'bytes', 'sha256'], 'archive pin');
  need(typeof expected.path === 'string' && path.isAbsolute(expected.path) && Number.isSafeInteger(expected.bytes) &&
    expected.bytes > 0 && expected.bytes <= LIMITS.archiveBytes && /^[a-f0-9]{64}$/.test(expected.sha256), 'Invalid archive pin');
  const actual = deadlineFile.pin(ordinary(expected.path, deadline), {deadline, minBytes: 1, maxBytes: LIMITS.archiveBytes});
  need(actual.bytes === expected.bytes && actual.sha256 === expected.sha256, 'Authenticated archive changed'); return actual;
}
function manifest(files, deadline) {
  need(Array.isArray(files) && files.length > 0 && files.length <= LIMITS.entries, 'Invalid file manifest entry budget');
  const names = new Map(), directories = new Map(); let total = 0, pathCharacters = 0;
  for (const file of files) {
    deadlineFile.check(deadline);
    keys(file, ['path', 'bytes', 'sha256'], 'file manifest row');
    need(typeof file.path === 'string' && file.path && Number.isSafeInteger(file.bytes) && file.bytes >= 0 &&
      /^[a-f0-9]{64}$/.test(file.sha256), 'Invalid file manifest row');
    need(!names.has(file.path.toLowerCase()), 'Duplicate or case-colliding manifest file');
    pathCharacters += file.path.length;
    need(file.path.length <= 32760 && pathCharacters <= LIMITS.pathCharacters &&
      file.path.split('/').every(part => part.length <= 255), 'Archive member path exceeds character budget');
    deadlineFile.safePath(process.cwd(), file.path, deadline);
    names.set(file.path.toLowerCase(), {...file}); total += file.bytes;
    need(Number.isSafeInteger(total) && total <= LIMITS.rawBytes, 'File manifest exceeds raw byte budget');
    for (let parent = path.posix.dirname(file.path); parent !== '.'; parent = path.posix.dirname(parent)) {
      deadlineFile.check(deadline);
      const old = directories.get(parent.toLowerCase());
      need(old === undefined || old === parent, 'Case-colliding archive directory'); directories.set(parent.toLowerCase(), parent);
    }
  }
  for (const name of names.keys()) {deadlineFile.check(deadline); need(!directories.has(name), 'Archive file/directory path conflict');}
  deadlineFile.check(deadline);
  return {files: [...names.values()], names, directories, bytes: total};
}
function checkMembers(value, expected, deadline) {
  keys(value, ['schema', 'entries'], 'ZIP metadata');
  need(value.schema === 'soui.zip-members/v1' && Array.isArray(value.entries) && value.entries.length > 0 &&
    value.entries.length <= LIMITS.entries, 'Invalid ZIP member metadata');
  const seen = new Set(), actual = new Set();
  for (const entry of value.entries) {
    deadlineFile.check(deadline);
    keys(entry, ['path', 'bytes', 'attributes'], 'ZIP member');
    need(typeof entry.path === 'string' && Number.isSafeInteger(entry.bytes) && entry.bytes >= 0 &&
      Number.isSafeInteger(entry.attributes) && entry.attributes >= 0 && entry.attributes <= 0xffffffff, 'Invalid ZIP member');
    const directory = entry.path.endsWith('/'), name = directory ? entry.path.slice(0, -1) : entry.path;
    need(name.length > 0 && name.split('/').every(part => part.length <= 255), 'Invalid ZIP member path');
    deadlineFile.safePath(process.cwd(), name, deadline);
    const folded = name.toLowerCase(), unixType = (entry.attributes >>> 16) & 0xf000;
    need(!(entry.attributes & 0x400) && [0, directory ? 0x4000 : 0x8000].includes(unixType), 'ZIP linked or special member refused');
    need(Boolean(entry.attributes & 0x10) === directory || !(entry.attributes & 0x10), 'ZIP member directory attributes differ');
    need(!seen.has(folded), 'Duplicate or case-colliding ZIP member'); seen.add(folded);
    if (directory) {
      need(entry.bytes === 0 && expected.directories.get(folded) === name, 'Unlisted ZIP directory member');
    } else {
      const wanted = expected.names.get(folded);
      need(wanted && wanted.path === name && wanted.bytes === entry.bytes, 'ZIP member differs from authenticated file manifest');
      actual.add(name);
    }
  }
  need(actual.size === expected.files.length, 'ZIP member closed set differs');
  deadlineFile.check(deadline);
}
function verifyDirectory(root, expected, deadline) {
  const files = [], directories = [];
  function walk(relative = '') {
    const full = deadlineFile.safePath(root, relative, deadline), stat = deadlineFile.io(deadline, () => fs.lstatSync(full));
    if (stat.isDirectory()) {
      if (relative) directories.push(relative);
      need(directories.length <= expected.directories.size, 'Extracted directory closed set differs');
      for (const name of deadlineFile.io(deadline, () => fs.readdirSync(full))) {
        deadlineFile.check(deadline); walk(relative ? relative + '/' + name : name);
      }
    } else {
      ordinary(full, deadline); files.push(relative); need(files.length <= expected.files.length, 'Extracted file closed set differs');
    }
  }
  walk();
  deadlineFile.check(deadline);
  need(isDeepStrictEqual(files.sort(), expected.files.map(row => row.path).sort()) &&
    isDeepStrictEqual(directories.sort(), [...expected.directories.values()].sort()), 'Extracted file/directory closed set differs');
  for (const file of expected.files) {
    const full = ordinary(deadlineFile.safePath(root, file.path, deadline), deadline);
    need(deadlineFile.io(deadline, () => fs.lstatSync(full)).size === file.bytes, 'Extracted file changed: ' + file.path);
    const actual = deadlineFile.pin(full, {deadline, maxBytes: file.bytes});
    need(actual.bytes === file.bytes && actual.sha256 === file.sha256, 'Extracted file changed: ' + file.path);
  }
  deadlineFile.check(deadline);
}
function destinationPath(destination, deadline) {
  need(typeof destination === 'string' && path.isAbsolute(destination), 'Absolute destination path required');
  destination = fullPath(destination, deadline);
  need(!deadlineFile.io(deadline, () => fs.existsSync(destination)), 'Extraction destination already exists');
  const parent = deadlineFile.safePath(path.dirname(destination), '', deadline);
  need(deadlineFile.io(deadline, () => fs.lstatSync(parent)).isDirectory(), 'Destination parent must already exist'); return destination;
}
function errorInfo(error) {return {message: error.message, code: error.code || null, errno: error.errno ?? null, syscall: error.syscall || null};}
function extract(options) {
  need(options && typeof options === 'object', 'Extraction options required');
  const {archive, files, sevenZipPath, timeoutMs = 120000} = options;
  need(Number.isSafeInteger(timeoutMs) && timeoutMs > 0 && timeoutMs <= LIMITS.timeoutMs, 'Invalid aggregate extraction timeoutMs');
  need(typeof sevenZipPath === 'string' && path.isAbsolute(sevenZipPath), 'Absolute archive tool path required');
  // One absolute monotonic deadline; no native step can restart the budget.
  const deadline = performance.now() + timeoutMs, expected = manifest(files, deadline), destination = destinationPath(options.destination, deadline);
  const parent = path.dirname(destination), steps = [], originalArchive = archivePin(archive, deadline);
  const tool = fullPath(sevenZipPath, deadline);
  const powershell = fullPath(options.powershellPath || path.join(process.env.SystemRoot || 'C:/Windows', 'System32/WindowsPowerShell/v1.0/powershell.exe'), deadline);
  const metadataScript = deadlineFile.safePath(__dirname, 'Archive-Members.ps1', deadline);
  const context = {schema: 'soui.archive-extraction/v1', archive: originalArchive, destination, files: expected.files.length,
    bytes: expected.bytes, timeoutMs, steps}; let stage, stageIdentity, failure, result;
  function remaining() {
    return Math.ceil(deadlineFile.check(deadline));
  }
  function run(name, executable, args) {
    const timeout = remaining(), started = performance.now();
    const output = cp.spawnSync(executable, args, {cwd: parent, encoding: 'utf8', windowsHide: true, timeout,
      maxBuffer: LIMITS.nativeOutputBytes, input: ''});
    const row = {name, executable, arguments: args, timeoutMs: timeout, elapsedMs: performance.now() - started,
      exitCode: output.status, signal: output.signal || null, stdout: output.stdout || '', stderr: output.stderr || '',
      error: output.error ? errorInfo(output.error) : null}; steps.push(row);
    if (output.error) throw output.error;
    need(output.status === 0 && !output.signal, 'Archive ' + name + ' failed with exit ' + output.status); return row.stdout;
  }
  function ownedStage(enforceDeadline = true) {
    const full = enforceDeadline ? deadlineFile.safePath(stage, '', deadline) : id.safePath(stage);
    const current = enforceDeadline ? deadlineFile.io(deadline, () => fs.lstatSync(full)) : fs.lstatSync(full);
    need(path.dirname(stage) === parent && path.basename(stage).startsWith('.soui-extract-') && current.isDirectory() &&
      current.dev === stageIdentity.dev && current.ino === stageIdentity.ino, 'Extraction stage ownership changed');
  }
  try {
    remaining(); context.tool = profiles.toolIdentity(tool, {deadline});
    const listed = run('members', powershell, ['-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', metadataScript, '-Archive', originalArchive.path]);
    archivePin(archive, deadline); checkMembers(deadlineFile.io(deadline, () => JSON.parse(listed.replace(/^\uFEFF/, ''))), expected, deadline);
    const args = ['-tzip', '-sccUTF-8', '-p-', '--', originalArchive.path];
    run('test', tool, ['t', ...args]); archivePin(archive, deadline);
    const candidate = path.join(parent, '.soui-extract-' + crypto.randomUUID().replaceAll('-', '').slice(0, 12));
    deadlineFile.check(deadline); fs.mkdirSync(candidate); stage = candidate;
    context.stage = stage; stageIdentity = fs.lstatSync(stage); deadlineFile.check(deadline);
    run('extract', tool, ['x', '-y', '-o' + stage, ...args]);
    archivePin(archive, deadline); ownedStage(); verifyDirectory(stage, expected, deadline); remaining();
    need(isDeepStrictEqual(context.tool, profiles.toolIdentity(tool, {deadline})), 'Archive tool/runtime identity changed');
    archivePin(archive, deadline); remaining(); destinationPath(destination, deadline); ownedStage();
    result = {...context, steps: steps.map(({stdout, stderr, ...row}) => ({...row,
      stdout: {bytes: Buffer.byteLength(stdout), sha256: deadlineFile.io(deadline, () => id.hash(Buffer.from(stdout)))},
      stderr: {bytes: Buffer.byteLength(stderr), sha256: deadlineFile.io(deadline, () => id.hash(Buffer.from(stderr)))}}))};
    remaining(); fs.renameSync(deadlineFile.safePath(stage, '', deadline), destination);
    context.published = true; result.published = true; remaining();
  } catch (error) {failure = error; error.extraction = context;}
  if (stage && fs.existsSync(stage)) {
    try {
      ownedStage(false);
      fs.rmSync(stage, {recursive: true, force: false});
    } catch (error) {
      if (failure) failure.cleanupFailure = {...errorInfo(error), stage};
      else {failure = error; error.extraction = context;}
    }
  }
  if (failure) throw failure; return result;
}
module.exports = {extract};

if (require.main === module) {
  try {
    const [plan, ...extra] = process.argv.slice(2); need(plan && !extra.length, 'Usage: archive-stage.cjs PLAN.json');
    need(fs.lstatSync(ordinary(plan)).size <= LIMITS.nativeOutputBytes, 'Extraction plan exceeds byte budget');
    const options = id.readJson(plan); keys(options, ['schema', 'archive', 'files', 'destination', 'sevenZipPath', 'timeoutMs'], 'extraction plan');
    need(options.schema === 'soui.archive-extraction-plan/v1', 'Unknown extraction plan schema');
    console.log(id.encoded(extract(options)));
  } catch (error) {
    console.error(error.message);
    if (error.extraction) console.error('Extraction diagnostics: ' + JSON.stringify({extraction: error.extraction,
      cleanupFailure: error.cleanupFailure || null, archiveToolObservation: error.archiveToolObservation || null}));
    process.exitCode = 1;
  }
}
