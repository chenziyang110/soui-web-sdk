'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),zlib=require('node:zlib'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../../..'),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const index=read(path.join(__dirname,'INDEX.json')),summary=read(path.join(__dirname,'SUMMARY.json'));
const originals=new Map();
for(const row of index.rows){
 assert.match(row.archive.path,/^raw\/[0-9]{3}-[^/\\]+$/);
 const bytes=fs.readFileSync(path.join(__dirname,row.archive.path));
 assert.equal(bytes.length,row.archive.bytes);assert.equal(hash(bytes),row.archive.sha256);
 assert(['gzip','identity'].includes(row.encoding));
 const raw=row.encoding==='gzip'?zlib.gunzipSync(bytes):bytes;
 assert.equal(raw.length,row.original.bytes);assert.equal(hash(raw),row.original.sha256);
 assert(!originals.has(row.original.path));originals.set(row.original.path,raw);
}
function find(suffix){const matches=[...originals].filter(([p])=>p.replaceAll('\\','/').endsWith(suffix));assert.equal(matches.length,1,suffix);return matches[0][1];}
const vendor=JSON.parse(find('/VENDOR-AUDIT.json')),notices=JSON.parse(find('/NOTICE-AUDIT.json'));
const expected=require(path.join(root,'tools/internal/vendor-test-names.json')).names;
for(const row of vendor.rows){
 const data=fs.readFileSync(path.join(root,row.stdout.path));assert.equal(hash(data),row.stdout.sha256);assert.equal(data.length,row.stdout.bytes);
 const passed=[...data.toString('utf8').matchAll(/^\s*\d+\/208 Test\s+#\d+: (kernel\.native\.\S+)\s+\.{2,}\s+Passed\s+/gm)].map(m=>m[1]).sort();
 assert.deepEqual(passed,expected);assert.deepEqual(row.passedVendorCases,expected);
 const originalResult=JSON.parse(originals.get(row.result.path));
 assert(originalResult.natural&&originalResult.stable&&originalResult.result.passed);
 assert.equal(originalResult.result.exitCode,0);assert.equal(originalResult.terminal.activeProcesses,0);
}
for(const row of summary.portableBuilds){
 const result=JSON.parse(find('/build-'+row.architecture+'/RESULT.json'));
 assert(result.natural&&result.sourceUnchanged&&result.vendorDefaultOff&&result.sdkTestsOn);
 assert.equal(result.result.exitCode,0);assert.equal(result.result.supervisorExitCode,0);
 assert.equal(result.terminal.activeProcesses,0);assert.equal(result.terminal.terminatedOwnedJob,false);
 const registry=require(path.join(root,'tools/internal/vendor-tests.cjs')).registry(find('/registry-'+row.architecture+'.json').toString('utf8'),'OFF');
 assert.equal(registry.registeredNativeTests,row.registeredNativeTests);
 assert.equal(registry.registeredVendorTests,0);
}
const facts=read(path.join(root,'verification/production-validation/0.5-source30-release-2026-10-16/scoped-results/RELEASE-FACTS.json'));
assert.equal(notices.applications.length,6);
for(const app of notices.applications){
 const released=facts.applicationArchives.find(r=>r.name===app.archive.name);assert(released);
 assert.equal(app.archive.sha256,released.sha256);assert.equal(app.archive.bytes,released.bytes);
 assert.equal(app.noticeFilesChecked,notices.sourceNotices.length);assert.equal(app.openSslSsLeayTextPresent,true);
}
assert.equal(summary.releaseReady,false);assert.equal(notices.licenseReviewComplete,false);assert.equal(notices.redistributionTermsApproved,false);
console.log(JSON.stringify({archiveFiles:index.rows.length,portableArchitectures:summary.portableBuilds.map(x=>x.architecture),vendorCasesPerArchitecture:expected.length,noticeFilesPerArchive:notices.sourceNotices.length,scope:'archive integrity and recorded conclusions; does not rerun builds or inspect external ZIPs',releaseReady:false}));
