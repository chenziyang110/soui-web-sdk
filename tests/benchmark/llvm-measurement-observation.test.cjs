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
test('measurement membership is nine real targets with thirteen original compile bindings', () => {
  assert.equal(Object.keys(reader.targets).length, 9);
  assert.equal(Object.values(reader.targets).flat().length, 13);
  assert.equal(reader.targets.OwnedProcessRunner.includes('benchmarks/protocol/owned-tail-evidence.cpp'), true);
  assert.equal(reader.targets.OwnedTailMutationFixture.includes('benchmarks/protocol/owned-tail-evidence.cpp'), true);
});

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
