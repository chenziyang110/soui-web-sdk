'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const platform = require('../../tools/platform-info.cjs');

const root = path.resolve(__dirname, '../..');
const cli = path.join(root, 'tools/platform-info.cjs');
const cmakeCandidates = [
  process.env.CMAKE_COMMAND,
  path.join(root, '.build-tools/cmake/data/bin/cmake'),
  'cmake',
].filter(Boolean);
const cmake = cmakeCandidates.find((command) => {
  const result = spawnSync(command, ['--version'], { encoding: 'utf8', timeout: 10000 });
  return !result.error && !result.signal && result.status === 0;
});
const notSupported = (fn, message) => assert.throws(fn, (error) => {
  assert.equal(error.code, 'not_supported');
  if (message) assert.match(error.message, message);
  return true;
});

test('architecture aliases preserve ISA rather than infer x64 from pointer width', () => {
  const cases = {
    x86: ['x86', 'ia32', 'Win32', 'i386', 'i486', 'i586', 'i686'],
    x64: ['x64', 'AMD64', 'x86_64', 'x86-64'],
    arm32: ['arm', 'ARM32', 'armhf', 'armel', 'armv5tel', 'armv6l', 'armv7l', 'armv7hl', 'armv7-a', 'armv8l'],
    arm64: ['ARM64', 'aarch64'],
  };
  for (const [expected, aliases] of Object.entries(cases)) {
    for (const alias of aliases) {
      assert.equal(platform.normalizeArchitecture(alias), expected, alias);
      const target = platform.resolveTarget({
        os: 'Linux', processor: alias, pointerSize: expected.endsWith('64') ? 8 : 4,
      });
      assert.equal(target.arch, expected);
    }
  }
});

test('operating-system aliases normalize without accepting different target systems', () => {
  for (const alias of ['Windows', 'win32', 'Windows_NT']) assert.equal(platform.normalizeOs(alias), 'windows');
  for (const alias of ['Darwin', 'macos', 'osx']) assert.equal(platform.normalizeOs(alias), 'macos');
  assert.equal(platform.normalizeOs('Linux'), 'linux');
  for (const unknown of ['', 'FreeBSD', 'Android', 'iOS', 'WindowsStore']) {
    notSupported(() => platform.resolveTarget({ os: unknown, arch: 'x64', pointerSize: 8 }));
  }
});

test('unknown architectures and pointer-only guesses fail closed', () => {
  for (const unknown of ['', 'mips64', 'riscv64', 'ppc64', 'loongarch64', 'armv9', 'aarch64_be']) {
    notSupported(() => platform.resolveTarget({ os: 'linux', arch: unknown, pointerSize: 8 }));
  }
  notSupported(() => platform.resolveTarget({ os: 'linux', pointerSize: 8 }));
  notSupported(() => platform.resolveTarget({ os: 'linux', pointerSize: 4 }));
});

test('ARM64EC is rejected in every target hint', () => {
  for (const field of ['arch', 'processor', 'compilerArch', 'compilerArchitectureId', 'generatorPlatform', 'compilerTarget']) {
    notSupported(() => platform.resolveTarget({ os: 'windows', arch: 'x64', [field]: 'ARM64EC', pointerSize: 8 }), /ARM64EC/);
  }
});

test('pointer-size mismatch rejects x32, AArch64 ILP32 and incorrect declarations', () => {
  for (const [arch, pointerSize] of [['x86', 8], ['x64', 4], ['arm32', 8], ['arm64', 4], ['arm64', 16]]) {
    notSupported(() => platform.resolveTarget({ os: 'linux', arch, pointerSize }), /pointer/);
  }
  assert.equal(platform.resolveTarget({ os: 'linux', arch: 'arm64', pointerSize: 8 }).arch, 'arm64');
});

test('cross-compilation uses target hints rather than the separate host processor', () => {
  const target = platform.resolveTarget({
    os: 'linux', processor: 'aarch64', hostProcessor: 'AMD64', compilerTarget: 'aarch64-linux-gnu', pointerSize: 8,
  });
  assert.equal(target.arch, 'arm64');
  assert.equal(target.architectureSource, 'explicit_target');
  const windows = platform.resolveTarget({ os: 'windows', processor: 'AMD64', generatorPlatform: 'Win32,version=10.0', pointerSize: 4 });
  assert.equal(windows.arch, 'x86');
  const actual = platform.resolveTarget({ os: 'linux', processor: 'x86_64', compilerProbeArch: 'x86', pointerSize: 4 });
  assert.equal(actual.arch, 'x86');
});

