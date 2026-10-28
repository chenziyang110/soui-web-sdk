#!/usr/bin/env node
'use strict';

// Application directory staging, not an installer, dependency bundler or release.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { generatePreflight } = require('./desktop-preflight.cjs');
const maximumFiles = 10000;
const maximumFileBytes = 256 * 1024 * 1024;
const maximumTotalBytes = 1024 * 1024 * 1024;
const noticeOptions = ['sdk-license', 'runtime-notices', 'webview2-license', 'webview2-notices'];
const optionNames = ['binary', 'assets', 'output', 'name', 'platform', 'arch', 'version', 'identifier', ...noticeOptions];

function parseArgs(args) {
  const options = {};
  for (let i = 0; i < args.length; i += 2) {
    const name = args[i].startsWith('--') ? args[i].slice(2) : '';
    if (!optionNames.includes(name) || options[name] !== undefined || !args[i + 1] || args[i + 1].startsWith('--'))
      throw new Error(`Invalid or repeated option: ${args[i]}`);
    options[name] = args[i + 1];
  }
  return validateOptions(options);
}

function validateOptions(options) {
  if (!options || typeof options !== 'object' || Array.isArray(options) ||
      Object.keys(options).some(name => !optionNames.includes(name))) throw new Error('Invalid staging options.');
  for (const name of optionNames) {
    if ((name === 'identifier' || noticeOptions.includes(name)) && options[name] === undefined) continue;
    if (typeof options[name] !== 'string' || !options[name] || options[name].includes('\0'))
      throw new Error(`Missing or invalid --${name}.`);
  }
  if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(options.name) || !portableName(options.name))
    throw new Error('Application name must be a portable ASCII name, starting with a letter (max 64 characters).');
  if (!['windows', 'linux', 'macos'].includes(options.platform) ||
      !['x86', 'x64', 'arm32', 'arm64', 'universal2'].includes(options.arch) ||
      (options.arch === 'universal2' && options.platform !== 'macos') ||
      (options.platform === 'windows' && options.arch === 'arm32') ||
      (options.platform === 'macos' && !['x64', 'arm64', 'universal2'].includes(options.arch)))
    throw new Error('Unsupported platform/architecture combination.');
  if (!/^\d{1,5}\.\d{1,5}\.\d{1,5}$/.test(options.version))
    throw new Error('Application --version must have three numeric components (not an SDK version claim).');
  if (options.platform === 'macos' && !options.identifier)
    throw new Error('macOS requires an explicit --identifier, such as com.example.application.');
  if (options.identifier && (options.identifier.length > 4096 ||
      !/^[A-Za-z][A-Za-z0-9-]*(?:\.[A-Za-z][A-Za-z0-9-]*)+$/.test(options.identifier)))
    throw new Error('Invalid reverse-DNS application identifier.');
  return options;
}

