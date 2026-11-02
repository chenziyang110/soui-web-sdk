'use strict';
// Prepared data-only recipe. Root must review this file and pass the explicit gate.
// No ZIP access, child process, SDK/build/test/VM execution, or source writes.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const assert = require('assert/strict');

const BASE = 'F:/work/SOUI_WEB/out/open-issues-closeout-20261029/license-delivery-reuse-current';
const PLAN = `${BASE}/Plan.v2.prepared.json`;
const PLAN_SHA = 'f36644214df785062d6f936e5b5bb0a3200437dca423a5e9bfef7bcec8bc5bd4';
const GATE = 'ROOT_REVIEWED_PUBLISHED_060_MATERIAL_REUSE_V1';
assert.deepEqual(process.argv.slice(2), ['--root-reviewed', GATE, '--plan-sha256', PLAN_SHA],
  'Execution requires Root review of the exact recipe and plan, then the explicit gate');

const digest = b => crypto.createHash('sha256').update(b).digest('hex');
const normalized = p => path.resolve(p).replace(/\\/g, '/');
const key = p => normalized(p).toLowerCase();
const samePin = (a, b) => a.bytes === b.bytes && a.sha256 === b.sha256;
const relativeSafe = p => typeof p === 'string' && p.length > 0 && p.length < 300 &&
  !p.includes('\\') && !p.includes(':') && !p.includes('\0') && !p.startsWith('/') &&
  p.split('/').every(x => x && x !== '.' && x !== '..');
const identity = s => ['dev', 'ino', 'size', 'mtimeNs', 'ctimeNs', 'nlink'].map(k => String(s[k]));
const records = new Map();
const failures = [];
let plan;
const output = `${BASE}/attempt-${crypto.randomUUID()}`;
fs.mkdirSync(output); // Fresh output only; preserve failures and all prior attempts.
const write = (name, value) => fs.writeFileSync(`${output}/${name}`, `${JSON.stringify(value, null, 2)}\n`, { flag: 'wx' });
const check = (condition, code, detail) => { if (!condition) failures.push({ code, detail }); return !!condition; };

function plainAncestors(file) {
  let current = normalized(file);
  while (true) {
    const st = fs.lstatSync(current);
    assert(!st.isSymbolicLink(), `Reparse/symlink input forbidden: ${current}`);
    const parent = normalized(path.dirname(current));
    if (parent === current) break;
    current = parent;
  }
  assert.equal(key(fs.realpathSync(file)), key(file), `Input path redirected: ${file}`);
}

function read(file, role, expected, maxBytes = 2 * 1024 * 1024) {
  const k = key(file);
  if (records.has(k)) {
    const prior = records.get(k);
    if (expected) check(samePin(prior, expected), 'PIN_MISMATCH', { path: file, role, expected, actual: { bytes: prior.bytes, sha256: prior.sha256 } });
    if (!prior.roles.includes(role)) prior.roles.push(role);
    return prior;
  }
  try {
    assert(!/\.(?:exe|dll|lib|obj|pdb|zip|7z|iso|qcow2|vhdx?)$/i.test(file), 'Binary/archive input forbidden');
    plainAncestors(file);
    const before = fs.statSync(file, { bigint: true });
    assert(before.isFile() && before.nlink === 1n, 'Input must be a single-link ordinary file');
    assert(before.size >= 0n && before.size <= BigInt(maxBytes), 'Input exceeds material/metadata bound');
    const bytes = fs.readFileSync(file);
    const after = fs.statSync(file, { bigint: true });
    assert.deepEqual(identity(after), identity(before), 'Input changed during read');
    assert(!(bytes[0] === 0x4d && bytes[1] === 0x5a), 'PE bytes forbidden');
    const result = { path: normalized(file), roles: [role], bytes: bytes.length, sha256: digest(bytes), identity: identity(after), data: bytes };
    records.set(k, result);
    if (expected) check(samePin(result, expected), 'PIN_MISMATCH', { path: file, role, expected, actual: { bytes: result.bytes, sha256: result.sha256 } });
    return result;
  } catch (error) {
    failures.push({ code: 'INPUT_READ_FAILED', path: file, role, error: String(error.message) });
    return null;
  }
}

