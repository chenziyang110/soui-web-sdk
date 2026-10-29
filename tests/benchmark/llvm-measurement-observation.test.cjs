'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const reader = require('../../tools/internal/llvm-measurement-observation.cjs');

test('actual Ninja path grammar preserves drive colon, spaces, dollar and continuation', () => {
  const input = 'build CMakeFiles/Owned.dir/Release/a.cpp.obj: CXX_COMPILER__Owned_Release C$:/source$ dir/a$$.cpp $\n  | C$:/include/header.h || order\n';
  const [edge] = reader.edges(input);
  assert.deepEqual(edge.outputs, ['CMakeFiles/Owned.dir/Release/a.cpp.obj']);
  assert.deepEqual(edge.inputs, ['C:/source dir/a$.cpp']);
  assert.deepEqual(edge.implicitInputs, ['C:/include/header.h']);
  assert.deepEqual(edge.orderOnlyInputs, ['order']);
});
test('ambiguous Ninja edge delimiters and unsupported expansion are rejected', () => {
  for (const input of ['build a: rule C:/unescaped.cpp', 'build a: rule $variable', 'build a: rule x | | y',
    'build a: rule x || y | z', 'build a: rule x |', 'build | a: rule x', 'build a: rule x $']) {
    assert.throws(() => reader.edges(input), /Ninja|Malformed|Unsupported|Unterminated/);
  }
});
test('Ninja implicit output and validation inputs remain distinct', () => {
  const [edge] = reader.edges('build a | sidecar: rule src | implicit || order |@ validate');
  assert.deepEqual(edge.implicitOutputs, ['sidecar']);
  assert.deepEqual(edge.validations, ['validate']);
});
test('dependency extent is exact and VALID or STALE remains visible', () => {
  const blocks = reader.dependencies('a.obj: #deps 2, deps mtime 42 (VALID)\n    C:/src.cpp\n    C:/h.h\n\nb.obj: #deps 1, deps mtime 43 (STALE)\n    C:/b.cpp\n');
  assert.equal(blocks.length, 2);
  assert.deepEqual(blocks[0].headers, ['C:/src.cpp', 'C:/h.h']);
  assert.equal(blocks[1].status, 'STALE'); // observe() rejects this; parsing never relabels it VALID.
});
test('short, overlong, malformed and oversized dependency blocks fail', () => {
  for (const input of ['a.obj: #deps 2, deps mtime 1 (VALID)\n    a.cpp\n',
    'a.obj: #deps 1, deps mtime 1 (VALID)\n    a.cpp\n    b.h\n',
    '    orphan.h\n', 'a.obj: #deps 100001, deps mtime 1 (VALID)\n',
    'a.obj: #deps 1, deps mtime 1 (VALID)\n  wrong-indent.h\n']) {
    assert.throws(() => reader.dependencies(input), /Ninja|Excess|Malformed|bound/);
  }
});
test('direct Windows compile argv preserves quoted input and rejects unterminated quote', () => {
  assert.deepEqual(reader.argv('"C:\\LLVM dir\\clang-cl.exe" /Fo"C:\\build dir\\x.obj" /imsvc"C:\\SDK dir\\include" "C:\\src dir\\x.cpp"'),
    ['C:\\LLVM dir\\clang-cl.exe', '/FoC:\\build dir\\x.obj', '/imsvcC:\\SDK dir\\include', 'C:\\src dir\\x.cpp']);
  assert.throws(() => reader.argv('clang-cl.exe "unterminated'), /Unterminated/);
  assert.throws(() => reader.argv('clang-cl.exe\n/x'), /Invalid/);
});
test('measurement membership is ten real targets with fifteen original compile bindings', () => {
  assert.equal(Object.keys(reader.targets).length, 10);
  assert.equal(Object.values(reader.targets).flat().length, 15);
  assert.deepEqual(reader.targets.BurstInput, ['benchmarks/protocol/burst-input.cpp', 'benchmarks/protocol/burst-desktop-observer.cpp']);
  assert.equal(reader.targets.OwnedProcessRunner.includes('benchmarks/protocol/owned-tail-evidence.cpp'), true);
  assert.equal(reader.targets.OwnedTailMutationFixture.includes('benchmarks/protocol/owned-tail-evidence.cpp'), true);
});