// This is a versioned, fixed SDK notice set, not an application dependency audit.
function noticeLayout(platform, name) {
  const directory = platform === 'macos' ? `${name}.app/Contents/Resources/legal` : 'legal';
  return [['sdk-license', 'LICENSE.md'], ['runtime-notices', 'THIRD_PARTY_NOTICES.md'],
    ...(platform === 'windows' ? [['webview2-license', 'WebView2-LICENSE.txt'], ['webview2-notices', 'WebView2-NOTICE.txt']] : [])]
    .map(([option, filename]) => ({ option, path: `${directory}/${filename}` }));
}
function noticeSnapshot(source) {
  noSymlinkAncestors(source);
  const initial = fs.lstatSync(source);
  if (!initial.isFile() || initial.isSymbolicLink())
    throw new Error('Notice must be a nonempty regular file of at most 16 MiB.');
  // A FIFO must not wait for a writer, even if it replaces a regular file
  // between lstat and open. This does not make mutable input a safe sandbox.
  const descriptor = fs.openSync(source, fs.constants.O_RDONLY | (fs.constants.O_NOFOLLOW || 0) | (fs.constants.O_NONBLOCK || 0));
  try {
    const before = fs.fstatSync(descriptor);
    if (!before.isFile() || before.size < 1 || before.size > 16 * 1024 * 1024)
      throw new Error('Notice must be a nonempty regular file of at most 16 MiB.');
    if (initial.dev !== before.dev || initial.ino !== before.ino || initial.size !== before.size ||
        initial.mtimeMs !== before.mtimeMs || initial.ctimeMs !== before.ctimeMs)
      throw new Error('Notice input changed during staging.');
    const data = Buffer.alloc(before.size + 1);
    let count = 0, read;
    while (count < data.length && (read = fs.readSync(descriptor, data, count, data.length - count, null)) > 0) count += read;
    const after = fs.fstatSync(descriptor);
    if (count !== before.size || before.size !== after.size || before.mtimeMs !== after.mtimeMs || before.ctimeMs !== after.ctimeMs)
      throw new Error('Notice input changed during staging.');
    const bytes = data.subarray(0, count);
    return { bytes, stat: before, sha256: crypto.createHash('sha256').update(bytes).digest('hex') };
  } finally { fs.closeSync(descriptor); }
}
function portableName(name) {
  return name.length > 0 && Buffer.byteLength(name, 'utf8') <= 240 &&
    !/[<>:"/\\|?*\x00-\x1f\x7f-\x9f]/.test(name) && !/[. ]$/.test(name) &&
    name !== '.' && name !== '..' && name === name.normalize('NFC') &&
    !/^(?:con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³])(?:\.|$)/i.test(name);
}
function within(parent, child) {
  const relative = path.relative(parent, child);
  return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
}
function noSymlinkAncestors(input) {
  const absolute = path.resolve(input);
  const parsed = path.parse(absolute);
  let current = parsed.root;
  for (const segment of absolute.slice(parsed.root.length).split(path.sep).filter(Boolean)) {
    current = path.join(current, segment);
    const stat = fs.lstatSync(current);
    if (stat.isSymbolicLink()) throw new Error('Symlink input or parent directory is not permitted.');
  }
  return absolute;
}
function genericMachCpu(cpu, subtype) {
  return (cpu === 0x1000007 && [3, 0x80000003].includes(subtype)) ||
    (cpu === 0x100000c && subtype === 0);
}
function inspectMachSlice(sliceRead) {
  const head = sliceRead(0, 32);
  if (head.readUInt32LE(0) !== 0xfeedfacf) throw new Error('Expected a little-endian 64-bit Mach-O slice.');
  const arch = { 0x1000007: 'x64', 0x100000c: 'arm64' }[head.readUInt32LE(4)];
  if (!arch || head.readUInt32LE(12) !== 2) throw new Error('Unsupported Mach-O executable.');
  if (!genericMachCpu(head.readUInt32LE(4), head.readUInt32LE(8)))
    throw new Error('Mach-O requires a generic x86_64 or arm64 CPU subtype.');
  const count = head.readUInt32LE(16), size = head.readUInt32LE(20);
  if (!count || count > 4096 || size > 1024 * 1024 || size < count * 8)
    throw new Error('Invalid Mach-O load command bounds.');
  const commands = sliceRead(32, size);
  let position = 0, minimum = null;
  for (let index = 0; index < count; ++index) {
    if (position + 8 > size) throw new Error('Truncated Mach-O load command.');
    const command = commands.readUInt32LE(position), length = commands.readUInt32LE(position + 4);
    if (length < 8 || length % 8 || length > size - position) throw new Error('Invalid Mach-O load command size.');
    if ([0x25, 0x2f, 0x30].includes(command)) throw new Error('Mach-O targets a non-macOS Apple platform.');
    if (command === 0x32 || command === 0x24) {
      if (minimum !== null) throw new Error('Ambiguous Mach-O platform declarations.');
      if (length < (command === 0x32 ? 24 : 16)) throw new Error('Truncated Mach-O platform declaration.');
      if (command === 0x32 && commands.readUInt32LE(position + 8) !== 1)
        throw new Error('Mach-O build platform is not macOS.');
      const version = commands.readUInt32LE(position + (command === 0x32 ? 12 : 8));
      if (version === 0) throw new Error('Mach-O minimum OS version is missing.');
      minimum = Math.max(version, 12 * 65536);
    }
    position += length;
  }
  if (position !== size || minimum === null) throw new Error('Mach-O requires an unambiguous macOS platform declaration.');
  return { arch, cpu: head.readUInt32LE(4), subtype: head.readUInt32LE(8), minimum };
}
function macIdentity(arch, minimum) {
  return { platform: 'macos', arch, format: 'Mach-O', minimumOSVersion:
    `${minimum >>> 16}.${(minimum >>> 8) & 255}.${minimum & 255}` };
}
function inspectBinary(file) {
  const descriptor = fs.openSync(file, 'r');
  try {
    const fileBytes = fs.fstatSync(descriptor, { bigint: true }).size;
    if (fileBytes > BigInt(maximumFileBytes)) throw new Error('Executable exceeds inspection size limit.');
    const fileSize = Number(fileBytes);
    const read = (offset, size) => {
      if (!Number.isSafeInteger(offset) || offset < 0 || offset + size > fileSize)
        throw new Error('Truncated executable header.');
      const bytes = Buffer.alloc(size);
      if (fs.readSync(descriptor, bytes, 0, size, offset) !== size) throw new Error('Truncated executable header.');
      return bytes;
    };
    const head = read(0, 64);
    if (head.subarray(0, 4).equals(Buffer.from([0x7f, 0x45, 0x4c, 0x46]))) {
      if (![0, 3].includes(head[7])) throw new Error('ELF OSABI is not Linux/System V.');
      if (head[5] !== 1 || head[6] !== 1 || ![2, 3].includes(head.readUInt16LE(16)))
        throw new Error('Only little-endian ELF executable/PIE input is supported.');
      const arch = { 3: 'x86', 62: 'x64', 40: 'arm32', 183: 'arm64' }[head.readUInt16LE(18)];
      if (!arch || head[4] !== (['x64', 'arm64'].includes(arch) ? 2 : 1)) throw new Error('Unsupported ELF machine/class.');
      const wide = head[4] === 2;
      const entry = wide ? head.readBigUInt64LE(24) : BigInt(head.readUInt32LE(24));
      if (entry === 0n) throw new Error('ELF has no executable entry point.');
      const offset = wide ? head.readBigUInt64LE(32) : BigInt(head.readUInt32LE(28));
      const size = head.readUInt16LE(wide ? 54 : 42);
      const count = head.readUInt16LE(wide ? 56 : 44);
      if (!count || count > 4096 || size < (wide ? 56 : 32) || size * count > 1024 * 1024 || offset > BigInt(maximumFileBytes))
        throw new Error('Unsupported ELF program header table.');
      const programs = read(Number(offset), size * count);
      let interpreter = false, executableEntry = false;
      for (let i = 0; i < count; ++i) {
        const at = i * size, type = programs.readUInt32LE(at);
        if (type === 3) {
          const start = wide ? programs.readBigUInt64LE(at + 8) : BigInt(programs.readUInt32LE(at + 4));
          const length = wide ? programs.readBigUInt64LE(at + 32) : BigInt(programs.readUInt32LE(at + 16));
          if (interpreter || start > BigInt(maximumFileBytes) || length < 2n || length > 4096n)
            throw new Error('Invalid ELF interpreter segment.');
          const interpreterPath = read(Number(start), Number(length));
          if (interpreterPath[0] !== 47 || interpreterPath.at(-1) !== 0 || interpreterPath.subarray(0, -1).includes(0))
            throw new Error('Invalid ELF interpreter path.');
          interpreter = true;
        }
        if (type !== 1) continue;
        const flags = programs.readUInt32LE(at + (wide ? 4 : 24));
        const address = wide ? programs.readBigUInt64LE(at + 16) : BigInt(programs.readUInt32LE(at + 8));
        const memorySize = wide ? programs.readBigUInt64LE(at + 40) : BigInt(programs.readUInt32LE(at + 20));
        if ((flags & 1) && entry >= address && entry - address < memorySize) executableEntry = true;
      }
      if (!executableEntry || (head.readUInt16LE(16) === 3 && !interpreter))
        throw new Error('ELF must have an executable entry segment; ET_DYN requires PT_INTERP (static PIE is not supported).');
      return { platform: 'linux', arch, format: 'ELF' };
    }
    if (head[0] === 0x4d && head[1] === 0x5a) {
      const offset = head.readUInt32LE(60);
      if (offset < 64 || offset > 16 * 1024 * 1024) throw new Error('Invalid PE header offset.');
      const pe = read(offset, 94);
      if (pe.readUInt32LE(0) !== 0x4550 || !(pe.readUInt16LE(22) & 2) || (pe.readUInt16LE(22) & 0x2000))
        throw new Error('Input is not a PE executable image.');
      const arch = { 0x14c: 'x86', 0x8664: 'x64', 0xaa64: 'arm64' }[pe.readUInt16LE(4)];
      if (!arch || pe.readUInt16LE(20) < 70 || pe.readUInt16LE(24) !== (arch === 'x86' ? 0x10b : 0x20b))
        throw new Error('Unsupported PE machine/optional header.');
      if (pe.readUInt16LE(20) > 4096) throw new Error('PE optional header exceeds bounds.');
      read(offset + 24, pe.readUInt16LE(20));
      if (![2, 3].includes(pe.readUInt16LE(92))) throw new Error('PE subsystem must be Windows GUI or console, not native/driver/EFI.');
      return { platform: 'windows', arch, format: 'PE' };
    }
    if (head.readUInt32LE(0) === 0xfeedfacf) {
      const slice = inspectMachSlice(read);
      return macIdentity(slice.arch, slice.minimum);
    }
    const magic = head.readUInt32BE(0);
    if ([0xcafebabe, 0xcafebabf, 0xbebafeca, 0xbfbafeca].includes(magic)) {
      const little = magic === 0xbebafeca || magic === 0xbfbafeca;
      if (little) throw new Error('Swapped-endian fat containers are not supported by the macOS loader.');
      const wide = magic === 0xcafebabf;
      const u32 = (bytes, at) => bytes.readUInt32BE(at);
      const u64 = (bytes, at) => bytes.readBigUInt64BE(at);
      if (u32(head, 4) !== 2) throw new Error('Universal2 requires exactly two architecture slices.');
      const stride = wide ? 32 : 20, end = 8 + stride * 2;
      const table = read(8, stride * 2), slices = [], architectures = new Set();
      let minimum = 0;
      for (let at = 0; at < table.length; at += stride) {
        const cpu = u32(table, at), subtype = u32(table, at + 4);
        const offset = wide ? u64(table, at + 8) : BigInt(u32(table, at + 8));
        const size = wide ? u64(table, at + 16) : BigInt(u32(table, at + 12));
        const alignment = u32(table, at + (wide ? 24 : 16));
        if (alignment > 31 || offset < BigInt(end) || size < 32n ||
            offset + size > BigInt(fileSize) || offset % (1n << BigInt(alignment)) !== 0n ||
            (wide && u32(table, at + 28) !== 0)) throw new Error('Invalid universal2 slice bounds or alignment.');
        if (slices.some(other => offset < other.end && other.start < offset + size))
          throw new Error('Overlapping universal2 slices.');
        if (!genericMachCpu(cpu, subtype) || architectures.has(cpu))
          throw new Error('Universal2 requires generic x86_64 and arm64 slices.');
        if (offset % BigInt(cpu === 0x100000c ? 16384 : 4096) !== 0n)
          throw new Error('Mach-O slice does not satisfy loader page alignment.');
        const slice = inspectMachSlice((relative, count) => {
          if (BigInt(relative) + BigInt(count) > size) throw new Error('Mach-O read exceeds its slice.');
          return read(Number(offset) + relative, count);
        });
        if (slice.cpu !== cpu || slice.subtype !== subtype) throw new Error('Mach-O slice identity differs from fat table.');
        minimum = Math.max(minimum, slice.minimum);
        architectures.add(cpu); slices.push({ start: offset, end: offset + size });
      }
      return macIdentity('universal2', minimum);
    }
    throw new Error('Unknown or unsupported executable format.');
  } finally { fs.closeSync(descriptor); }
}
function scanAssets(root) {
  const files = [];
  let total = 0;
  let count = 0;
  function walk(directory, relative, depth) {
    if (depth > 64) throw new Error('Asset directory depth exceeds 64.');
    const seen = new Set();
    for (const name of fs.readdirSync(directory).sort()) {
      if (++count > maximumFiles) throw new Error('Asset entry limit exceeded (10000 files/directories).');
      if (!portableName(name)) throw new Error('Asset tree contains a non-portable filename.');
      const folded = name.toLowerCase();
      if (seen.has(folded)) throw new Error('Asset filenames collide on a case-insensitive filesystem.');
      seen.add(folded);
      const source = path.join(directory, name);
      const target = relative ? `${relative}/${name}` : name;
      if (Buffer.byteLength(target) > 4096) throw new Error('Asset relative path is too long.');
      const stat = fs.lstatSync(source);
      if (stat.isSymbolicLink() || (!stat.isDirectory() && !stat.isFile()))
        throw new Error('Asset symlinks and special files are not permitted.');
      if (stat.isDirectory()) { walk(source, target, depth + 1); continue; }
      total += stat.size;
      if (stat.size > maximumFileBytes || total > maximumTotalBytes || files.length >= maximumFiles)
        throw new Error('Asset staging limit exceeded (256 MiB/file, 1 GiB total, 10000 files).');
      files.push({ source, relative: target, size: stat.size });
    }
  }
  walk(root, '', 0);
  if (!files.some(file => file.relative === 'index.html')) throw new Error('Asset tree requires root index.html.');
  return files;
}
function hashFile(file) {
  const hash = crypto.createHash('sha256');
  const descriptor = fs.openSync(file, 'r');
  try {
    const bytes = Buffer.alloc(64 * 1024);
    for (;;) {
      const count = fs.readSync(descriptor, bytes, 0, bytes.length, null);
      if (!count) break;
      hash.update(bytes.subarray(0, count));
    }
  } finally { fs.closeSync(descriptor); }
  return hash.digest('hex');
}
function stage(options) {
  validateOptions(options);
  const binary = noSymlinkAncestors(options.binary);
  const assets = noSymlinkAncestors(options.assets);
  const output = path.resolve(options.output);
  const parent = noSymlinkAncestors(path.dirname(output));
  if (!fs.statSync(parent).isDirectory() || !portableName(path.basename(output))) throw new Error('Invalid output directory.');
  if (fs.existsSync(output)) throw new Error('Output already exists; refusing to overwrite.');
  const stat = fs.statSync(binary);
  if (!stat.isFile() || stat.size > maximumFileBytes) throw new Error('Binary must be a regular file of at most 256 MiB.');
  if (!fs.statSync(assets).isDirectory()) throw new Error('Assets must be a directory.');
  if (within(assets, output) || within(output, assets) || within(output, binary)) throw new Error('Input and output trees must not overlap.');
  const identity = inspectBinary(binary);
  if (identity.platform !== options.platform || identity.arch !== options.arch) throw new Error('Executable platform/architecture does not match requested metadata.');
  if (options.platform === 'linux' && ['web', 'legal'].includes(options.name.toLowerCase()))
    throw new Error('Application name conflicts with the resource directory.');
  const assetsList = scanAssets(assets);
  const mac = options.platform === 'macos';
  const binaryRelative = mac ? `${options.name}.app/Contents/MacOS/${options.name}` : `${options.name}${options.platform === 'windows' ? '.exe' : ''}`;
  const assetsRelative = mac ? `${options.name}.app/Contents/Resources/web` : 'web';
  const layout = noticeLayout(options.platform, options.name);
  if (noticeOptions.some(option => options[option] !== undefined && !layout.some(item => item.option === option)))
    throw new Error('Notice sources do not match the requested platform.');
  const notices = layout.map(item => {
    if (typeof options[item.option] !== 'string' || !options[item.option]) throw new Error(`Missing --${item.option}.`);
    const source = noSymlinkAncestors(options[item.option]);
    if (within(assets, source) || within(output, source) || within(source, output) || source === binary)
      throw new Error('Notice sources and application input/output trees must not overlap.');
    return { ...item, source, ...noticeSnapshot(source) };
  });
  const binaryStat = fs.statSync(binary);
  for (let index = 0; index < notices.length; ++index) {
    const current = notices[index];
    const sameFile = stat => stat.dev === current.stat.dev && stat.ino === current.stat.ino;
    if (sameFile(binaryStat) || notices.slice(0, index).some(other => sameFile(other.stat)))
      throw new Error('Notice source collision.');
  }
  const entries = [];
  fs.mkdirSync(output); // Atomic no-overwrite boundary. Never recursively remove on failure.
  function generated(relative, content, mode = 0o644) {
    const target = path.join(output, relative);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, content, { flag: 'wx', mode });
    entries.push({ path: relative, size: fs.statSync(target).size, sha256: hashFile(target) });
  }
  function copy(source, relative, expectedSize, mode) {
    // Trusted immutable input is required: these checks do not eliminate TOCTOU.
    const current = fs.lstatSync(source);
    if (!current.isFile() || current.isSymbolicLink() || current.size !== expectedSize) throw new Error('Input changed during staging.');
    const target = path.join(output, relative);
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.copyFileSync(source, target, fs.constants.COPYFILE_EXCL);
    fs.chmodSync(target, mode);
    if (fs.statSync(target).size !== expectedSize) throw new Error('Input changed during copy.');
    entries.push({ path: relative, size: expectedSize, sha256: hashFile(target) });
  }
  copy(binary, binaryRelative, stat.size, 0o755);
  for (const file of assetsList) copy(file.source, `${assetsRelative}/${file.relative}`, file.size, 0o644);
  for (const notice of notices) generated(notice.path, notice.bytes);
  const doctor = generatePreflight({ platform: options.platform, arch: identity.arch,
    minimumOSVersion: identity.minimumOSVersion || null });
  generated(doctor.filename, doctor.content, doctor.mode);
  if (options.platform === 'linux') generated('launch.sh', `#!/bin/sh\nset -eu\nHERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nif [ "\${1-}" = "--doctor" ]; then\n  shift\n  if [ "$#" -ne 0 ]; then printf '%s\\n' 'SOUI_PREFLIGHT_ARGUMENTS: --doctor accepts no arguments.' >&2; exit 2; fi\n  exec /bin/sh "$HERE/doctor.sh"\nfi\nif /bin/sh "$HERE/doctor.sh"; then :; else\n  code=$?\n  printf '%s\\n' 'SOUI_STARTUP_BLOCKED: resolve the reported runtime issue before launching.' >&2\n  exit "$code"\nfi\nexec "$HERE/${options.name}" "$@"\n`, 0o755);
  if (mac) generated(`${options.name}.app/Contents/Info.plist`, `<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0"><dict>\n<key>CFBundleExecutable</key><string>${options.name}</string>\n<key>CFBundleIdentifier</key><string>${options.identifier}</string>\n<key>CFBundleName</key><string>${options.name}</string>\n<key>CFBundlePackageType</key><string>APPL</string>\n<key>CFBundleShortVersionString</key><string>${options.version}</string>\n<key>CFBundleVersion</key><string>${options.version}</string>\n<key>LSMinimumSystemVersion</key><string>${identity.minimumOSVersion}</string>\n<key>NSHighResolutionCapable</key><true/>\n</dict></plist>\n`);
  const dependencies = {
    windows: ['Compatible Windows installation', 'Microsoft Edge WebView2 Runtime matching the application architecture', 'Microsoft Visual C++ v14 runtime matching the application architecture for /MD builds', 'Any additional DLLs required by the supplied binary'],
    linux: ['Compatible Linux distribution, libc/C++ runtime and display session', 'GTK >= 3.24 and WebKitGTK 4.1 >= 2.40, including system helper processes and resources', 'Other shared libraries required by the supplied binary; inspect on the target machine'],
    macos: [`macOS >= ${identity.minimumOSVersion || '12.0'}`, 'System AppKit/Foundation/WebKit frameworks', 'Any additional libraries required by the supplied binary'],
  }[options.platform];
  generated('DEPENDENCIES.txt', `Development application directory, not a self-contained installer.\nRequired target runtime (not bundled or installed):\n${dependencies.map(item => `- ${item}`).join('\n')}\n\nRuntime diagnostics: ${doctor.filename} (no Node required). Linux: launch.sh --doctor; macOS: /bin/sh doctor.sh; Windows: PowerShell -NoProfile -File doctor.ps1 under existing execution policy.\nDiagnostic output uses fixed status codes and no user paths; redirect it explicitly if a report is needed. Exit 0 is not runtime acceptance. No persistent application log is captured.\nNo signing, notarization, runtime installation or GUI acceptance performed.\nBinary headers establish format/architecture only, not ABI/dependency compatibility.\nConservative format support excludes static PIE without PT_INTERP; macOS accepts thin or bounded x86_64/arm64 universal2 Mach-O.\nInput assets must be trusted and immutable during staging; this is not a filesystem sandbox.\nThe optional HTTP resource service supports files up to 8 MiB even though staging permits larger assets.\nInclude all applicable application/SDK/third-party license notices before distribution.\n`);
  // Recheck the original sources and copied bytes before publishing the final manifest.
  for (const notice of notices) {
    const current = noticeSnapshot(notice.source);
    if (current.sha256 !== notice.sha256 || current.stat.dev !== notice.stat.dev || current.stat.ino !== notice.stat.ino ||
        current.stat.mtimeMs !== notice.stat.mtimeMs || current.stat.ctimeMs !== notice.stat.ctimeMs ||
        hashFile(path.join(output, notice.path)) !== notice.sha256)
      throw new Error('Notice input changed during staging.');
  }
  const manifest = { schema: 'soui.desktop.application-stage/2', name: options.name,
    applicationVersion: options.version, identifier: options.identifier || null,
    platform: identity.platform, architecture: identity.arch, executableFormat: identity.format,
    executable: binaryRelative, assets: assetsRelative, minimumOSVersion: identity.minimumOSVersion || null, developmentOnly: true,
    signed: false, dependenciesBundled: false, nativeRuntimeVerified: false,
    notices: { schema: 'soui.desktop.notices/1', files: notices.map(item => item.path) },
    diagnostics: { entry: doctor.filename, requiresNode: false, executesApplication: false, successMeans: 'no-known-blocker-not-runtime-acceptance' },
    requiredRuntime: dependencies, files: entries.sort((a, b) => a.path.localeCompare(b.path)) };
  // Written last: its absence identifies an incomplete staging directory.
  fs.writeFileSync(path.join(output, 'application-manifest.json'), JSON.stringify(manifest, null, 2) + '\n', { flag: 'wx' });
  return manifest;
}
if (require.main === module) {
  try {
    const manifest = stage(parseArgs(process.argv.slice(2)));
    console.log(`Staged ${manifest.name} (${manifest.platform}/${manifest.architecture}); target runtime verification remains required.`);
  } catch (error) {
    console.error(`Application staging failed: ${error.message}`);
    process.exitCode = 1;
  }
}
module.exports = { parseArgs, inspectBinary, scanAssets, noticeLayout, stage };
