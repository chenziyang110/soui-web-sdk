'use strict';

// Read-only collection from the exact existing build artifacts; no compiler or DLL is run.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const zlib = require('node:zlib');
const assert = require('node:assert/strict');
const directory = __dirname;
const root = path.resolve(directory, '../../..');
const buildDirectory = 'verification/production-validation/0.6.0-source2-kernel-2026-10-29';
const sourceIdentity = '26c5b73e4915bb4539e87b987cd8e952005f0fe41d987138d8d8e3b13eb25c95';
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const slash = value => value.replaceAll('\\', '/');
const pin = (name, bytes) => ({path: slash(name), bytes: bytes.length, sha256: digest(bytes)});
const compare = (a, b) => a < b ? -1 : a > b ? 1 : 0;

function read(name, expected) {
  const entry = fs.lstatSync(name, {bigint: true});
  assert(entry.isFile() && !entry.isSymbolicLink(), 'Regular input required: ' + name);
  const fd = fs.openSync(name, 'r');
  let bytes;
  try {
    const before = fs.fstatSync(fd, {bigint: true});
    assert.equal(before.dev, entry.dev); assert.equal(before.ino, entry.ino);
    bytes = fs.readFileSync(fd);
    const after = fs.fstatSync(fd, {bigint: true});
    for (const field of ['dev', 'ino', 'size', 'mtimeNs', 'ctimeNs']) assert.equal(before[field], after[field]);
    assert.equal(BigInt(bytes.length), before.size);
  } finally { fs.closeSync(fd); }
  if (expected) {
    assert.equal(bytes.length, expected.bytes, 'Input size: ' + name);
    assert.equal(digest(bytes), expected.sha256, 'Input SHA-256: ' + name);
  }
  return bytes;
}
function repositoryInput(relativePath, expected) {
  const bytes = read(path.join(root, relativePath), expected);
  return {bytes, record: pin(relativePath, bytes)};
}
function write(name, bytes) {
  if (!Buffer.isBuffer(bytes)) bytes = Buffer.from(JSON.stringify(bytes, null, 2) + '\n');
  fs.writeFileSync(path.join(directory, name), bytes, {flag: 'wx'});
  return pin(name, bytes);
}
function peInfo(dll) {
  assert.equal(dll.toString('ascii', 0, 2), 'MZ');
  const offset = dll.readUInt32LE(0x3c);
  assert.equal(dll.readUInt32LE(offset), 0x4550);
  const optional = offset + 24, magic = dll.readUInt16LE(optional);
  assert(magic === 0x20b || magic === 0x10b);
  const sectionStart = optional + dll.readUInt16LE(offset + 20);
  return {
    machine: dll.readUInt16LE(offset + 4), timestamp: dll.readUInt32LE(offset + 8),
    preferredBase: magic === 0x20b ? Number(dll.readBigUInt64LE(optional + 24)) : dll.readUInt32LE(optional + 28),
    sections: Array.from({length: dll.readUInt16LE(offset + 6)}, (_, i) => {
      const at = sectionStart + i * 40;
      return {number: i + 1, name: dll.toString('ascii', at, at + 8).replace(/\0.*$/, ''),
        virtualSize: dll.readUInt32LE(at + 8), rva: dll.readUInt32LE(at + 12),
        rawSize: dll.readUInt32LE(at + 16), rawOffset: dll.readUInt32LE(at + 20)};
    })
  };
}
function inspect(map, dll) {
  const lines = map.toString('utf8').split(/\r?\n/), pe = peInfo(dll), groups = new Map(), examples = [];
  assert.equal(parseInt(lines.find(x => x.includes('Timestamp is')).match(/Timestamp is ([a-f\d]+)/i)[1], 16), pe.timestamp);
  assert.equal(parseInt(lines.find(x => x.includes('Preferred load address is')).match(/Preferred load address is ([a-f\d]+)/i)[1], 16), pe.preferredBase);
  let symbolRows = 0, mappedRows = 0, zeroSectionRows = 0, opensslMappedRows = 0;
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    const match = line.match(/^\s+([\da-f]{4}):([\da-f]{8})\s+(\S+)\s+([\da-f]{8,16})(?:\s+(?:(?:f|i)\s+)*(\S.*?))?\s*$/i);
    if (!match) {
      assert(!/^\s+[\da-f]{4}:[\da-f]{8}\s+/i.test(line) || /\s+[\da-f]+H\s+/i.test(line), 'Unparsed symbol row ' + (i + 1));
      continue;
    }
    symbolRows++;
    const origin = match[5] || '<unattributed>', colon = origin.indexOf(':');
    const library = origin === '<unattributed>' ? origin : colon < 0 ? '<direct>' : origin.slice(0, colon);
    const member = colon < 0 ? origin : origin.slice(colon + 1);
    if (!groups.has(library)) groups.set(library, {library, symbolRows: 0, members: new Set()});
    const group = groups.get(library); group.symbolRows++; group.members.add(member);
    const section = parseInt(match[1], 16), offset = parseInt(match[2], 16), address = parseInt(match[4], 16);
    if (section === 0) { zeroSectionRows++; assert.notEqual(library, 'openssl'); continue; }
    const entry = pe.sections[section - 1];
    assert(entry, 'Unknown PE section');
    assert.equal(pe.preferredBase + entry.rva + offset, address, 'Symbol address mismatch at ' + (i + 1));
    mappedRows++;
    if (library === 'openssl') {
      opensslMappedRows++;
      if (/^_?SSL_(new|connect)$/.test(match[3])) {
        assert(offset + 16 <= entry.rawSize, 'Example has no raw code bytes');
        const start = entry.rawOffset + offset;
        examples.push({line: i + 1, symbol: match[3], member, section, offset, address,
          machineCodeFirst16Hex: dll.subarray(start, start + 16).toString('hex')});
      }
    }
  }
  assert.equal(examples.length, 2);
  const result = [...groups.values()].map(g => ({library: g.library, symbolRows: g.symbolRows,
    memberCount: g.members.size, members: [...g.members].sort(compare)})).sort((a, b) => compare(a.library, b.library));
  const openssl = groups.get('openssl'); assert(openssl);
  assert.equal(opensslMappedRows, openssl.symbolRows);
  return {pe, symbolRows, unparsedSymbolRows: 0,
    addressMapping: {mappedRows, zeroSectionRows, invalidSectionRows: 0, mismatchedAddressRows: 0},
    groups: result, openssl: {symbolRows: openssl.symbolRows, memberCount: openssl.members.size,
      allSymbolAddressesMappedToExactDll: true, examples}};
}