test('conflicting explicit architecture hints fail even if pointer size agrees', () => {
  notSupported(() => platform.resolveTarget({ os: 'linux', arch: 'x64', compilerTarget: 'aarch64-linux-gnu', pointerSize: 8 }), /Conflicting/);
  notSupported(() => platform.resolveTarget({ os: 'windows', generatorPlatform: 'x64', compilerProbeArch: 'arm64', pointerSize: 8 }), /Conflicting/);
  notSupported(() => platform.resolveTarget({ os: 'linux', processor: 'aarch64', compilerProbeArch: 'x64', pointerSize: 8 }), /conflicts/);
  notSupported(() => platform.resolveTarget({ os: 'linux', processor: 'AMD64', compilerTarget: 'aarch64-linux-gnu', pointerSize: 8 }), /conflicts/);
  notSupported(() => platform.resolveTarget({ os: 'linux', processor: 'aarch64', compilerProbeArch: 'arm32', pointerSize: 4 }), /conflicts/);
  notSupported(() => platform.resolveTarget({ os: 'linux', processor: 'unknown', compilerProbeArch: 'x64', pointerSize: 8 }), /Unknown/);
});

test('Windows explicit compiler targets can differ from the CMake host processor', () => {
  for (const [selection, arch, pointerSize] of [['ARM', 'arm32', 4], ['ARM64', 'arm64', 8]]) {
    for (const field of ['generatorPlatform', 'compilerArchitectureId', 'compilerArch']) {
      const target = platform.resolveTarget({
        os: 'windows', processor: 'AMD64', [field]: selection,
        compilerProbeArch: arch, pointerSize,
      });
      assert.equal(target.arch, arch);
      notSupported(() => platform.resolveTarget({
        os: 'windows', processor: 'AMD64', [field]: selection,
        compilerProbeArch: 'x64', pointerSize: 8,
      }), /Conflicting/);
    }
  }
  const cross = platform.resolveTarget({
    os: 'windows', processor: 'AMD64', compilerTarget: 'aarch64-pc-windows-msvc',
    compilerProbeArch: 'arm64', pointerSize: 8,
  });
  assert.equal(cross.arch, 'arm64');
});

test('macOS universal2 is canonical and list-order independent', () => {
  for (const list of [['arm64', 'x86_64'], ['x86_64', 'arm64', 'arm64'], 'aarch64;AMD64', 'x64,arm64']) {
    const target = platform.resolveTarget({ os: 'darwin', osxArchitectures: list, pointerSize: 8 });
    assert.equal(target.arch, 'universal2');
    assert.deepEqual(target.architectures, ['x64', 'arm64']);
  }
  assert.equal(platform.resolveTarget({ os: 'macos', arch: 'universal2' }).arch, 'universal2');
  assert.equal(platform.resolveTarget({ os: 'macos', processor: 'arm64', osxArchitectures: ['x86_64'] }).arch, 'x64');
  notSupported(() => platform.resolveTarget({ os: 'linux', arch: 'universal2' }));
  notSupported(() => platform.resolveTarget({ os: 'macos', arch: 'universal2', osxArchitectures: ['arm64'] }));
  notSupported(() => platform.resolveTarget({ os: 'macos', osxArchitectures: 'arm64;;x86_64' }));
  notSupported(() => platform.resolveTarget({ os: 'macos', arch: 'universal2', compilerProbeArch: 'arm64' }));
});

test('macOS 32-bit targets are explicitly unsupported', () => {
  for (const arch of ['x86', 'arm32']) {
    notSupported(() => platform.resolveTarget({ os: 'macos', arch, pointerSize: 4 }), /macOS/);
    notSupported(() => platform.resolveTarget({ os: 'darwin', osxArchitectures: [arch, 'arm64'] }), /macOS/);
  }
});

test('desktop and binary dependencies reject all non-Windows x86/x64 targets', () => {
  for (const targetOs of ['windows', 'linux', 'macos']) {
    for (const arch of targetOs === 'macos' ? ['x64', 'arm64', 'universal2'] : ['x86', 'x64', 'arm32', 'arm64']) {
      const target = platform.resolveTarget({ os: targetOs, arch });
      assert.equal(platform.requireCapability(target, 'portable_core').status, 'supported');
      for (const name of ['desktop_host', 'soui_prebuilt', 'miniblink_prebuilt']) {
        if (targetOs === 'windows' && ['x86', 'x64'].includes(arch)) {
          assert.equal(platform.requireCapability(target, name).status, 'supported');
        } else notSupported(() => platform.requireCapability(target, name), /Windows x86\/x64 only/);
      }
    }
  }
  notSupported(() => platform.requireCapability({ os: 'linux', arch: 'x64' }, 'unrecognized'));
});

