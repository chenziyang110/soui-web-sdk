'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const id = require('./delivery-identity.cjs');
const webProfiles = require('./web-build-profile.cjs');
const webTargets = require('./web-targets.cjs');

function publishStagedDirectory(stage, destination) {
  const delay = new Int32Array(new SharedArrayBuffer(4));
  for (let attempt = 0; ; ++attempt) {
    try {
      fs.renameSync(stage, destination);
      return;
    } catch (error) {
      // Use the SDK installer's bounded Windows publication policy. Keep
      // persistent errors and destination conflicts on the rollback path.
      if (process.platform !== 'win32' || !['EPERM', 'EACCES'].includes(error.code) ||
          attempt >= 5 || !fs.existsSync(stage) || fs.existsSync(destination)) throw error;
      Atomics.wait(delay, 0, 0, 30 * (attempt + 1));
    }
  }
}

function replaceGroups(groups, application, distribution, expectedProfile) {
  application = id.safePath(application);
  distribution = id.safePath(distribution);
  const destination = id.safePath(application, 'resources');
  const checkedGroups = groups.map(({source, prefix = '', expectedTarget}) => {
    source = id.safePath(source);
    const names = id.walk(source);
    if (!names.includes('index.html')) throw new Error('Web build missing index.html');
    if (names.some(name => name.toLowerCase() === 'legal' || name.toLowerCase().startsWith('legal/'))) throw new Error('Web build owns reserved legal path');
    const checkedProfile = webProfiles.verifyDirectory(source, expectedProfile);
    if (expectedTarget) webTargets.verifyTargetDirectory(source, expectedTarget, {requireManifest: true});
    return {source, prefix, names, expectedTarget, checkedProfile};
  });
  if (new Set(checkedGroups.map(group => group.checkedProfile.profile)).size !== 1)
    throw new Error('Auto Web build profiles must match');
  const requiredNotices = ['LICENSE.md', 'THIRD_PARTY_NOTICES.md'];
  for (const name of requiredNotices) {
    if (!fs.statSync(id.safePath(distribution, name)).isFile()) throw new Error('SDK legal materials missing: ' + name);
  }
  const noticeBytes = fs.readFileSync(id.safePath(distribution, 'THIRD_PARTY_NOTICES.md'));
  const redistributionDoc = 'docs/licenses/v0.6.0-redistribution.md';
  const referencedDocs = noticeBytes.toString('utf8').includes(redistributionDoc) ? [redistributionDoc] : [];
  for (const name of referencedDocs) {
    const file = id.safePath(distribution, name);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()) throw new Error('SDK legal materials missing: ' + name);
  }
  const licenses = id.walk(distribution, 'third_party/licenses');
  if (!licenses.length) throw new Error('SDK legal materials missing');
  const legal = [...requiredNotices, ...licenses, ...referencedDocs];
  const suffix = crypto.randomUUID();
  const stage = id.safePath(application, `.resources-new-${suffix}`);
  const backup = id.safePath(application, `.resources-old-${suffix}`);
  fs.mkdirSync(stage, {recursive: true});
  let moved = false;
  try {
    for (const {source, prefix, names, expectedTarget, checkedProfile} of checkedGroups) {
      const groupRoot = prefix ? id.safePath(stage, prefix) : stage;
      for (const name of names) {
        const target = id.safePath(groupRoot, name);
        fs.mkdirSync(path.dirname(target), {recursive: true});
        fs.copyFileSync(id.safePath(source, name), target);
      }
      webProfiles.verifyDirectory(groupRoot, checkedProfile.profile);
      if (expectedTarget) webTargets.verifyTargetDirectory(groupRoot, expectedTarget, {requireManifest: true});
      if (id.snapshot(source, names).sha256 !== id.snapshot(groupRoot, names).sha256) throw new Error('Web source changed during deployment');
    }
    for (const name of legal) {
      const target = id.safePath(stage, `legal/${name}`);
      fs.mkdirSync(path.dirname(target), {recursive: true});
      fs.copyFileSync(id.safePath(distribution, name), target);
    }
    for (const name of legal) {
      if (id.hash(fs.readFileSync(id.safePath(distribution, name))) !==
          id.hash(fs.readFileSync(id.safePath(stage, `legal/${name}`)))) throw new Error('SDK legal material changed during deployment');
    }
    if (!fs.readFileSync(id.safePath(stage, 'legal/THIRD_PARTY_NOTICES.md')).equals(noticeBytes))
      throw new Error('SDK legal material changed during deployment');
    if (fs.existsSync(destination)) { fs.renameSync(destination, backup); moved = true; }
    try { publishStagedDirectory(stage, destination); }
    catch (error) {
      if (moved) fs.renameSync(backup, destination);
      throw error;
    }
    if (moved) fs.rmSync(id.safePath(application, path.basename(backup)), {recursive: true});
  } finally {
    if (fs.existsSync(stage)) fs.rmSync(id.safePath(application, path.basename(stage)), {recursive: true});
  }
  return {files: checkedGroups.reduce((count, group) => count + group.names.length, legal.length),
    webProfile: checkedGroups[0].checkedProfile.profile};
}
function replace(source, application, distribution, expectedProfile, expectedTarget) {
  return replaceGroups([{source, expectedTarget}], application, distribution, expectedProfile);
}
function autoTargets(projectRoot) {
  const root = id.safePath(projectRoot), project = id.readJson(id.safePath(root, 'project.json'));
  const plan = webTargets.resolve(root);
  if (project.runtimeProfile !== 'auto' || !plan.configured || plan.default !== 'modern' ||
      plan.targets.length !== 2 || !['modern', 'compatibility'].every(name => plan.targets.some(target => target.name === name)))
    throw new Error('Auto applications require distinct modern and compatibility Web targets');
  return plan.targets.map(target => ({source: id.safePath(root, target.outDir),
    prefix: target.name === 'modern' ? 'web-modern' : 'web-compat',
    expectedTarget: {...target, configurationSha256: plan.configurationSha256}}));
}
function replaceAuto(projectRoot, application, distribution, expectedProfile) {
  return replaceGroups(autoTargets(projectRoot), application, distribution, expectedProfile);
}
function verifyAutoDirectory(application, expectedProfile, projectRoot) {
  const root = id.safePath(application, 'resources');
  const groups = projectRoot ? autoTargets(projectRoot) : [
    {prefix: 'web-modern', expectedTarget: 'modern'}, {prefix: 'web-compat', expectedTarget: 'compatibility'}];
  if (fs.readdirSync(root).some(name => !['web-modern', 'web-compat', 'legal'].includes(name)))
    throw new Error('Auto resources contain mixed or unexpected Web roots');
  const verified = [];
  for (const {prefix, expectedTarget} of groups) {
    const directory = id.safePath(root, prefix);
    const profile = webProfiles.verifyDirectory(directory, expectedProfile);
    const target = webTargets.verifyTargetDirectory(directory, expectedTarget, {requireManifest: true});
    verified.push({profile: profile.profile, configurationSha256: target.configurationSha256});
  }
  if (new Set(verified.map(value => value.profile)).size !== 1 ||
      new Set(verified.map(value => value.configurationSha256)).size !== 1)
    throw new Error('Auto Web outputs do not share a build profile and target configuration');
  return {webProfile: verified[0].profile, webTargets: ['modern', 'compatibility']};
}
module.exports = {replace, replaceAuto, autoTargets, verifyAutoDirectory};
if (require.main === module) {
  try {
    if (process.argv[2] === 'auto') {
      if (process.argv.length !== 6) throw new Error('Usage: deploy-resources.cjs auto PROJECT_ROOT APP_OUTPUT SDK_ROOT');
      console.log(JSON.stringify(replaceAuto(process.argv[3], process.argv[4], process.argv[5])));
      process.exit(0);
    }
    if (![5, 6].includes(process.argv.length)) throw new Error('Usage: deploy-resources.cjs WEB_ROOT APP_OUTPUT SDK_ROOT [PROJECT_ROOT]');
    let expectedTarget;
    if (process.argv[5]) {
      const root = id.safePath(process.argv[5]), plan = webTargets.resolve(root);
      if (plan.configured) {
        const source = path.resolve(process.argv[2]);
        const target = plan.targets.find(target => path.resolve(root, target.outDir).toLowerCase() === source.toLowerCase());
        if (!target) throw new Error('Web output does not belong to a configured target');
        expectedTarget = {...target, configurationSha256: plan.configurationSha256};
      }
    }
    console.log(JSON.stringify(replace(process.argv[2], process.argv[3], process.argv[4], undefined, expectedTarget)));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