function json(pin, role) {
  const r = read(pin.path, role, pin);
  assert(r, `Required metadata unavailable: ${pin.path}`);
  return JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(r.data));
}

function rowsMap(rows, role) {
  assert(Array.isArray(rows), `${role}: files array required`);
  const m = new Map();
  for (const row of rows) {
    assert(relativeSafe(row.path) && Number.isSafeInteger(row.bytes) && row.bytes >= 0 && /^[0-9a-f]{64}$/.test(row.sha256), `${role}: invalid file row`);
    assert(!m.has(row.path.toLowerCase()), `${role}: duplicate/case-colliding path`);
    m.set(row.path.toLowerCase(), { path: row.path, bytes: row.bytes, sha256: row.sha256 });
  }
  return m;
}

function equalRows(actualRows, expectedRows, role) {
  const actual = rowsMap(actualRows, role), expected = rowsMap(expectedRows, `${role}-expected`);
  check(actual.size === expected.size, 'ROW_COUNT_MISMATCH', { role, actual: actual.size, expected: expected.size });
  for (const [k, row] of expected) {
    const a = actual.get(k);
    check(a && a.path === row.path && samePin(a, row), 'ROW_MISMATCH', { role, expected: row, actual: a || null });
  }
  return actual;
}

function material(root, row, role) {
  assert(relativeSafe(row.path), 'Unsafe material path');
  return read(`${root}/${row.path}`, role, row);
}

function compareNotice(source, installed, architecture) {
  if (!source || !installed) return;
  const rule = plan.allowedInstalledNoticeTransform;
  const decode = r => new TextDecoder('utf-8', { fatal: true }).decode(r.data);
  const a = decode(source), b = decode(installed);
  const count = a.split(rule.sourcePrefix).length - 1;
  const rawEqual = source.data.equals(installed.data);
  const typedEqual = a.replace(/\r\n/g, '\n').split(rule.sourcePrefix).join(rule.installedPrefix) === b.replace(/\r\n/g, '\n');
  check(count === 5 && count === rule.expectedLinkReplacements && !rawEqual && typedEqual,
    'INSTALLED_NOTICE_TRANSFORM_MISMATCH', { architecture, linkReplacements: count, rawEqual, typedEqual });
  return { architecture, rawEqual, exactFiveLinkRewriteAndLfTextEquivalent: typedEqual, linkReplacements: count,
    sourceEol: /\r\n/.test(a) ? 'contains-CRLF' : 'LF', installedEol: /\r\n/.test(b) ? 'contains-CRLF' : 'LF', eolOriginClaimed: false };
}

const result = { schema: 'soui.published-060-legal-material-reuse/actual-v1', startedUtc: new Date().toISOString(),
  authority: 'Root executes exact reviewed plan and recipe with explicit gate', planSha256: PLAN_SHA,
  scope: 'Published sealed0.6 material identity only; archive closures and whole-prefix/source-SDK closure reused from pinned prior evidence',
  archiveBytesRead: false, archiveRehashedOrExtracted: false, sdkOrPrefixWholeClosureRerun: false,
  sourceOrSdkModified: false, buildOrTestOrVmExecuted: false, legalApproval: false, redistributionApproval: false,
  currentBranchNoticesApproved: false, releaseReady: false, productionQualificationComplete: false,
  fullInstalledMaterialClosedSetClaimed: false, failures };

