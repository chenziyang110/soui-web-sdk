'use strict';

const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const identity = require('../tools/internal/delivery-identity.cjs');
const builds = require('../tools/internal/build-evidence.cjs');

const [destination, ...suiteIds] = process.argv.slice(2);
if (!destination || suiteIds.length === 0) throw new Error('Destination and suite IDs required');
const target = path.resolve(destination);
if (target === root || target.startsWith(root + path.sep) || fs.existsSync(target)) {
  throw new Error('Use a fresh destination outside the SDK tree');
}

const records = ['x64', 'x86'].map(architecture => {
  const build = builds.verifyBuild(root, architecture, 'Release', true);
  return {architecture, build};
});
const expected = records[0].build.source.sha256;
if (records.some(({build}) => build.source.sha256 !== expected)) {
  throw new Error('The two SDK builds have different source identities');
}
const source = identity.source(root);
if (source.sha256 !== expected) throw new Error('Current source differs from the SDK builds');

fs.mkdirSync(target, {recursive: true});
for (const file of source.files) {
  const from = path.join(root, file.path);
  const to = path.join(target, file.path);
  fs.mkdirSync(path.dirname(to), {recursive: true});
  fs.copyFileSync(from, to);
}
for (const {architecture} of records) {
  for (const relative of [
    `out/sdk-${architecture}/build-Release.json`,
    `out/sdk-${architecture}/Release/soui_sdk.lib`,
  ]) {
    const to = path.join(target, relative);
    fs.mkdirSync(path.dirname(to), {recursive: true});
    fs.copyFileSync(path.join(root, relative), to);
  }
}
for (const suiteId of suiteIds) {
  if (!/^[0-9a-f-]{36}$/i.test(suiteId)) throw new Error('Invalid suite ID');
  const from = path.join(root, 'out', 'acceptance', suiteId);
  const suite = JSON.parse(fs.readFileSync(path.join(from, 'build.json'), 'utf8'));
  if (suite.sourceSha256 !== expected || !records.some(({build}) => build.buildId === suite.buildId)) {
    throw new Error('Suite source or build identity differs: ' + suiteId);
  }
  const to = path.join(target, 'out', 'acceptance', suiteId);
  fs.mkdirSync(path.dirname(to), {recursive: true});
  fs.cpSync(from, to, {recursive: true, errorOnExist: true, force: false});
}
if (identity.source(target).sha256 !== expected) throw new Error('Frozen source differs');
for (const {architecture, build} of records) {
  const frozen = builds.verifyBuild(target, architecture, 'Release', true);
  if (frozen.buildId !== build.buildId) throw new Error('Frozen SDK build differs');
}
const result = {
  schema: 'soui.local-acceptance-freeze/v1',
  sourceSha256: expected,
  sdkBuildIds: Object.fromEntries(records.map(({architecture, build}) => [architecture, build.buildId])),
  suiteIds,
  createdAt: new Date().toISOString(),
};
fs.writeFileSync(path.join(target, 'freeze.json'), identity.encoded(result));
console.log(identity.encoded(result));
