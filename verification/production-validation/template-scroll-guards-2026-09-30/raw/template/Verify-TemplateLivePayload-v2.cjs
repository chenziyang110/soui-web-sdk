'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');

function sha(file) { return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'); }
function verifyLivePayload(stage, payload) {
  assert(payload && Array.isArray(payload.files) && payload.files.length > 0, 'Payload files missing');
  const expected = new Set();
  for (const entry of payload.files) {
    assert.equal(typeof entry.path, 'string');
    const rel = entry.path.replaceAll('\\', '/');
    const parts = rel.split('/');
    assert(parts.every(part => part && part !== '.' && part !== '..' && !part.includes(':')), 'Unsafe payload path');
    assert(!expected.has(rel), 'Duplicate payload path');
    expected.add(rel);
    let file = stage;
    for (let index = 0; index < parts.length; index += 1) {
      file = path.join(file, parts[index]);
      const stat = fs.lstatSync(file);
      assert(!stat.isSymbolicLink(), 'Linked payload path: ' + rel);
      if (index < parts.length - 1) assert(stat.isDirectory(), 'Parent is not directory: ' + rel);
      else {
        assert(stat.isFile(), 'Payload entry is not file: ' + rel);
        assert.equal(stat.size, entry.bytes, 'Payload size changed: ' + rel);
        assert.equal(sha(file), entry.sha256, 'Payload bytes changed: ' + rel);
      }
    }
  }
  function walk(directory, prefix = '') {
    assert(!fs.lstatSync(directory).isSymbolicLink(), 'Linked stage directory');
    return fs.readdirSync(directory, {withFileTypes:true}).flatMap(entry => {
      assert(!entry.isSymbolicLink(), 'Linked stage entry');
      const rel = prefix ? prefix + '/' + entry.name : entry.name;
      if (entry.isDirectory()) return walk(path.join(directory,entry.name),rel);
      assert(entry.isFile(), 'Unsupported stage entry');
      return [rel];
    });
  }
  const extras = walk(stage).filter(file => !expected.has(file)).sort();
  assert(extras.every(file => file === 'smoke.log' || file === 'source-navigation.log'), 'Unexpected runtime file');
  const logs = {};
  for (const rel of extras) {
    const file = path.join(stage,rel);
    if (rel === 'source-navigation.log') {
      const expectedNavigation = 'allowed=1 url=' + pathToFileURL(path.join(stage,'resources/index.html')).href;
      const entries = fs.readFileSync(file,'utf8').trim().split(/\r?\n/);
      assert(entries.every(entry => entry === expectedNavigation), 'Unexpected runtime navigation');
    }
    logs[rel] = {bytes:fs.statSync(file).size,sha256:sha(file)};
  }
  return {payloadSha256:payload.sha256,checkedFiles:expected.size,
    legalFiles:payload.files.filter(entry => entry.path.replaceAll('\\','/').startsWith('resources/legal/')).length,
    extras,logs,verifiedAt:new Date().toISOString()};
}
module.exports = {verifyLivePayload};