try {
  assert.equal(key(__filename), key(`${BASE}/Run-ReuseComposition.cjs`));
  const recipe = read(__filename, 'executed-recipe');
  assert(recipe, 'Executed recipe identity unavailable');
  result.executedRecipe = { path: recipe.path, bytes: recipe.bytes, sha256: recipe.sha256 };
  plan = json({ path: PLAN, bytes: 49957, sha256: PLAN_SHA }, 'reviewed-plan');
  assert.equal(plan.schema, 'soui.published-060-legal-delivery-reuse-plan/v2');
  assert.deepEqual(plan.roots, { sealed: 'C:/sr06', sourceSdk: 'C:/s06sdk' });
  assert.equal(plan.executionApproved, false); // A preparation flag is not substituted for Root's explicit execution gate.
  const facts = json(plan.releaseFacts, 'prior-release-facts');
  const sourceManifest = json(plan.sourceSdkManifest, 'prior-source-sdk-manifest');
  for (const p of [...plan.existingCanonicalReferences, ...plan.controls]) read(p.path, 'prior-reference-or-control', p);
  const sourceRows = rowsMap(sourceManifest.files, 'source-sdk');
  check(sourceRows.size === 1496, 'SOURCE_MANIFEST_COUNT', sourceRows.size);
  const archiveFacts = facts.scopedCompleted.sixApplicationArchives;
  check(archiveFacts.allSixArchiveClosuresVerified === true && archiveFacts.closingHashesVerified === true,
    'PRIOR_ARCHIVE_CLOSURE_NOT_COMPLETE', null);
  check(samePin(facts.scopedCompleted.sourceSdkAndGeneratedProjects.manifest, plan.sourceSdkManifest), 'SOURCE_PROOF_PIN_MISMATCH', null);
  const declared = rowsMap(plan.declaredLegalSet, 'common-legal-set');
  check(declared.size === 206, 'COMMON_LEGAL_COUNT', declared.size);
  for (const [k, row] of declared) check(sourceRows.get(k)?.path === row.path && samePin(sourceRows.get(k) || {}, row), 'SOURCE_LEGAL_ROW_MISMATCH', row);
  result.applications = [];
  assert.equal(plan.applications.length, 6);
  for (const app of plan.applications) {
    const old = archiveFacts.archives.find(x => x.target === app.target && x.architecture === app.architecture);
    check(old && old.fullExtractionVerified === true && samePin(old.archive, app.archive) && samePin(old.sidecar, app.sidecar) && old.payloadSha256 === app.payloadSha256,
      'PRIOR_APPLICATION_PROOF_MISMATCH', { target: app.target, architecture: app.architecture });
    const side = json(app.sidecar, 'prior-application-sidecar');
    check(side.target === app.target && side.architecture === app.architecture && side.payload.sha256 === app.payloadSha256,
      'SIDECAR_IDENTITY_MISMATCH', { target: app.target, architecture: app.architecture });
    const rows = side.payload.files.filter(x => x.path.startsWith('resources/legal/')).map(x => ({ ...x, path: x.path.slice('resources/legal/'.length) }));
    equalRows(rows, plan.declaredLegalSet, `${app.target}-${app.architecture}-declared-legal`);
    result.applications.push({ target: app.target, architecture: app.architecture, archive: app.archive, sidecar: app.sidecar,
      payloadSha256: app.payloadSha256, declaredLegalRows: rows.length, closureReuseOnly: true, priorFullExtractionVerified: old?.fullExtractionVerified === true });
  }

  const sourceData = new Map();
  const extra = plan.installedSupplement.filter(x => !declared.has(x.source.toLowerCase()));
  for (const [name, root] of Object.entries(plan.roots)) {
    const byPath = new Map();
    for (const row of declared.values()) byPath.set(row.path, material(root, row, `${name}-legal`));
    for (const x of extra) {
      const row = sourceRows.get(x.source.toLowerCase());
      assert(row?.path === x.source, 'Supplement source missing from source-SDK manifest');
      byPath.set(row.path, material(root, row, `${name}-supplement`));
    }
    sourceData.set(name, byPath);
  }

  result.installed = [];
  assert.equal(plan.installedPrefixes.length, 2);
  for (const prefix of plan.installedPrefixes) {
    assert(prefix.architecture === 'x64' || prefix.architecture === 'x86');
    assert.equal(prefix.root, `C:/sr06/out/release-install-${prefix.architecture}-release`);
    const prior = facts.scopedCompleted.installedPublicConsumers.prefixes.find(x => x.architecture === prefix.architecture);
    check(prior?.root === prefix.root && samePin(prior?.closure || {}, prefix.closure), 'PREFIX_PROOF_PIN_MISMATCH', prefix.architecture);
    const closure = json(prefix.closure, 'prior-installed-prefix-closure');
    const rows = rowsMap(closure.files, `${prefix.architecture}-prefix`);
    check(rows.size === 473 && closure.noticeRelativeLinksMissing?.length === 0, 'PREFIX_PRIOR_CLOSURE_INVALID', prefix.architecture);
    const wanted = [...declared.values()].map(x => ({ installed: `share/SouiSdk/${x.path}`, source: x.path, relation: x.path === 'THIRD_PARTY_NOTICES.md' ? 'notice-link-transform' : 'raw-equality' }));
    for (const row of declared.values()) if (row.path.startsWith('third_party/licenses/')) wanted.push({ installed: `share/SouiSdk/licenses/${row.path.slice('third_party/licenses/'.length)}`, source: row.path, relation: 'raw-equality' });
    wanted.push(...plan.installedSupplement);
    check(wanted.length === 412, 'INSTALLED_MATERIAL_COUNT', { architecture: prefix.architecture, actual: wanted.length });
    const relations = [];
    for (const x of wanted) {
      const row = rows.get(x.installed.toLowerCase());
      if (!check(row?.path === x.installed, 'INSTALLED_DECLARED_ROW_MISSING', { architecture: prefix.architecture, path: x.installed })) continue;
      const actual = material(prefix.root, row, `${prefix.architecture}-installed-material`);
      const source = sourceData.get('sealed').get(x.source);
      if (!actual || !source) continue;
      const rawEqual = actual.data.equals(source.data);
      let relationPassed;
      if (x.relation === 'notice-link-transform') relationPassed = !!compareNotice(source, actual, prefix.architecture)?.exactFiveLinkRewriteAndLfTextEquivalent;
      else if (x.relation === 'remove-exact-one-final-LF-from-source') relationPassed = !rawEqual && source.data.at(-1) === 10 && source.data.subarray(0, -1).equals(actual.data);
      else relationPassed = rawEqual;
      check(relationPassed, 'INSTALLED_SOURCE_RELATION_MISMATCH', { architecture: prefix.architecture, ...x, rawEqual });
      relations.push({ path: x.installed, source: x.source, relation: x.relation, rawEqual, relationPassed });
    }
    result.installed.push({ architecture: prefix.architecture, priorPrefixMembers: 473, selectedMaterialMembers: wanted.length,
      rawEqualToSource: relations.filter(x => x.rawEqual).length, typedNonRawRelations: relations.filter(x => !x.rawEqual), relations });
  }
  const materialCount = [...records.values()].filter(x => x.roles.some(r => /-legal$|-supplement$|-installed-material$/.test(r))).length;
  check(materialCount === 1238, 'PHYSICAL_MATERIAL_COUNT', materialCount);
  result.physicalMaterialFilesRead = materialCount;
} catch (error) {
  failures.push({ code: 'COMPOSITION_ABORTED', error: String(error.stack || error) });
} finally {
  for (const r of records.values()) {
    try {
      plainAncestors(r.path);
      check(JSON.stringify(identity(fs.statSync(r.path, { bigint: true }))) === JSON.stringify(r.identity), 'INPUT_CHANGED_AFTER_READ', r.path);
    } catch (error) { failures.push({ code: 'INPUT_FINAL_IDENTITY_FAILED', path: r.path, error: String(error.message) }); }
  }
  result.completedUtc = new Date().toISOString();
  result.engineeringMaterialCompositionPassed = failures.length === 0;
  result.status = failures.length ? 'FAIL' : 'PASS_PUBLISHED_060_MATERIAL_COMPOSITION_ONLY';
  result.unresolved = ['Human terms and redistribution approval', 'Broader runtime inventory/version ambiguities and production qualification',
    'Current branch notices are outside this published0.6 composition', 'Archive contents are reused from prior immutable proofs, not re-extracted or rehashed here'];
  write('Inputs.actual.json', [...records.values()].map(({ data, ...r }) => r));
  write('Summary.actual.json', result);
  const files = ['Inputs.actual.json', 'Summary.actual.json'].map(name => { const b = fs.readFileSync(`${output}/${name}`); return { path: name, bytes: b.length, sha256: digest(b) }; });
  write('Files.index.json', { schema: 'soui.data-only-index/v1', files });
  console.log(JSON.stringify({ output, status: result.status, failures: failures.length, physicalMaterialFilesRead: result.physicalMaterialFilesRead || 0 }));
  process.exitCode = failures.length ? 1 : 0;
}
