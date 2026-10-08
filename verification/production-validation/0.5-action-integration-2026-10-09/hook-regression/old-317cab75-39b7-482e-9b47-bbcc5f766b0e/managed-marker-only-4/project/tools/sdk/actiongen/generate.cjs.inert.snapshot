'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { createHash, randomBytes } = require('node:crypto');
const { emitCpp, emitTypescript, emitMarkdown } = require('./emit.cjs');
const generatorVersion = '1';
const manifestPath = 'contracts/generated/actiongen-manifest.json';
const outputPaths = ['app/generated/actions.hpp', 'web/src/generated/actions.ts',
  'contracts/generated/tools.json', 'docs/generated/actions.md'];

function fail(message) { throw new Error(message); }
function sha256(text) { return createHash('sha256').update(text).digest('hex'); }
function readBounded(file, maximum, singleLink = false) {
  const handle = fs.openSync(file, 'r');
  try {
    const stat = fs.fstatSync(handle);
    if (!stat.isFile() || stat.size > maximum) fail(`Expected a bounded file: ${file}`);
    if (singleLink && stat.nlink !== 1) fail(`Output file is hard-linked: ${file}`);
    const buffer = Buffer.alloc(maximum + 1);
    let count = 0;
    while (count < buffer.length) {
      const read = fs.readSync(handle, buffer, count, buffer.length - count, null);
      if (read === 0) break;
      count += read;
    }
    if (count > maximum) fail(`File exceeds the byte limit: ${file}`);
    return buffer.subarray(0, count);
  } finally { fs.closeSync(handle); }
}
function canonical(value) {
  if (Array.isArray(value)) return value.map(canonical);
  if (value !== null && typeof value === 'object') {
    const result = Object.create(null);
    for (const key of Object.keys(value).sort()) result[key] = canonical(value[key]);
    return result;
  }
  return value;
}
function exact(value, required) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.keys(value).some(key => !required.includes(key)) ||
      required.some(key => !Object.hasOwn(value, key))) fail('Unknown or missing source field.');
}
const keywords = new Set(('alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto bitand bitor bool break '+
  'case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue '+
  'co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float '+
  'for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected '+
  'public reflexpr register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct '+
  'switch synchronized template this thread_local throw true try typedef typeid typename union unsigned using virtual void '+
  'volatile wchar_t while xor xor_eq').split(' '));
// Tokens from SDK, C/C++ standard and Windows headers can expand before C++ parsing.
// Do not undefine them or rename JSON properties: reject these known collisions.
const macroNames = new Set(('NULL EOF BUFSIZ FILENAME_MAX FOPEN_MAX TMP_MAX L_tmpnam SEEK_SET SEEK_CUR SEEK_END '+
  'stdin stdout stderr errno assert CHAR_BIT CHAR_MIN CHAR_MAX SCHAR_MIN SCHAR_MAX UCHAR_MAX SHRT_MIN SHRT_MAX USHRT_MAX '+
  'INT_MIN INT_MAX UINT_MAX LONG_MIN LONG_MAX ULONG_MAX LLONG_MIN LLONG_MAX ULLONG_MAX '+
  'SIZE_MAX PTRDIFF_MIN PTRDIFF_MAX INTPTR_MIN INTPTR_MAX UINTPTR_MAX INTMAX_MIN INTMAX_MAX UINTMAX_MAX '+
  'WCHAR_MIN WCHAR_MAX WINT_MIN WINT_MAX SIG_ATOMIC_MIN SIG_ATOMIC_MAX '+
  'FLT_RADIX FLT_ROUNDS FLT_DIG FLT_MIN FLT_MAX FLT_EPSILON DBL_DIG DBL_MIN DBL_MAX DBL_EPSILON '+
  'LDBL_DIG LDBL_MIN LDBL_MAX LDBL_EPSILON NAN INFINITY HUGE_VAL HUGE_VALF HUGE_VALL '+
  'INTMAX_C UINTMAX_C EXIT_SUCCESS EXIT_FAILURE RAND_MAX MB_CUR_MAX '+
  'TRUE FALSE ERROR NO_ERROR MAX_PATH interface near far small pascal cdecl min max IN OUT OPTIONAL CONST '+
  'GetMessage PeekMessage DispatchMessage DefWindowProc SendMessage PostMessage MessageBox '+
  'CreateWindow CreateWindowEx RegisterClass RegisterClassEx UnregisterClass GetClassName '+
  'GetWindowText SetWindowText GetObject LoadString DrawText TextOut FormatMessage '+
  'CreateFile DeleteFile CopyFile MoveFile GetFileAttributes SetFileAttributes FindFirstFile FindNextFile '+
  'LoadLibrary GetModuleHandle GetCurrentDirectory SetCurrentDirectory GetCommandLine CreateProcess ShellExecute GetUserName '+
  'WINAPI CALLBACK APIENTRY SOUI_SDK_VERSION_MAJOR SOUI_SDK_VERSION_MINOR SOUI_SDK_VERSION_PATCH SOUI_SDK_VERSION_STRING').split(' '));
