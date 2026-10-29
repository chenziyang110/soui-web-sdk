'use strict';

// Engineering evidence only. Reads existing files and creates this archive's JSON
// files with wx. Does not run a compiler, DLL, application, test, VM or license tool.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const directory = __dirname;
const root = path.resolve(directory, '../../..');
const sourceRoot = 'third_party/sources/miniblink/';
const linkedRoot = 'verification/production-validation/current-link-members-0.6.0-2026-10-29/';
const buildRoot = 'verification/production-validation/0.6.0-source2-kernel-2026-10-29/';
const sourceIdentity = '26c5b73e4915bb4539e87b987cd8e952005f0fe41d987138d8d8e3b13eb25c95';
const expectedInputs = {
  [buildRoot + 'common-provenance/source/Source2.source-after.json']: 'c698985f7d46d02984a7eec46d96c1ebeac5164b816b61b560d96b657c136809',
  [linkedRoot + 'x64.json']: 'b23f784b59d3cdc957700d7719402db3c9498c07a812ad1a1e2c92c79b58b024',
  [linkedRoot + 'x86.json']: '831ad561f957842f7e3ed30ca3b92d8cc29e2f75743ffd43541de9852c9ffaa2',
  [linkedRoot + 'SUMMARY.json']: '642b77b3dd3bc188b37e2f95f6cde6c365305d45a93a6955a5edcf4805e1caaf',
  [linkedRoot + 'Files.index.json']: '409ae653d42a05178a9faf976b38f413a58767e1e54434e8a986154c6044bafe'
};
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const slash = text => text.replaceAll('\\', '/');
const compare = (a, b) => a < b ? -1 : a > b ? 1 : 0;
const cache = new Map();
const critical = new Map();
const flags = {binaryLicenseReviewComplete: false, transitiveDependencyInventoryComplete: false,
  applicableTermsReviewed: false, redistributionTermsApproved: false,
  productionQualificationComplete: false, fullDevtoolsGoalComplete: false, releaseReady: false};

function relative(name) {
  assert(typeof name === 'string' && name && !path.isAbsolute(name) && !name.includes('\\'));
  assert(!name.split('/').some(part => !part || part === '.' || part === '..' || part.includes(':')));
  return name;
}
function read(name, fresh = false) {
  relative(name);
  if (!fresh && cache.has(name)) return cache.get(name);
  const full = path.join(root, name), entry = fs.lstatSync(full, {bigint: true});
  assert(entry.isFile() && !entry.isSymbolicLink(), 'Plain file required: ' + name);
  assert(entry.size <= 67108864n, 'Bounded source/metadata read: ' + name);
  const fd = fs.openSync(full, 'r');
  let bytes;
  try {
    const before = fs.fstatSync(fd, {bigint: true});
    for (const field of ['dev', 'ino', 'size']) assert.equal(before[field], entry[field]);
    bytes = fs.readFileSync(fd);
    const after = fs.fstatSync(fd, {bigint: true});
    for (const field of ['dev', 'ino', 'size', 'mtimeNs', 'ctimeNs']) assert.equal(before[field], after[field], 'Input changed: ' + name);
    assert.equal(BigInt(bytes.length), before.size);
  } finally { fs.closeSync(fd); }
  const result = {pin: {path: name, bytes: bytes.length, sha256: digest(bytes)}, bytes};
  if (!fresh) cache.set(name, result);
  return result;
}
function input(name) {
  const result = read(name);
  if (expectedInputs[name]) assert.equal(result.pin.sha256, expectedInputs[name], 'Frozen input digest: ' + name);
  critical.set(name, result.pin);
  return result;
}
function json(name) { return JSON.parse(input(name).bytes); }
function write(name, value) {
  const bytes = Buffer.from(JSON.stringify(value, null, 2) + '\n');
  fs.writeFileSync(path.join(directory, name), bytes, {flag: 'wx'});
  return {path: name, bytes: bytes.length, sha256: digest(bytes)};
}
function ownPin(name) { return read(slash(path.relative(root, path.join(directory, name)))).pin; }
assert.deepEqual(fs.readdirSync(directory).sort(), ['README.md', 'collect.cjs', 'initial-incomplete-collection']);
const priorFiles = fs.readdirSync(path.join(directory, 'initial-incomplete-collection')).sort().map(name => {
  const pin = ownPin('initial-incomplete-collection/' + name);
  return {...pin, path: 'initial-incomplete-collection/' + name};
});
assert.equal(priorFiles.length, 7);
const priorCollection = {directory: 'initial-incomplete-collection', status: 'incomplete-parser-superseded',
  reason: 'The initial block regex consumed self-closing ClCompile items through a later closing tag. Basename candidates and component counts were incomplete. All original files are preserved; no test or legal approval was inferred.', files: priorFiles};