test('matrix records policy, unprobed toolchains and no invented validation', () => {
  const matrix = platform.getMatrix();
  assert.equal(matrix.targets.length, 13);
  assert.deepEqual(matrix.excludedArchitectures, ['ARM64EC']);
  for (const target of matrix.targets) {
    assert.equal(target.toolchain.status, 'not_probed');
    for (const capability of Object.values(target.capabilities)) assert.equal(capability.validation, 'not_tested');
    if (target.os === 'macos' && ['x86', 'arm32'].includes(target.arch)) {
      assert.equal(target.capabilities.portable_core.status, 'not_supported');
    }
  }
});

test('current-platform information identifies Node process and unprobed compiler', () => {
  const info = platform.getPlatformInfo();
  assert.equal(info.host.source, 'node_process');
  assert.equal(info.target.architectureSource, 'node_process');
  assert.equal(info.target.pointerSize, null);
  assert.equal(info.target.crossCompiling, false);
  assert.equal(info.toolchain.status, 'not_probed');
  notSupported(() => platform.getPlatformInfo({ pointerSize: info.target.expectedPointerSize === 8 ? 4 : 8 }));
});

test('system WebView source policy is distinct from the legacy desktop binaries', () => {
  for (const [os, arch] of [['linux', 'x64'], ['linux', 'arm64'], ['macos', 'arm64'], ['windows', 'arm64']]) {
    const target = platform.resolveTarget({ os, arch });
    assert.equal(platform.requireCapability(target, 'system_desktop').status, 'supported');
    assert.equal(platform.requireCapability(target, 'system_desktop').validation, 'not_tested');
    notSupported(() => platform.requireCapability(target, 'desktop_host'));
  }
  notSupported(() => platform.requireCapability(platform.resolveTarget({ os: 'windows', arch: 'arm32' }), 'system_desktop'));
});

function runCommand(command, args, options) {
  const result = spawnSync(command, args, options);
  assert.ifError(result.error);
  assert.ok(!result.signal, `Child terminated by ${result.signal}`);
  assert.ok(Number.isInteger(result.status), 'Child did not report a normal exit status');
  return result;
}

function runCli(args) {
  return runCommand(process.execPath, [cli, ...args],
    { encoding: 'utf8', timeout: 10000, windowsHide: true });
}

test('CLI produces JSON and fails unsupported requirements with nonzero status', { timeout: 50000 }, () => {
  const matrix = runCli(['--matrix', '--json']);
  assert.equal(matrix.status, 0, matrix.stderr);
  assert.equal(JSON.parse(matrix.stdout).kind, 'support_matrix');
  const arm = runCli(['--os', 'windows', '--arch', 'aarch64', '--pointer-size', '8']);
  assert.equal(arm.status, 0, arm.stderr);
  assert.equal(JSON.parse(arm.stdout).target.arch, 'arm64');
  const refused = runCli(['--os', 'linux', '--arch', 'x64', '--require', 'desktop_host']);
  assert.notEqual(refused.status, 0);
  assert.equal(JSON.parse(refused.stderr).error, 'not_supported');
  const unknown = runCli(['--arch', 'riscv64']);
  assert.notEqual(unknown.status, 0);
  assert.equal(JSON.parse(unknown.stderr).error, 'not_supported');
});

test('CLI rejects ambiguous/missing arguments and ignored probe options', () => {
  for (const args of [['--arch'], ['--unknown'], ['--matrix', '--arch', 'x64'], ['--compiler', 'g++']]) {
    notSupported(() => platform.parseArguments(args));
  }
});

test('missing local CMake reports unavailable without downloading tools', { timeout: 20000 }, () => {
  const result = platform.probeToolchain({ os: 'linux', arch: 'x64' }, { cmake: path.join(root, 'nonexistent-cmake-for-test') });
  assert.equal(result.status, 'unavailable');
  assert.equal(result.stage, 'cmake');
  assert.equal(result.runtimeValidation, 'not_tested');
});

