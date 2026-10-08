const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const root = __dirname;
const external = 'C:\\SOUI-SDK-production-validation\\preview15-v02-win7-current-20260927';
const commandLogs = 'C:\\SOUI-SDK-production-validation\\preview15-v02-interaction-20260927\\command-logs';
const manifestPath = path.join(root, 'raw-manifest.json');

const selections = [
  { base: external, dir: 'layout-interaction-v8-source', files: [
    'CMakeLists.txt', 'V02InputProbe.cpp', 'Validate-Source.cjs',
    'run-layout-interaction.ps1', 'export-serial.ps1',
  ] },
  { base: external, dir: '.', files: [
    'Build-V8Probe.cjs', 'Prepare-LayoutInteractionV8.cjs',
    'Freeze-LayoutInteractionV8.cjs', 'Verify-LayoutInteractionV8.cjs',
    'Launch-LayoutInteractionV8.ps1', 'Run-LayoutInteractionV8.ps1',
    'Review-LayoutInteractionV8.cjs', 'Capture-LayoutTimelineV8.cjs',
    'Monitor-DisplayExplore.cjs', 'V8-PREPARATION.md',
  ] },
  { base: external, dir: 'probe-v8-build-attempt1', files: [
    'result.json', 'gates.ndjson', 'static-source-validation.json',
    'x86-configure.stdout.log', 'x86-configure.stderr.log',
    'x86-build.stdout.log', 'x86-build.stderr.log',
    'build/x86/Release/V02InputProbe.exe',
    'build/x86/V02InputProbe.dir/Release/V02InputProbe.tlog/CL.command.1.tlog',
    'build/x86/V02InputProbe.dir/Release/V02InputProbe.tlog/link.command.1.tlog',
  ] },
  { base: external, dir: 'layout-interaction-v8-attempt1', files: [
    'freeze.json', 'input.json',
    'volume/soui-input/run-display-explore.cmd',
    'volume/soui-input/run-display-explore.ps1',
    'volume/soui-input/V02InputProbe.exe',
    'volume/soui-input/SouiStarter-payload.tsv',
    'volume/soui-input/SouiWorkbench-payload.tsv',
    'volume/soui-input/transfer-files.tsv',
  ] },
  { base: external, dir: 'guest-layout-interaction-v8-attempt1', files: [
    'gate.json', 'gate-before-launch.json', 'frozen-check-before.log',
    'launch-script.ps1', 'monitor-script.cjs',
  ] },
  ...['probe-v8-build-attempt1-driver', 'v8-prepare-attempt1',
    'v8-freeze-attempt1', 'v8-verify-attempt1',
    'guest-layout-interaction-v8-attempt1-driver'].map((dir) => ({
    base: commandLogs, dir, files: ['result.json', 'stdout.log', 'stderr.log'],
    prefix: 'command-logs',
  })),
];

function sha256(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
}

function selected() {
  return selections.flatMap(({ base, dir, files, prefix }) => files.map((relative) => ({
    original: path.resolve(base, dir, relative),
    archived: path.join('raw', prefix || 'v8', dir, relative),
  })));
}