const inventoryFile = buildRoot + 'common-provenance/source/Source2.source-after.json';
const inventory = json(inventoryFile);
assert.equal(inventory.schema, 'soui.kernel-source-files/v1');
assert.equal(inventory.identity, sourceIdentity);
const sourceEntries = new Map(inventory.files.map(row => [row.path.toLowerCase(), row]));
assert.equal(sourceEntries.size, inventory.files.length);
const records = Object.fromEntries(['x64', 'x86'].map(arch => [arch, json(linkedRoot + arch + '.json')]));
for (const [arch, record] of Object.entries(records)) {
  assert.equal(record.architecture, arch); assert.equal(record.sdkVersion, '0.6.0');
  assert.equal(record.sourceIdentity, sourceIdentity);
  assert.equal(record.sourceInventory.sha256, input(inventoryFile).pin.sha256);
}
json(linkedRoot + 'SUMMARY.json'); json(linkedRoot + 'Files.index.json');
input('third_party/manifests/kernel-build-060-source2.json');
input('third_party/manifests/dependencies.json');
input('third_party/manifests/license-review.json');
input('third_party/licenses/KERNEL-NOTICES.md');
input('THIRD_PARTY_NOTICES.md');
input('docs/licenses/v0.6.0-redistribution.md');
const historicalMaterials = json('third_party/licenses/kernel-source/index.json.txt');

function sourcePin(name) {
  const expected = sourceEntries.get(name.toLowerCase());
  if (!expected) return {path: sourceRoot + name, source2EntryPresent: false, currentBytesChecked: false};
  const result = read(sourceRoot + expected.path);
  assert.equal(result.pin.bytes, expected.bytes, 'Source2 size mismatch: ' + name);
  assert.equal(result.pin.sha256, expected.sha256, 'Source2 digest mismatch: ' + name);
  return {...result.pin, source2EntryPresent: true, currentBytesChecked: true, currentBytesEqualSource2: true};
}
function statement(name, pattern) {
  const pin = sourcePin(name);
  assert(pin.currentBytesEqualSource2, 'Statement must belong to current Source2');
  const lines = read(pin.path).bytes.toString('utf8').split(/\r?\n/);
  const result = lines.flatMap((text, i) => pattern.test(text) ? [{line: i + 1, text}] : []);
  assert(result.length, 'Missing declaration: ' + name);
  return {file: pin, statements: result};
}
function version(value, status, specs, note = null) {
  return {value, status, basis: specs.map(([name, pattern]) => statement(name, pattern)),
    upstreamSourceArchiveIdentityProven: false, ...(note ? {note} : {})};
}
function material(source, retained, note = null) {
  const current = source ? sourcePin(source) : null;
  const notices = retained.map(name => read(name).pin);
  return {currentSource: current, retainedNotices: notices,
    retainedBytesEqualCurrentSource: current?.currentBytesEqualSource2 ? notices.map(n => n.bytes === current.bytes && n.sha256 === current.sha256) : notices.map(() => null),
    ...(note ? {note} : {})};
}