function cmakeScript(variables, extra = '') {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-platform-test-'));
  try {
    const source = [
      'cmake_minimum_required(VERSION 3.24)',
      ...Object.entries(variables).map(([name, value]) => `set(${name} "${String(value).replace(/\\/g, '/').replace(/"/g, '\\"')}")`),
      `include("${path.join(root, 'cmake/SouiPlatform.cmake').replace(/\\/g, '/')}")`,
      'soui_detect_platform()',
      'message(STATUS "TARGET=${SOUI_TARGET_OS}/${SOUI_TARGET_ARCH};ARCHES=${SOUI_TARGET_ARCHITECTURES};POINTER=${SOUI_TARGET_POINTER_SIZE};CROSS=${SOUI_TARGET_CROSSCOMPILING};DESKTOP=${SOUI_DESKTOP_HOST_SUPPORTED}")',
      extra,
    ].join('\n');
    const file = path.join(directory, 'test.cmake');
    fs.writeFileSync(file, source);
    return runCommand(cmake, ['-P', file], { encoding: 'utf8', timeout: 30000 });
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
}

test('CMake synthetic target variables normalize aliases and preserve ARM64', { skip: !cmake && 'CMake is not installed', timeout: 160000 }, () => {
  for (const [system, processor, pointer, expected] of [
    ['Windows', 'AMD64', 8, 'windows/x64'], ['Windows', 'ARM64', 8, 'windows/arm64'],
    ['Linux', 'armv7l', 4, 'linux/arm32'], ['Linux', 'i686', 4, 'linux/x86'],
    ['Darwin', 'aarch64', 8, 'macos/arm64'],
  ]) {
    const result = cmakeScript({ CMAKE_SYSTEM_NAME: system, CMAKE_SYSTEM_PROCESSOR: processor, CMAKE_SIZEOF_VOID_P: pointer });
    assert.equal(result.status, 0, result.stderr);
    assert.match(result.stdout, new RegExp(`TARGET=${expected}`));
  }
});

test('CMake cross builds use declared target rather than host processor', { skip: !cmake && 'CMake is not installed', timeout: 70000 }, () => {
  const result = cmakeScript({
    CMAKE_SYSTEM_NAME: 'Linux', CMAKE_HOST_SYSTEM_NAME: 'Windows',
    CMAKE_SYSTEM_PROCESSOR: 'aarch64', CMAKE_HOST_SYSTEM_PROCESSOR: 'AMD64',
    CMAKE_CXX_COMPILER_TARGET: 'aarch64-linux-gnu', CMAKE_SIZEOF_VOID_P: 8, CMAKE_CROSSCOMPILING: true,
  });
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /TARGET=linux\/arm64.*CROSS=true/);
  const x86 = cmakeScript({
    CMAKE_SYSTEM_NAME: 'Windows', CMAKE_SYSTEM_PROCESSOR: 'AMD64',
    CMAKE_GENERATOR_PLATFORM: 'Win32', CMAKE_SIZEOF_VOID_P: 4,
  });
  assert.equal(x86.status, 0, x86.stderr);
  assert.match(x86.stdout, /TARGET=windows\/x86/);
});

test('CMake Windows VS and compiler architecture selections override the host processor', { skip: !cmake && 'CMake is not installed', timeout: 220000 }, () => {
  for (const [selection, arch, pointerSize] of [['ARM', 'arm32', 4], ['ARM64', 'arm64', 8]]) {
    for (const field of ['CMAKE_GENERATOR_PLATFORM', 'CMAKE_VS_PLATFORM_NAME', 'CMAKE_CXX_COMPILER_ARCHITECTURE_ID']) {
      const result = cmakeScript({
        CMAKE_SYSTEM_NAME: 'Windows', CMAKE_SYSTEM_PROCESSOR: 'AMD64',
        [field]: selection, CMAKE_SIZEOF_VOID_P: pointerSize,
      });
      assert.equal(result.status, 0, result.stderr);
      assert.match(result.stdout, new RegExp(`TARGET=windows/${arch};`));
      assert.match(result.stdout, /DESKTOP=FALSE/);
    }
  }
  const target = cmakeScript({
    CMAKE_SYSTEM_NAME: 'Windows', CMAKE_SYSTEM_PROCESSOR: 'AMD64',
    CMAKE_CXX_COMPILER_TARGET: 'aarch64-pc-windows-msvc', CMAKE_SIZEOF_VOID_P: 8,
  });
  assert.equal(target.status, 0, target.stderr);
  assert.match(target.stdout, /TARGET=windows\/arm64;/);
});

