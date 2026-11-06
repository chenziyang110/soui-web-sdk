'use strict';
// Build-time provenance only. Never loads the SDK or changes an accepted receipt.
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const crypto = require('node:crypto');
const { isVersion } = require('../../tools/internal/sdk-version.cjs');
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const encoded = value => JSON.stringify(value, null, 2) + '\n';
const need = (condition, message) => { if (!condition) throw new Error(message); };
const ROOT = path.resolve(__dirname, '../..');
const DEFAULT_WEBVIEW2_DESCRIPTOR = path.join(ROOT, 'sdk/renderers/webview2/dependency.json');
const ZIP_INVENTORY_MODULE = path.join(ROOT, 'tools/internal/performance-zip-inventory.mjs');
const MAX_RECEIPT_BYTES = 16 * 1024 * 1024;
const MAX_WEBVIEW2_ARCHIVE_BYTES = 64 * 1024 * 1024;
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));

function regular(file, maxBytes, label = 'SDK input') {
  need(path.isAbsolute(file), 'Absolute ' + label + ' path required');
  for (let item = path.resolve(file);; item = path.dirname(item)) {
    need(!fs.lstatSync(item).isSymbolicLink(), 'Linked ' + label + ': ' + item);
    if (path.dirname(item) === item) break;
  }
  const stat = fs.statSync(file);
  need(stat.isFile(), label + ' is not a file: ' + file);
  if (maxBytes !== undefined) need(stat.size <= maxBytes, label + ' is too large');
  return fs.readFileSync(file);
}

function inventory(prefix, relative = '') {
  const result = [];
  for (const name of fs.readdirSync(path.join(prefix, relative)).sort()) {
    const next = relative ? relative + '/' + name : name;
    const file = path.join(prefix, next), stat = fs.lstatSync(file);
    need(!stat.isSymbolicLink(), 'Linked SDK input: ' + next);
    if (stat.isDirectory()) result.push(...inventory(prefix, next));
    else { const bytes = regular(file, undefined, 'SDK input'); result.push({ path: next, bytes: bytes.length, sha256: hash(bytes) }); }
  }
  return result;
}

function normalizedSource(bytes, row, label) {
  need(row && Number.isSafeInteger(row.bytes) && row.bytes >= 0 && /^[a-f0-9]{64}$/.test(row.sha256),
    'Invalid source manifest row: ' + label);
  need(row.normalization === undefined || ['none', 'lf'].includes(row.normalization),
    'Invalid source normalization: ' + label);
  return row.normalization === 'lf'
    ? Buffer.from(bytes.toString('latin1').replace(/\r\n/g, '\n'), 'latin1')
    : bytes;
}

function pathKey(input) {
  let absolute = path.resolve(input);
  need(path.isAbsolute(absolute), 'Absolute path required for overlap check');
  const suffix = [];
  while (!fs.existsSync(absolute)) {
    const parent = path.dirname(absolute);
    need(parent !== absolute, 'No existing ancestor for overlap check: ' + input);
    suffix.unshift(path.basename(absolute));
    absolute = parent;
  }
  const resolved = path.resolve(fs.realpathSync.native(absolute), ...suffix);
  return process.platform === 'win32' ? resolved.toLowerCase() : resolved;
}

function overlaps(left, right) {
  const a = pathKey(left), b = pathKey(right);
  const relative = (from, to) => path.relative(from, to);
  const isWithin = (parent, child) => {
    const value = relative(parent, child);
    return value === '' || (!path.isAbsolute(value) && value !== '..' && !value.startsWith('..' + path.sep));
  };
  return isWithin(a, b) || isWithin(b, a);
}

function validateFreezePaths({ prefix, receipt, archive, descriptor }, manifest, header) {
  need(path.isAbsolute(manifest) && path.isAbsolute(header), 'Absolute SDK identity output paths required');
  const outputs = [['manifest', manifest], ['header', header]];
  for (const [label, output] of outputs) {
    for (let item = path.resolve(output);; item = path.dirname(item)) {
      try {
        need(!fs.lstatSync(item).isSymbolicLink(), 'Linked SDK identity output: ' + item);
      } catch (error) {
        if (error.code !== 'ENOENT') throw error;
      }
      if (path.dirname(item) === item) break;
    }
    const parent = path.dirname(output);
    need(fs.statSync(parent).isDirectory(), 'SDK identity output directory must exist: ' + parent);
    if (fs.existsSync(output)) {
      const stat = fs.lstatSync(output);
      need(stat.isFile() && !stat.isSymbolicLink() && stat.nlink === 1,
        'SDK identity output must be a private regular file: ' + output);
    }
    for (const [inputName, input] of Object.entries({ prefix, receipt, archive, descriptor })) {
      need(!overlaps(output, input), 'SDK identity output overlaps ' + inputName + ': ' + output);
    }
  }
  need(!overlaps(manifest, header), 'SDK identity manifest and header outputs overlap');
}