for (const group of ['', '_LEAST', '_FAST']) for (const width of [8, 16, 32, 64]) {
  macroNames.add(`INT${group}${width}_MIN`);
  macroNames.add(`INT${group}${width}_MAX`);
  macroNames.add(`UINT${group}${width}_MAX`);
}
for (const sign of ['INT', 'UINT']) for (const width of [8, 16, 32, 64]) macroNames.add(`${sign}${width}_C`);
function identifier(value) {
  if (typeof value !== 'string' || !/^[A-Za-z][A-Za-z0-9_]{0,63}$/.test(value) ||
      value.includes('__') || keywords.has(value)) fail(`Unsupported C++ identifier: ${String(value)}`);
  if (macroNames.has(value)) fail(`C++ identifier conflicts with a known header macro: ${value}`);
}
function fieldIdentifiers(schema) {
  if (schema.type === 'object') {
    for (const [key, value] of Object.entries(schema.properties)) {
      identifier(key);
      // Generated TS literals must not create prototype-mutating object members.
      if (key === 'constructor' || key === 'prototype') fail('Reserved generated property name.');
      fieldIdentifiers(value);
    }
  } else if (schema.type === 'array') fieldIdentifiers(schema.items);
}
async function readSource(file) {
  const bytes = readBounded(file, 524288);
  const text = bytes.toString('utf8');
  // Invalid UTF-8 must not silently become a replacement character.
  if (!bytes.equals(Buffer.from(text))) fail('Action source is not valid UTF-8.');
  const raw = JSON.parse(text);
  exact(raw, ['schema', 'cppNamespace', 'actions']);
  if (raw.schema !== 'soui.action-source/1') fail('Expected soui.action-source/1.');
  identifier(raw.cppNamespace);
  if (['std', 'soui', 'nlohmann'].includes(raw.cppNamespace)) fail('Generated types must use an application-owned namespace.');
  if (!Array.isArray(raw.actions) || raw.actions.length < 1 || raw.actions.length > 64)
    fail('Expected between one and 64 actions.');
  const core = await import(pathToFileURL(path.resolve(__dirname, '../../../sdk/web/src/action-contract-core.js')).href);
  const methods = new Set(), symbols = new Set();
  const actions = raw.actions.map(entry => {
    exact(entry, ['method', 'capability', 'cppName', 'action']);
    if (typeof entry.method !== 'string' || !/^[A-Za-z0-9_.:-]{1,128}$/.test(entry.method) ||
        /^(bridge|task|desktop)\./.test(entry.method)) fail('Action method must be an application-owned bounded name.');
    if (typeof entry.capability !== 'string' || !/^[A-Za-z0-9_.:-]{1,128}$/.test(entry.capability))
      fail('Action capability must be a bounded identifier.');
    identifier(entry.cppName);
    if (methods.has(entry.method) || symbols.has(entry.cppName)) fail('Duplicate method or C++ action name.');
    methods.add(entry.method); symbols.add(entry.cppName);
    const action = core.readSouiActionContract(entry.action, 'E_ACTION_SOURCE');
    fieldIdentifiers(action.inputSchema); fieldIdentifiers(action.outputSchema);
    if (Object.hasOwn(action.outputSchema.properties, 'error')) fail('Success output cannot declare the Bridge business-error field.');
    return { method: entry.method, capability: entry.capability, cppName: entry.cppName, action };
  });
  return canonical({ schema: raw.schema, cppNamespace: raw.cppNamespace, actions });
}