// Authored object headers and command records exercise the reader only. No
// executable/compiler is launched and this fixture is never native evidence.
function completeMeasurementFixture(t) {
  const fs = require('node:fs'), path = require('node:path'), os = require('node:os'), vm = require('node:vm');
  const profile = require('../../tools/internal/llvm-profile-inputs.cjs');
  const temporaryRoot = fs.realpathSync(os.tmpdir());
  const root = fs.mkdtempSync(path.join(temporaryRoot, 'soui-measurement-observer-'));
  const build = path.join(root, 'out', 'benchmark-protocol');
  t.after(() => {
    assert.equal(fs.realpathSync(root), root);
    assert.equal(path.dirname(root), temporaryRoot);
    fs.rmSync(root, {recursive: true});
  });
  const write = (filename, bytes) => {fs.mkdirSync(path.dirname(filename), {recursive: true}); fs.writeFileSync(filename, bytes);};
  const slash = value => value.replaceAll('\\', '/');
  const ninja = value => slash(value).replaceAll('$', () => '$$').replaceAll(':', '$:').replaceAll(' ', '$ ');
  const clang = path.join(root, 'tools', 'clang-cl.exe'), linker = path.join(root, 'tools', 'lld-link.exe');
  write(clang, 'inert compiler fixture\n'); write(linker, 'inert linker fixture\n');
  const discovery = {ToolPins: {CLANG: profile.pin(clang), LINKER: profile.pin(linker)},
    Target: 'x86_64-pc-windows-msvc', IncludeDirectories: []};
  const request = {value: {architecture: 'x64', discovery}};
  write(path.join(build, 'CMakeCache.txt'), '# inert configured fixture\n');
  const configured = {cache: profile.pin(path.join(build, 'CMakeCache.txt')), compilerRecords: [],
    inputIdentity: 'authored-input-identity', target: discovery.Target};
  const filename = require.resolve('../../tools/internal/llvm-measurement-observation.cjs');
  const module = {exports: {}}, localRequire = require('node:module').createRequire(filename);
  vm.compileFunction(fs.readFileSync(filename, 'utf8'), ['module', 'require'], {filename})(module, name =>
    name === './llvm-profile-inputs.cjs' ? {...profile, inspectCache: () => configured,
      toolPath(value, pin) {assert.equal(path.resolve(value), path.resolve(pin.path)); profile.verifyPin(pin); return pin;}} : localRequire(name));
  const inventory = {
    ProcessTreeSampler: ['benchmarks/protocol/process-tree-sampler.cpp'],
    InputTrace: ['benchmarks/protocol/input-trace.cpp', 'benchmarks/protocol/desktop-observer.cpp'],
    OwnedProcessRunner: ['benchmarks/protocol/owned-process-runner.cpp', 'benchmarks/protocol/owned-tail-evidence.cpp'],
    OwnedProcessAssignmentFixture: ['tests/benchmark/owned-process-assignment-fixture.cpp', 'benchmarks/protocol/owned-tail-evidence.cpp'],
    OwnedTailProcessFixture: ['tests/benchmark/owned-tail-process-fixture.cpp'],
    OwnedTailMutationFixture: ['tests/benchmark/owned-tail-mutation-fixture.cpp', 'benchmarks/protocol/owned-tail-evidence.cpp'],
    OwnedTailApiFixture: ['tests/benchmark/owned-tail-api-fixture.cpp'],
    NativeTraceIdentity: ['tests/benchmark/native-trace-identity.cpp'],
    DisplayCalibration: ['benchmarks/protocol/display-calibration.cpp'],
    BurstInput: ['benchmarks/protocol/burst-input.cpp', 'benchmarks/protocol/burst-desktop-observer.cpp']
  };
  const commands = [], commandLines = [], edges = ['include CMakeFiles/common.ninja'], deps = [];
  for (const [target, files] of Object.entries(inventory)) {
    const objects = [];
    for (const relative of files) {
      const file = path.join(root, relative), output = `CMakeFiles/${target}.dir/Release/${path.basename(file)}.obj`;
      const object = path.join(build, output), bytes = Buffer.alloc(20); bytes.writeUInt16LE(34404);
      write(file, '// inert source fixture\n'); write(object, bytes); objects.push(output);
      const command = `"${slash(clang)}" --no-default-config /X /clang:-fms-compatibility-version=19.29.30159 --target=${discovery.Target} /MD /Fo"${output}" -c "${slash(file)}"`;
      commands.push({directory: build, output, file, command});
      commandLines.push(command.replace(' /MD ', ' /showIncludes /MD '));
      edges.push(`build ${output}: CXX_COMPILER__${target}_Release ${ninja(file)}`);
      deps.push(`${output}: #deps 0, deps mtime 1 (VALID)`);
    }
    const executable = path.join(build, 'Release', target + '.exe'); write(executable, 'inert executable fixture\n');
    edges.push(`build Release/${target}.exe: CXX_EXECUTABLE_LINKER__${target}_Release ${objects.join(' ')}`);
    commandLines.push(`"${slash(linker)}" /out:"${slash(executable)}" ${objects.join(' ')}`);
  }
  write(path.join(build, 'compile_commands.json'), JSON.stringify(commands));
  write(path.join(build, 'CMakeFiles/impl-Release.ninja'), edges.join('\n') + '\n');
  write(path.join(build, 'CMakeFiles/common.ninja'), `include CMakeFiles/rules.ninja\ncmake_ninja_workdir = ${ninja(build)}/\n`);
  write(path.join(build, 'CMakeFiles/rules.ninja'), '# inert rules fixture\n');
  write(path.join(build, 'ninja-deps-Release.log'), deps.join('\n') + '\n');
  write(path.join(build, 'ninja-commands-Release.log'), commandLines.join('\n') + '\n');
  write(path.join(build, 'compile.log'), commandLines.map((line, i) => `[${i + 1}/${commandLines.length}] ${line}`).join('\n') + '\n');
  return {reader: module.exports, request, build, root, commands, edges, write};
}