assert.equal(process.argv.length, 2, 'Run collect.cjs without arguments from any directory.');
for (const name of ['Collection.actual.json', 'SUMMARY.json', 'Files.index.json', 'x64.json', 'x86.json', 'x64.map.gz', 'x86.map.gz'])
  assert(!fs.existsSync(path.join(directory, name)), 'Preserve existing collection: ' + name);
const source = repositoryInput(buildDirectory + '/common-provenance/source/Source2.source-after.json');
const snapshot = JSON.parse(source.bytes);
assert.equal(snapshot.identity, sourceIdentity);
const sourceFile = relative => {
  const expected = snapshot.files.find(row => row.path === relative); assert(expected);
  return repositoryInput('third_party/sources/miniblink/' + relative, expected);
};
const opensslHeader = sourceFile('node/openssl/openssl/include/openssl/opensslv.h');
const opensslLicense = sourceFile('node/openssl/openssl/LICENSE');
const notice = repositoryInput('third_party/licenses/d52/openssl/LICENSE.txt');
assert(opensslLicense.bytes.equals(notice.bytes), 'Source and preserved license differ');
assert(/OPENSSL_VERSION_NUMBER\s+0x1000208fL/.test(opensslHeader.bytes.toString('utf8')));
const rows = [], generatedFiles = [];
for (const architecture of ['x64', 'x86']) {
  const summary = repositoryInput(buildDirectory + '/' + architecture + '/summary.json');
  const actual = JSON.parse(summary.bytes);
  const build = repositoryInput(buildDirectory + '/' + architecture + '/build-result.json', actual.buildResult);
  const built = JSON.parse(build.bytes);
  assert.equal(actual.architecture, architecture); assert.equal(actual.sourceIdentity, sourceIdentity);
  assert(actual.wholeJobNatural && actual.result.exitCode === 0 && actual.result.supervisorExitCode === 0);
  assert.equal(actual.releaseReady, false); assert.equal(built.sourceIdentity, sourceIdentity);
  assert.equal(source.record.bytes, built.sourceAfter.bytes); assert.equal(source.record.sha256, built.sourceAfter.sha256);
  for (const role of ['map', 'dll']) {
    assert.equal(actual[role].bytes, built[role].bytes); assert.equal(actual[role].sha256, built[role].sha256);
  }
  const map = read(actual.map.path, actual.map), dll = read(actual.dll.path, actual.dll);
  const delivered = repositoryInput('third_party/prebuilt/' + architecture + '/miniblink/node.dll', actual.dll);
  const inspection = inspect(map, dll);
  assert.equal(inspection.pe.machine, architecture === 'x64' ? 0x8664 : 0x14c);
  const archive = zlib.gzipSync(map, {level: 9});
  assert(zlib.gunzipSync(archive).equals(map));
  const archivedMap = write(architecture + '.map.gz', archive); generatedFiles.push(archivedMap);
  const record = {schema: 'soui.exact-linked-members/v1', sdkVersion: '0.6.0', architecture,
    summary: summary.record, buildResult: build.record, sourceInventory: source.record,
    sourceIdentity, dll: pin(actual.dll.path, dll), delivered: delivered.record,
    map: pin(actual.map.path, map), archivedMap, ...inspection,
    binaryLicenseReviewComplete: false, transitiveDependencyInventoryComplete: false,
    productionQualificationComplete: false, fullDevtoolsGoalComplete: false, releaseReady: false};
  const recordPin = write(architecture + '.json', record); generatedFiles.push(recordPin);
  rows.push({architecture, record: recordPin, symbolRows: record.symbolRows, openssl: record.openssl});
}
const summaryPin = write('SUMMARY.json', {schema: 'soui.current-linked-members-summary/v1', sdkVersion: '0.6.0',
  createdUtc: new Date().toISOString(), sourceInventory: source.record, sourceIdentity,
  opensslSource: {versionBasis: 'Source header, not runtime detection or license approval.',
    version: '1.0.2h', header: opensslHeader.record, license: opensslLicense.record,
    preservedNotice: notice.record, sourceAndPreservedLicenseBytesEqual: true},
  architectures: rows, binaryLicenseReviewComplete: false, transitiveDependencyInventoryComplete: false,
  productionQualificationComplete: false, fullDevtoolsGoalComplete: false, releaseReady: false,
  limitations: ['MAP symbol-origin membership is mechanical evidence, not an exhaustive nested component/license inventory.',
    'Direct object and unattributed rows remain separate; library inputs do not imply every archive member was retained.',
    'Identical-code folding permits shared addresses; symbol counts are not function counts or runtime coverage.',
    'Zero-section symbols retain their original classification and are not claimed to map into DLL sections.',
    'No compiler, DLL, product, VM or long test was executed by this collection. Historical failures and approvals are unchanged.']});