test('CMake rejects unknown systems/architectures, ARM64EC and pointer conflicts', { skip: !cmake && 'CMake is not installed', timeout: 340000 }, () => {
  const base = { CMAKE_SYSTEM_NAME: 'Linux', CMAKE_SYSTEM_PROCESSOR: 'x86_64', CMAKE_SIZEOF_VOID_P: 8 };
  for (const override of [
    { CMAKE_SYSTEM_NAME: 'FreeBSD' }, { CMAKE_SYSTEM_PROCESSOR: 'mips64' },
    { CMAKE_SYSTEM_PROCESSOR: 'ARM64EC' }, { CMAKE_CXX_COMPILER_ARCHITECTURE_ID: 'ARM64EC' },
    { CMAKE_SIZEOF_VOID_P: 4 }, { CMAKE_SIZEOF_VOID_P: '' },
    { CMAKE_OSX_ARCHITECTURES: 'x86_64;arm64' },
    { CMAKE_CXX_COMPILER_ARCHITECTURE_ID: 'x64', CMAKE_GENERATOR_PLATFORM: 'ARM64' },
    { CMAKE_SYSTEM_PROCESSOR: 'aarch64', CMAKE_CXX_COMPILER_ARCHITECTURE_ID: 'x64' },
    { CMAKE_SYSTEM_PROCESSOR: 'aarch64', CMAKE_CXX_COMPILER_ARCHITECTURE_ID: 'ARM', CMAKE_SIZEOF_VOID_P: 4 },
    { CMAKE_SYSTEM_NAME: 'Darwin', CMAKE_SYSTEM_PROCESSOR: 'x86', CMAKE_SIZEOF_VOID_P: 4 },
  ]) {
    const result = cmakeScript({ ...base, ...override });
    assert.notEqual(result.status, 0, JSON.stringify(override));
    assert.match(result.stderr, /not_supported/);
  }
});

test('CMake macOS multi-architecture normalization matches the Node contract', { skip: !cmake && 'CMake is not installed', timeout: 160000 }, () => {
  for (const arches of ['x86_64;arm64', 'arm64;x86_64', 'arm64;x86_64;arm64']) {
    const result = cmakeScript({
      CMAKE_SYSTEM_NAME: 'Darwin', CMAKE_SYSTEM_PROCESSOR: 'arm64',
      CMAKE_OSX_ARCHITECTURES: arches, CMAKE_SIZEOF_VOID_P: 8,
    });
    assert.equal(result.status, 0, result.stderr);
    assert.match(result.stdout, /TARGET=macos\/universal2;ARCHES=x64;arm64/);
  }
  const invalid = cmakeScript({
    CMAKE_SYSTEM_NAME: 'Darwin', CMAKE_SYSTEM_PROCESSOR: 'arm64',
    CMAKE_OSX_ARCHITECTURES: 'arm64;i386', CMAKE_SIZEOF_VOID_P: 8,
  });
  assert.notEqual(invalid.status, 0);
  assert.match(invalid.stderr, /macOS supports only/);
  const thinCross = cmakeScript({
    CMAKE_SYSTEM_NAME: 'Darwin', CMAKE_SYSTEM_PROCESSOR: 'arm64',
    CMAKE_OSX_ARCHITECTURES: 'x86_64', CMAKE_SIZEOF_VOID_P: 8,
  });
  assert.equal(thinCross.status, 0, thinCross.stderr);
  assert.match(thinCross.stdout, /TARGET=macos\/x64;ARCHES=x64/);
});

test('CMake dependency requests explicitly fail on unsupported target', { skip: !cmake && 'CMake is not installed', timeout: 280000 }, () => {
  for (const capability of ['desktop_host', 'soui_prebuilt', 'miniblink_prebuilt']) {
    for (const [system, arch] of [['Linux', 'x86_64'], ['Windows', 'ARM64'], ['Darwin', 'arm64']]) {
      const result = cmakeScript({
        CMAKE_SYSTEM_NAME: system, CMAKE_SYSTEM_PROCESSOR: arch, CMAKE_SIZEOF_VOID_P: 8,
      }, `soui_require_capability(${capability})`);
      assert.notEqual(result.status, 0);
      assert.match(result.stderr, /not_supported/);
      assert.match(result.stderr, /Windows\s+x86\/x64 only/);
    }
  }
});

