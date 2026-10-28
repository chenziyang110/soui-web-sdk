#!/usr/bin/env node
'use strict';

// Dependency-free target policy and local toolchain reporting. "supported" is a
// source/build policy; it is never evidence that a target passed runtime tests.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

function unsupported(message) {
  const error = new Error(message);
  error.code = 'not_supported';
  return error;
}

function normalizeOs(value) {
  const raw = String(value || '').trim().toLowerCase();
  if (['windows', 'win32', 'windows_nt'].includes(raw)) return 'windows';
  if (raw === 'linux') return 'linux';
  if (['darwin', 'macos', 'macosx', 'osx'].includes(raw)) return 'macos';
  throw unsupported(`Unknown target operating system '${value}'.`);
}

function normalizeArchitecture(value) {
  const raw = String(value || '').trim().toLowerCase();
  if (raw.includes('arm64ec')) throw unsupported('ARM64EC is not a supported ARM64 target.');
  if (/^(x86|win32|ia32|i[3-6]86)$/.test(raw)) return 'x86';
  if (/^(x64|amd64|x86_64|x86-64)$/.test(raw)) return 'x64';
  if (/^(arm64|aarch64)$/.test(raw)) return 'arm64';
  if (/^(arm|arm32|armhf|armel|armv[4-8](l|hl|tel)?|armv7-a)$/.test(raw)) return 'arm32';
  throw unsupported(`Unknown target architecture '${value}'; no x86/x64 fallback is permitted.`);
}

function normalizeAppleArchitectures(values) {
  const input = Array.isArray(values) ? values : String(values || '').split(/[;,]/);
  if (input.some((value) => String(value).trim() === '')) {
    throw unsupported('An empty macOS architecture-list member is not supported.');
  }
  const arches = [...new Set(input.map(normalizeArchitecture))];
  if (arches.some((arch) => !['x64', 'arm64'].includes(arch))) {
    throw unsupported('macOS supports only x64, arm64 or universal2.');
  }
  return ['x64', 'arm64'].filter((arch) => arches.includes(arch));
}

function resolveTarget(options) {
  const targetOs = normalizeOs(options.os || options.systemName);
  const hints = [];
  for (const key of ['arch', 'compilerArch', 'compilerArchitectureId', 'generatorPlatform']) {
    if (options[key] !== undefined && options[key] !== '') {
      if (key === 'arch' && String(options[key]).toLowerCase() === 'universal2') continue;
      hints.push(normalizeArchitecture(String(options[key]).split(',')[0]));
    }
  }
  if (options.compilerTarget) hints.push(normalizeArchitecture(options.compilerTarget.split('-')[0]));
  const processorArch = options.processor !== undefined && options.processor !== ''
    ? normalizeArchitecture(options.processor) : undefined;
  const requestedUniversal = String(options.arch || '').toLowerCase() === 'universal2';
  if (targetOs !== 'macos' && (requestedUniversal || options.osxArchitectures !== undefined)) {
    throw unsupported('macOS architecture lists and universal2 require a macOS target.');
  }
  const appleArches = options.osxArchitectures !== undefined
    ? normalizeAppleArchitectures(options.osxArchitectures)
    : (requestedUniversal ? ['x64', 'arm64'] : []);
  if (requestedUniversal && appleArches.length !== 2) {
    throw unsupported('universal2 requires both x86_64 and arm64 slices.');
  }
  let arch;
  let architectures;
  if (appleArches.length === 2) {
    if (hints.some((hint) => !appleArches.includes(hint))) {
      throw unsupported('Conflicting macOS compiler and architecture-list targets.');
    }
    if (options.compilerProbeArch && options.compilerProbeArch !== 'universal2') {
      throw unsupported('The compiler did not build both universal2 architecture slices.');
    }
    arch = 'universal2';
    architectures = appleArches;
  } else {
    const actual = options.compilerProbeArch
      ? normalizeArchitecture(options.compilerProbeArch)
      : undefined;
    arch = actual || hints[0] || appleArches[0] || normalizeArchitecture(options.processor);
    if ([...hints, ...appleArches].some((hint) => hint !== arch)) {
      throw unsupported(`Conflicting target architecture hints for '${arch}'.`);
    }
    architectures = [arch];
  }
  // Windows CMake's system processor may name the host even for VS -A,
  // a cross developer prompt or an explicit compiler target. Architecture
  // hints are still cross-checked with compilerProbeArch when it is supplied.
  const windowsCompilerSelection = targetOs === 'windows'
    && ['generatorPlatform', 'compilerTarget', 'compilerArchitectureId', 'compilerArch']
      .some((key) => options[key] !== undefined && options[key] !== '');
  if (processorArch && !architectures.includes(processorArch) && appleArches.length === 0 && !windowsCompilerSelection) {
    const x86WidthSelection = ['x86', 'x64'].includes(processorArch) && ['x86', 'x64'].includes(arch);
    if (!x86WidthSelection) {
      throw unsupported(`Target processor '${processorArch}' conflicts with compiler target '${arch}'. Fix the cross-compiling toolchain; a host compiler cannot substitute for the target compiler.`);
    }
  }
  if (targetOs === 'macos' && ['x86', 'arm32'].includes(arch)) {
    throw unsupported(`macOS does not support '${arch}'; use x64, arm64 or universal2.`);
  }
  const expectedSize = ['x86', 'arm32'].includes(arch) ? 4 : 8;
  const pointerSize = options.pointerSize === undefined ? null : Number(options.pointerSize);
  if (pointerSize !== null && ![4, 8].includes(pointerSize)) {
    throw unsupported(`Unsupported pointer size '${options.pointerSize}'; expected bytes (4 or 8).`);
  }
  if (pointerSize !== null && pointerSize !== expectedSize) {
    throw unsupported(`Target ${arch} requires ${expectedSize}-byte pointers, but ${pointerSize} was supplied.`);
  }
  return {
    os: targetOs,
    arch,
    architectures,
    pointerSize,
    expectedPointerSize: expectedSize,
    architectureSource: options.compilerProbeArch ? 'compiler_probe'
      : hints.length ? 'explicit_target'
        : appleArches.length ? 'cmake_osx_architectures' : 'system_processor',
  };
}