test('complete ten-target observation preserves both BurstInput objects and its actual link record', t => {
  const f = completeMeasurementFixture(t), observation = f.reader.observe(f.request, f.build, f.root);
  assert.equal(observation.compileObjects, 15);
  assert.equal(observation.executableTargets, 10);
  const bindings = observation.bindings.filter(row => row.target === 'BurstInput');
  assert.deepEqual(bindings.map(row => require('node:path').basename(row.source.path)), ['burst-input.cpp', 'burst-desktop-observer.cpp']);
  assert.equal(observation.links.filter(row => row.target === 'BurstInput').length, 1);
  f.reader.verify(observation);
});

test('historical nine-target observations cannot authorize the current inventory', () => {
  assert.throws(() => reader.verify({schema: 'soui.llvm-measurement-observation/v1', compileObjects: 13,
    executableTargets: 9, bindings: Array(13), links: Array(9), compilerVerified: false,
    nativeClosureVerified: false, qualificationPassed: false}), /scope/);
});

for (const change of ['missing-burst-object', 'foreign-compile-record', 'foreign-compile-edge', 'foreign-link-edge', 'missing-burst-link']) {
  test(`${change} is rejected without filtering actual compiler evidence`, t => {
    const f = completeMeasurementFixture(t), path = require('node:path');
    if (change === 'missing-burst-object') {
      f.commands.pop(); f.write(path.join(f.build, 'compile_commands.json'), JSON.stringify(f.commands));
    } else if (change === 'foreign-compile-record') {
      f.commands.push({...f.commands[0], file: path.join(f.root, 'foreign.cpp'), output: 'CMakeFiles/Foreign.dir/Release/foreign.cpp.obj'});
      f.write(path.join(f.build, 'compile_commands.json'), JSON.stringify(f.commands));
    } else {
      const edges = change === 'foreign-compile-edge' ? [...f.edges, 'build foreign.obj: CXX_COMPILER__Foreign_Release foreign.cpp'] :
        change === 'foreign-link-edge' ? [...f.edges, 'build Release/Foreign.exe: CXX_EXECUTABLE_LINKER__Foreign_Release foreign.obj'] :
        f.edges.filter(line => !line.startsWith('build Release/BurstInput.exe:'));
      f.write(path.join(f.build, 'CMakeFiles/impl-Release.ninja'), edges.join('\n') + '\n');
    }
    assert.throws(() => f.reader.observe(f.request, f.build, f.root), /15 Release compile records|edge count differs|linker edge/);
  });
}