function safePath(root, relative) {
  const target = path.resolve(root, relative);
  if (!target.startsWith(root + path.sep)) fail('Output escapes the project root.');
  let current = root;
  for (const component of relative.split('/')) {
    current = path.join(current, component);
    // lstat also detects dangling links, unlike existsSync.
    try {
      const stat = fs.lstatSync(current);
      if (stat.isSymbolicLink()) fail('Output path contains a symlink or junction.');
      if (stat.isFile() && stat.nlink !== 1) fail(`Output file is hard-linked: ${current}`);
    }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
  return target;
}
function currentOwnership(root) {
  const manifest = safePath(root, manifestPath);
  if (!fs.existsSync(manifest)) {
    for (const relative of outputPaths) if (fs.existsSync(safePath(root, relative))) fail('Existing output has no generator ownership manifest.');
    return;
  }
  const saved = JSON.parse(readBounded(manifest, 8192, true).toString('utf8'));
  exact(saved, ['schema', 'generatorVersion', 'sourceSha256', 'outputs']);
  if (saved.schema !== 'soui.actiongen-manifest/1' || saved.generatorVersion !== generatorVersion ||
      !/^[a-f0-9]{64}$/.test(saved.sourceSha256) || !saved.outputs ||
      Object.keys(saved.outputs).sort().join('\n') !== [...outputPaths].sort().join('\n')) fail('Invalid output ownership manifest.');
  for (const relative of outputPaths) {
    const file = safePath(root, relative);
    if (!/^[a-f0-9]{64}$/.test(saved.outputs[relative])) fail('Invalid generated output hash.');
    if (!fs.existsSync(file)) continue; // Missing owned outputs can be regenerated explicitly.
    if (sha256(readBounded(file, 8388608, true)) !== saved.outputs[relative])
      fail(`Generated output was modified; restore or move it before regeneration: ${relative}`);
  }
}
function publishOutputs(root, outputs, paths) {
  const previous = Object.fromEntries(Object.keys(outputs).map(file => [file,
    fs.existsSync(paths[file]) ? sha256(readBounded(paths[file], 8388608, true)) : null]));
  const staged = [];
  const unchanged = file => {
    const target = safePath(root, file);
    const actual = fs.existsSync(target) ? sha256(readBounded(target, 8388608, true)) : null;
    if (actual !== previous[file]) fail(`Output changed during generation: ${file}`);
  };
  const sameFile = (stat, expected) => stat.dev === expected.dev && stat.ino === expected.ino;
  try {
    for (const [file, text] of Object.entries(outputs)) {
      const target = safePath(root, file);
      fs.mkdirSync(path.dirname(target), { recursive: true });
      const relative = `${path.posix.dirname(file)}/.${path.posix.basename(file)}.actiongen-${randomBytes(12).toString('hex')}.tmp`;
      const temporary = safePath(root, relative);
      // Exclusive creation never opens an existing link or business file for writing.
      const handle = fs.openSync(temporary, 'wx');
      try {
        const entry = { file, relative, temporary, identity: fs.fstatSync(handle, { bigint: true }) };
        staged.push(entry);
        fs.writeFileSync(handle, Buffer.from(text, 'utf8'));
        fs.fsyncSync(handle);
      } finally { fs.closeSync(handle); }
    }
    // Recheck the complete original write set before publishing the first output.
    for (const file of Object.keys(outputs)) unchanged(file);
    for (const entry of staged) {
      unchanged(entry.file);
      const temporary = safePath(root, entry.relative);
      const stat = fs.lstatSync(temporary, { bigint: true });
      if (!stat.isFile() || stat.nlink !== 1n || !sameFile(stat, entry.identity)) fail('Staged output identity changed.');
      // Rename replaces the directory entry, never writes through a late file link.
      // The manifest is last; partial group publication remains hash-detectable.
      fs.renameSync(temporary, paths[entry.file]);
    }
  } finally {
    for (const entry of staged) {
      try {
        const temporary = safePath(root, entry.relative);
        const stat = fs.lstatSync(temporary, { bigint: true });
        if (stat.isFile() && sameFile(stat, entry.identity)) fs.unlinkSync(temporary);
      } catch (error) { if (error.code !== 'ENOENT') throw error; }
    }
  }
}
function argumentsFor(argv) {
  const options = { check: false };
  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    if (arg === '--check' && !options.check) options.check = true;
    else if ((arg === '--source' || arg === '--root') && !options[arg.slice(2)] && argv[i + 1] && !argv[i + 1].startsWith('--'))
      options[arg.slice(2)] = argv[++i];
    else fail('Usage: actiongen.cjs --source <actions.json> --root <project> [--check]');
  }
  if (!options.source || !options.root) fail('Both --source and --root are required.');
  return options;
}
async function run(argv) {
  const options = argumentsFor(argv);
  const requestedRoot = path.resolve(options.root);
  if (fs.lstatSync(requestedRoot).isSymbolicLink() || !fs.statSync(requestedRoot).isDirectory()) fail('Project root must be a real directory.');
  const root = fs.realpathSync.native(requestedRoot);
  const source = await readSource(path.resolve(options.source));
  const identity = sha256(JSON.stringify(source));
  const note = `Generated by soui-actiongen/${generatorVersion}; source-sha256:${identity}`;
  const outputs = {
    [outputPaths[0]]: emitCpp(source, note),
    [outputPaths[1]]: emitTypescript(source, note),
    [outputPaths[2]]: JSON.stringify({ schema: 'soui.action-tools/1', advisory: true, sourceSha256: identity,
      tools: source.actions.map(({ method, action }) => ({ name: method,
        description: action.description || action.title, inputSchema: action.inputSchema })) }, null, 2) + '\n',
    [outputPaths[3]]: emitMarkdown(source, note)
  };
  const manifest = { schema: 'soui.actiongen-manifest/1', generatorVersion, sourceSha256: identity,
    outputs: Object.fromEntries(Object.entries(outputs).map(([file, text]) => [file, sha256(text)])) };
  outputs[manifestPath] = JSON.stringify(manifest, null, 2) + '\n';
  // Validate the complete write set before creating any directory or output.
  const paths = Object.fromEntries(Object.keys(outputs).map(file => [file, safePath(root, file)]));
  // Enforce manifest ownership and actual byte hashes for check as well as generate.
  currentOwnership(root);
  if (options.check) {
    for (const [file, text] of Object.entries(outputs)) {
      const target = paths[file];
      if (!fs.existsSync(target) || !readBounded(target, 8388608, true).equals(Buffer.from(text, 'utf8')))
        fail(`Missing or stale generated output: ${file}`);
    }
    console.log(`Action outputs checked (${identity}).`);
    return;
  }
  publishOutputs(root, outputs, paths);
  console.log(`Generated ${source.actions.length} action(s) (${identity}).`);
}

module.exports = { run };
