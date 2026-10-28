'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { spawnSync } = require('node:child_process');

// Opt-in real source-tree configuration. Only imported synthetic PE inputs are
// staged; no SDK/application binary is built or run. The relocated package below
// is a metadata fixture using the generated production Config, not a rebuilt SDK.
test('notice provenance survives source configuration, relocation and Debug/Release staging', t => {
  if (process.platform !== 'win32' || !process.env.SOUI_DESKTOP_NOTICE_WEBVIEW2_ROOT)
    return t.skip('requires Windows and an explicit existing WebView2 SDK package');
  const cmake = process.env.CMAKE || 'cmake';
  const root = path.resolve(__dirname, '../..');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-notice-cmake-'));
  t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
  const posix = value => value.replaceAll('\\', '/');
  const isolation = { VCPkgLocalAppDataDisabled: 'true', VcpkgEnabled: 'false', VcpkgApplocalDeps: 'false',
    VcpkgEnableManifest: 'false', VcpkgManifestInstall: 'false', VcpkgAutoLink: 'false' };
  const env = { ...process.env, ...isolation };
  const globals = Object.entries(isolation).map(([key, value]) => `${key}=${value}`);
  const configArgs = JSON.parse(process.env.SOUI_NOTICE_CMAKE_ARGS || '["-G","Visual Studio 17 2022","-A","x64","-T","v143,host=x64"]');
  const run = (args, success = true) => {
    const result = spawnSync(cmake, args, { encoding: 'utf8', timeout: 120000, env });
    if (success) assert.equal(result.status, 0, `${result.error || ''}\n${result.stdout?.slice(-6000)}\n${result.stderr?.slice(-6000)}`);
    else assert.notEqual(result.status, 0);
    return result.stdout + result.stderr;
  };
  const write = (file, data) => { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, data); };
  for (const [config, marker] of [['Debug', 17], ['Release', 29]]) {
    const pe = Buffer.alloc(512); pe.writeUInt16LE(0x5a4d); pe.writeUInt32LE(64, 60);
    pe.writeUInt32LE(0x4550, 64); pe.writeUInt16LE(0x8664, 68); pe.writeUInt16LE(240, 84);
    pe.writeUInt16LE(2, 86); pe.writeUInt16LE(0x20b, 88); pe.writeUInt16LE(3, 156); pe[511] = marker;
    write(path.join(dir, `${config}.exe`), pe);
  }
  write(path.join(dir, 'web/index.html'), '<!doctype html>fixture');
  const application = `
add_executable(notice_app IMPORTED)
set_target_properties(notice_app PROPERTIES IMPORTED_CONFIGURATIONS "Debug;Release"
  IMPORTED_LOCATION_DEBUG "${posix(dir)}/Debug.exe" IMPORTED_LOCATION_RELEASE "${posix(dir)}/Release.exe")
soui_stage_application(TARGET notice_app NAME NoticeApp ASSETS "${posix(dir)}/web"
  OUTPUT "\${CMAKE_BINARY_DIR}/stage-$<CONFIG>" VERSION 1.0.0)
`;
  const source = path.join(dir, 'source'), build = path.join(dir, 'source-build');
  write(path.join(source, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.24)
project(NoticeSource LANGUAGES C CXX)
set(SOUI_BUILD_PORTABLE ON)
set(SOUI_BUILD_DESKTOP ON)
add_subdirectory("${posix(root)}/sdk" sdk)
${application}`);
  const webview = path.resolve(process.env.SOUI_DESKTOP_NOTICE_WEBVIEW2_ROOT);
  run(['-S', source, '-B', build, ...configArgs, `-DCMAKE_VS_GLOBALS=${globals.join(';')}`,
    `-DSOUI_WEBVIEW2_SDK_ROOT=${posix(webview)}`]);
  const expected = new Map([
    ['LICENSE.md', path.join(root, 'LICENSE.md')],
    ['THIRD_PARTY_NOTICES.md', path.join(root, 'sdk/portable/THIRD_PARTY_NOTICES.md')],
    ['WebView2-LICENSE.txt', path.join(webview, 'LICENSE.txt')], ['WebView2-NOTICE.txt', path.join(webview, 'NOTICE.txt')],
  ]);
  function stageAndCheck(directory, config) {
    const logFile = path.join(directory, `notice-${config}.log`);
    run(['--build', directory, '--config', config, '--target', 'notice_app_stage', '--parallel', '2', '--',
      '/nr:false', '/v:minimal', `/flp:LogFile=${logFile};Verbosity=diagnostic;Encoding=UTF-8`, ...globals.map(value => `/p:${value}`)]);
    const log = fs.readFileSync(logFile, 'utf8');
    assert.doesNotMatch(log, /(?:[A-Za-z]:[^\r\n]*[/\\]vcpkg[/\\]scripts[/\\]|applocal\.ps1)/i);
    const staged = path.join(directory, `stage-${config}`);
    for (const [name, sourceFile] of expected) assert.deepEqual(fs.readFileSync(path.join(staged, 'legal', name)), fs.readFileSync(sourceFile));
    assert.deepEqual(fs.readFileSync(path.join(staged, 'NoticeApp.exe')), fs.readFileSync(path.join(dir, `${config}.exe`)));
    const manifest = JSON.parse(fs.readFileSync(path.join(staged, 'application-manifest.json')));
    assert.equal(manifest.schema, 'soui.desktop.application-stage/2'); assert.equal(manifest.notices.files.length, 4);
  }
  stageAndCheck(build, 'Debug'); stageAndCheck(build, 'Release');
  // Stale find_library cache must not silently bind another package's notices.
  const mismatch = run(['-S', source, '-B', path.join(dir, 'mismatch'), ...configArgs,
    `-DCMAKE_VS_GLOBALS=${globals.join(';')}`, `-DSOUI_WEBVIEW2_SDK_ROOT=${posix(webview)}`,
    `-DSOUI_DESKTOP_WEBVIEW2_LOADER=${posix(path.join(dir, 'Debug.exe'))}`], false);
  assert.match(mismatch, /cached loader\/headers do not match/);

  const original = path.join(dir, 'package-before-move');
  const moduleDir = path.join(original, 'lib/cmake/SouiDesktop');
  write(path.join(moduleDir, 'SouiDesktopConfig.cmake'), fs.readFileSync(path.join(build, 'sdk/desktop/SouiDesktopConfig.cmake')));
  write(path.join(moduleDir, 'SouiDesktopApplication.cmake'), fs.readFileSync(path.join(root, 'sdk/desktop/SouiDesktopApplication.cmake')));
  write(path.join(moduleDir, 'SouiDesktopTargets.cmake'), 'add_library(Soui::Desktop INTERFACE IMPORTED)\nset_target_properties(Soui::Desktop PROPERTIES SOUI_APPLICATION_PLATFORM windows SOUI_APPLICATION_ARCH x64)\n');
  // Exact-version dependency metadata fixture, with a deliberately disturbed
  // PACKAGE_PREFIX_DIR to exercise preservation of Desktop's own prefix.
  write(path.join(original, 'lib/cmake/SouiRuntime/SouiRuntimeConfig.cmake'), 'add_library(Soui::Runtime INTERFACE IMPORTED)\nset(PACKAGE_PREFIX_DIR "dependency-must-not-rebind-desktop")\n');
  write(path.join(original, 'lib/cmake/SouiRuntime/SouiRuntimeConfigVersion.cmake'), 'set(PACKAGE_VERSION "0.6.0")\nset(PACKAGE_VERSION_EXACT TRUE)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n');
  for (const tool of ['stage-desktop-app.cjs', 'verify-desktop-app.cjs', 'archive-desktop-app.cjs', 'desktop-preflight.cjs'])
    write(path.join(original, 'share/SouiDesktop/tools', tool), fs.readFileSync(path.join(root, 'tools', tool)));
  for (const [name, sourceFile] of expected) {
    const destination = name.startsWith('WebView2-') ? `SouiDesktop/webview2/${name.slice(9)}` : `SouiRuntime/${name}`;
    write(path.join(original, 'share', destination), fs.readFileSync(sourceFile));
  }
  write(path.join(original, 'lib/SouiDesktop/webview2/WebView2LoaderStatic.lib'), Buffer.from('metadata fixture, never linked'));
  const relocated = path.join(dir, 'relocated SDK'); fs.renameSync(original, relocated);
  const consumer = path.join(dir, 'consumer'), consumerBuild = path.join(dir, 'consumer-build');
  write(path.join(consumer, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.24)
project(NoticeInstalled LANGUAGES NONE)
if(FOREIGN_LOADER)
  add_library(Soui::DesktopWebView2Loader STATIC IMPORTED)
  set_target_properties(Soui::DesktopWebView2Loader PROPERTIES
    IMPORTED_LOCATION "${posix(relocated)}/lib/SouiDesktop/webview2/WebView2LoaderStatic.lib"
    IMPORTED_LOCATION_RELEASE "${posix(dir)}/Release.exe")
  if(NOT OMIT_IMPORTED_CONFIGURATIONS)
    set_property(TARGET Soui::DesktopWebView2Loader PROPERTY IMPORTED_CONFIGURATIONS Release)
  endif()
endif()
if(FOREIGN_MAPPING)
  add_library(Soui::DesktopWebView2Loader STATIC IMPORTED)
  set_target_properties(Soui::DesktopWebView2Loader PROPERTIES
    IMPORTED_LOCATION "${posix(relocated)}/lib/SouiDesktop/webview2/WebView2LoaderStatic.lib"
    MAP_IMPORTED_CONFIG_RELEASE Hidden IMPORTED_LOCATION_HIDDEN "${posix(dir)}/Release.exe")
endif()
find_package(SouiDesktop CONFIG REQUIRED)
${application}`);
  run(['-S', consumer, '-B', consumerBuild, ...configArgs, `-DCMAKE_VS_GLOBALS=${globals.join(';')}`, `-DCMAKE_PREFIX_PATH=${posix(relocated)}`]);
  stageAndCheck(consumerBuild, 'Debug'); stageAndCheck(consumerBuild, 'Release');
  const archived = run(['--build', consumerBuild, '--config', 'Release', '--target', 'notice_app_archive_stage', '--parallel', '2', '--',
    '/nr:false', ...globals.map(value => `/p:${value}`)]);
  assert.ok(fs.existsSync(path.join(consumerBuild, 'stage-Release-delivery/NoticeApp-1.0.0-windows-x64.zip')), archived);
  for (const [name, options] of [
    ['foreign-loader', ['-DFOREIGN_LOADER=ON']],
    ['unlisted-loader', ['-DFOREIGN_LOADER=ON', '-DOMIT_IMPORTED_CONFIGURATIONS=ON']],
    ['mapped-loader', ['-DFOREIGN_MAPPING=ON']],
  ]) {
    const foreign = run(['-S', consumer, '-B', path.join(dir, name), ...configArgs,
      `-DCMAKE_VS_GLOBALS=${globals.join(';')}`, `-DCMAKE_PREFIX_PATH=${posix(relocated)}`, ...options], false);
    assert.match(foreign.replace(/\s+/g, ' '), /configuration loader belongs to a different SDK package/);
  }
  t.diagnostic('Real source SDK configuration; four explicit stage targets, relocated Config metadata fixture, two configurations; no application execution.');
});
