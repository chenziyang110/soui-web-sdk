'use strict';
const fs = require('node:fs');
const path = require('node:path');
const profile = require('./llvm-profile-inputs.cjs');
const need = (ok, message) => { if (!ok) throw new Error(message); };
function headerFiles(root, relative = '') {
  const directory = path.join(root, relative), stat = fs.lstatSync(directory);
  need(stat.isDirectory() && !stat.isSymbolicLink(), 'Reference SDK header directory must be plain');
  return fs.readdirSync(directory).sort().flatMap(name => {
    const next = relative ? relative + '/' + name : name, item = fs.lstatSync(path.join(root, next));
    need(!item.isSymbolicLink(), 'Reference SDK headers cannot contain linked entries');
    if (item.isDirectory()) return headerFiles(root, next);
    need(item.isFile(), 'Reference SDK header entry is not a file');
    return [next];
  });
}
function verify(root, architecture, configuration, requestFile, archive, includeRoot) {
  need(['x64', 'x86'].includes(architecture) && ['Debug', 'Release'].includes(configuration), 'Invalid Reference SDK architecture/configuration');
  const request = profile.readRequest(requestFile, architecture, root), selected = {nativeToolchain: 'LLVM22MSVC142', profileRequest: requestFile};
  // The request and configured compiler alone cannot admit an installed SDK.
  const report = require('./build-evidence.cjs').verifyBuild(root, architecture, configuration, true, selected);
  const observation = require('./llvm-build-observer.cjs').verifyStandardBuild(root, architecture, configuration, true, selected);
  const produced = profile.pin(path.join(root, 'out', 'sdk-' + architecture, configuration, 'soui_sdk.lib'));
  const installed = profile.pin(archive);
  need(installed.bytes === produced.bytes && installed.sha256 === produced.sha256, 'Reference imported archive differs from the current verified SDK build');
  const originals = headerFiles(path.join(root, 'sdk/include')).map(name => ({name, source: path.join(root, 'sdk/include', name)}));
  originals.push({name: 'soui/action-json/json.hpp', source: path.join(root, 'third_party/include/json/json.hpp')});
  originals.sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
  need(new Set(originals.map(item => item.name.toLowerCase())).size === originals.length &&
    JSON.stringify(headerFiles(includeRoot)) === JSON.stringify(originals.map(item => item.name)), 'Reference installed public header membership differs');
  const headers = originals.map(item => {
    const sourcePin = profile.pin(item.source), installedPin = profile.pin(path.join(includeRoot, item.name));
    need(sourcePin.bytes === installedPin.bytes && sourcePin.sha256 === installedPin.sha256, 'Reference installed SDK header differs: ' + item.name);
    return {name: item.name, source: sourcePin, installed: installedPin};
  });
  profile.verifyPin(produced); profile.verifyPin(installed); profile.verifyPin(request.pin);
  headers.forEach(item => { profile.verifyPin(item.source); profile.verifyPin(item.installed); });
  return {schema: 'soui.reference-llvm-sdk-inputs/v1', profile: 'LLVM22MSVC142', architecture, configuration,
    buildId: report.buildId, requestFile: request.pin, inputIdentity: request.inputIdentity, bridgeIdentity: request.bridgeIdentity,
    toolchain: request.value.toolchain, observation,
    producedArchive: produced, installedArchive: installed, headers,
    scope: 'current verified SDK archive and exact installed headers; consumer compilation and closure are separate', qualificationPassed: false};
}
module.exports = {verify, headerFiles};
if (require.main === module) {
  try {
    const [command, ...args] = process.argv.slice(2);
    need(command === 'verify' && args.length === 6, 'Usage: llvm-reference-sdk.cjs verify ROOT ARCH CONFIG REQUEST ARCHIVE INCLUDE');
    console.log(JSON.stringify(verify(...args)));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