function verify() {
  const manifest = readJson(manifestPath);
  const expected = selected();
  if (manifest.schema !== 'soui.win7-v02-v8-prelaunch-refusal-archive/v1' ||
      manifest.files.length !== expected.length) throw new Error('Manifest schema or count mismatch');
  for (let i = 0; i < expected.length; i++) {
    const entry = manifest.files[i];
    if (entry.path !== expected[i].archived.replaceAll('\\', '/') ||
        entry.original !== expected[i].original) throw new Error('Manifest selection changed');
    const archived = path.resolve(root, entry.path);
    if (!archived.startsWith(path.join(root, 'raw') + path.sep)) throw new Error('Path escaped archive');
    if (fs.statSync(archived).size !== entry.bytes || sha256(archived) !== entry.sha256) {
      throw new Error(`Archived bytes differ: ${entry.path}`);
    }
    if (fs.existsSync(entry.original) && sha256(entry.original) !== entry.sha256) {
      throw new Error(`Original bytes differ: ${entry.original}`);
    }
  }
  if (fs.existsSync(path.join(external, 'guest-layout-interaction-v8-attempt1', 'launch.json'))) {
    throw new Error('Unexpected own QEMU launch in rejected attempt');
  }
  const build = readJson(path.join(root, 'raw/v8/probe-v8-build-attempt1/result.json'));
  const frozenFile = path.join(root, 'raw/v8/layout-interaction-v8-attempt1/freeze.json');
  const freeze = readJson(frozenFile);
  const driver = readJson(path.join(root, 'raw/command-logs/guest-layout-interaction-v8-attempt1-driver/result.json'));
  const gate = readJson(path.join(root, 'raw/v8/guest-layout-interaction-v8-attempt1/gate-before-launch.json'));
  const stderr = fs.readFileSync(path.join(root, 'raw/command-logs/guest-layout-interaction-v8-attempt1-driver/stderr.log'), 'utf8');
  const guestFiles = fs.readdirSync(path.join(root, 'raw/v8/guest-layout-interaction-v8-attempt1')).sort();
  const expectedGuestFiles = ['frozen-check-before.log', 'gate-before-launch.json', 'gate.json',
    'launch-script.ps1', 'monitor-script.cjs'].sort();
  const preliminaryStages = ['probe-v8-build-attempt1-driver', 'v8-prepare-attempt1',
    'v8-freeze-attempt1', 'v8-verify-attempt1'];
  for (const stage of preliminaryStages) {
    const result = readJson(path.join(root, 'raw/command-logs', stage, 'result.json'));
    if (result.exitCode !== 0) throw new Error(`Preliminary stage did not pass: ${stage}`);
  }
  if (build.status !== 'built-x86' || build.stages.some((s) => s.exitCode !== 0) ||
      build.executables[0].sha256 !== '2ad0e9eb92c7ef7b2ba8e72c672db58fd9fa250f5dbbb1698795c359cb0e5d96' ||
      sha256(frozenFile) !== '764452636fd31dd5cac25611882f67fe578a7fe8357e36ce9d79a9826166502b' ||
      freeze.buildId !== 'f0c38056-058a-456e-be99-17446127f7ec' ||
      freeze.sourceSha256 !== '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d' ||
      driver.exitCode !== 1 || gate.processes.length === 0 ||
      !gate.processes.some((p) => p.Name.startsWith('qemu-system-')) ||
      !stderr.includes('Desktop load appeared before QEMU launch') ||
      JSON.stringify(guestFiles) !== JSON.stringify(expectedGuestFiles)) {
    throw new Error('Expected build, binding, or protective refusal absent');
  }
  process.stdout.write(`verified ${manifest.files.length} raw files; build passed; guest prelaunch refused\n`);
}

function copy() {
  if (fs.existsSync(manifestPath)) throw new Error('Archive already exists; refusing overwrite');
  const files = selected();
  for (const item of files) {
    if (!fs.existsSync(item.original)) throw new Error(`Missing original: ${item.original}`);
    if (fs.existsSync(path.resolve(root, item.archived))) throw new Error(`Archive destination exists: ${item.archived}`);
  }
  const entries = [];
  for (const item of files) {
    const destination = path.resolve(root, item.archived);
    fs.mkdirSync(path.dirname(destination), { recursive: true });
    fs.copyFileSync(item.original, destination, fs.constants.COPYFILE_EXCL);
    entries.push({ path: item.archived.replaceAll('\\', '/'), original: item.original,
      bytes: fs.statSync(destination).size, sha256: sha256(destination) });
  }
  fs.writeFileSync(manifestPath, JSON.stringify({
    schema: 'soui.win7-v02-v8-prelaunch-refusal-archive/v1',
    buildStatus: 'built-x86', guestStatus: 'refused-before-own-vm',
    win7CurrentV02Verified: false, releaseReady: false, files: entries,
  }, null, 2) + '\n', { flag: 'wx' });
  verify();
}

if (process.argv[2] === 'copy') copy();
else if (process.argv[2] === 'verify') verify();
else throw new Error('Usage: node Archive.cjs copy|verify');
