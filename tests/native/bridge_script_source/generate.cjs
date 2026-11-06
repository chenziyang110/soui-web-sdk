'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const [mode, first, second, third] = process.argv.slice(2);

function span(bytes, begin, end, name) {
  const start = bytes.indexOf(Buffer.from(begin));
  assert.ok(start >= 0 && bytes.indexOf(Buffer.from(begin), start + begin.length) < 0, 'Ambiguous ' + name);
  const stop = bytes.indexOf(Buffer.from(end), start + begin.length);
  assert.ok(stop > start, 'Missing end for ' + name);
  const raw = bytes.subarray(start, stop);
  return {raw, metadata: {name, startByte: start, endByteExclusive: stop, sha256: sha(raw)}};
}
function statement(bytes, begin, name) {
  const start = bytes.indexOf(Buffer.from(begin));
  assert.ok(start >= 0 && bytes.indexOf(Buffer.from(begin), start + begin.length) < 0, 'Ambiguous ' + name);
  const ending = /;\r?\n/.exec(bytes.subarray(start).toString('utf8'));
  assert.ok(ending, 'Missing statement end for ' + name);
  const stop = start + ending.index + 1;
  const raw = bytes.subarray(start, stop);
  assert.ok(!raw.some(byte => byte > 127), 'Statement must be ASCII for byte offsets');
  return {raw, metadata: {name, startByte: start, endByteExclusive: stop, sha256: sha(raw)}};
}
function fresh(file, bytes) { fs.writeFileSync(file, bytes, {flag: 'wx'}); }

if (mode === '--capture') {
  assert.equal(process.argv.length, 6, 'capture baseline-host baseline-pending fixture-directory');
  const host = fs.readFileSync(first), pending = fs.readFileSync(second);
  const literal = span(host, 'std::string json_string_literal(', 'std::filesystem::path canonical_path(', 'json-literal');
  const response = span(host, 'std::string bridge_resolution_script(', 'std::uint32_t bridge_wait_timeout(', 'response');
  const create = statement(host, 'const auto create = std::string{"return "}', 'pending-expression');
  const immediate = statement(host, 'const auto resolve = "return " + bridge_resolution_script(', 'immediate-expression');
  const pendingBegin = pending.indexOf(Buffer.from('inline constexpr const char* create_bridge_pending_script ='));
  const pendingEnd = pending.indexOf(Buffer.from(')JS";'), pendingBegin) + 5;
  assert.ok(pendingBegin >= 0 && pendingEnd > pendingBegin);
  const pendingRaw = pending.subarray(pendingBegin, pendingEnd);
  const reference = Buffer.concat([
    Buffer.from('// Extracted legacy production bytes; see legacy-reference.json and generate.cjs.\nnamespace legacy_bridge_source {\nnamespace detail {\n'),
    pendingRaw, Buffer.from('\n}\n'), literal.raw, response.raw,
    Buffer.from('\nstd::string pending_source(const std::string& request_id, std::uint32_t timeout, bool renewal) {\n  const std::string_view method = renewal ? "bridge.session.renew" : "benchmark.echo";\n  '),
    create.raw, Buffer.from('\n  return create;\n}\nstd::string immediate_source(const std::string& request_id, const std::string& response) {\n  '),
    immediate.raw, Buffer.from('\n  return resolve;\n}\n}\n')
  ]);
  const manifest = {schema: 'soui.bridge-script-legacy-reference/v1',
    sources: [{path: 'sdk/src/host/soui_miniblink_host.cpp', bytes: host.length, sha256: sha(host)},
      {path: 'sdk/src/host/bridge_pending_script.h', bytes: pending.length, sha256: sha(pending)}],
    fragments: [literal.metadata, response.metadata, create.metadata, immediate.metadata,
      {name: 'pending-expression-source', startByte: pendingBegin, endByteExclusive: pendingEnd, sha256: sha(pendingRaw)}],
    reference: {path: 'legacy-source.inc', bytes: reference.length, sha256: sha(reference)},
    scope: 'Actual old production functions/expressions; only namespace and callable wrappers added. No copied Host execution or GUI.'};
  fs.mkdirSync(third, {recursive: true});
  fresh(path.join(third, 'legacy-source.inc'), reference);
  fresh(path.join(third, 'legacy-reference.json'), JSON.stringify(manifest, null, 2) + '\n');
  process.stdout.write(JSON.stringify({captured: true, reference: manifest.reference}) + '\n');
} else if (mode === '--verify') {
  assert.equal(process.argv.length, 5, 'verify fixture-directory output-directory');
  const manifest = JSON.parse(fs.readFileSync(path.join(first, 'legacy-reference.json'), 'utf8'));
  assert.equal(manifest.schema, 'soui.bridge-script-legacy-reference/v1');
  assert.equal(manifest.reference.path, 'legacy-source.inc');
  const raw = fs.readFileSync(path.join(first, manifest.reference.path));
  assert.equal(raw.length, manifest.reference.bytes); assert.equal(sha(raw), manifest.reference.sha256);
  for (const row of manifest.sources) assert.ok(!path.isAbsolute(row.path) && !row.path.includes(':'));
  fs.mkdirSync(second, {recursive: true});
  const receipt = JSON.stringify({schema: 'soui.bridge-script-source-projection/v1', reference: manifest.reference,
    baselineSources: manifest.sources, legacyReferenceVerified: true}, null, 2) + '\n';
  const output = path.join(second, 'source-binding.json');
  if (!fs.existsSync(output) || fs.readFileSync(output, 'utf8') !== receipt) fs.writeFileSync(output, receipt);
} else throw new Error('Expected --capture or --verify');