function parseInstalledVersion(bytes) {
  const text = bytes.toString('utf8').replace(/^\uFEFF/, '');
  const matches = [...text.matchAll(/^[ \t]*#[ \t]*define[ \t]+SOUI_SDK_VERSION_STRING[ \t]+"([^"]+)"[ \t]*$/gm)];
  need(matches.length === 1 && isVersion(matches[0][1]), 'Installed SDK version header is invalid');
  return matches[0][1];
}

async function inspect({ prefix, receipt, archive, version, architecture, configuration, dependencyDescriptor }) {
  dependencyDescriptor = dependencyDescriptor || DEFAULT_WEBVIEW2_DESCRIPTOR;
  need(path.isAbsolute(prefix) && path.isAbsolute(receipt) && path.isAbsolute(archive) && path.isAbsolute(dependencyDescriptor),
    'Absolute SDK prefix, receipt, WebView2 package archive and dependency descriptor required');
  need(isVersion(version) && !version.includes('-'), 'Exact numeric SDK version required');
  need(['x64', 'x86'].includes(architecture) && ['Release', 'Debug'].includes(configuration), 'Invalid SDK architecture/configuration');
  const receiptBytes = regular(receipt, MAX_RECEIPT_BYTES, 'SDK receipt');
  const build = JSON.parse(receiptBytes.toString('utf8').replace(/^\uFEFF/, ''));
  need(build.schema === 'soui.source-build/v2' && build.nativeBuild === 'passed' && build.sdkOnly === true,
    'An accepted SDK-only build receipt is required');
  need(isVersion(build.version) && build.version.split('-')[0] === version &&
      build.architecture === architecture && build.configuration === configuration,
    'SDK receipt version, architecture or configuration differs');
  need(typeof build.buildId === 'string' && /^[A-Za-z0-9_.-]{1,80}$/.test(build.buildId), 'Invalid SDK build ID');
  need(/^[a-f0-9]{64}$/.test(build.source?.sha256) && Array.isArray(build.source.files) && build.source.files.length,
    'SDK source manifest is missing');
  need(hash(encoded(build.source.files)) === build.source.sha256, 'SDK source manifest digest differs');
  const sources = new Map();
  const foldedSources = new Set();
  for (const row of build.source.files) {
    need(row && typeof row.path === 'string' && row.path.length > 0 && !row.path.includes('\\') &&
      !row.path.startsWith('/') && !row.path.split('/').some(part => !part || part === '.' || part === '..'),
    'Invalid SDK source path');
    const folded = row.path.toLowerCase();
    need(!foldedSources.has(folded) && /^[a-f0-9]{64}$/.test(row.sha256) && Number.isSafeInteger(row.bytes) && row.bytes >= 0,
      'Invalid or duplicate SDK source file');
    foldedSources.add(folded);
    sources.set(row.path, row);
  }

  const descriptorRow = sources.get('sdk/renderers/webview2/dependency.json');
  need(descriptorRow, 'Pinned WebView2 dependency descriptor is absent from SDK build sources');
  const descriptorBytes = regular(dependencyDescriptor, 1024 * 1024, 'WebView2 dependency descriptor');
  const normalizedDescriptor = normalizedSource(descriptorBytes, descriptorRow, 'sdk/renderers/webview2/dependency.json');
  need(normalizedDescriptor.length === descriptorRow.bytes && hash(normalizedDescriptor) === descriptorRow.sha256,
    'WebView2 dependency descriptor differs from SDK build sources');
  const dependency = JSON.parse(descriptorBytes.toString('utf8').replace(/^\uFEFF/, ''));
  need(dependency.schema === 'soui.optional-dependency/1' && dependency.id === 'Microsoft.Web.WebView2' &&
      typeof dependency.version === 'string' && /^\d+\.\d+\.\d+\.\d+$/.test(dependency.version) &&
      /^[a-f0-9]{64}$/.test(dependency.sha256), 'Invalid pinned WebView2 dependency descriptor');
  const expectedArchiveName = `microsoft.web.webview2.${dependency.version}.nupkg`;
  need(path.basename(archive).toLowerCase() === expectedArchiveName.toLowerCase(), 'WebView2 package archive name/version differs');
  const archiveBytes = regular(archive, MAX_WEBVIEW2_ARCHIVE_BYTES, 'WebView2 package archive');
  need(hash(archiveBytes) === dependency.sha256, 'WebView2 package archive differs from pinned dependency SHA-256');
  const { zipInventory } = await import(pathToFileURL(ZIP_INVENTORY_MODULE).href);
  const packageFiles = await zipInventory(archive);
  const packageLoaderPath = `build/native/${architecture}/WebView2LoaderStatic.lib`;
  const packageLoader = packageFiles.find(row => row.path === packageLoaderPath);
  need(packageLoader && packageLoader.bytes > 0, 'Pinned WebView2 archive loader library is missing');

  const files = inventory(prefix), byPath = new Map(files.map(row => [row.path, row]));
  let headers = 0;
  for (const row of files.filter(row => row.path.startsWith('include/'))) {
    const sourceName = row.path === 'include/soui/action-json/json.hpp' ? 'third_party/include/json/json.hpp' : 'sdk/' + row.path;
    const source = sources.get(sourceName);
    need(source, 'Installed header is absent from SDK build sources: ' + row.path);
    const bytes = regular(path.join(prefix, row.path), undefined, 'Installed SDK header');
    const normalized = normalizedSource(bytes, source, row.path);
    need(hash(normalized) === source.sha256 && normalized.length === source.bytes, 'Installed header differs from SDK source: ' + row.path);
    ++headers;
  }
  need(headers > 0, 'Installed SDK headers missing');
  const versionHeader = byPath.get('include/soui/version.h');
  need(versionHeader, 'Installed SDK version header missing');
  const installedVersion = parseInstalledVersion(regular(path.join(prefix, versionHeader.path), undefined, 'Installed SDK version header'));
  need(installedVersion === build.version && installedVersion.split('-')[0] === version,
    'Installed SDK version header differs from the SDK build receipt');

  function library(group, sourceName, installedName) {
    const expected = group?.files?.filter(row => row.path === sourceName);
    const actual = byPath.get(installedName);
    need(expected?.length === 1 && actual && expected[0].sha256 === actual.sha256 && expected[0].bytes === actual.bytes,
      'Installed SDK library differs from accepted build: ' + installedName);
    return { path: installedName, bytes: actual.bytes, sha256: actual.sha256 };
  }
  const libraries = {
    sdk: library(build.library, configuration + '/soui_sdk.lib', 'lib/' + configuration + '/soui_sdk.lib'),
    webview2: library(build.optionalLibraries?.webview2, 'renderers/webview2/' + configuration + '/soui_webview2.lib',
      'lib/' + configuration + '/soui_webview2.lib')
  };
  const installedLoader = byPath.get('lib/webview2/WebView2LoaderStatic.lib');
  need(installedLoader && installedLoader.bytes === packageLoader.bytes && installedLoader.sha256 === packageLoader.sha256,
    'Installed WebView2 loader differs from the pinned dependency archive');
  const nativeToolchain = build.nativeToolchain?.profile || 'MSVC143';
  need(['MSVC143', 'LLVM22MSVC142'].includes(nativeToolchain), 'Unknown SDK native toolchain profile');
  libraries.loader = { ...installedLoader, packageVersion: dependency.version, packageSha256: dependency.sha256,
    packageEntry: packageLoaderPath, dependencyDescriptorSha256: descriptorRow.sha256 };
  const summary = { schema: 'soui.performance-sdk-build/v1', sdkVersion: build.version, packageVersion: version,
    architecture, configuration, sdkBuildId: build.buildId, sourceSha256: build.source.sha256, sdkNativeToolchain: nativeToolchain,
    buildReceiptSha256: hash(receiptBytes), installedInputsSha256: hash(encoded(files)),
    installedFileCount: files.length, headerCount: headers, libraries,
    sourceBinding: 'verified-build-receipt-installed-headers-libraries-and-pinned-webview2-loader' };
  return { schema: 'soui.performance-sdk-inputs/v1', summary, files };
}

function cppStringLiteral(value) {
  // JSON string escaping is compatible with an ordinary C++ string literal for this ASCII-only summary.
  return JSON.stringify(value);
}

async function freeze(options, manifest, header) {
  const descriptor = options.dependencyDescriptor || DEFAULT_WEBVIEW2_DESCRIPTOR;
  validateFreezePaths({ ...options, descriptor }, manifest, header);
  const value = await inspect(options);
  const headerText = '#pragma once\nnamespace perf { inline constexpr char frozen_sdk_build_json[] = ' +
    cppStringLiteral(JSON.stringify(value.summary)) + '; }\n';
  fs.writeFileSync(manifest, encoded(value));
  fs.writeFileSync(header, headerText);
  return value.summary;
}

async function verify(options, manifest) {
  const actual = await inspect(options), frozen = readJson(manifest);
  need(encoded(actual) === encoded(frozen), 'Frozen installed SDK inputs or receipt changed; configure a new consumer');
  return actual.summary;
}

module.exports = { inspect, freeze, verify };
if (require.main === module) {
  (async () => {
    const [command, prefix, receipt, archive, version, architecture, configuration, manifest, header] = process.argv.slice(2);
    const options = { prefix, receipt, archive, version, architecture, configuration };
    need(command === 'freeze' || command === 'verify', 'Expected freeze or verify');
    need(manifest && (command !== 'freeze' || header), 'Manifest/header output required');
    const summary = command === 'freeze' ? await freeze(options, manifest, header) : await verify(options, manifest);
    process.stdout.write(encoded(summary));
  })().catch(error => { console.error(error.message); process.exitCode = 1; });
}