function getCapabilities(target) {
  const portable = ['windows', 'linux'].includes(target.os)
    ? ['x86', 'x64', 'arm32', 'arm64'].includes(target.arch)
    : target.os === 'macos' && ['x64', 'arm64', 'universal2'].includes(target.arch);
  const desktop = target.os === 'windows' && ['x86', 'x64'].includes(target.arch);
  const capability = (supported, requirements, reason) => ({
    status: supported ? 'supported' : 'not_supported',
    requirements: supported ? requirements : [],
    reason: supported ? null : reason,
    validation: 'not_tested',
  });
  const desktopReason = 'The existing desktop host and SOUI/Miniblink prebuilt dependencies support Windows x86/x64 only.';
  return {
    portable_core: capability(portable, ['compatible C++17 target toolchain'],
      'Portable core supports Windows/Linux x86/x64/arm32/arm64 and macOS x64/arm64/universal2.'),
    system_desktop: capability(portable && !(target.os === 'windows' && target.arch === 'arm32'),
      target.os === 'linux' ? ['target GTK3 >= 3.24 and WebKitGTK 4.1 >= 2.40 development/runtime packages', 'working graphical session']
        : target.os === 'macos' ? ['Apple SDK, macOS >= 12.0', 'AppKit and WKWebView']
          : ['MSVC-compatible toolchain', 'matching Microsoft WebView2 SDK loader', 'supported Evergreen Runtime with ICoreWebView2_18'],
      'Separate opt-in system-WebView backend; source route only, target dependencies and native GUI validation are required. Linux 32-bit availability depends on the target distribution. Windows ARM32 and macOS 32-bit are unsupported.'),
    desktop_host: capability(desktop, ['supported Windows MSVC-compatible toolchain', 'SOUI and Miniblink target dependencies'], desktopReason),
    soui_prebuilt: capability(desktop, ['matching third_party/prebuilt target files'], desktopReason),
    miniblink_prebuilt: capability(desktop, ['matching third_party/prebuilt target files'], desktopReason),
  };
}

function requireCapability(target, name) {
  const capability = getCapabilities(target)[name];
  if (!capability) throw unsupported(`Unknown capability '${name}'.`);
  if (capability.status !== 'supported') {
    throw unsupported(`${name} on ${target.os}/${target.arch}: ${capability.reason}`);
  }
  return capability;
}

function getMatrix() {
  const targets = [];
  for (const targetOs of ['windows', 'linux', 'macos']) {
    for (const arch of ['x86', 'x64', 'arm32', 'arm64', ...(targetOs === 'macos' ? ['universal2'] : [])]) {
      targets.push({
        os: targetOs,
        arch,
        architectures: arch === 'universal2' ? ['x64', 'arm64'] : [arch],
        expectedPointerSize: ['x86', 'arm32'].includes(arch) ? 4 : 8,
        capabilities: getCapabilities({ os: targetOs, arch }),
        toolchain: { status: 'not_probed' },
      });
    }
  }
  return {
    schemaVersion: 1,
    kind: 'support_matrix',
    note: 'Supported means source/build policy, not installed dependencies, runtime validation or production readiness.',
    excludedArchitectures: ['ARM64EC'],
    targets,
  };
}