test('local compiler probe configures and links only its exact requested target', { skip: !cmake && 'CMake is not installed', timeout: 260000 }, () => {
  const target = platform.resolveTarget({ os: process.platform, arch: process.arch });
  const result = platform.probeToolchain(target, { cmake });
  assert.equal(result.status, 'available', JSON.stringify(result));
  assert.equal(result.stage, 'compile_and_link');
  assert.equal(result.runtimeValidation, 'not_tested');
});

test('local compiler probe rejects a mismatched requested target', { skip: !cmake && 'CMake is not installed', timeout: 260000 }, () => {
  const target = platform.resolveTarget({ os: process.platform, arch: process.arch });
  const mismatched = { ...target, os: target.os === 'windows' ? 'linux' : 'windows' };
  const refused = platform.probeToolchain(mismatched, { cmake });
  assert.equal(refused.status, 'unavailable');
  assert.equal(refused.stage, 'configure');
  assert.match(refused.reason, /does not match requested target/);
});

test('real configure rejects an ARM cross-toolchain that accidentally uses the x64 host compiler', {
  skip: (!cmake || process.platform !== 'linux' || process.arch !== 'x64') && 'Requires a Linux x64 compiler host',
  timeout: 130000,
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-wrong-cross-test-'));
  try {
    fs.writeFileSync(path.join(directory, 'CMakeLists.txt'), [
      'cmake_minimum_required(VERSION 3.24)',
      'project(SouiWrongCrossTarget LANGUAGES CXX)',
      `include("${path.join(root, 'cmake/SouiPlatform.cmake').replace(/\\/g, '/')}")`,
      'soui_detect_platform()',
      '',
    ].join('\n'));
    // Declaring the target does not turn the native compiler into a cross
    // compiler. This is deliberately a broken but otherwise configurable file.
    const toolchain = path.join(directory, 'wrong-toolchain.cmake');
    fs.writeFileSync(toolchain, 'set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR aarch64)\n');
    const result = runCommand(cmake, ['-S', directory, '-B', path.join(directory, 'build'),
      `-DCMAKE_TOOLCHAIN_FILE=${toolchain}`], { encoding: 'utf8', timeout: 120000 });
    assert.notEqual(result.status, 0, result.stdout);
    const message = result.stderr.replace(/\s+/g, ' ');
    assert.match(message, /CMAKE_SYSTEM_PROCESSOR 'arm64' conflicts with compiler target 'x64'/);
    assert.match(message, /host compiler cannot substitute/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('real compiler OS macros reject a mislabeled CMAKE_SYSTEM_NAME', {
  skip: (!cmake || process.platform !== 'linux') && 'Requires a Linux compiler host',
  timeout: 130000,
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-wrong-os-test-'));
  try {
    fs.writeFileSync(path.join(directory, 'CMakeLists.txt'), [
      'cmake_minimum_required(VERSION 3.24)',
      'project(SouiWrongOperatingSystem LANGUAGES CXX)',
      'set(CMAKE_SYSTEM_NAME Darwin)',
      `include("${path.join(root, 'cmake/SouiPlatform.cmake').replace(/\\/g, '/')}")`,
      'soui_detect_platform()',
      '',
    ].join('\n'));
    const result = runCommand(cmake, ['-S', directory, '-B', path.join(directory, 'build')],
      { encoding: 'utf8', timeout: 120000 });
    assert.notEqual(result.status, 0, result.stdout);
    assert.match(result.stderr.replace(/\s+/g, ' '), /compiler OS\/architecture probe failed/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('real compiler probes reject a false compiler architecture declaration', {
  skip: (!cmake || process.platform !== 'linux' || process.arch !== 'x64') && 'Requires a Linux x64 compiler host',
  timeout: 130000,
}, () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-wrong-compiler-arch-test-'));
  try {
    fs.writeFileSync(path.join(directory, 'CMakeLists.txt'), [
      'cmake_minimum_required(VERSION 3.24)',
      'project(SouiWrongCompilerArchitecture LANGUAGES CXX)',
      'set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID ARM64)',
      `include("${path.join(root, 'cmake/SouiPlatform.cmake').replace(/\\/g, '/')}")`,
      'soui_detect_platform()',
      '',
    ].join('\n'));
    const result = runCommand(cmake, ['-S', directory, '-B', path.join(directory, 'build')],
      { encoding: 'utf8', timeout: 120000 });
    assert.notEqual(result.status, 0, result.stdout);
    assert.match(result.stderr.replace(/\s+/g, ' '), /conflicting target architecture 'arm64' and compiler target 'x64'/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test('legacy libstdc++ ABI fails configuration without weakening TaskStore swap guarantees', {
  skip: !cmake && 'CMake is not installed',
  timeout: 130000,
}, (context) => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-legacy-abi-test-'));
  try {
    fs.writeFileSync(path.join(directory, 'CMakeLists.txt'), [
      'cmake_minimum_required(VERSION 3.24)',
      'project(SouiLegacyStringAbi LANGUAGES CXX)',
      'include(CheckCXXSourceCompiles)',
      'check_cxx_source_compiles("#include <string>\\n#ifndef __GLIBCXX__\\n#error Not libstdc++\\n#endif\\nint main() { return 0; }" USES_LIBSTDCXX)',
      'if(NOT USES_LIBSTDCXX)',
      '  message(STATUS "SOUI_TEST_NOT_LIBSTDCXX")',
      '  return()',
      'endif()',
      'set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_USE_CXX11_ABI=0")',
      `include("${path.join(root, 'sdk/cmake/SouiPortableRuntime.cmake').replace(/\\/g, '/')}")`,
      '',
    ].join('\n'));
    const result = runCommand(cmake, ['-S', directory, '-B', path.join(directory, 'build')],
      { encoding: 'utf8', timeout: 120000 });
    if (result.status === 0 && result.stdout.includes('SOUI_TEST_NOT_LIBSTDCXX')) {
      context.skip('Selected compiler uses a different standard library; legacy libstdc++ ABI is inapplicable');
      return;
    }
    assert.notEqual(result.status, 0, result.stdout);
    assert.match(result.stderr.replace(/\s+/g, ' '), /existing TaskStore non-throwing swap contract/);
    assert.match(result.stderr, /_GLIBCXX_USE_CXX11_ABI=1/);
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});


test('CMake macOS compiler identification accepts slice lists without relaxing other hints', { skip: !cmake && 'CMake is not installed', timeout: 700000 }, () => {
  const base = { CMAKE_SYSTEM_NAME: 'Darwin', CMAKE_SYSTEM_PROCESSOR: 'arm64',
    CMAKE_OSX_ARCHITECTURES: 'arm64;x86_64', CMAKE_SIZEOF_VOID_P: 8 };
  for (const field of ['CMAKE_C_COMPILER_ARCHITECTURE_ID', 'CMAKE_CXX_COMPILER_ARCHITECTURE_ID']) {
    for (const value of ['arm64;x86_64', 'x86_64;arm64']) {
      const result = cmakeScript({ ...base, [field]: value });
      assert.equal(result.status, 0, result.stderr);
      assert.match(result.stdout, /TARGET=macos\/universal2;ARCHES=x64;arm64/);
    }
    const thin = cmakeScript({ ...base, CMAKE_OSX_ARCHITECTURES: 'arm64', [field]: 'arm64' });
    assert.equal(thin.status, 0, thin.stderr);
    assert.match(thin.stdout, /TARGET=macos\/arm64;/);
    for (const value of ['arm64;;x86_64', ';arm64;x86_64', 'arm64;x86_64;', 'arm64;ARM64EC', 'arm64;mips64']) {
      const result = cmakeScript({ ...base, [field]: value });
      assert.notEqual(result.status, 0, value);
      assert.match(result.stderr, /not_supported/);
    }
    const conflict = cmakeScript({ ...base, CMAKE_OSX_ARCHITECTURES: 'arm64', [field]: 'arm64;x86_64' });
    assert.notEqual(conflict.status, 0);
    assert.match(conflict.stderr, /conflicting/);
    for (const system of ['Linux', 'Windows']) {
      const result = cmakeScript({ ...base, CMAKE_SYSTEM_NAME: system, CMAKE_OSX_ARCHITECTURES: '', [field]: 'arm64;x86_64' });
      assert.notEqual(result.status, 0, system);
      assert.match(result.stderr, /unknown target architecture/);
    }
  }
  const generator = cmakeScript({ ...base, CMAKE_GENERATOR_PLATFORM: 'arm64;x86_64' });
  assert.notEqual(generator.status, 0);
  assert.match(generator.stderr, /unknown target architecture/);
});