generatedFiles.push(summaryPin);
write('Collection.actual.json', {schema: 'soui.link-member-collection-actual/v1', createdUtc: new Date().toISOString(),
  executable: pin(process.execPath, read(process.execPath)), argv: process.argv, cwd: slash(process.cwd()),
  collector: pin('collect.cjs', read(__filename)), verifier: pin('verify.cjs', read(path.join(directory, 'verify.cjs'))),
  inputSummaryPaths: rows.map(row => JSON.parse(read(path.join(directory, row.architecture + '.json'))).summary),
  generatedFiles, productExecuted: false, buildsExecuted: false, testsExecuted: false,
  binaryLicenseReviewComplete: false, productionQualificationComplete: false, fullDevtoolsGoalComplete: false, releaseReady: false});
const names = fs.readdirSync(directory).sort(compare);
assert(names.every(name => fs.lstatSync(path.join(directory, name)).isFile()), 'Unexpected non-file in collection');
write('Files.index.json', {schema: 'soui.evidence-closed-file-index/v1', excluded: ['Files.index.json'],
  files: names.map(name => pin(name, read(path.join(directory, name))))});
console.log(JSON.stringify({collection: 'Collection.actual.json', index: 'Files.index.json',
  architectures: rows.map(row => ({architecture: row.architecture, symbolRows: row.symbolRows,
    opensslSymbolRows: row.openssl.symbolRows, opensslMembers: row.openssl.memberCount})), releaseReady: false}));