function cmakeQuoted(value) {
  return String(value).replace(/\\/g, '/').replace(/"/g, '\\"').replace(/\$/g, '\\$');
}

function probeProcessFailure(result, diagnostic) {
  const code = value => typeof value === 'string' && /^[A-Z][A-Z0-9_]{0,31}$/.test(value)
    ? ` (${value})` : '';
  // Partial output from an interrupted process is not a compiler rejection.
  if (result.error) return `CMake process error${code(result.error.code)}.`;
  if (result.signal) return `CMake process terminated${code(result.signal)}.`;
  if (!Number.isInteger(result.status)) return 'CMake process did not report a normal exit status.';
  return result.status === 0 ? null : diagnostic();
}

// An explicit local probe compiles and links, but never runs, a tiny C++17
// consumer. The configured target must match the requested target exactly.
// No tools are downloaded, no packages installed, and no remote CI is invoked.
function probeToolchain(target, options = {}) {
  const cmake = options.cmake || 'cmake';
  const version = spawnSync(cmake, ['--version'], { encoding: 'utf8', timeout: 10000, windowsHide: true });
  const versionFailure = probeProcessFailure(version, () => (version.stderr || '').trim());
  if (versionFailure !== null) {
    return { status: 'unavailable', stage: 'cmake', reason: versionFailure, runtimeValidation: 'not_tested' };
  }
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-platform-probe-'));
  try {
    const modulePath = path.resolve(__dirname, '../cmake/SouiPlatform.cmake');
    const moduleText = cmakeQuoted(modulePath);
    const source = [
      'cmake_minimum_required(VERSION 3.24)',
      'project(SouiToolchainProbe LANGUAGES CXX)',
      `include("${moduleText}")`,
      'soui_detect_platform()',
      `if(NOT SOUI_TARGET_OS STREQUAL "${target.os}" OR NOT SOUI_TARGET_ARCH STREQUAL "${target.arch}")`,
      '  message(FATAL_ERROR "SOUI not_supported: configured compiler does not match requested target: ${SOUI_TARGET_OS}/${SOUI_TARGET_ARCH}")',
      'endif()',
      'soui_require_capability(portable_core)',
      'add_executable(soui_toolchain_probe probe.cpp)',
      'target_compile_features(soui_toolchain_probe PRIVATE cxx_std_17)',
      '',
    ].join('\n');
    fs.writeFileSync(path.join(directory, 'CMakeLists.txt'), source);
    fs.writeFileSync(path.join(directory, 'probe.cpp'),
      '#include <atomic>\n#include <string_view>\nint main() { std::atomic<int> value{0}; constexpr std::string_view text{"soui"}; return value.load() + (text.size() != 4); }\n');
    const buildPath = path.join(directory, 'build');
    const args = ['-S', directory, '-B', buildPath];
    if (options.toolchain) args.push(`-DCMAKE_TOOLCHAIN_FILE=${path.resolve(options.toolchain)}`);
    if (options.generator) args.push('-G', options.generator);
    if (options.generatorPlatform) args.push('-A', options.generatorPlatform);
    if (options.compiler) args.push(`-DCMAKE_CXX_COMPILER=${options.compiler}`);
    if (target.os === 'macos') {
      args.push(`-DCMAKE_OSX_ARCHITECTURES=${target.architectures.map((arch) => arch === 'x64' ? 'x86_64' : 'arm64').join(';')}`);
    }
    const configure = spawnSync(cmake, args, { encoding: 'utf8', timeout: 120000, windowsHide: true });
    const configureFailure = probeProcessFailure(configure,
      () => ((configure.stderr || '') + (configure.stdout || '')).slice(-12000));
    if (configureFailure !== null) {
      return { status: 'unavailable', stage: 'configure', reason: configureFailure, runtimeValidation: 'not_tested' };
    }
    const build = spawnSync(cmake, ['--build', buildPath, '--config', 'Release'], { encoding: 'utf8', timeout: 120000, windowsHide: true });
    const buildFailure = probeProcessFailure(build,
      () => ((build.stderr || '') + (build.stdout || '')).slice(-12000));
    return {
      status: buildFailure === null ? 'available' : 'unavailable',
      stage: 'compile_and_link',
      reason: buildFailure,
      scope: 'minimal C++17 target consumer; SDK build and runtime acceptance are separate',
      runtimeValidation: 'not_tested',
    };
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
}

function getPlatformInfo(options = {}) {
  const host = {
    os: normalizeOs(process.platform),
    arch: normalizeArchitecture(process.arch),
    source: 'node_process',
    note: 'Describes the running Node process, which may be emulated; it is not a target compiler probe.',
  };
  const hasTarget = ['os', 'systemName', 'arch', 'processor', 'compilerArch', 'compilerArchitectureId',
    'compilerTarget', 'generatorPlatform', 'osxArchitectures'].some((key) => options[key] !== undefined);
  const target = hasTarget
    ? resolveTarget({ ...options, os: options.os || options.systemName || host.os })
    : { ...resolveTarget({ ...options, os: host.os, arch: host.arch }), architectureSource: 'node_process' };
  target.crossCompiling = target.os !== host.os || target.arch !== host.arch;
  const result = {
    schemaVersion: 1,
    kind: 'platform_info',
    host,
    target,
    capabilities: getCapabilities(target),
    toolchain: { status: 'not_probed', runtimeValidation: 'not_tested' },
  };
  if (options.require) requireCapability(target, options.require);
  if (options.probeToolchain) result.toolchain = probeToolchain(target, options);
  return result;
}

function parseArguments(argv) {
  const options = {};
  const values = {
    '--os': 'os', '--arch': 'arch', '--pointer-size': 'pointerSize',
    '--processor': 'processor', '--compiler-arch': 'compilerArch',
    '--compiler-target': 'compilerTarget', '--generator-platform': 'generatorPlatform',
    '--osx-architectures': 'osxArchitectures', '--require': 'require',
    '--toolchain': 'toolchain', '--generator': 'generator', '--compiler': 'compiler', '--cmake': 'cmake',
  };
  for (let index = 0; index < argv.length; index += 1) {
    const flag = argv[index];
    if (flag === '--matrix') options.matrix = true;
    else if (flag === '--json') { /* JSON is also the default. */ }
    else if (flag === '--probe-toolchain') options.probeToolchain = true;
    else if (flag === '--help' || flag === '-h') options.help = true;
    else if (values[flag] && index + 1 < argv.length && !argv[index + 1].startsWith('--')) {
      options[values[flag]] = argv[++index];
    } else throw unsupported(`Unknown option or missing argument: '${flag}'.`);
  }
  if (options.matrix && Object.keys(options).some((key) => !['matrix', 'help'].includes(key))) {
    throw unsupported('--matrix cannot be combined with target or probe options.');
  }
  if (!options.probeToolchain && ['toolchain', 'generator', 'compiler', 'cmake'].some((key) => options[key])) {
    throw unsupported('Toolchain configuration options require --probe-toolchain.');
  }
  return options;
}

function main(argv) {
  try {
    const options = parseArguments(argv);
    if (options.help) {
      process.stdout.write(
        'Usage: node tools/platform-info.cjs [--json] [--matrix]\n'
        + '  [--os windows|linux|macos --arch x86|x64|arm32|arm64|universal2]\n'
        + '  [--pointer-size 4|8] [--osx-architectures "x86_64;arm64"]\n'
        + '  [--processor NAME] [--compiler-arch NAME] [--compiler-target TRIPLE]\n'
        + '  [--generator-platform NAME] [--require portable_core|system_desktop|desktop_host|soui_prebuilt|miniblink_prebuilt]\n'
        + '  [--probe-toolchain [--toolchain FILE] [--generator NAME] [--compiler FILE] [--cmake FILE]]\n'
        + 'Toolchain probing builds a temporary C++17 consumer without running it or installing tools.\n');
      return 0;
    }
    const result = options.matrix ? getMatrix() : getPlatformInfo(options);
    process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
    return options.probeToolchain && result.toolchain.status !== 'available' ? 1 : 0;
  } catch (error) {
    process.stderr.write(`${JSON.stringify({ error: error.code || 'platform_error', message: error.message })}\n`);
    return 1;
  }
}

module.exports = {
  normalizeOs, normalizeArchitecture, normalizeAppleArchitectures,
  resolveTarget, getCapabilities, requireCapability, getMatrix, getPlatformInfo,
  probeToolchain, parseArguments, main,
};
if (require.main === module) process.exitCode = main(process.argv.slice(2));
