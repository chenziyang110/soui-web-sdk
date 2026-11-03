'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const filename = path.join(__dirname, 'platform-matrix.test.cjs');
const source = fs.readFileSync(filename, 'utf8');
const toolFilename = path.resolve(__dirname, '../../tools/platform-info.cjs');
const toolSource = fs.readFileSync(toolFilename, 'utf8');
const cmakeSource = fs.readFileSync(path.join(__dirname, 'CMakeLists.txt'), 'utf8')
  .replace(/^\s*#[^\n]*$/gm, '');
const cliCase = 'CLI produces JSON and fails unsupported requirements with nonzero status';
const matrixGroups = [
  ['policy', 100, [
    'architecture aliases preserve ISA rather than infer x64 from pointer width',
    'operating-system aliases normalize without accepting different target systems',
    'unknown architectures and pointer-only guesses fail closed',
    'ARM64EC is rejected in every target hint',
    'pointer-size mismatch rejects x32, AArch64 ILP32 and incorrect declarations',
    'cross-compilation uses target hints rather than the separate host processor',
    'conflicting explicit architecture hints fail even if pointer size agrees',
    'Windows explicit compiler targets can differ from the CMake host processor',
    'macOS universal2 is canonical and list-order independent',
    'macOS 32-bit targets are explicitly unsupported',
    'desktop and binary dependencies reject all non-Windows x86/x64 targets',
    'matrix records policy, unprobed toolchains and no invented validation',
    'current-platform information identifies Node process and unprobed compiler',
    'system WebView source policy is distinct from the legacy desktop binaries',
    cliCase,
    'CLI rejects ambiguous/missing arguments and ignored probe options',
    'missing local CMake reports unavailable without downloading tools',
  ]],
  ['cmake-aliases', 200, ['CMake synthetic target variables normalize aliases and preserve ARM64']],
  ['cmake-cross', 110, ['CMake cross builds use declared target rather than host processor']],
  ['cmake-windows', 260, ['CMake Windows VS and compiler architecture selections override the host processor']],
  ['cmake-invalid', 380, ['CMake rejects unknown systems/architectures, ARM64EC and pointer conflicts']],
  ['cmake-macos', 200, ['CMake macOS multi-architecture normalization matches the Node contract']],
  ['cmake-dependencies', 320, ['CMake dependency requests explicitly fail on unsupported target']],
  ['cmake-slices', 740, ['CMake macOS compiler identification accepts slice lists without relaxing other hints']],
  ['probe-native', 300, ['local compiler probe configures and links only its exact requested target']],
  ['probe-mismatch', 300, ['local compiler probe rejects a mismatched requested target']],
  ['linux-cross', 170, ['real configure rejects an ARM cross-toolchain that accidentally uses the x64 host compiler']],
  ['linux-os', 170, ['real compiler OS macros reject a mislabeled CMAKE_SYSTEM_NAME']],
  ['linux-arch', 170, ['real compiler probes reject a false compiler architecture declaration']],
  ['abi', 170, ['legacy libstdc++ ABI fails configuration without weakening TaskStore swap guarantees']],
];

const cliResults = [
  { status: 0, stdout: '{"kind":"support_matrix"}' },
  { status: 0, stdout: '{"target":{"arch":"arm64"}}' },
  { status: 1, stderr: '{"error":"not_supported"}' },
  { status: 1, stderr: '{"error":"not_supported"}' },
];

function scenario(failureIndex, failure, name = cliCase, normal = cliResults,
  discovery = [{ status: 0, stdout: 'cmake version mock' }],
  host = { platform: 'linux', arch: 'x64' }, platform = {}) {
  const callbacks = new Map();
  const calls = [], discoveryCalls = [], registrations = new Map();
  const directories = new Set(), files = new Map(), skipped = [];
  let nonce = 0;
  const memoryFs = {
    mkdtempSync(prefix) { const dir = `${prefix}${++nonce}`; directories.add(dir); return dir; },
    writeFileSync(file, data) {
      assert.ok(directories.has(path.dirname(file)), 'Write outside owned mock directory');
      files.set(file, data);
    },
    rmSync(dir) {
      assert.ok(directories.delete(dir), 'Removal outside owned mock directory');
      for (const file of files.keys()) if (path.dirname(file) === dir) files.delete(file);
    },
  };
  const closed = object => new Proxy(object, { get(target, key) {
    assert.ok(Object.hasOwn(target, key), `Unexpected mock operation: ${String(key)}`);
    return target[key];
  } });
  const modules = {
    'node:test': (name, options, body) => {
      assert.ok(!callbacks.has(name), `Duplicate matrix case: ${name}`);
      callbacks.set(name, typeof options === 'function' ? options : body);
      registrations.set(name, typeof options === 'function' ? {} : options);
    },
    'node:assert/strict': assert,
    'node:fs': closed(memoryFs),
    'node:os': closed({ tmpdir: () => 'mock-temp' }),
    'node:path': path,
    '../../tools/platform-info.cjs': platform,
    'node:child_process': { spawnSync(command, args, options) {
      if (args.length === 1 && args[0] === '--version') {
        assert.equal(options.timeout, 10000);
        const candidates = ['mock-cmake', path.resolve(__dirname, '../../.build-tools/cmake/data/bin/cmake'), 'cmake'];
        const index = discoveryCalls.length;
        assert.equal(command, candidates[index]);
        assert.ok(index < discovery.length, 'Unexpected version discovery');
        discoveryCalls.push(command);
        return discovery[index];
      }
      if (command === 'mock-node') {
        assert.equal(args[0], path.resolve(__dirname, '../../tools/platform-info.cjs'));
      } else {
        assert.equal(command, 'mock-cmake');
        assert.ok(args[0] === '-P' ? files.has(args[1]) : args[0] === '-S' && directories.has(args[1]));
      }
      const index = calls.length;
      assert.ok(index < normal.length, 'Unexpected child command');
      calls.push({ args: Array.from(args), options });
      return { stdout: '', stderr: '', signal: null,
        ...(index === failureIndex ? failure : normal[index]) };
    } },
  };
  new vm.Script(source, { filename }).runInNewContext({
    __dirname,
    process: { env: { CMAKE_COMMAND: 'mock-cmake' }, execPath: 'mock-node', ...host },
    require(name) {
      assert.ok(Object.hasOwn(modules, name), `Unexpected module: ${name}`);
      return modules[name];
    },
  }, { timeout: 1000 });
  assert.equal(typeof callbacks.get(name), 'function');
  return { calls, discoveryCalls, registrations, directories, skipped,
    run: () => callbacks.get(name)({ skip: reason => skipped.push(reason) }) };
}

function probeScenario(failedStage, outcome) {
  const calls = [], directories = new Set(), files = new Map();
  const stages = ['cmake', 'configure', 'compile_and_link'];
  let created = 0, removed = 0;
  const closed = object => new Proxy(object, { get(target, key) {
    assert.ok(Object.hasOwn(target, key), `Unexpected probe operation: ${String(key)}`);
    return target[key];
  } });
  const memoryFs = {
    mkdtempSync(prefix) { const dir = `${prefix}${++created}`; directories.add(dir); return dir; },
    writeFileSync(file, data) {
      assert.ok(directories.has(path.dirname(file)), 'Probe write outside owned mock directory');
      files.set(file, data);
    },
    rmSync(dir, options) {
      assert.ok(directories.delete(dir), 'Probe removal outside owned mock directory');
      assert.equal(options.recursive, true);
      assert.equal(options.force, true);
      for (const file of files.keys()) if (path.dirname(file) === dir) files.delete(file);
      ++removed;
    },
  };
  const modules = {
    'node:fs': closed(memoryFs),
    'node:os': closed({ tmpdir: () => 'mock-probe-temp' }),
    'node:path': path,
    'node:child_process': closed({ spawnSync(command, args, options) {
      assert.equal(command, 'mock-cmake');
      const stage = stages[calls.length];
      assert.ok(stage, 'Unexpected probe child command');
      assert.equal(options.windowsHide, true);
      assert.equal(options.timeout, stage === 'cmake' ? 10000 : 120000);
      if (stage === 'cmake') assert.deepEqual(Array.from(args), ['--version']);
      else if (stage === 'configure') {
        assert.equal(args.length, 4);
        assert.equal(args[0], '-S');
        assert.equal(args[2], '-B');
        assert.ok(directories.has(args[1]));
        assert.ok(files.has(path.join(args[1], 'CMakeLists.txt')));
        assert.ok(files.has(path.join(args[1], 'probe.cpp')));
        assert.equal(args[3], path.join(args[1], 'build'));
      } else {
        assert.deepEqual(Array.from(args), ['--build', calls[1].args[3], '--config', 'Release']);
      }
      calls.push({ stage, args: Array.from(args) });
      return { status: 0, signal: null, stdout: '', stderr: '',
        ...(stage === failedStage ? outcome : {}) };
    } }),
  };
  const module = { exports: {} };
  new vm.Script(toolSource, { filename: toolFilename }).runInNewContext({
    __dirname: path.dirname(toolFilename), module,
    require(name) {
      assert.ok(Object.hasOwn(modules, name), `Unexpected probe module: ${name}`);
      return modules[name];
    },
  }, { timeout: 1000 });
  return { platform: module.exports, calls,
    run: () => module.exports.probeToolchain({ os: 'windows', arch: 'x64' }, { cmake: 'mock-cmake' }),
    assertClean() {
      assert.equal(directories.size, 0);
      assert.equal(files.size, 0);
      assert.equal(removed, created);
    },
  };
}

const mismatchText = 'configured compiler does not match requested target: linux/x64';
const abnormalProbeResults = [
  ['error', { status: 1, error: Object.assign(new Error(mismatchText), { code: 'ETIMEDOUT' }) }],
  ['error with zero status', { status: 0, error: new Error(mismatchText) }],
  ['signal', { status: null, signal: 'SIGTERM' }],
  ['signal with zero status', { status: 0, signal: 'SIGTERM' }],
  ['absent status', { status: null }],
  ['string status', { status: '1' }],
  ['fractional status', { status: 0.5 }],
].map(([name, result]) => [name, { stdout: mismatchText, stderr: mismatchText, ...result }]);

function assertProbeShape(result, stage, status) {
  assert.equal(result.status, status);
  assert.equal(result.stage, stage);
  assert.equal(result.runtimeValidation, 'not_tested');
  assert.deepEqual(Object.keys(result).sort(),
    ['status', 'stage', 'reason', 'runtimeValidation', ...(stage === 'compile_and_link' ? ['scope'] : [])].sort());
}

test('real probeToolchain source preserves normal successful compile and link classification', () => {
  const probe = probeScenario();
  const result = probe.run();
  assertProbeShape(result, 'compile_and_link', 'available');
  assert.equal(result.reason, null);
  assert.equal(probe.calls.length, 3);
  probe.assertClean();
});

for (const [index, stage] of ['cmake', 'configure', 'compile_and_link'].entries()) {
  test(`real probeToolchain source preserves ordinary nonzero ${stage} results including empty output`, () => {
    for (const stderr of [mismatchText, '']) {
      const probe = probeScenario(stage, { status: 1, stderr });
      const result = probe.run();
      assertProbeShape(result, stage, 'unavailable');
      assert.equal(result.reason, stderr);
      assert.equal(probe.calls.length, index + 1);
      probe.assertClean();
    }
  });
  test(`real probeToolchain source classifies abnormal ${stage} results without partial refusal text`, () => {
    for (const [name, outcome] of abnormalProbeResults) {
      const probe = probeScenario(stage, outcome);
      const result = probe.run();
      assertProbeShape(result, stage, 'unavailable');
      assert.equal(typeof result.reason, 'string', name);
      assert.doesNotMatch(result.reason, /does not match requested target/, name);
      assert.equal(probe.calls.length, index + 1, name);
      probe.assertClean();
    }
  });
}

const mismatchCase = 'local compiler probe rejects a mismatched requested target';
function mismatchScenario(outcome) {
  const probe = probeScenario('configure', outcome);
  const matrix = scenario(undefined, undefined, mismatchCase, [], [{ status: 0 }],
    { platform: 'linux', arch: 'x64' }, probe.platform);
  return { probe, matrix };
}

test('real mismatch callback accepts an ordinary nonzero configure refusal', () => {
  const { probe, matrix } = mismatchScenario({ status: 1, stderr: mismatchText });
  matrix.run();
  assert.equal(probe.calls.length, 2);
  assert.equal(matrix.calls.length, 0);
  probe.assertClean();
});

test('real mismatch callback rejects abnormal configure results despite expected partial text', () => {
  for (const [name, outcome] of abnormalProbeResults) {
    const { probe, matrix } = mismatchScenario(outcome);
    assert.throws(matrix.run, { code: 'ERR_ASSERTION', operator: 'match' }, name);
    assert.equal(probe.calls.length, 2, name);
    assert.equal(matrix.calls.length, 0, name);
    probe.assertClean();
  }
});

test('CTest groups cover all thirty matrix cases exactly once with explicit budgets', () => {
  const declarations = Array.from(cmakeSource.matchAll(
    /_soui_platform_matrix_group\(\s*([a-z-]+)\s+(\d+)\s+"((?:\\.|[^"\\])*)"\s*\)/g));
  assert.equal(declarations.length, matrixGroups.length);
  const { registrations } = scenario();
  const names = [...registrations.keys()];
  const expectedNames = matrixGroups.flatMap(([, , cases]) => cases);
  assert.equal(expectedNames.length, 30);
  assert.deepEqual([...names].sort(), [...expectedNames].sort());
  const selections = new Map(names.map(name => [name, 0]));
  for (const [suffix, timeout, cases] of matrixGroups) {
    const declaration = declarations.filter(match => match[1] === suffix);
    assert.equal(declaration.length, 1, suffix);
    assert.equal(Number(declaration[0][2]), timeout, suffix);
    const pattern = new RegExp(declaration[0][3].replace(/\\([\\"])/g, '$1'));
    assert.deepEqual(names.filter(name => pattern.test(name)).sort(), [...cases].sort(), suffix);
    for (const name of names) if (pattern.test(name)) selections.set(name, selections.get(name) + 1);
    if (suffix !== 'policy') assert.equal(registrations.get(cases[0]).timeout, (timeout - 40) * 1000);
  }
  for (const [name, count] of selections) assert.equal(count, 1, name);
});

test('CTest legacy aggregate requires the complete setup list and compiler resource lock', () => {
  const groupBody = cmakeSource.match(
    /function\(_soui_platform_matrix_group suffix timeout pattern\)([\s\S]*?)endfunction\(\)/)?.[1];
  assert.ok(groupBody);
  assert.match(groupBody, /set\(_name "sdk\.portable\.platform-matrix-\$\{suffix\}"\)/);
  assert.match(groupBody, /COMMAND "\$\{SOUI_PORTABLE_NODE_EXECUTABLE\}" --test "--test-name-pattern=\$\{pattern\}"\s+"\$\{CMAKE_CURRENT_SOURCE_DIR\}\/platform-matrix\.test\.cjs"/);
  assert.match(groupBody, /TIMEOUT "\$\{timeout\}"\s+ENVIRONMENT "CMAKE_COMMAND=\$\{CMAKE_COMMAND\}"/);
  assert.match(groupBody, /set\(_soui_platform_matrix_setup "\$\{_soui_platform_matrix_setup\};\$\{_name\}" PARENT_SCOPE\)/);
  assert.match(cmakeSource, /set\(_soui_platform_matrix_setup sdk\.portable\.platform-matrix-cli\)/);
  assert.match(cmakeSource, /add_test\(NAME sdk\.portable\.platform-matrix-cli\s+COMMAND "\$\{SOUI_PORTABLE_NODE_EXECUTABLE\}" --test "\$\{CMAKE_CURRENT_SOURCE_DIR\}\/platform-matrix-cli\.test\.cjs"\)/);
  assert.match(cmakeSource, /set_tests_properties\(sdk\.portable\.platform-matrix-cli PROPERTIES TIMEOUT 10\)/);
  assert.equal((cmakeSource.match(/FIXTURES_SETUP/g) || []).length, 1);
  assert.match(cmakeSource, /set_tests_properties\(\$\{_soui_platform_matrix_setup\} PROPERTIES\s+FIXTURES_SETUP soui_platform_matrix_groups LABELS platform-matrix\)/);
  assert.equal((cmakeSource.match(/FIXTURES_REQUIRED/g) || []).length, 1);
  assert.match(cmakeSource, /add_test\(NAME sdk\.portable\.platform-matrix COMMAND "\$\{CMAKE_COMMAND\}" -E true\)/);
  assert.match(cmakeSource, /set_tests_properties\(sdk\.portable\.platform-matrix PROPERTIES TIMEOUT 5\s+FIXTURES_REQUIRED soui_platform_matrix_groups LABELS platform-matrix\)/);
  const locks = Array.from(cmakeSource.matchAll(
    /set_tests_properties\(([^()]*)PROPERTIES\s+RESOURCE_LOCK\s+(\w+)\s*\)/g));
  assert.equal(locks.length, 1);
  assert.equal(locks[0][2], 'soui_platform_compiler');
  assert.deepEqual(locks[0][1].trim().split(/\s+/), matrixGroups
    .filter(([suffix]) => suffix.startsWith('probe-') || suffix.startsWith('linux-') || suffix === 'abi')
    .map(([suffix]) => `sdk.portable.platform-matrix-${suffix}`));
});

test('compiler case budgets preserve bounded probe waits and platform skips', () => {
  for (const host of [{ platform: 'linux', arch: 'x64' }, { platform: 'linux', arch: 'arm64' },
    { platform: 'win32', arch: 'x64' }, { platform: 'darwin', arch: 'arm64' }]) {
    const { registrations } = scenario(undefined, undefined, cliCase, cliResults,
      [{ status: 0 }], host);
    assert.equal(registrations.size, 30);
    for (const [suffix, , [name]] of matrixGroups.filter(([suffix]) =>
      suffix.startsWith('probe-') || suffix.startsWith('linux-') || suffix === 'abi')) {
      const options = registrations.get(name);
      assert.equal(options.timeout, suffix.startsWith('probe-') ? 260000 : 130000, name);
      if (suffix.startsWith('linux-')) {
        assert.equal(Boolean(options.skip), host.platform !== 'linux' ||
          (suffix !== 'linux-os' && host.arch !== 'x64'), name);
      } else assert.equal(options.skip, false, name);
    }
  }
});

test('CLI harness bounds all four child waits and preserves normal success/refusal assertions', () => {
  const { calls, run } = scenario();
  run();
  assert.equal(calls.length, 4);
  for (const { options } of calls) {
    assert.ok(Number.isInteger(options.timeout) && options.timeout > 0 && options.timeout <= 10000);
    assert.equal(options.windowsHide, true);
  }
});

for (let index = 0; index < 4; ++index) {
  test(`CLI harness rejects timeout at command ${index + 1} before later commands`, () => {
    const error = Object.assign(new Error('mock CLI timeout'), { code: 'ETIMEDOUT' });
    const { calls, run } = scenario(index, {
      error, status: null, signal: 'SIGTERM', stderr: '{"error":"not_supported"}',
    });
    assert.throws(run, /mock CLI timeout/);
    assert.equal(calls.length, index + 1);
  });
}

for (const index of [2, 3]) {
  test(`CLI harness rejects signal at refusal command ${index + 1}`, () => {
    const { calls, run } = scenario(index, {
      status: null, signal: 'SIGTERM', stderr: '{"error":"not_supported"}',
    });
    assert.throws(run, /SIGTERM/);
    assert.equal(calls.length, index + 1);
  });
  test(`CLI harness rejects absent exit status at refusal command ${index + 1}`, () => {
    const { calls, run } = scenario(index, { status: null, stderr: '{"error":"not_supported"}' });
    assert.throws(run, /normal exit status/);
    assert.equal(calls.length, index + 1);
  });
}

const success = stdout => ({ status: 0, stdout });
const refusal = stderr => ({ status: 1, stderr });
const repeated = (count, result) => Array.from({ length: count }, () => result);
const universal = success('TARGET=macos/universal2;ARCHES=x64;arm64');
const thin = success('TARGET=macos/arm64;ARCHES=arm64');
const cmakeCases = [
  ['CMake rejects unknown systems/architectures, ARM64EC and pointer conflicts',
    repeated(11, refusal('not_supported'))],
  ['CMake macOS multi-architecture normalization matches the Node contract',
    [...repeated(3, universal), refusal('macOS supports only'), success('TARGET=macos/x64;ARCHES=x64')]],
  ['CMake dependency requests explicitly fail on unsupported target',
    repeated(9, refusal('not_supported Windows x86/x64 only'))],
  ['CMake macOS compiler identification accepts slice lists without relaxing other hints',
    [...repeated(2, null).flatMap(() => [universal, universal, thin,
      ...repeated(5, refusal('not_supported')), refusal('conflicting'),
      ...repeated(2, refusal('unknown target architecture'))]), refusal('unknown target architecture')]],
  ['real configure rejects an ARM cross-toolchain that accidentally uses the x64 host compiler',
    [refusal("CMAKE_SYSTEM_PROCESSOR 'arm64' conflicts with compiler target 'x64'; host compiler cannot substitute")]],
  ['real compiler OS macros reject a mislabeled CMAKE_SYSTEM_NAME',
    [refusal('compiler OS/architecture probe failed')]],
  ['real compiler probes reject a false compiler architecture declaration',
    [refusal("conflicting target architecture 'arm64' and compiler target 'x64'")]],
  ['legacy libstdc++ ABI fails configuration without weakening TaskStore swap guarantees',
    [refusal('existing TaskStore non-throwing swap contract; _GLIBCXX_USE_CXX11_ABI=1')]],
];

for (const [name, results] of cmakeCases) {
  test(`CMake harness preserves normal results: ${name}`, () => {
    const { calls, directories, run } = scenario(undefined, undefined, name, results);
    run();
    assert.equal(calls.length, results.length);
    for (const { args, options } of calls) assert.equal(options.timeout, args[0] === '-P' ? 30000 : 120000);
    assert.equal(directories.size, 0);
  });
  for (const fault of ['timeout', 'signal', 'status']) {
    test(`CMake harness rejects ${fault} despite matching partial output: ${name}`, () => {
      for (let index = 0; index < results.length; ++index) {
        if (results[index].status === 0) continue;
        const failure = { ...results[index], status: null };
        if (fault === 'timeout') failure.error = Object.assign(new Error('mock CMake timeout'), { code: 'ETIMEDOUT' });
        if (fault === 'signal') failure.signal = 'SIGTERM';
        const { calls, directories, skipped, run } = scenario(index, failure, name, results);
        assert.throws(run, fault === 'timeout' ? /mock CMake timeout/ : fault === 'signal' ? /SIGTERM/ : /normal exit status/);
        assert.equal(calls.length, index + 1, 'No later child command after failure');
        assert.equal(directories.size, 0, 'Mock fixture cleanup still runs');
        assert.equal(skipped.length, 0, 'Abnormal exit must not select an ABI skip');
      }
    });
  }
}

test('CMake ABI harness keeps a normal non-libstdc++ result explicitly skipped', () => {
  const [name] = cmakeCases.at(-1);
  const { directories, skipped, run } = scenario(undefined, undefined, name,
    [success('SOUI_TEST_NOT_LIBSTDCXX')]);
  run();
  assert.equal(skipped.length, 1);
  assert.equal(directories.size, 0);
});

for (const first of [{ status: null, error: new Error('mock unavailable') }, { status: 0, signal: 'SIGTERM' }]) {
  test(`CMake discovery preserves fallback after ${first.error ? 'error' : 'signal'}`, () => {
    const { discoveryCalls, registrations, run } = scenario(undefined, undefined, cliCase, cliResults,
      [first, { status: 0 }]);
    run();
    assert.equal(discoveryCalls.length, 2);
    assert.equal(registrations.get(cmakeCases[0][0]).skip, false);
  });
}

test('CMake discovery leaves pure CLI checks available when all candidates fail', () => {
  const { discoveryCalls, registrations, run } = scenario(undefined, undefined, cliCase, cliResults,
    repeated(3, { status: null, error: new Error('mock unavailable') }));
  run();
  assert.equal(discoveryCalls.length, 3);
  assert.equal(registrations.get(cmakeCases[0][0]).skip, 'CMake is not installed');
});
