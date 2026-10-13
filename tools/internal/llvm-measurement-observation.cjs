'use strict';
// These are raw compile/object/dependency observations. The caller still owns
// compiler Job closure, and this module does not grant an OS or ABI qualification.
const fs = require('node:fs');
const path = require('node:path');
const profile = require('./llvm-profile-inputs.cjs');
const need = (ok, message) => { if (!ok) throw new Error(message); };
const norm = value => path.resolve(value).replaceAll('\\', '/').toLowerCase();
const inside = (root, value) => norm(value).startsWith(norm(root) + '/');
const targets = {
  ProcessTreeSampler: ['benchmarks/protocol/process-tree-sampler.cpp'],
  InputTrace: ['benchmarks/protocol/input-trace.cpp', 'benchmarks/protocol/desktop-observer.cpp'],
  OwnedProcessRunner: ['benchmarks/protocol/owned-process-runner.cpp', 'benchmarks/protocol/owned-tail-evidence.cpp'],
  OwnedProcessAssignmentFixture: ['tests/benchmark/owned-process-assignment-fixture.cpp', 'benchmarks/protocol/owned-tail-evidence.cpp'],
  OwnedTailProcessFixture: ['tests/benchmark/owned-tail-process-fixture.cpp'],
  OwnedTailMutationFixture: ['tests/benchmark/owned-tail-mutation-fixture.cpp', 'benchmarks/protocol/owned-tail-evidence.cpp'],
  OwnedTailApiFixture: ['tests/benchmark/owned-tail-api-fixture.cpp'],
  NativeTraceIdentity: ['tests/benchmark/native-trace-identity.cpp'],
  DisplayCalibration: ['benchmarks/protocol/display-calibration.cpp']
};
function text(filename) {
  const pin = profile.pin(filename); need(pin.bytes <= 16777216, 'Measurement observation exceeds 16 MiB');
  const raw = fs.readFileSync(filename); need(!raw.includes(0), 'NUL in LLVM measurement text');
  return {pin, value: raw.toString('utf8')};
}
function logicalLines(value) {
  const result = []; let pending = '', continued = false;
  for (const line of value.split(/\r?\n/)) {
    need(!(continued && line === ''), 'Unterminated Ninja continuation');
    pending = continued ? pending + line.replace(/^[ \t]+/, '') : line;
    const dollars = /\$*$/.exec(line)[0].length;
    if (dollars % 2) { pending = pending.slice(0, -1); continued = true; continue; }
    result.push(pending); continued = false;
  }
  need(!continued, 'Unterminated Ninja continuation'); return result;
}
function words(value) {
  const result = []; let word = '';
  const flush = () => { if (word) { result.push({kind: 'path', value: word}); word = ''; } };
  for (let i = 0; i < value.length; i++) {
    const c = value[i];
    if (c === '$') { const next = value[++i]; need(['$', ':', ' '].includes(next), 'Unsupported Ninja variable/escape'); word += next; }
    else if (c === ' ' || c === '\t') flush();
    else if (c === ':' || c === '|') {
      flush(); let separator = c;
      if (c === '|' && ['|', '@'].includes(value[i + 1])) separator += value[++i];
      result.push({kind: c === ':' ? 'colon' : 'separator', value: separator});
    } else word += c;
  }
  flush(); return result;
}
function edges(value) {
  return logicalLines(value).filter(line => line.startsWith('build ')).map(raw => {
    const tokens = words(raw.slice(6)), colons = tokens.flatMap((token, i) => token.kind === 'colon' ? [i] : []);
    need(colons.length === 1 && colons[0] > 0 && colons[0] + 1 < tokens.length, 'Malformed Ninja edge');
    const split = colons[0], outputSections = [[], []]; let section = 0;
    for (const token of tokens.slice(0, split)) {
      if (token.kind === 'separator') { need(token.value === '|' && section === 0 && outputSections[0].length, 'Malformed Ninja output separator'); section = 1; }
      else { need(token.kind === 'path', 'Invalid Ninja output'); outputSections[section].push(token.value); }
    }
    need(outputSections[section].length, 'Empty Ninja output section');
    const rule = tokens[split + 1]; need(rule.kind === 'path' && /^[A-Za-z0-9_.-]+$/.test(rule.value), 'Invalid Ninja rule');
    const inputSections = [[], [], [], []]; section = 0; let lastSeparator = false;
    for (const token of tokens.slice(split + 2)) {
      if (token.kind === 'separator') {
        const next = {'|': 1, '||': 2, '|@': 3}[token.value];
        need(!lastSeparator && next > section, 'Duplicate/empty/out-of-order Ninja input separator'); section = next; lastSeparator = true;
      } else { need(token.kind === 'path', 'Invalid Ninja input'); inputSections[section].push(token.value); lastSeparator = false; }
    }
    need(!lastSeparator, 'Empty final Ninja input section');
    return {raw, outputs: outputSections[0], implicitOutputs: outputSections[1], rule: rule.value,
      inputs: inputSections[0], implicitInputs: inputSections[1], orderOnlyInputs: inputSections[2], validations: inputSections[3]};
  });
}
function dependencies(value) {
  const result = []; let current;
  const finish = () => { if (current) { need(current.headers.length === current.count, 'Ninja dependency count differs'); result.push(current); } };
  for (const line of value.split(/\r?\n/)) {
    if (line === '') continue;
    const match = /^(.+): #deps (0|[1-9][0-9]*), deps mtime ([0-9]+) \((VALID|STALE)\)$/.exec(line);
    if (match) { finish(); current = {object: match[1], count: Number(match[2]), mtime: match[3], status: match[4], headers: []};
      need(Number.isSafeInteger(current.count) && current.count <= 100000, 'Ninja dependency count exceeds bound'); }
    else { need(current && /^    \S/.test(line), 'Malformed Ninja dependency line'); current.headers.push(line.slice(4));
      need(current.headers.length <= current.count, 'Excess Ninja dependency lines'); }
  }
  finish(); return result;
}
function bindDependencies(blocks, build, object) {
  const matches = blocks.filter(item => norm(path.resolve(build, item.object)) === norm(object));
  need(matches.length === 1, 'Missing/duplicate measurement dependency object');
  const record = matches[0];
  need(record.status === 'VALID' && Number.isSafeInteger(record.count) && record.count >= 0 &&
    record.headers.length === record.count &&
    new Set(record.headers.map(item => norm(path.resolve(build, item)))).size === record.count,
  'Invalid/count-mismatched/duplicate/stale measurement dependencies');
  return record;
}

