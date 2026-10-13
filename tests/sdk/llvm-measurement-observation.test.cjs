'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const reader = require('../../tools/internal/llvm-measurement-observation.cjs');
const build = 'F:/command unit/build';
const object = build + '/CMakeFiles/real.dir/Release/a.cpp.obj';
const database = '"C:\\tool space\\clang-cl.exe" --target=x86_64-pc-windows-msvc /nologo -DCMAKE_INTDIR=\\"Release\\" /MD /imsvc"C:/SDK include" /Fo"CMakeFiles\\real.dir\\Release\\a.cpp.obj" /FdCMakeFiles\\real.dir\\Release\\ -c -- "F:\\source space\\a.cpp"';
const generated = database.replace(' /MD ', '   /MD\t').replace(' /Fo', ' /showIncludes /Fo');
const verbose = '[2/22] ' + generated;
const bind = (commands = generated, observed = verbose, db = database) => reader.bindCompileCommand(db, commands, observed, build, object);
test('real reader binds CMake whitespace plus one showIncludes and preserves all raw commands', () => {
  const result = bind();
  assert.equal(result.generatedCommand, generated);
  assert.equal(result.actualVerbose, verbose);
  assert.equal(result.dependencyFlag, '/showIncludes');
  assert.deepEqual(result.actualArgs.filter(arg => arg !== '/showIncludes'), reader.argv(database));
});
test('CRT argv keeps a trailing backslash separate from the next unquoted argument', () => {
  assert.deepEqual(reader.argv('clang-cl.exe /Fdobj\\ -c -- a.cpp'), ['clang-cl.exe', '/Fdobj\\', '-c', '--', 'a.cpp']);
  assert.notDeepEqual(reader.argv('clang-cl.exe /Fdobj\\ -c'), reader.argv('clang-cl.exe "/Fdobj\\ -c"'));
});
test('CRT argv preserves escaped quotes, paired quotes and empty arguments', () => {
  assert.deepEqual(reader.argv('"C:\\tool space\\clang-cl.exe" -DNAME=\\"x\\" "a""b" ""'),
    ['C:\\tool space\\clang-cl.exe', '-DNAME="x"', 'a"b', '']);
});
test('an unmatched quote or control character is rejected', () => {
  assert.throws(() => reader.argv('clang-cl.exe "bad'), /Unterminated/);
  assert.throws(() => reader.argv('"clang-cl.exe'), /executable quote/);
  assert.throws(() => reader.argv('clang-cl.exe \x0ba.cpp'), /Invalid compile command/);
});
test('query command without an actual progress-prefixed verbose record is rejected', () => {
  assert.throws(() => bind(generated, ''), /Unique actual/);
  assert.throws(() => bind(generated, generated), /Unique actual/);
  assert.throws(() => bind(generated, 'diagnostic mentions ' + generated), /Unique actual/);
});
test('a partial verbose substring is not a full command record', () => {
  assert.throws(() => bind(generated, verbose + ' EXTRA'), /argv differs/);
});
test('duplicate generated or actual records for the same object are rejected', () => {
  assert.throws(() => bind(generated + '\n' + generated), /Unique actual/);
  assert.throws(() => bind(generated, verbose + '\n[3/22] ' + generated), /Unique actual/);
  assert.throws(() => bind(generated + '\n' + generated.replace('/MD', '/MT')), /Unique actual/);
});
test('missing or repeated dependency flags are rejected', () => {
  assert.throws(() => bind(database, '[2/22] ' + database), /Exactly one/);
  const twice = generated.replace('/showIncludes', '/showIncludes /showIncludes');
  assert.throws(() => bind(twice, '[2/22] ' + twice), /Exactly one/);
  assert.throws(() => bind(generated, verbose, database.replace('/MD', '/MD /showincludes')), /Direct database/);
});
test('a wrong compiler executable is not accepted by basename', () => {
  const changed = generated.replace('C:\\tool space', 'C:\\other tool space');
  assert.throws(() => bind(changed, '[2/22] ' + changed), /argv differs/);
});
test('changed target, CRT, define, include or argument order is rejected', () => {
  for (const changed of [generated.replace('x86_64', 'i686'), generated.replace('/MD', '/MT'),
    generated.replace('Release', 'Debug'), generated.replace('SDK include', 'other include'),
    generated.replace('/nologo -DCMAKE', '-DNEW=1 /nologo -DCMAKE'), generated.replace(' /nologo', '') + ' /nologo'])
    assert.throws(() => bind(changed, '[2/22] ' + changed), /argv differs/);
});
test('wrong object and source are independently rejected', () => {
  const wrongObject = generated.replace('a.cpp.obj', 'b.cpp.obj');
  assert.throws(() => bind(wrongObject, '[2/22] ' + wrongObject), /Unique actual/);
  const wrongSource = generated.replace('source space\\a.cpp', 'source space\\b.cpp');
  assert.throws(() => bind(wrongSource, '[2/22] ' + wrongSource), /argv differs/);
});
test('response-file and shell-command forms cannot stand in for direct compilation', () => {
  const response = generated.replace('/MD', '/MD @flags.rsp');
  assert.throws(() => bind(response, '[2/22] ' + response), /without response file/);
  assert.throws(() => bind(generated, verbose, database + ' && other.exe'), /Direct database/);
});
test('same normalized output with duplicate Fo options is rejected', () => {
  const changed = generated + ' /Fo"CMakeFiles/real.dir/Release/a.cpp.obj"';
  assert.throws(() => bind(changed, '[2/22] ' + changed), /Ambiguous actual/);
});
test('Ninja progress must be complete and ordered within its declared total', () => {
  assert.throws(() => bind(generated, '[23/22] ' + generated), /Invalid Ninja progress/);
  assert.throws(() => bind(generated, '[0/22] ' + generated), /Unique actual/);
});