const projectSpecs = [
  ['zlib', 'build/zlib/zlib.vcxproj'], ['libjpeg', 'build/libjpeg/libjpeg.vcxproj'],
  ['libpng', 'build/libpng/libpng.vcxproj'], ['harfbuzz', 'build/harfbuzz/harfbuzz.vcxproj'],
  ['libxml', 'build/libxml/libxml.vcxproj'], ['ots', 'build/ots/ots.vcxproj'],
  ['openssl', 'node/openssl/openssl.vcxproj'], ['libcurl', 'build/libcurl/libcurl.vcxproj'],
  ['v8_5_7', 'build/v8_5_7/v8_5_7.vcxproj'], ['v8_5_7_1', 'build/v8_5_7/v8_5_7_1.vcxproj'],
  ['skia', 'build/skia/skia.vcxproj'], ['<direct>', 'build/miniblink/miniblink.vcxproj']
];
const sources = [], sourceIds = new Map();
function sourceId(name) {
  const expected = sourceEntries.get(name.toLowerCase()), canonical = expected?.path || name;
  if (!sourceIds.has(canonical)) {
    sourceIds.set(canonical, sources.length);
    // Release source buffers immediately; pins, not source text, enter the archive.
    const pin = sourcePin(canonical);
    sources.push(pin);
    if (pin.currentBytesChecked) cache.delete(pin.path);
  }
  return sourceIds.get(canonical);
}
function parseProject(name) {
  const pin = sourcePin(name); assert(pin.currentBytesEqualSource2);
  critical.set(pin.path, {path: pin.path, bytes: pin.bytes, sha256: pin.sha256});
  const text = read(pin.path).bytes.toString('utf8'), declarations = [];
  assert(!/<ObjectFileName\b/.test(text), 'Object override needs explicit parsing, not basename inference');
  // Parse every opening tag first. A greedy block regex can silently consume
  // self-closing items until another item's </ClCompile>, losing declarations.
  const pattern = /<ClCompile\b[^>]*\bInclude="([^"]+)"[^>]*>/g;
  for (const match of text.matchAll(pattern)) {
    const literal = match[1];
    assert(!/[&$%]/.test(literal), 'Nonliteral source needs explicit handling');
    const source = path.posix.normalize(path.posix.join(path.posix.dirname(name), slash(literal)));
    relative(source);
    let metadata = match[0];
    if (!/\/>$/.test(match[0])) {
      const close = text.indexOf('</ClCompile>', match.index + match[0].length);
      assert(close >= 0, 'Unclosed ClCompile declaration');
      const body = text.slice(match.index + match[0].length, close);
      assert(!/<ClCompile\b/.test(body), 'Nested compile declaration refused');
      metadata += body;
    }
    declarations.push({source, object: path.posix.basename(source, path.posix.extname(source)) + '.obj',
      projectLine: text.slice(0, match.index).split('\n').length,
      conditionalMetadataPresent: /\bCondition=|<ExcludedFromBuild\b/.test(metadata)});
  }
  assert(declarations.length);
  assert.equal(declarations.length, [...text.matchAll(/<ClCompile\s+Include="/g)].length, 'Every literal compile item must be parsed');
  return {pin, declarations};
}
const contexts = projectSpecs.map(([library, name]) => {
  const project = parseProject(name), merged = new Map();
  for (const arch of ['x64', 'x86']) {
    const group = records[arch].groups.find(row => row.library === library); assert(group);
    for (const member of group.members) {
      if (!merged.has(member)) {
        const candidates = project.declarations.filter(row => row.object.toLowerCase() === member.toLowerCase());
        merged.set(member, {member, architectures: [], candidates: candidates.map(row => ({sourceId: sourceId(row.source),
          projectLine: row.projectLine, conditionalMetadataPresent: row.conditionalMetadataPresent}))});
      }
      merged.get(member).architectures.push(arch);
    }
  }
  const members = [...merged.values()].sort((a, b) => compare(a.member, b.member));
  return {library, project: project.pin, projectCompileDeclarationCount: project.declarations.length,
    declarationsOutsideMatchedMembersNotInterpretedAsLinked: true,
    architectures: Object.fromEntries(['x64', 'x86'].map(arch => {
      const group = records[arch].groups.find(row => row.library === library), rows = members.filter(m => m.architectures.includes(arch));
      return [arch, {symbolRows: group.symbolRows, memberCount: group.memberCount,
        uniqueBasenameCandidateMembers: rows.filter(m => m.candidates.length === 1).length,
        ambiguousBasenameCandidateMembers: rows.filter(m => m.candidates.length > 1).length,
        noBasenameCandidateMembers: rows.filter(m => m.candidates.length === 0).map(m => m.member)}];
    })), members};
});

const componentSpecs = [
  {id: 'zlib', name: 'zlib', contexts: ['zlib'], prefixes: ['third_party/zlib/'],
    version: version(null, 'conflicting-local-declarations', [
      ['third_party/zlib/zlib.h', /^#define ZLIB_VERSION /], ['third_party/zlib/README.chromium', /^Version:/],
      ['third_party/zlib/LICENSE', /version 1\.2\.4/]], 'Header says 1.2.11; README says 1.2.5; retained LICENSE prelude says 1.2.4. Full upstream version is unknown.'),
    declaredTerms: 'zlib text; declaration conflict remains', materials: [material('third_party/zlib/LICENSE', ['third_party/licenses/d52/zlib/LICENSE.txt'])]},
  {id: 'ijg', name: 'IJG JPEG', contexts: ['libjpeg'], prefixes: ['third_party/libjpeg/'],
    version: version('9a (19-Jan-2014)', 'source-header-declaration', [['third_party/libjpeg/jversion.h', /^#define JVERSION/]]),
    declaredTerms: 'IJG accompanying README required by source header', materials: [material('third_party/libjpeg/jversion.h', ['third_party/licenses/ijg-jpeg-9a-README.txt'], 'Recovered README is retained material, not a byte copy of this header. Original source README is absent from this snapshot.')]},
  {id: 'libpng', name: 'libpng', contexts: ['libpng'], prefixes: ['third_party/libpng/'],
    version: version('1.6.28', 'source-header-declaration', [['third_party/libpng/png.h', /^#define PNG_LIBPNG_VER_STRING/]]),
    declaredTerms: 'libpng notice embedded in png.h', materials: [material('third_party/libpng/png.h', ['third_party/licenses/d52/libpng/png.h'])]},
  {id: 'harfbuzz', name: 'HarfBuzz', contexts: ['harfbuzz'], prefixes: ['third_party/harfbuzz-ng/'],
    version: version('1.1.1', 'matching-source-header-and-readme-declarations', [['third_party/harfbuzz-ng/src/hb-version.h', /^#define HB_VERSION_STRING/], ['third_party/harfbuzz-ng/README.chromium', /^Version:|^URL:/]]),
    declaredTerms: 'Old MIT; COPYING explicitly retains subsidiary file exceptions', materials: [material('third_party/harfbuzz-ng/COPYING', ['third_party/licenses/d52/harfbuzz/COPYING.txt'])]},
  {id: 'libxml', name: 'libxml2 partial patched snapshot', contexts: ['libxml'], prefixes: ['third_party/libxml/'],
    version: version('2.7.7', 'matching-source-header-and-readme-declarations', [['third_party/libxml/win32/include/libxml/xmlversion.h', /^#define LIBXML_DOTTED_VERSION/], ['third_party/libxml/README.chromium', /^Version:|^URL:|partial snapshot/]]),
    declaredTerms: 'MIT declaration in source Copyright', materials: [material('third_party/libxml/src/Copyright', ['third_party/licenses/d52/libxml/Copyright.txt'])]},
  {id: 'libxslt', name: 'libxslt nested in libxml archive', contexts: ['libxml'], prefixes: ['third_party/libxslt/'],
    version: version('1.1.28', 'matching-source-header-and-readme-declarations', [['third_party/libxslt/libxslt/xsltconfig.h', /^#define LIBXSLT_DOTTED_VERSION/], ['third_party/libxslt/README.chromium', /^Version:|^URL:/]]),
    declaredTerms: 'MIT declaration; separate copyright exceptions remain in source text', materials: [material('third_party/libxslt/Copyright', ['third_party/licenses/kernel-source/third_party/libxslt/Copyright.txt'])]},
  {id: 'ots', name: 'OpenType Sanitizer', contexts: ['ots'], prefixes: ['third_party/ots/'],
    version: {value: null, status: 'unknown', basis: [], upstreamSourceArchiveIdentityProven: false,
      note: 'No source version metadata established. A recovered notice filename containing 5.0.0 does not establish this source version.'},
    declaredTerms: 'Source header points to a BSD-style LICENSE missing from current source tree',
    materials: [material('third_party/ots/include/opentype-sanitiser.h', ['third_party/licenses/ots-5.0.0-LICENSE.txt'], 'Recovered notice is not proved to be the missing accompanying source LICENSE.')]},
  {id: 'brotli', name: 'Brotli decoder nested in OTS archive', contexts: ['ots'], prefixes: ['third_party/brotli/'],
    version: version('c90ec29f54e9165df815b0f1311cfddb1be4afad', 'local-readme-upstream-revision-declaration', [['third_party/brotli/README.chromium', /^Version:|^URL:|37a320dd81db8d546cd24a45b4c61d87b45dcade/]], 'Decoder subset with local modifications/cherry-pick; not proof of an unmodified upstream checkout.'),
    declaredTerms: 'Apache 2.0 declaration', materials: [material('third_party/brotli/LICENSE', ['third_party/licenses/d52/brotli/LICENSE.txt'])]},
  {id: 'sfntly', name: 'sfntly source-only declarations in OTS project', contexts: ['ots'], prefixes: ['third_party/sfntly/'],
    version: version('130f832eddf98467e6578b548cb74ce17d04a26d', 'local-readme-upstream-revision-declaration', [['third_party/sfntly/README.chromium', /^Version:|^URL:/]]),
    declaredTerms: 'Apache 2.0 declaration; project presence alone does not establish linkage', materials: [material('third_party/sfntly/COPYING.txt', ['third_party/licenses/kernel-source/third_party/sfntly/COPYING.txt.txt'])]},
  {id: 'openssl', name: 'OpenSSL', contexts: ['openssl'], prefixes: ['node/openssl/openssl/'],
    version: version('1.0.2h', 'source-header-declaration', [['node/openssl/openssl/include/openssl/opensslv.h', /^# define OPENSSL_VERSION_TEXT|^#define OPENSSL_VERSION_TEXT|1\.0\.2h/]]),
    declaredTerms: 'Complete dual OpenSSL/SSLeay text plus subsidiary declarations; no approval inferred', materials: [material('node/openssl/openssl/LICENSE', ['third_party/licenses/d52/openssl/LICENSE.txt'])]},
  {id: 'curl', name: 'libcurl', contexts: ['libcurl'], prefixes: ['third_party/libcurl/'],
    version: version('7.61.1', 'source-header-declaration', [['third_party/libcurl/include/curl/curlver.h', /^#define LIBCURL_VERSION /]]),
    declaredTerms: 'curl source declaration and recovered COPYING', materials: [material('third_party/libcurl/include/curl/curl.h', ['third_party/licenses/libcurl-7.61.1-COPYING.txt'], 'Original accompanying COPYING is absent. Retained recovered text is not a byte copy of this API header.')]},
  {id: 'cares', name: 'c-ares nested in libcurl; Main also declares sources', contexts: ['libcurl', '<direct>'], prefixes: ['node/cares/'],
    version: version('1.10.1-DEV', 'source-header-declaration', [['node/cares/include/ares_version.h', /^#define ARES_VERSION_STR/]]),
    declaredTerms: 'c-ares source file terms and Node aggregate section; header review not complete', materials: [material('node/LICENSE-nodejs', ['third_party/licenses/d52/node/LICENSE-nodejs.txt']), material('node/cares/src/ares_init.c', [])]},
  {id: 'v8', name: 'V8 shared source family for two static projects', contexts: ['v8_5_7', 'v8_5_7_1'], prefixes: ['v8_5_7/', 'gen/v8_5_7/'],
    version: version('5.7.246.0', 'source-header-declaration-shared-by-both-projects', [['v8_5_7/include/v8-version.h', /^#define V8_(MAJOR_VERSION|MINOR_VERSION|BUILD_NUMBER|PATCH_LEVEL|IS_CANDIDATE_VERSION) /]], 'The project name v8_5_7_1 is not a second source version.'),
    declaredTerms: 'V8 aggregate + LICENSE.v8 + fdlibm/Strongtalk/Valgrind/VTune subsidiary materials; applicable subset not approved',
    materials: ['LICENSE', 'LICENSE.v8', 'LICENSE.fdlibm', 'LICENSE.strongtalk', 'LICENSE.valgrind'].map(n => material('v8_5_7/' + n, ['third_party/licenses/d52/v8_5_7/' + n]))},
  {id: 'fdlibm', name: 'fdlibm-derived IEEE754 implementation in V8', contexts: ['v8_5_7'], prefixes: ['v8_5_7/src/base/ieee754.cc'],
    version: version(null, 'unknown-adapted-source-version', [['v8_5_7/src/base/ieee754.cc', /adapted from fdlibm|modified significantly/]]),
    declaredTerms: 'Sun permission notice retained in source header and LICENSE.fdlibm', materials: [material('v8_5_7/LICENSE.fdlibm', ['third_party/licenses/d52/v8_5_7/LICENSE.fdlibm'])]},
  {id: 'vtune', name: 'Intel VTune source declarations in second V8 project', contexts: ['v8_5_7_1'], prefixes: ['v8_5_7/src/third_party/vtune/'],
    version: {value: null, status: 'unknown', basis: [], upstreamSourceArchiveIdentityProven: false},
    declaredTerms: 'Local files declare dual BSD/GPLv2; applicable choice and completeness require human review', materials: [material('v8_5_7/src/third_party/vtune/LICENSE', ['third_party/licenses/kernel-source/v8_5_7/src/third_party/vtune/LICENSE.txt'])]},
  {id: 'skia', name: 'Skia source snapshot', contexts: ['skia', '<direct>'], prefixes: ['third_party/skia/', 'skia/'],
    version: version(null, 'explicit-local-unknown', [['third_party/skia/README.chromium', /^Version:|^URL:/]], 'Local README says Version: unknown. The Source2 hash identifies local bytes, not an upstream Skia release.'),
    declaredTerms: 'BSD root text with nested/file-specific terms unresolved', materials: [material('third_party/skia/LICENSE', ['third_party/licenses/d52/skia/LICENSE.txt'])]},
  {id: 'miniblink-main', name: 'Miniblink local Main integration', contexts: ['<direct>'], prefixes: ['content/', 'wke/', 'node/nodeblink.cpp'],
    version: {value: null, status: 'unknown-upstream-version-local-source2-identity', basis: [], upstreamSourceArchiveIdentityProven: false},
    declaredTerms: 'Root Apache 2.0 and separate WKE LGPL declaration; file-level applicability remains open', materials: [material('LICENSE', ['third_party/licenses/d52/miniblink/LICENSE.txt']), material('wke/wke.h', ['third_party/licenses/d52/miniblink/wke-license-declaration.h'], 'Retained WKE declaration is an extract; no complete file-byte equality asserted.')]},
  {id: 'blink', name: 'Blink/WebKit partial local source tree', contexts: ['<direct>'], prefixes: ['third_party/WebKit/', 'gen/blink/'],
    version: {value: null, status: 'unknown-exact-upstream-version', basis: [], upstreamSourceArchiveIdentityProven: false},
    declaredTerms: 'Separate Chromium/WebKit/Apple/LGPL and file-level declarations; no single root text covers all files',
    materials: [material('third_party/WebKit/LICENSE', ['third_party/licenses/d52/webkit/LICENSE']),
      material('third_party/WebKit/LICENSE_FOR_ABOUT_CREDITS', ['third_party/licenses/d52/webkit/LICENSE_FOR_ABOUT_CREDITS']),
      ...['LICENSE-APPLE', 'LICENSE-LGPL-2', 'LICENSE-LGPL-2.1'].map(n => material('third_party/WebKit/Source/core/' + n, ['third_party/licenses/d52/webkit/core/' + n]))]},
  {id: 'blink-dtoa', name: 'V8 double-conversion material in WebKit wtf/dtoa', contexts: ['<direct>'], prefixes: ['third_party/WebKit/Source/wtf/dtoa/'],
    version: {value: null, status: 'unknown', basis: [], upstreamSourceArchiveIdentityProven: false},
    declaredTerms: 'Local V8 BSD notice, byte-equal to retained LICENSE.v8; exact upstream component version unknown', materials: [material('third_party/WebKit/Source/wtf/dtoa/LICENSE', ['third_party/licenses/d52/v8_5_7/LICENSE.v8'])]},
  {id: 'chromium-support', name: 'Chromium-derived support directories in Main', contexts: ['<direct>'], prefixes: ['base/', 'cc/', 'net/', 'gin/', 'ui/'],
    version: {value: null, status: 'unknown-exact-upstream-version', basis: [], upstreamSourceArchiveIdentityProven: false},
    declaredTerms: 'File-level Chromium/BSD and other declarations need review; global attribution not inferred from directory names', materials: []},
  {id: 'node', name: 'Node source subset integrated in Main', contexts: ['<direct>'], prefixes: ['node/src/'],
    version: version('6.9.2', 'source-header-declaration-subset', [['node/src/node_version.h', /^#define NODE_(MAJOR_VERSION|MINOR_VERSION|PATCH_VERSION) /]]),
    declaredTerms: 'Node MIT plus aggregate third-party exceptions; this is not an unmodified Node distribution', materials: [material('node/LICENSE-nodejs', ['third_party/licenses/d52/node/LICENSE-nodejs.txt'])]},
  {id: 'http-parser', name: 'HTTP Parser compiled directly in Main', contexts: ['<direct>'], prefixes: ['node/src/http_parser.c'],
    version: version('2.7.0', 'source-header-declaration', [['node/src/http_parser.h', /^#define HTTP_PARSER_VERSION_(MAJOR|MINOR|PATCH) /]]),
    declaredTerms: 'MIT/NGINX-derived terms retained in source and dedicated text', materials: [material('node/LICENSE-http_parser', ['third_party/licenses/kernel-source/node/LICENSE-http_parser.txt'])]},
  {id: 'libuv', name: 'libuv source subset compiled directly in Main', contexts: ['<direct>'], prefixes: ['node/uv/'],
    version: version('1.9.1', 'source-header-declaration-subset', [['node/uv/include/uv-version.h', /^#define UV_VERSION_(MAJOR|MINOR|PATCH) /]]),
    declaredTerms: 'Node/libuv MIT aggregate with externally maintained exceptions', materials: [material('node/LICENSE-libuv', ['third_party/licenses/kernel-source/node/LICENSE-libuv.txt'])]},
  {id: 'icu-adaptation', name: 'Local ICU adaptation in Main', contexts: ['<direct>'], prefixes: ['third_party/icu/'],
    version: version(null, 'unknown-implementation-version-header-declares-54.1', [['third_party/icu/source/common/_unicode/uvernum.h', /^#define U_ICU_VERSION /]], 'Main declares only local source/uchar.cpp. Retained _unicode header 54.1 does not establish a complete or unmodified ICU 54.1 implementation.'),
    declaredTerms: 'Node aggregate contains ICU and subsidiary texts; applicability to this adapted source remains unresolved', materials: [material('node/LICENSE-nodejs', ['third_party/licenses/d52/node/LICENSE-nodejs.txt']), material('third_party/icu/source/uchar.cpp', [])]}
];

const components = componentSpecs.map(spec => ({...spec,
  evidenceMeaning: 'MAP membership + literal project basename candidate + current Source2 bytes; not a complete per-object compiler provenance or license decision.',
  matchedMembers: Object.fromEntries(['x64', 'x86'].map(arch => {
    const matches = [];
    for (const context of contexts.filter(c => spec.contexts.includes(c.library))) {
      for (const row of context.members.filter(m => m.architectures.includes(arch))) {
        const candidates = row.candidates.filter(c => spec.prefixes.some(prefix => sources[c.sourceId].path.slice(sourceRoot.length).startsWith(prefix)));
        if (candidates.length) matches.push({library: context.library, member: row.member,
          candidateSourceIds: candidates.map(c => c.sourceId), allProjectCandidateCount: row.candidates.length});
      }
    }
    return [arch, {candidateMemberCount: matches.length,
      uniqueProjectCandidateMemberCount: matches.filter(m => m.allProjectCandidateCount === 1).length,
      examples: matches.slice(0, 5)}];
  })), applicableTermsReviewed: false, redistributionTermsApproved: false}));

const noticeMaterials = historicalMaterials.materials.map(row => {
  assert(row.source.startsWith(sourceRoot));
  const current = sourcePin(row.source.slice(sourceRoot.length));
  return {originalHistoricalMaterial: row, currentSource: current,
    currentSourceEqualHistoricalMaterial: current.currentBytesEqualSource2 ? current.bytes === row.bytes && current.sha256 === row.sha256 : null,
    retainedNotices: row.retainedAt.map(name => {
      const notice = read(name).pin;
      return {...notice, bytesEqualCurrentSource: current.currentBytesEqualSource2 ? notice.bytes === current.bytes && notice.sha256 === current.sha256 : null};
    }), linkedSelectionEstablishedByThisMaterialIndex: false};
});
const outsideContexts = Object.fromEntries(['x64', 'x86'].map(arch => [arch,
  records[arch].groups.filter(group => !projectSpecs.some(([library]) => library === group.library))
    .map(({library, symbolRows, memberCount}) => ({library, symbolRows, memberCount}))]));
const gaps = [
  {id: 'source-version-conflicts', engineering: true, detail: 'Resolve zlib header 1.2.11 / README 1.2.5 / LICENSE prelude 1.2.4 before claiming a single upstream version. Preserve all three raw pins.'},
  {id: 'unknown-source-origins', engineering: true, detail: 'OTS, Skia, Blink/Chromium/Main, adapted ICU and subsidiary versions remain unknown where no local exact provenance exists. Obtain upstream revision/patch evidence; do not infer from notice filenames or engine family names.'},
  {id: 'recovered-notice-matching', engineering: true, detail: 'OTS, IJG and curl recovered notices are retained, but absent original accompanying source LICENSE/README/COPYING files prevent direct current-source byte equality. Current declarations and recovered texts are separately pinned.'},
  {id: 'member-source-ambiguity', engineering: true, detail: 'Basename candidate matches do not evaluate MSBuild conditions, preprocessor branches, PCH/header code, generated-resource contents, linker folding or renamed objects. Preserve ambiguous and unmatched members; inspect raw compile/link inputs where needed.'},
  {id: 'nested-and-file-level-completeness', engineering: true, detail: 'This is a bounded component crosswalk, not exhaustive transitive inventory. Header/inline/vendor resource material, adapted files, V8/Skia/WebKit/OpenSSL subsidiary notices and CRT/Windows SDK provider inputs still need complete per-binary review.'},
  {id: 'static-provider-origin', engineering: true, detail: 'Current Source2 live bytes and exact MAPs are bound, but the eleven libraries were reused. This collection does not add historical whole-provider archive byte provenance or perform new recompilation.'},
  {id: 'runtime-distribution-terms', engineering: true, detail: 'This archive covers the native kernel member/source crosswalk only. Installed SDK, Starter, Workbench, JavaScript/inspector assets, WebView2/CRT and future-version notice delivery require their own exact payload evidence and complete terms mapping; historic six-package 0.5.0 notice equality is not reclassified as 0.6.0 package approval.'},
  {id: 'human-terms-and-accountability', engineering: false, detail: 'Maintainer must identify the responsible reviewer and review applicable obligations, combined redistribution terms and release approval. No reviewer, license choice for subsidiary alternatives, legal opinion or approval is fabricated.'}
];
const crosswalk = {schema: 'soui.current-native-license-crosswalk/v1', sdkVersion: '0.6.0', issue: 'https://github.com/chenziyang110/soui-web-sdk/issues/39',
  sourceIdentity, sourceInventory: input(inventoryFile).pin, basis: 'Local engineering/source declarations, not legal analysis.',
  firstPartyLicense: {decision: 'MIT', pending: false, file: input('LICENSE.md').pin,
    scope: 'First-party SDK code only; does not replace third-party terms or close Issue #39.'},
  currentBinaries: Object.fromEntries(['x64', 'x86'].map(arch => [arch, {record: input(linkedRoot + arch + '.json').pin, delivered: records[arch].delivered}])),
  staticLibraryContextCount: 11, directMainContextCount: 1, collectionRevision: 2, priorCollection, components, noticeMaterials,
  historicalMaterialIndex: {pin: input('third_party/licenses/kernel-source/index.json.txt').pin,
    historicalSourceManifestSha256: historicalMaterials.sourceManifestSha256,
    currentRecheckMeaning: 'Original historical index remains unchanged. Current referenced source/notice bytes are checked separately against Source2; a matching text is material reuse, not new applicability approval.'},
  outsideContexts, gaps, responsibleReviewer: null, humanApprovalRecorded: false, ...flags};
const members = {schema: 'soui.current-member-source-candidates/v1', sdkVersion: '0.6.0', sourceIdentity,
  mappingMethod: 'Same exact MAP library context; literal ClCompile Include; case-insensitive default source basename + .obj. No ObjectFileName overrides present. A candidate is not asserted to be unique upstream provenance or an evaluated build configuration.',
  sourceRecords: sources, contexts, componentCountsAreNotAdditive: true,
  limitations: 'Nested selections overlap parent components. Unmatched, ambiguous, no-Source2-entry and condition-bearing candidates remain explicit. Absence of a candidate is not proof a component is absent from the DLL.', ...flags};
const stats = contexts.map(context => ({library: context.library, project: context.project.path,
  declaredCompileItems: context.projectCompileDeclarationCount, architectures: context.architectures}));
const summary = {schema: 'soui.current-native-license-crosswalk-summary/v1', sdkVersion: '0.6.0', sourceIdentity,
  staticLibraryContextCount: 11, directMainContextCount: 1, componentCount: components.length,
  matchedSourceRecordCount: sources.length,
  currentMatchedSourcesEqualSource2: sources.every(row => row.currentBytesEqualSource2),
  currentNoticeSourceCount: noticeMaterials.length,
  currentNoticeSourcesEqualHistoricalTextCount: noticeMaterials.filter(row => row.currentSourceEqualHistoricalMaterial).length,
  contexts: stats, componentSummary: components.map(component => ({id: component.id, version: component.version.value,
    versionStatus: component.version.status, candidateMembers: Object.fromEntries(['x64', 'x86'].map(arch => [arch, component.matchedMembers[arch].candidateMemberCount]))})),
  gapIds: gaps.map(gap => gap.id), firstPartyMitDecisionPending: false, collectionRevision: 2,
  priorCollectionStatus: priorCollection.status,
  compilerExecuted: false, binaryLoaded: false, testsExecuted: false, vmControlled: false,
  sourceOrPublishedArtifactsModified: false, legalApproval: false, ...flags};
// Detect drift in critical metadata/project files before creating any JSON output.
for (const [name, before] of critical) assert.deepEqual(read(name, true).pin, before, 'Critical input drift: ' + name);
const outputPins = [write('Crosswalk.json', crosswalk), write('MemberSourceCandidates.json', members), write('SUMMARY.json', summary)];
const actual = {schema: 'soui.current-license-crosswalk-collection/v1', createdUtc: new Date().toISOString(),
  collector: ownPin('collect.cjs'), readme: ownPin('README.md'), node: {version: process.version, executableSha256: digest(fs.readFileSync(process.execPath))},
  inputs: [...critical.values()].sort((a, b) => compare(a.path, b.path)), outputs: outputPins,
  actualOperation: 'Read/hash/parse existing local files; create-new JSON evidence in this directory only.',
  sourceCurrentRechecks: 'Every project, version/license material and matched source record with a Source2 entry was read and compared with Source2 bytes/hash. Unknown entries are explicit; no source tree was rewritten.',
  archiveMeaning: 'New engineering crosswalk. Does not alter the old 0.5.0 license-review record or the frozen current MAP archive.',
  collectionRevision: 2, priorCollection,
  compilerExecuted: false, dllLoaded: false, productExecuted: false, testsExecuted: false, vmControlled: false,
  externalRequestsMadeByCollector: false, environmentValuesPersisted: false, legalApproval: false, ...flags};
outputPins.push(write('Collection.actual.json', actual));
const files = [ownPin('collect.cjs'), ownPin('README.md')].map(row => ({...row, path: path.posix.basename(row.path)})).concat(outputPins, priorFiles).sort((a, b) => compare(a.path, b.path));
assert.deepEqual(fs.readdirSync(directory).sort(), ['initial-incomplete-collection', ...files.filter(file => !file.path.includes('/')).map(file => file.path)].sort());
write('Files.index.json', {schema: 'soui.current-license-crosswalk-files/v1', files,
  totalBytes: files.reduce((sum, file) => sum + file.bytes, 0), excludesSelf: true,
  scope: 'Closed set of source collector, README and mechanical collection outputs; not a license grant or completed qualification.'});
console.log(JSON.stringify({staticContexts: 11, directContexts: 1, components: components.length,
  matchedSourceRecords: sources.length, allMatchedSourcesEqualSource2: summary.currentMatchedSourcesEqualSource2,
  files: files.length + 1, releaseReady: false, binaryLicenseReviewComplete: false}));
