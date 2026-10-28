'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {isUtf8} = require('node:buffer');
const identity = require('./delivery-identity.cjs');
const webTargets = require('./web-targets.cjs');

const manifestName = '.soui-web-profile.json';
const schema = 'soui.web-build-profile/v1';
const fileLimit = 16 * 1024 * 1024;
const totalLimit = 128 * 1024 * 1024;
const fileCountLimit = 4096;
const sourceLimit = 16384;
const depthLimit = 32;
const scriptExtension = /\.(?:[cm]?js|css)$/i;
const sourceExtension = /\.(?:[cm]?ts|tsx|jsx|vue|svelte)$/i;
const forbiddenExtension = /\.(?:map|[cm]?ts|tsx|jsx|vue|svelte)$/i;
const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
const need = (condition, code) => { if (!condition) throw new Error(code); };
const hash = identity.hash;
const forward = value => value.split(path.sep).join('/');
const plainObject = value => value && typeof value === 'object' && !Array.isArray(value);
const mapExposure = /sourceMappingURL|\bsourcesContent["']?\s*:|data:application\/json[^\r\n]*;base64,/i;

function resolveProfile(value) {
  if (value === undefined) return 'distribution';
  need(value === 'distribution' || value === 'source-diagnostic', 'E_WEB_PROFILE');
  return value;
}
function charge(budget, bytes) {
  need(Number.isSafeInteger(bytes) && bytes >= 0 && bytes <= fileLimit, 'E_WEB_FILE_BUDGET');
  budget.bytes += bytes;
  need(budget.bytes <= totalLimit, 'E_WEB_TOTAL_BUDGET');
}
function read(file, budget) {
  identity.safePath(file);
  const fd = fs.openSync(file, 'r');
  try {
    const before = fs.fstatSync(fd);
    need(before.isFile() && before.nlink === 1, 'E_WEB_REGULAR_FILE');
    charge(budget, before.size);
    // Allocate only after the declared size has passed the budget. One byte
    // beyond that size detects growth rather than following an unbounded read.
    const bytes = Buffer.alloc(before.size + 1);
    let offset = 0, count;
    while (offset < bytes.length && (count = fs.readSync(fd, bytes, offset, bytes.length - offset, null))) offset += count;
    const after = fs.fstatSync(fd);
    need(offset === before.size && ['dev', 'ino', 'size', 'mtimeMs', 'ctimeMs', 'nlink'].every(key => before[key] === after[key]),
      'E_WEB_FILE_CHANGED');
    return bytes.subarray(0, offset);
  } finally { fs.closeSync(fd); }
}
function text(bytes) {
  need(isUtf8(bytes), 'E_WEB_UTF8');
  return bytes.toString('utf8');
}
function parseJson(bytes) {
  const value = text(bytes);
  // Bound JSON nesting before JSON.parse allocates nested objects. Braces in
  // strings, including escaped quotes, do not count as structure.
  let depth = 0, quoted = false, escaped = false;
  for (const character of value) {
    if (quoted) {
      if (escaped) escaped = false;
      else if (character === '\\') escaped = true;
      else if (character === '"') quoted = false;
    } else if (character === '"') quoted = true;
    else if (character === '{' || character === '[') need(++depth <= depthLimit, 'E_WEB_JSON_DEPTH');
    else if (character === '}' || character === ']') --depth;
  }
  try { return JSON.parse(value); } catch { throw new Error('E_WEB_JSON'); }
}
function files(directory, excludeLegal) {
  const root = identity.safePath(directory);
  need(fs.statSync(root).isDirectory(), 'E_WEB_DIRECTORY');
  let count = 0, bytes = 0;
  // Reuse the delivery walk and its path/link rules; reject budgets during the
  // walk, before any file body is read.
  const names = identity.walk(root, '', name => {
    need(name.split('/').length <= depthLimit, 'E_WEB_PATH_DEPTH');
    const stat = fs.statSync(identity.safePath(root, name));
    // Installed payloads add SDK-owned legal materials after Web publication.
    // Only their top-level directory is outside this Web-source policy.
    if (excludeLegal && name.toLowerCase() === 'legal' && stat.isDirectory()) return true;
    if (!stat.isDirectory()) {
      need(stat.isFile() && stat.nlink === 1, 'E_WEB_REGULAR_FILE');
      need(++count <= fileCountLimit, 'E_WEB_FILE_COUNT');
      need(stat.size <= fileLimit, 'E_WEB_FILE_BUDGET');
      bytes += stat.size;
      need(bytes <= totalLimit, 'E_WEB_TOTAL_BUDGET');
    }
    return false;
  });
  return {root, names};
}
function validateMappings(map) {
  // ECMA-426 v3 segments contain one, four or five signed base64 VLQs.
  // Decode without a second tuple tree so the raw map budget also bounds work.
  let cursor = 0, source = 0, line = 0, column = 0, name = 0, generated = 0;
  const mappings = map.mappings;
  while (cursor < mappings.length) {
    if (mappings[cursor] === ';') { ++cursor; generated = 0; continue; }
    const segment = [];
    while (cursor < mappings.length && mappings[cursor] !== ',' && mappings[cursor] !== ';') {
      let value = 0, multiplier = 1, digit;
      do {
        need(cursor < mappings.length, 'E_WEB_MAPPING_VLQ');
        digit = alphabet.indexOf(mappings[cursor++]);
        need(digit >= 0, 'E_WEB_MAPPING_VLQ');
        value += (digit & 31) * multiplier;
        need(Number.isSafeInteger(value), 'E_WEB_MAPPING_VLQ');
        multiplier *= 32;
        need(multiplier <= 2 ** 55, 'E_WEB_MAPPING_VLQ');
      } while (digit & 32);
      segment.push((value % 2 ? -1 : 1) * Math.floor(value / 2));
      need(segment.length <= 5, 'E_WEB_MAPPING_SEGMENT');
    }
    need([1, 4, 5].includes(segment.length), 'E_WEB_MAPPING_SEGMENT');
    generated += segment[0];
    need(Number.isSafeInteger(generated) && generated >= 0 && segment[0] >= 0, 'E_WEB_MAPPING_COLUMN');
    if (segment.length >= 4) {
      source += segment[1]; line += segment[2]; column += segment[3];
      need(Number.isSafeInteger(source) && source >= 0 && source < map.sources.length &&
        Number.isSafeInteger(line) && line >= 0 && Number.isSafeInteger(column) && column >= 0, 'E_WEB_MAPPING_SOURCE');
    }
    if (segment.length === 5) {
      name += segment[4];
      need(Number.isSafeInteger(name) && name >= 0 && name < map.names.length, 'E_WEB_MAPPING_NAME');
    }
    if (mappings[cursor] === ',') {
      ++cursor;
      need(cursor < mappings.length && mappings[cursor] !== ',' && mappings[cursor] !== ';', 'E_WEB_MAPPING_SEGMENT');
    }
  }
}
function inlineMap(code, budget) {
  const occurrences = [...code.matchAll(/sourceMappingURL\s*=/g)];
  if (!occurrences.length) {
    need(!/data:application\/json[^\r\n]*;base64,|\b(?:sourcesContent)["']?\s*:/i.test(code), 'E_WEB_UNLINKED_MAP');
    return null;
  }
  need(occurrences.length === 1, 'E_WEB_MAP_COUNT');
  const annotations = [...code.matchAll(/(?:\/\/[#@]|\/\*[#@])\s*sourceMappingURL\s*=\s*(data:application\/json(?:;charset=utf-8)?;base64,([A-Za-z0-9+/]*={0,2}))(?:\s*\*\/)?[ \t]*(?:\r?\n)?$/g)];
  need(annotations.length === 1, 'E_WEB_INLINE_MAP');
  const encoded = annotations[0][2];
  // Check decoded length before Buffer.from; strict round-trip rejects lax
  // base64 decoding, padding aliases and trailing garbage.
  const size = encoded.length / 4 * 3 - (encoded.endsWith('==') ? 2 : encoded.endsWith('=') ? 1 : 0);
  need(encoded.length % 4 === 0 && size <= fileLimit, 'E_WEB_MAP_BUDGET');
  charge(budget, size);
  const bytes = Buffer.from(encoded, 'base64');
  need(bytes.length === size && bytes.toString('base64') === encoded, 'E_WEB_MAP_BASE64');
  const map = parseJson(bytes);
  const keys = ['version', 'file', 'sourceRoot', 'sources', 'sourcesContent', 'names', 'mappings', 'ignoreList', 'x_google_ignoreList'];
  need(plainObject(map) && map.version === 3 && Object.keys(map).every(key => keys.includes(key)) &&
    (!Object.hasOwn(map, 'sourceRoot') || map.sourceRoot === '') &&
    (!Object.hasOwn(map, 'file') || typeof map.file === 'string'), 'E_WEB_MAP_SHAPE');
  need(Array.isArray(map.sources) && map.sources.length <= sourceLimit &&
    map.sources.every(value => typeof value === 'string' && value.length > 0) &&
    Array.isArray(map.sourcesContent) && map.sourcesContent.length === map.sources.length &&
    map.sourcesContent.every(value => typeof value === 'string') &&
    Array.isArray(map.names) && map.names.every(value => typeof value === 'string') &&
    typeof map.mappings === 'string', 'E_WEB_MAP_ARRAYS');
  for (const key of ['ignoreList', 'x_google_ignoreList']) if (Object.hasOwn(map, key)) {
    need(Array.isArray(map[key]) && map[key].every(index => Number.isSafeInteger(index) && index >= 0 && index < map.sources.length),
      'E_WEB_MAP_IGNORE');
  }
  validateMappings(map);
  return {map, bytes: bytes.length, sha256: hash(bytes)};
}
function projection(map, scriptFile, roots, budget) {
  const seen = new Set();
  return map.sources.map((source, index) => {
    if (/^(?:\0|virtual:|vite:|rollup:|<[^>]+>$)/.test(source)) return {index, kind: 'virtual'};
    need(!/[\\\x00-\x1f\x7f?#]/.test(source) && !source.startsWith('/') &&
      !/^[a-z][a-z\d+.-]*:/i.test(source), 'E_WEB_SOURCE_PATH');
    const absolute = path.resolve(path.dirname(scriptFile), source);
    if (source.split('/').includes('node_modules')) {
      need(roots.some(({packageRoot}) => {
        const relative = path.relative(path.join(packageRoot, 'node_modules'), absolute);
        return relative && relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative);
      }), 'E_WEB_SOURCE_OUTSIDE_ROOTS');
      return {index, kind: 'third-party'};
    }
    for (const {scope, root, prefix = ''} of roots) {
      const relative = forward(path.relative(root, absolute));
      if (!relative || relative === '..' || relative.startsWith('../') || path.isAbsolute(relative)) continue;
      const file = identity.safePath(root, relative);
      const projectedPath = prefix ? prefix + '/' + relative : relative;
      const key = scope + '/' + projectedPath;
      need(!seen.has(key), 'E_WEB_SOURCE_DUPLICATE'); seen.add(key);
      const bytes = read(file, budget);
      // Vite's imported media modules map to a generated asset-reference
      // wrapper, not to UTF-8 text from the original binary resource.
      if (/\.(?:mp4|webm|gif|png)$/i.test(relative) &&
          /^export default "__VITE_ASSET__[A-Za-z0-9_$]+__"$/.test(map.sourcesContent[index])) {
        return {index, kind: 'virtual'};
      }
      const content = text(bytes);
      need(content === map.sourcesContent[index], 'E_WEB_SOURCE_CONTENT');
      return {index, kind: 'business', scope, path: projectedPath, bytes: bytes.length, sha256: hash(bytes)};
    }
    // plugin-legacy 6 builds polyfills at its own root before copying the
    // inline-mapped chunk into the app output. Its core-js/systemjs labels
    // therefore lack node_modules and are not relative to the final asset.
    // Accept only these two installed packages, with exact original content.
    const legacySource = /^(?:\.\.\/)+(core-js|systemjs)\/(.+)$/.exec(source);
    if (legacySource) {
      const appRoot = roots.find(root => root.scope === 'app').packageRoot;
      const file = identity.safePath(appRoot, `node_modules/${legacySource[1]}/${legacySource[2]}`);
      need(text(read(file, budget)) === map.sourcesContent[index], 'E_WEB_SOURCE_CONTENT');
      return {index, kind: 'third-party'};
    }
    throw new Error('E_WEB_SOURCE_OUTSIDE_ROOTS');
  });
}
function validateProjection(map, entries, directory) {
  need(Array.isArray(entries) && entries.length === map.sources.length, 'E_WEB_SOURCE_PROJECTION');
  for (let index = 0; index < entries.length; ++index) {
    const entry = entries[index];
    need(plainObject(entry) && entry.index === index, 'E_WEB_SOURCE_PROJECTION');
    if (entry.kind === 'business') {
      need(Object.keys(entry).sort().join(',') === 'bytes,index,kind,path,scope,sha256' &&
        ['app', 'sdk'].includes(entry.scope) && typeof entry.path === 'string' && entry.path,
      'E_WEB_SOURCE_PROJECTION');
      identity.safePath(directory, entry.path);
      const bytes = Buffer.from(map.sourcesContent[index]);
      need(entry.bytes === bytes.length && entry.sha256 === hash(bytes), 'E_WEB_SOURCE_PROJECTION');
    } else {
      need(Object.keys(entry).sort().join(',') === 'index,kind' && ['third-party', 'virtual'].includes(entry.kind),
        'E_WEB_SOURCE_PROJECTION');
    }
  }
}
function inspect(directory, profile, roots, excludeLegal = false) {
  const tree = files(directory, excludeLegal), budget = {bytes: 0}, scripts = [];
  let maps = 0;
  for (const name of tree.names) {
    need(!forbiddenExtension.test(name), 'E_WEB_STANDALONE_SOURCE');
    const bytes = read(identity.safePath(tree.root, name), budget);
    // Inline HTML scripts cannot carry an unbound map outside the emitted
    // JS/CSS inventory. This helper supports maps on separate assets only.
    if (/\.html?$/i.test(name)) need(!mapExposure.test(text(bytes)), 'E_WEB_HTML_INLINE_MAP');
    if (!scriptExtension.test(name)) {
      // Public assets may retain their original extension. A JSON/text/SVG
      // container must not bypass the map policy applied to script assets.
      if (isUtf8(bytes)) need(!mapExposure.test(bytes.toString('utf8')), 'E_WEB_NON_SCRIPT_MAP');
      continue;
    }
    const code = text(bytes);
    if (profile === 'distribution') need(!mapExposure.test(code), 'E_WEB_DISTRIBUTION_SOURCE');
    const inline = inlineMap(code, budget);
    if (inline) ++maps;
    scripts.push({path: name, bytes: bytes.length, sha256: hash(bytes), map: inline ? {
      bytes: inline.bytes, sha256: inline.sha256,
      ...(roots ? {sources: projection(inline.map, identity.safePath(tree.root, name), roots, budget)} : {}),
    } : null, ...(inline ? {parsedMap: inline.map} : {})});
  }
  return {tree, scripts, maps, bytes: budget.bytes};
}
function verifyDirectory(directory, expectedProfile, options = {}) {
  need(plainObject(options) && Object.keys(options).every(key => ['requireManifest', 'excludeLegal'].includes(key)) &&
    ['requireManifest', 'excludeLegal'].every(key => options[key] === undefined || typeof options[key] === 'boolean'), 'E_WEB_OPTIONS');
  const root = identity.safePath(directory), marker = identity.safePath(root, manifestName);
  const marked = fs.existsSync(marker);
  need(marked || !options.requireManifest, 'E_WEB_MANIFEST_REQUIRED');
  const manifest = marked ? parseJson(read(marker, {bytes: 0})) : null;
  if (marked) need(plainObject(manifest) && Object.keys(manifest).sort().join(',') === 'profile,schema,scripts' &&
    manifest.schema === schema && Array.isArray(manifest.scripts) && manifest.scripts.length <= fileCountLimit, 'E_WEB_MANIFEST');
  const profile = resolveProfile(expectedProfile === undefined ? manifest?.profile : expectedProfile);
  need(!marked || manifest.profile === profile, 'E_WEB_PROFILE_MISMATCH');
  need(marked || profile === 'distribution', 'E_WEB_MANIFEST_REQUIRED');
  const current = inspect(root, profile, undefined, options.excludeLegal === true);
  const byName = new Map(current.scripts.map(script => [script.path, script]));
  const bound = new Set();
  let businessSources = 0, typedBusinessSources = 0, thirdPartySources = 0, virtualSources = 0;
  for (const script of manifest?.scripts ?? []) {
    need(plainObject(script) && Object.keys(script).sort().join(',') === 'bytes,map,path,sha256' &&
      typeof script.path === 'string' && scriptExtension.test(script.path) && !bound.has(script.path), 'E_WEB_MANIFEST_SCRIPT');
    identity.safePath(root, script.path); bound.add(script.path);
    const actual = byName.get(script.path);
    need(actual && actual.bytes === script.bytes && actual.sha256 === script.sha256, 'E_WEB_SCRIPT_IDENTITY');
    need((script.map === null) === (actual.map === null), 'E_WEB_MAP_IDENTITY');
    if (actual.map) {
      need(plainObject(script.map) && Object.keys(script.map).sort().join(',') === 'bytes,sha256,sources' &&
        script.map.bytes === actual.map.bytes && script.map.sha256 === actual.map.sha256, 'E_WEB_MAP_IDENTITY');
      validateProjection(actual.parsedMap, script.map.sources, root);
      for (const source of script.map.sources) {
        if (source.kind === 'business') { ++businessSources; if (sourceExtension.test(source.path)) ++typedBusinessSources; }
        else if (source.kind === 'third-party') ++thirdPartySources;
        else ++virtualSources;
      }
    }
  }
  // The classic MiniBlink entry writer changes HTML only. A marked build must
  // keep an exact JS/CSS inventory, including assets without a source map.
  if (marked) for (const script of current.scripts) need(bound.has(script.path), 'E_WEB_UNBOUND_SCRIPT');
  if (profile === 'source-diagnostic') need(current.maps > 0 && typedBusinessSources > 0, 'E_WEB_DIAGNOSTIC_SOURCES');
  const target = webTargets.verifyTargetDirectory(root, undefined, {excludeLegal: options.excludeLegal === true});
  return {profile, marked, maps: current.maps, businessSources, typedBusinessSources, thirdPartySources, virtualSources,
    files: current.tree.names.length, scripts: current.scripts.length, bytes: current.bytes, ...target};
}
function buildProfile(value = process.env.SOUI_WEB_BUILD_PROFILE, options = {}) {
  const profile = resolveProfile(value);
  need(plainObject(options) && Object.keys(options).every(key => key === 'appSourceDirectories'), 'E_WEB_SOURCE_ROOTS');
  const directories = options.appSourceDirectories ?? ['src'];
  need(Array.isArray(directories) && directories.length > 0 && directories.length <= 16 &&
    directories.every(directory => typeof directory === 'string' && directory.length > 0) &&
    new Set(directories.map(directory => directory.toLowerCase())).size === directories.length, 'E_WEB_SOURCE_ROOTS');
  for (const directory of directories) identity.safePath(process.cwd(), directory);
  let resolved, selected, closed = false, failed = false;
  const plugin = {name: 'soui-web-build-profile', enforce: 'post', apply: 'build',
    config(config) {
      selected = webTargets.viteSelection(config);
      if (!selected) return;
      return {root: path.dirname(identity.safePath(selected.root, selected.entry)), publicDir: false,
        build: {outDir: identity.safePath(selected.root, selected.outDir), emptyOutDir: true,
          rollupOptions: {input: identity.safePath(selected.root, selected.entry)}}};
    },
    generateBundle(_options, bundle) {
      if (!selected) return;
      try {
        const generated = Object.keys(bundle).map(name => name.toLowerCase());
        for (const name of selected.publicAssets) {
          const key = name.toLowerCase();
          need(!generated.some(other => other === key || other.startsWith(key + '/') || key.startsWith(other + '/')),
            'E_WEB_TARGET_ASSET_COLLISION');
          const source = webTargets.read(identity.safePath(path.join(selected.root, 'web/public'), name));
          this.emitFile({type: 'asset', fileName: name, source});
        }
      } catch (error) { failed = true; throw error; }
    },
    buildEnd(error) { if (error) failed = true; },
    renderError() { failed = true; },
    configResolved(config) {
      need(!resolved && config && typeof config.root === 'string' && typeof config.build?.outDir === 'string', 'E_WEB_CONFIG');
      resolved = {root: identity.safePath(config.root), output: identity.safePath(path.resolve(config.root, config.build.outDir))};
    },
    closeBundle() {
      if (failed) return;
      need(resolved && !closed, 'E_WEB_PLUGIN_LIFECYCLE'); closed = true;
      const marker = identity.safePath(resolved.output, manifestName);
      need(!fs.existsSync(marker), 'E_WEB_MANIFEST_EXISTS');
      const sdkRoot = path.resolve(__dirname, '../../sdk/web');
      const appRoot = selected ? identity.safePath(selected.root, 'web') : resolved.root;
      const roots = directories.map(directory => ({scope: 'app',
        root: identity.safePath(appRoot, directory), packageRoot: appRoot,
        prefix: options.appSourceDirectories === undefined ? '' : directory}));
      roots.push({scope: 'sdk', root: path.join(sdkRoot, 'src'), packageRoot: sdkRoot});
      const current = inspect(resolved.output, profile, roots);
      const scripts = current.scripts.map(({parsedMap, ...script}) => script);
      const manifest = {schema, profile, scripts};
      const bytes = Buffer.from(JSON.stringify(manifest, null, 2) + '\n');
      need(bytes.length <= fileLimit, 'E_WEB_FILE_BUDGET');
      fs.writeFileSync(marker, bytes, {flag: 'wx'});
      if (selected) {
        need(!fs.existsSync(identity.safePath(resolved.output, webTargets.manifestName)), 'E_WEB_TARGET_MANIFEST_EXISTS');
        webTargets.createManifest(resolved.output, selected);
      }
      return verifyDirectory(resolved.output, profile, {requireManifest: true});
    }};
  return {profile, build: {sourcemap: profile === 'source-diagnostic' ? 'inline' : false, minify: profile === 'distribution'}, plugin};
}
module.exports = {resolveProfile, manifestName, buildProfile, verifyDirectory};