// Windows argv quoting, for the direct clang-cl compile database command only.
function argv(command) {
  need(typeof command === 'string' && !/[\x00-\x08\x0a-\x1f]/.test(command), 'Invalid compile command');
  const result = []; let i = 0;
  // The CRT treats argv[0] as a quoted pathname, without backslash escaping.
  while (command[i] === ' ' || command[i] === '\t') i++;
  let program = '', programQuoted = false;
  while (i < command.length && (programQuoted || !/[ \t]/.test(command[i]))) {
    if (command[i] === '"') programQuoted = !programQuoted;
    else program += command[i];
    i++;
  }
  need(program && !programQuoted, 'Invalid compile executable quote'); result.push(program);
  while (i < command.length) {
    while (command[i] === ' ' || command[i] === '\t') i++;
    if (i === command.length) break;
    let value = '', quoted = false;
    while (i < command.length && (quoted || !/[ \t]/.test(command[i]))) {
      let slashes = 0; while (command[i] === '\\') { slashes++; i++; }
      if (command[i] === '"') { value += '\\'.repeat(Math.floor(slashes / 2));
        if (slashes % 2) { value += '"'; i++; }
        else if (quoted && command[i + 1] === '"') { value += '"'; i += 2; }
        else { quoted = !quoted; i++; } }
      else { value += '\\'.repeat(slashes);
        if (i === command.length || (!quoted && /[ \t]/.test(command[i]))) break;
        value += command[i++]; }
    }
    need(!quoted, 'Unterminated compile command quote'); result.push(value);
  }
  return result;
}
function verboseCommands(value) {
  return value.split(/\r?\n/).flatMap(raw => {
    const match = /^\[([1-9][0-9]*)\/([1-9][0-9]*)\] (.+)$/.exec(raw);
    if (!match) return [];
    const current = Number(match[1]), total = Number(match[2]);
    need(Number.isSafeInteger(current) && Number.isSafeInteger(total) && current <= total, 'Invalid Ninja progress prefix');
    return [{raw, command: match[3]}];
  });
}
function bindCompileCommand(command, commandsText, verboseText, build, object) {
  const expected = argv(command), hasResponse = args => args.some(arg => /^@|^[/-]clang:@/i.test(arg));
  const objectKey = value => path.win32.resolve(build, value).replaceAll('\\', '/').toLowerCase();
  need(path.win32.basename(expected[0]).toLowerCase() === 'clang-cl.exe' && !hasResponse(expected) &&
    !expected.some(arg => /^[/-]showincludes$/i.test(arg) || ['&&', '||', '&', '|'].includes(arg)),
    'Direct database command without dependency flag/response file required');
  const select = rows => rows.flatMap(row => {
    if (!row.command.includes('/Fo')) return [];
    const args = argv(row.command), outputs = args.filter(arg => arg.startsWith('/Fo'));
    if (!outputs.some(arg => objectKey(arg.slice(3)) === objectKey(object))) return [];
    need(outputs.length === 1, 'Ambiguous actual measurement object argument');
    return [{...row, args}];
  });
  const generated = select(commandsText.split(/\r?\n/).filter(Boolean).map(raw => ({raw, command: raw})));
  const actual = select(verboseCommands(verboseText));
  need(generated.length === 1 && actual.length === 1, 'Unique actual measurement compile command/verbose required');
  for (const row of [generated[0], actual[0]]) {
    need(!hasResponse(row.args) && row.args.filter(arg => arg === '/showIncludes').length === 1,
      'Exactly one actual measurement dependency flag without response file required');
    const filtered = row.args.filter(arg => arg !== '/showIncludes');
    need(filtered.length === expected.length && filtered.every((arg, i) => arg === expected[i]),
      'Measurement actual compile argv differs from database');
  }
  return {generatedCommand: generated[0].raw, actualVerbose: actual[0].raw,
    dependencyFlag: '/showIncludes', actualArgs: actual[0].args};
}
function coff(filename, architecture) {
  const pin = profile.pin(filename); need(pin.bytes >= 20 && pin.bytes <= 67108864, 'Invalid bounded measurement COFF object');
  const fd = fs.openSync(filename, 'r'), header = Buffer.alloc(20);
  try { need(fs.readSync(fd, header, 0, 20, 0) === 20, 'Truncated COFF object'); } finally { fs.closeSync(fd); }
  let machine = header.readUInt16LE(0), format = 'COFF';
  if (machine === 0 && header.readUInt16LE(2) === 65535) { need(header.readUInt16LE(4) >= 2 && pin.bytes >= 56, 'Invalid native BigObj'); machine = header.readUInt16LE(6); format = 'BigObj'; }
  need(machine === (architecture === 'x64' ? 34404 : 332), 'Measurement object target machine differs');
  return {...pin, machine, format};
}
function observe(request, build, root) {
  const configured = profile.inspectCache(request, build, path.join(root, 'benchmarks/protocol'), 'Release');
  const records = {database: text(path.join(build, 'compile_commands.json')), ninja: text(path.join(build, 'CMakeFiles/impl-Release.ninja')),
    commands: text(path.join(build, 'ninja-commands-Release.log')), dependencies: text(path.join(build, 'ninja-deps-Release.log')),
    verbose: text(path.join(build, 'compile.log'))};
  const commands = JSON.parse(records.database.value).filter(row => /(?:^|[\\/])Release[\\/]/.test(row.output || ''));
  const wanted = Object.entries(targets).flatMap(([target, files]) => files.map(relative => ({target, source: path.join(root, relative)})));
  need(commands.length === wanted.length && commands.length === 13, 'All nine measurement targets require exactly 13 Release compile records');
  const compileEdges = edges(records.ninja.value).filter(edge => /^CXX_COMPILER(?:_|$)/.test(edge.rule));
  need(compileEdges.length === commands.length, 'Measurement Release compiler edge count differs');
  const blocks = dependencies(records.dependencies.value); need(blocks.length === commands.length, 'Measurement dependency object count differs');
  const seen = new Set(), sourceSeen = new Set(), dependencyPins = new Map(), bindings = [];
  for (const command of commands) {
    need(norm(command.directory) === norm(build) && path.isAbsolute(command.file) && typeof command.output === 'string', 'Measurement compile cwd/source/output differs');
    const object = path.resolve(build, command.output); need(inside(build, object) && !seen.has(norm(object)), 'Duplicate/outside measurement object'); seen.add(norm(object));
    const selected = wanted.filter(item => norm(item.source) === norm(command.file) && norm(object).includes(`/cmakefiles/${item.target.toLowerCase()}.dir/release/`));
    need(selected.length === 1, 'Measurement compile record does not identify its target and original source');
    sourceSeen.add(selected[0].target + '|' + norm(command.file));
    const edge = compileEdges.filter(item => item.outputs.length === 1 && norm(path.resolve(build, item.outputs[0])) === norm(object));
    need(edge.length === 1 && edge[0].implicitOutputs.length === 0 && edge[0].inputs.length === 1 && edge[0].validations.length === 0 &&
      norm(path.resolve(build, edge[0].inputs[0])) === norm(command.file), 'Unique measurement source/object/compiler edge differs');
    const args = argv(command.command), d = request.value.discovery;
    const compiler = profile.toolPath(args[0], d.ToolPins.CLANG, request);
    for (const flag of ['--no-default-config', '/X', '/clang:-fms-compatibility-version=19.29.30159', '--target=' + d.Target])
      need(args.includes(flag), 'Measurement compile flag missing: ' + flag);
    need(args.filter(arg => arg === '/MD' || arg === '-MD').length === 1 && !args.some(arg => /^[-/]M[DT]d?$/.test(arg) && arg !== '/MD' && arg !== '-MD'), 'Measurement runtime must be the original Release dynamic CRT');
    need(!args.some(arg => /^\/(?:GL|LTCG)(?:$|:)/i.test(arg)), 'Unexpected measurement whole program flag');
    const includes = args.filter(arg => arg.startsWith('/imsvc')).map(arg => arg.slice(6));
    need(JSON.stringify(includes) === JSON.stringify(d.IncludeDirectories), 'Measurement compiler include selection differs');
    const outputArgs = args.filter(arg => arg.startsWith('/Fo'));
    need(outputArgs.length === 1 && norm(path.resolve(build, outputArgs[0].slice(3))) === norm(object) &&
      args.some(arg => norm(path.resolve(build, arg)) === norm(command.file)), 'Measurement actual command object/source differs');
    const commandEvidence = bindCompileCommand(command.command, records.commands.value, records.verbose.value, build, object);
    // Ninja's MSVC dependency log records headers, not the translation unit.
    // Source identity is already bound by the database, edge, command and raw pin.
    const deps = bindDependencies(blocks, build, object);
    const headers = deps.headers.map(raw => {
      const filename = path.resolve(build, raw);
      need(inside(root, filename) || d.IncludeDirectories.some(directory => inside(directory, filename)), 'Measurement dependency outside selected source/header roots');
      const value = profile.pin(filename); dependencyPins.set(norm(filename), value); return {raw, pin: value};
    });
    bindings.push({target: selected[0].target, source: profile.pin(command.file), object: coff(object, request.value.architecture),
      compiler, command: command.command, commandEvidence, edge: edge[0], dependencies: {count: deps.count, mtime: deps.mtime, status: deps.status, headers}});
  }
  need(sourceSeen.size === wanted.length, 'Measurement target/source membership is incomplete');
  const links = [], d = request.value.discovery;
  for (const target of Object.keys(targets)) {
    const output = path.join(build, 'Release', target + '.exe');
    const matches = edges(records.ninja.value).filter(edge => /^CXX_EXECUTABLE_LINKER(?:_|$)/.test(edge.rule) &&
      edge.outputs.length === 1 && norm(path.resolve(build, edge.outputs[0])) === norm(output));
    need(matches.length === 1 && bindings.filter(item => item.target === target).every(item =>
      matches[0].inputs.some(input => norm(path.resolve(build, input)) === norm(item.object.path))), 'Measurement linker edge omits its own actual objects');
    const actual = records.commands.value.split(/\r?\n/).filter(line => {
      const outputs = [...line.matchAll(/\/out:(?:"([^"\r\n]+)"|([^\s"]+))/gi)];
      return outputs.length === 1 && norm(path.resolve(build, outputs[0][1] || outputs[0][2])) === norm(output);
    });
    need(actual.length === 1 && verboseCommands(records.verbose.value).filter(row => row.command === actual[0]).length === 1,
      'Unique actual measurement link command is absent from commands/verbose');
    const tools = [...actual[0].matchAll(/(?:"([^"\r\n]*lld-link\.exe)"|([^\s"]*lld-link\.exe))(?=\s|$)/gi)];
    need(tools.length === 1, 'Measurement link command does not identify exactly one LLD executable');
    const linker = profile.toolPath(tools[0][1] || tools[0][2], d.ToolPins.LINKER, request);
    links.push({target, command: actual[0], linker, edge: matches[0], executable: profile.pin(output)});
  }
  return {schema: 'soui.llvm-measurement-observation/v1', ...configured, records: Object.fromEntries(Object.entries(records).map(([key, value]) => [key, value.pin])),
    bindings, links, dependencyPins: [...dependencyPins.values()].sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0),
    compileObjects: 13, executableTargets: 9, compiledObjectsVerified: true,
    compilerVerified: false, nativeClosureVerified: false, genericSupervisionPassed: null, qualificationPassed: false};
}
function verify(observation) {
  need(observation?.schema === 'soui.llvm-measurement-observation/v1' && observation.compileObjects === 13 &&
    observation.executableTargets === 9 && observation.bindings?.length === 13 && observation.links?.length === 9 && observation.compilerVerified === false &&
    observation.nativeClosureVerified === false && observation.qualificationPassed === false, 'Invalid measurement observation scope');
  Object.values(observation.records).forEach(profile.verifyPin);
  profile.verifyPin(observation.cache); observation.compilerRecords.forEach(record => profile.verifyPin(record.pin));
  observation.bindings.forEach(binding => { profile.verifyPin(binding.source); profile.verifyPin(binding.object); });
  observation.links.forEach(link => profile.verifyPin(link.executable));
  observation.dependencyPins.forEach(profile.verifyPin);
}
module.exports = {observe, verify, logicalLines, words, edges, dependencies, bindDependencies, argv, verboseCommands, bindCompileCommand, targets};
