'use strict';

// Independent archive reader: uses tokenized MAP rows rather than the collector's row regex.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const zlib = require('node:zlib');
const assert = require('node:assert/strict');
const directory = __dirname, root = path.resolve(directory, '../../..');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function pinned(base, record) {
  assert(!path.isAbsolute(record.path));
  const name = path.resolve(base, record.path);
  assert(name.startsWith(path.resolve(base) + path.sep), 'Input must remain inside its declared root');
  const stat = fs.lstatSync(name); assert(stat.isFile() && !stat.isSymbolicLink());
  const bytes = fs.readFileSync(name);
  assert.equal(bytes.length, record.bytes); assert.equal(hash(bytes), record.sha256);
  return bytes;
}
const read = name => JSON.parse(fs.readFileSync(path.join(directory, name), 'utf8'));
const index = read('Files.index.json');
assert.deepEqual(index.excluded, ['Files.index.json']);
assert.equal(new Set(index.files.map(row => row.path)).size, index.files.length);
assert.deepEqual(fs.readdirSync(directory).sort(), [...index.files.map(row => row.path), 'Files.index.json'].sort());
for (const file of index.files) pinned(directory, file);
const summary = read('SUMMARY.json'), collection = read('Collection.actual.json');
assert.equal(summary.sdkVersion, '0.6.0');
const source = JSON.parse(pinned(root, summary.sourceInventory));
assert.equal(source.identity, summary.sourceIdentity);
const sourceLicense = pinned(root, summary.opensslSource.license), notice = pinned(root, summary.opensslSource.preservedNotice);
assert(sourceLicense.equals(notice));
const header = pinned(root, summary.opensslSource.header);
assert(/OPENSSL_VERSION_NUMBER\s+0x1000208fL/.test(header.toString('utf8')));
for (const record of [summary.opensslSource.header, summary.opensslSource.license]) {
  const relative = record.path.slice('third_party/sources/miniblink/'.length);
  const original = source.files.find(file => file.path === relative); assert(original);
  assert.equal(original.bytes, record.bytes); assert.equal(original.sha256, record.sha256);
}
for (const file of collection.generatedFiles) pinned(directory, file);
pinned(directory, collection.collector); pinned(directory, collection.verifier);
assert.equal(collection.buildsExecuted, false); assert.equal(collection.productExecuted, false);
const results = [];
for (const architecture of ['x64', 'x86']) {
  const entry = summary.architectures.find(row => row.architecture === architecture); assert(entry);
  const row = JSON.parse(pinned(directory, entry.record));
  const actual = JSON.parse(pinned(root, row.summary));
  const built = JSON.parse(pinned(root, row.buildResult));
  assert.equal(actual.architecture, architecture); assert(actual.wholeJobNatural);
  assert.equal(actual.result.exitCode, 0); assert.equal(actual.result.supervisorExitCode, 0);
  assert.equal(actual.sourceIdentity, source.identity); assert.equal(built.sourceIdentity, source.identity);
  assert.deepEqual(row.sourceInventory, summary.sourceInventory);
  assert.equal(built.sourceAfter.bytes, row.sourceInventory.bytes); assert.equal(built.sourceAfter.sha256, row.sourceInventory.sha256);
  for (const role of ['map', 'dll']) {
    assert.equal(actual[role].bytes, row[role].bytes); assert.equal(actual[role].sha256, row[role].sha256);
    assert.equal(built[role].bytes, row[role].bytes); assert.equal(built[role].sha256, row[role].sha256);
  }
  const archive = pinned(directory, row.archivedMap), map = zlib.gunzipSync(archive);
  assert.equal(map.length, row.map.bytes); assert.equal(hash(map), row.map.sha256);
  const dll = pinned(root, row.delivered);
  assert.equal(dll.length, row.dll.bytes); assert.equal(hash(dll), row.dll.sha256);
  const pe = dll.readUInt32LE(0x3c); assert.equal(dll.readUInt32LE(pe), 0x4550);
  const opt = pe + 24, magic = dll.readUInt16LE(opt);
  assert(magic === 0x20b || magic === 0x10b);
  const base = magic === 0x20b ? Number(dll.readBigUInt64LE(opt + 24)) : dll.readUInt32LE(opt + 28);
  const sections = Array.from({length: dll.readUInt16LE(pe + 6)}, (_, i) => {
    const at = opt + dll.readUInt16LE(pe + 20) + i * 40;
    return {number: i + 1, name: dll.toString('ascii', at, at + 8).split('\0')[0],
      virtualSize: dll.readUInt32LE(at + 8), rva: dll.readUInt32LE(at + 12),
      rawSize: dll.readUInt32LE(at + 16), rawOffset: dll.readUInt32LE(at + 20)};
  });
  assert.deepEqual(row.pe, {machine: dll.readUInt16LE(pe + 4), timestamp: dll.readUInt32LE(pe + 8), preferredBase: base, sections});
  assert.equal(row.pe.machine, architecture === 'x64' ? 0x8664 : 0x14c);
  const lines = map.toString('utf8').split(/\r?\n/), groups = new Map();
  const timestamp = lines.find(line => line.includes('Timestamp is')).trim().split(/\s+/)[2];
  const preferredBase = lines.find(line => line.includes('Preferred load address is')).trim().split(/\s+/)[4];
  assert.equal(parseInt(timestamp, 16), row.pe.timestamp); assert.equal(parseInt(preferredBase, 16), base);
  let total = 0, zero = 0, mapped = 0, opensslMapped = 0; const examples = [];
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]; if (!/^\s+[\da-f]{4}:[\da-f]{8}\s+/i.test(line)) continue;
    const tokens = line.trim().split(/\s+/);
    if (/^[\da-f]+H$/i.test(tokens[1])) continue; // section-size row
    assert(/^[\da-f]{8,16}$/i.test(tokens[2]), 'Malformed symbol row ' + (i + 1));
    const [section, offset] = tokens[0].split(':').map(x => parseInt(x, 16));
    const symbol = tokens[1], address = parseInt(tokens[2], 16);
    let originAt = 3; while (tokens[originAt] === 'f' || tokens[originAt] === 'i') originAt++;
    const origin = tokens.slice(originAt).join(' ') || '<unattributed>', split = origin.indexOf(':');
    const library = origin === '<unattributed>' ? origin : split === -1 ? '<direct>' : origin.slice(0, split);
    const member = split === -1 ? origin : origin.slice(split + 1);
    if (!groups.has(library)) groups.set(library, {count: 0, members: new Set()});
    const group = groups.get(library); group.count++; group.members.add(member); total++;
    if (section === 0) { zero++; assert.notEqual(library, 'openssl'); continue; }
    const entry = sections[section - 1]; assert(entry);
    assert.equal(base + entry.rva + offset, address); mapped++;
    if (library === 'openssl') {
      opensslMapped++;
      if (['SSL_new', 'SSL_connect', '_SSL_new', '_SSL_connect'].includes(symbol)) {
        assert(offset + 16 <= entry.rawSize);
        const start = entry.rawOffset + offset;
        examples.push({line: i + 1, symbol, member, section, offset, address,
          machineCodeFirst16Hex: dll.subarray(start, start + 16).toString('hex')});
      }
    }
  }
  const names = [...groups.keys()].sort();
  const actualGroups = names.map(library => ({library, symbolRows: groups.get(library).count,
    memberCount: groups.get(library).members.size, members: [...groups.get(library).members].sort()}));
  assert.deepEqual(actualGroups, row.groups); assert.equal(total, row.symbolRows); assert.equal(row.unparsedSymbolRows, 0);
  assert.deepEqual(row.addressMapping, {mappedRows: mapped, zeroSectionRows: zero, invalidSectionRows: 0, mismatchedAddressRows: 0});
  assert.equal(opensslMapped, groups.get('openssl').count); assert.equal(examples.length, 2);
  assert.deepEqual(row.openssl, {symbolRows: opensslMapped, memberCount: groups.get('openssl').members.size,
    allSymbolAddressesMappedToExactDll: true, examples});
  assert.equal(entry.symbolRows, total); assert.deepEqual(entry.openssl, row.openssl);
  for (const record of [row, summary, collection])
    for (const flag of ['binaryLicenseReviewComplete', 'productionQualificationComplete', 'fullDevtoolsGoalComplete', 'releaseReady']) assert.equal(record[flag], false);
  assert.equal(row.transitiveDependencyInventoryComplete, false);
  results.push({architecture, symbolRows: total, opensslSymbolRows: opensslMapped, opensslMembers: groups.get('openssl').members.size,
    mappedRows: mapped, zeroSectionRows: zero, rawMapSha256: hash(map), exactDllSha256: hash(dll)});
}
assert.equal(summary.transitiveDependencyInventoryComplete, false);
console.log(JSON.stringify({schema: 'soui.current-link-members-verification/v1', indexedFiles: index.files.length,
  closedFileSetVerified: true, exactDllAndMapIdentityVerified: true, allSectionSymbolAddressesVerified: true,
  architectureResults: results, binaryLicenseReviewComplete: false, releaseReady: false}));