test('closed MSVC Ninja header-only record binds without inventing the translation unit as a dependency', () => {
  const fs = require('node:fs'), path = require('node:path');
  const raw = fs.readFileSync(path.join(__dirname, 'fixtures/llvm-ninja-msvc-header-only-deps.txt'), 'utf8');
  const blocks = reader.dependencies(raw), build = path.resolve('unit-build');
  const record = reader.bindDependencies(blocks, build, path.resolve(build, blocks[0].object));
  assert.equal(record.count, 1);
  assert.equal(record.status, 'VALID');
  assert.deepEqual(record.headers, ['../../../sdk/src/host/cursor_queue.h']);
  assert.equal(record.headers.some(value => value.endsWith('cursor_queue.cpp')), false);
});
test('VALID zero-header records remain complete while stale, duplicate and mismatched records reject', () => {
  const path = require('node:path'), build = path.resolve('unit-build'), object = path.join(build, 'zero.obj');
  const [empty] = reader.dependencies('zero.obj: #deps 0, deps mtime 42 (VALID)\n');
  assert.strictEqual(reader.bindDependencies([empty], build, object), empty);
  assert.deepEqual(empty.headers, []);
  assert.throws(() => reader.bindDependencies([{...empty, status: 'STALE'}], build, object), /stale/);
  assert.throws(() => reader.bindDependencies([empty, {...empty}], build, object), /duplicate/);
  assert.throws(() => reader.bindDependencies([{...empty, count: 1}], build, object), /count-mismatched/);
  assert.throws(() => reader.bindDependencies([{...empty, count: 2, headers: ['header.h', './header.h']}], build, object), /duplicate/);
  assert.throws(() => reader.bindDependencies([empty], build, path.join(build, 'other.obj')), /Missing/);
});

test('common is mandatory in measurement and ordinary SDK snapshots and raw tampering rejects', () => {
  const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
  const profile = require('../../tools/internal/llvm-profile-inputs.cjs');
  const standard = require('../../tools/internal/llvm-build-observer.cjs');
  const fixture = require('./fixtures/ninja-workdir-reader.json');
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-ninja-common-'));
  try {
    const build = path.join(temporary, 'out', 'x86'), cmake = path.join(build, 'CMakeFiles');
    fs.mkdirSync(cmake, {recursive: true});
    const definition = 'cmake_ninja_workdir = ' + build.replaceAll('\\', '/').replaceAll('$', () => '$$').replaceAll(':', '$:').replaceAll(' ', '$ ') + '/';
    const impl = fixture.implInclude + '\n' + fixture.actualBuildLines.join('\n');
    fs.writeFileSync(path.join(cmake, 'impl-Release.ninja'), impl);
    fs.writeFileSync(path.join(cmake, 'common.ninja'), fixture.commonInclude + '\n' + definition);
    fs.writeFileSync(path.join(cmake, 'rules.ninja'), '# scope leaf\n');
    const records = Object.fromEntries(Object.entries({ninja: 'impl-Release.ninja', common: 'common.ninja', rules: 'rules.ninja'})
      .map(([key, file]) => [key, profile.pin(path.join(cmake, file))]));
    const binding = reader.ninjaWorkdirScope(impl, fs.readFileSync(records.common.path, 'utf8'), '', build);
    reader.verifyNinjaWorkdirRecords(records, binding, build);
    const row = {ninjaScopePins: records, ninjaWorkdirBinding: binding, evidencePins: Object.values(records)};
    standard.validateNinjaScope(row, temporary, 'x86', 'Release', false);
    assert.throws(() => reader.verifyNinjaWorkdirRecords({...records, common: undefined}, binding, build), /required/);
    assert.throws(() => standard.validateNinjaScope({...row, ninjaScopePins: undefined}, temporary, 'x86', 'Release', false), /required/);
    assert.throws(() => standard.validateNinjaScope({...row, evidencePins: row.evidencePins.filter(pin => pin !== records.common)}, temporary, 'x86', 'Release', false), /evidence snapshot/);
    assert.throws(() => reader.verifyNinjaWorkdirRecords(records, {...binding, value: 'F:/foreign/'}, build), /binding differs/);
    fs.appendFileSync(records.common.path, '\n# changed after observation\n');
    assert.throws(() => reader.verifyNinjaWorkdirRecords(records, binding, build), /changed/);
    assert.throws(() => standard.validateNinjaScope(row, temporary, 'x86', 'Release', false), /changed/);
    const scope = {schema: 'soui.llvm-measurement-observation/v1', compileObjects: 15, executableTargets: 10, bindings: Array(15), links: Array(10),
      compilerVerified: false, nativeClosureVerified: false, qualificationPassed: false, records: {ninja: records.ninja}};
    assert.throws(() => reader.verify(scope), /required/);
  } finally {
    assert.ok(path.dirname(temporary) === os.tmpdir() && path.basename(temporary).startsWith('soui-ninja-common-'));
    fs.rmSync(temporary, {recursive: true});
  }
});
