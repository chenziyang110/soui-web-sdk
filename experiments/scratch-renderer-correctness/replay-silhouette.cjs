'use strict';

// Offline only: replay captured ImageData through the actual source methods.
// --source-root is the Scratch Editor monorepo, with npm dependencies installed.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const Module = require('node:module');
const {TextDecoder} = require('node:util');
const {pathToFileURL} = require('node:url');

const sdkRoot = path.resolve(__dirname, '../..');
const profile = 'local-hull-cache-v1';
const upstream = 'b4b934aaf6641a539325a971249234f4a50a2f03';
const maximumRgbaBytes = 65536;
const maximumReportBytes = 4 * 1024 * 1024;
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const lf = text => text.replace(/\r\n/g, '\n');
const canonical = value => Array.isArray(value) ? value.map(canonical) : value && typeof value === 'object' ?
    Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])])) : value;
const same = (left, right) => JSON.stringify(canonical(left)) === JSON.stringify(canonical(right));
function requireTrue(value, code, message) {
    if (!value) {const error = new Error(message); error.code = code; throw error;}
}
function parseArguments(argv) {
    const options = {};
    for (let index = 0; index < argv.length; index += 2) {
        const key = argv[index];
        requireTrue(['--source-root', '--report', '--output'].includes(key) && !options[key] &&
            typeof argv[index + 1] === 'string' && argv[index + 1] && !argv[index + 1].startsWith('--'),
        'E_USAGE', 'Usage: node replay-silhouette.cjs --source-root <Scratch Editor monorepo> --report <browser JSON> --output <new JSON>');
        options[key] = path.resolve(argv[index + 1]);
    }
    requireTrue(Object.keys(options).length === 3, 'E_USAGE', 'All three explicit paths are required.');
    return {sourceRoot: options['--source-root'], report: options['--report'], output: options['--output']};
}
function readPlain(filename, maximumBytes) {
    const initial = fs.lstatSync(filename);
    requireTrue(initial.isFile() && !initial.isSymbolicLink() && initial.size <= maximumBytes,
        'E_INPUT_FILE', 'A bounded ordinary file is required: ' + filename);
    const descriptor = fs.openSync(filename, 'r');
    try {
        const before = fs.fstatSync(descriptor);
        requireTrue(before.isFile() && before.size === initial.size && before.ino === initial.ino &&
            before.dev === initial.dev, 'E_INPUT_CHANGED', 'Input changed while opening: ' + filename);
        const buffer = Buffer.alloc(before.size + 1);
        let count = 0;
        while (count < buffer.length) {
            const received = fs.readSync(descriptor, buffer, count, buffer.length - count, null);
            if (!received) break;
            count += received;
        }
        const after = fs.fstatSync(descriptor), current = fs.lstatSync(filename);
        requireTrue(count === before.size && after.size === before.size && after.mtimeMs === before.mtimeMs &&
            after.ctimeMs === before.ctimeMs && current.isFile() && !current.isSymbolicLink() &&
            current.ino === before.ino && current.dev === before.dev,
        'E_INPUT_CHANGED', 'Input changed while reading: ' + filename);
        return buffer.subarray(0, count);
    } finally {fs.closeSync(descriptor);}
}
function utf8(bytes) {return new TextDecoder('utf-8', {fatal: true}).decode(bytes);}
function plain(value) {return value !== null && typeof value === 'object' && !Array.isArray(value);}
function finiteVector(value, length) {
    return Array.isArray(value) && value.length === length && value.every(item => typeof item === 'number' && Number.isFinite(item));
}
function decodeSnapshot(snapshot, expectedBytes) {
    requireTrue(plain(snapshot) && snapshot.bytes === expectedBytes && expectedBytes > 0 && expectedBytes <= maximumRgbaBytes &&
        typeof snapshot.base64 === 'string' && snapshot.base64.length === 4 * Math.ceil(expectedBytes / 3) &&
        /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(snapshot.base64),
    'E_RGBA', 'Exact bounded canonical base64 and byte dimensions are required.');
    const bytes = Buffer.from(snapshot.base64, 'base64');
    requireTrue(bytes.length === expectedBytes && bytes.toString('base64') === snapshot.base64,
        'E_RGBA', 'RGBA base64 is truncated or noncanonical.');
    let checksum = 2166136261;
    for (const byte of bytes) checksum = Math.imul(checksum ^ byte, 16777619) >>> 0;
    requireTrue(snapshot.fnv1a32 === checksum.toString(16).padStart(8, '0'), 'E_RGBA', 'Captured RGBA checksum differs.');
    return bytes;
}
function validateReport(report) {
    requireTrue(plain(report) && report.schema === 'soui.scratch-renderer-correctness/v1' && report.correctnessOnly === true &&
        typeof report.passed === 'boolean' && typeof report.completed === 'boolean' && Array.isArray(report.failures) &&
        plain(report.page) && report.page.rendererProfile === profile && report.page.sourceCommit === upstream &&
        Array.isArray(report.svgFinalOracles) && report.svgFinalOracles.length === 2,
    'E_REPORT', 'A captured browser renderer report for the registered source/profile is required.');
    const rows = [report.svgVariantOracle, ...report.svgFinalOracles];
    const neutralEffects = {u_brightness: 0, u_color: 0, u_fisheye: 1, u_ghost: 1, u_mosaic: 1, u_pixelate: 0, u_whirl: 0};
    const inputs = rows.map((row, index) => {
        requireTrue(plain(row) && plain(row.after) && plain(row.diagnostic), 'E_PREMISE', 'Missing captured raster/diagnostic row.');
        const meta = row.after, diagnostic = row.diagnostic;
        const dimensions = [meta.silhouetteWidth, meta.silhouetteHeight, meta.backingWidth, meta.backingHeight,
            meta.naturalWidth, meta.naturalHeight];
        requireTrue(dimensions.every(value => Number.isSafeInteger(value) && value > 0 && value <= 4096) &&
            finiteVector(meta.skinSize, 2) && meta.skinSize.every(value => value > 0 && value <= 4096) &&
            finiteVector(meta.rotationCenter, 3) && meta.rotationCenter[2] === 0 && finiteVector(meta.scale, 2) &&
            meta.scale.every(value => value !== 0 && Math.abs(value) <= 10000) && Number.isFinite(meta.direction) &&
            Number.isFinite(meta.largestMIPScale) && meta.largestMIPScale > 0 && meta.largestMIPScale <= 256 &&
            Math.floor(meta.skinSize[0] * meta.largestMIPScale) === meta.silhouetteWidth &&
            Math.floor(meta.skinSize[1] * meta.largestMIPScale) === meta.silhouetteHeight && meta.svgLoaded === true &&
            same(row.before, meta) && same(row.beforePixels, meta) &&
            row.rgbaBytes === meta.backingWidth * meta.backingHeight * 4,
        'E_PREMISE', 'Raster dimensions, transforms, or pre/post-MIP premises differ.');
        requireTrue(diagnostic.enabledEffects === 0 && same(diagnostic.effectUniforms, neutralEffects),
            'E_EFFECTS', 'This replay only accepts the captured zero-effect path.');
        requireTrue(typeof diagnostic.svgSource === 'string' && diagnostic.svgSource.startsWith('data:image/svg+xml;utf8,') &&
            diagnostic.svgSource.length <= 65536 && Number.isSafeInteger(diagnostic.svgGeneration) && diagnostic.svgGeneration > 0 &&
            finiteVector(diagnostic.inverseMatrix, 16) && finiteVector(diagnostic.modelMatrix, 16) &&
            typeof diagnostic.skinUseNearest === 'boolean' && ['nearest', 'linear'].includes(diagnostic.cpuTouchMode) &&
            plain(diagnostic.sourceCanvas) && diagnostic.sourceCanvas.width === meta.silhouetteWidth &&
            diagnostic.sourceCanvas.height === meta.silhouetteHeight && diagnostic.sourceCanvas.extraReadbackPerformed === false,
        'E_PREMISE', 'Missing fixed decoded-SVG identity or nonintrusive sampling premises.');
        requireTrue(Array.isArray(row.hull) && row.hull.length > 0 && row.hull.length <= 8192 &&
            row.hull.every(point => finiteVector(point, 2)) && plain(row.bounds) && plain(row.bounds.bubble) &&
            plain(row.bounds.bubble.value) && same(Object.keys(row.bounds.bubble.value).sort(), ['bottom', 'left', 'right', 'top']) &&
            Object.values(row.bounds.bubble.value).every(value => typeof value === 'number' && Number.isFinite(value)),
        'E_ORACLE', 'Exact nonempty browser hull and bubble values are required.');
        return {index, row, meta, bytes: decodeSnapshot(diagnostic.silhouette, meta.silhouetteWidth * meta.silhouetteHeight * 4)};
    });
    requireTrue(inputs.every(input => same(input.meta, inputs[0].meta) &&
        input.row.diagnostic.svgSource === inputs[0].row.diagnostic.svgSource) &&
        !inputs[0].bytes.equals(inputs[1].bytes) && inputs[1].bytes.equals(inputs[2].bytes) &&
        inputs[1].row.diagnostic.svgGeneration === inputs[2].row.diagnostic.svgGeneration &&
        inputs[1].row.diagnostic.svgGeneration > inputs[0].row.diagnostic.svgGeneration,
    'E_PREMISE', 'Replay requires identical SVG/geometry, changed known-to-final bytes, and identical off/on finals.');
    return inputs;
}
function normalizedPin(filename, relative, maximumBytes = 16 * 1024 * 1024) {
    const bytes = readPlain(filename, maximumBytes), normalized = lf(utf8(bytes));
    return {path: relative, bytes: bytes.length, sha256: sha(bytes), normalizedSha256: sha(normalized)};
}
async function verifyRendererProfile(sourceRoot) {
    const policy = path.join(sdkRoot, 'demos/scratch/upstream/renderer-profiles.mjs');
    const {rendererPatchDescriptors} = await import(pathToFileURL(policy).href);
    const definitions = rendererPatchDescriptors(profile);
    const expectedNames = ['LocalHullCache.js', 'RenderWebGL.js', 'Silhouette.js'];
    requireTrue(same(definitions.map(item => path.posix.basename(item.path)).sort(), expectedNames),
        'E_SOURCE_PIN', 'The registered renderer profile closure changed.');
    const pins = definitions.map(definition => {
        const pin = normalizedPin(path.join(sourceRoot, definition.path), definition.path);
        requireTrue(pin.normalizedSha256 === definition.afterSha256, 'E_SOURCE_PIN', 'Registered renderer source differs: ' + definition.path);
        return pin;
    });
    const originalPath = 'demos/scratch/upstream/test-fixtures/renderer-profiles/RenderWebGL.original.js';
    const original = normalizedPin(path.join(sdkRoot, originalPath), originalPath);
    requireTrue(original.normalizedSha256 === definitions.find(item => item.path.endsWith('/RenderWebGL.js')).beforeSha256,
        'E_SOURCE_PIN', 'The fixed upstream RenderWebGL oracle fixture changed.');
    return {pins, original, policy: normalizedPin(policy, 'demos/scratch/upstream/renderer-profiles.mjs')};
}
function methodText(text) {
    const source = lf(text), anchor = '    _getConvexHullPointsForDrawable (';
    const start = source.indexOf(anchor), end = source.indexOf('\n    }\n', start);
    requireTrue(start >= 0 && end > start && source.indexOf(anchor, start + anchor.length) === -1,
        'E_SOURCE_METHOD', 'The checked hull source method is not unique.');
    return source.slice(start, end + '\n    }'.length);
}
function loadSourceMethods(sourceRoot, originalFilename) {
    const renderRoot = path.join(sourceRoot, 'packages/scratch-render');
    const local = Module.createRequire(path.join(renderRoot, 'package.json'));
    const boundary = () => {throw new Error('E_BROWSER_BOUNDARY: SVG decode, Canvas operations and GL require browser integration.');};
    global.window = {};
    global.document = {createElement(name) {
        if (name === 'img') return {};
        requireTrue(name === 'canvas', 'E_BROWSER_BOUNDARY', 'Unexpected browser element.');
        return {getContext(kind) {requireTrue(kind === '2d', 'E_BROWSER_BOUNDARY', 'No GL context is constructed.'); return Object.freeze({});}};
    }};
    global.ImageData = class ImageData {
        constructor(data, width, height) {this.data = data; this.width = width; this.height = height;}
    };
    const twgl = local('twgl.js'), hull = local('hull.js');
    const EffectTransform = local('./src/EffectTransform.js'), ShaderManager = local('./src/ShaderManager.js');
    const Drawable = local('./src/Drawable.js'), BitmapSkin = local('./src/BitmapSkin.js');
    const Cache = local('./src/LocalHullCache.js');
    const svgFilename = path.join(renderRoot, 'src/SVGSkin.js'), svgModule = new Module(svgFilename, module);
    svgModule.filename = svgFilename; svgModule.paths = Module._nodeModulePaths(renderRoot);
    const normalRequire = Module.createRequire(svgFilename);
    svgModule.require = name => name === '@scratch/scratch-svg-renderer' ?
        {loadSvgString: boundary, serializeSvgToString: boundary} : normalRequire(name);
    svgModule._compile(utf8(readPlain(svgFilename, 2 * 1024 * 1024)), svgFilename);
    const SVGSkin = svgModule.exports;
    const evaluate = filename => new Function('twgl', 'EffectTransform', 'hull', 'BitmapSkin', 'SVGSkin', 'ShaderManager',
        '"use strict";return ({' + methodText(utf8(readPlain(filename, 2 * 1024 * 1024))) + '})._getConvexHullPointsForDrawable;')(
        twgl, EffectTransform, hull, BitmapSkin, SVGSkin, ShaderManager);
    return {Drawable, SVGSkin, Cache, original: evaluate(originalFilename),
        candidate: evaluate(path.join(renderRoot, 'src/RenderWebGL.js')), boundary};
}
function replay(inputs, source) {
    const make = input => {
        const skin = new source.SVGSkin(0, {gl: new Proxy({}, {get: source.boundary})});
        skin._size = [...input.meta.skinSize]; skin._svgImageLoaded = true;
        skin._largestMIPScale = input.meta.largestMIPScale;
        skin._rotationCenter[0] = input.meta.rotationCenter[0]; skin._rotationCenter[1] = input.meta.rotationCenter[1];
        // This method would decode/rasterize in a browser. Offline replay starts
        // after that boundary, from the exact captured ImageData bytes.
        skin.updateSilhouette = () => requireTrue(skin._silhouette._colorData !== null,
            'E_BROWSER_BOUNDARY', 'Captured ImageData must be installed before replay.');
        const drawable = new source.Drawable(0); drawable.skin = skin;
        drawable.updateScale(input.meta.scale); drawable.updateDirection(input.meta.direction);
        return {skin, drawable, renderer: {_allDrawables: [drawable], _localHullCache: null}};
    };
    const update = (skin, input) => skin._silhouette.update(new global.ImageData(
        new Uint8ClampedArray(input.bytes), input.meta.silhouetteWidth, input.meta.silhouetteHeight));
    const check = (fixture, input, points, label) => {
        requireTrue(same(points, input.row.hull), 'E_HULL', label + ': actual source differs from the exact captured hull.');
        fixture.drawable.setConvexHullPoints(points);
        const bubble = fixture.drawable.getBoundsForBubble();
        requireTrue(same(bubble, input.row.bounds.bubble.value), 'E_BUBBLE', label + ': actual source differs from the exact captured bubble.');
        requireTrue(same(Array.from(fixture.drawable.getUniforms().u_modelMatrix), input.row.diagnostic.modelMatrix) &&
            same(Array.from(fixture.drawable._inverseMatrix), input.row.diagnostic.inverseMatrix) &&
            fixture.skin.useNearest(input.meta.scale, fixture.drawable) === input.row.diagnostic.skinUseNearest &&
            (fixture.drawable.isTouching === fixture.drawable._isTouchingNearest ? 'nearest' : 'linear') === input.row.diagnostic.cpuTouchMode,
        'E_PREMISE', label + ': captured transform or sampling premises do not match the actual Drawable.');
        return canonical(bubble);
    };
    const snapshots = inputs.map(input => {
        const fixture = make(input); update(fixture.skin, input);
        const original = source.original.call(fixture.renderer, 0);
        const originalBubble = check(fixture, input, original, 'Original ' + input.index);
        const uncached = source.candidate.call(fixture.renderer, 0);
        check(fixture, input, uncached, 'Candidate off ' + input.index);
        fixture.renderer._localHullCache = new source.Cache();
        const cached = source.candidate.call(fixture.renderer, 0), again = source.candidate.call(fixture.renderer, 0);
        const cachedBubble = check(fixture, input, cached, 'Candidate on ' + input.index);
        requireTrue(cached === again && Object.isFrozen(cached) && cached.every(Object.isFrozen) &&
            fixture.renderer._localHullCache.size === 1 && fixture.renderer._localHullCache.pointCount === cached.length,
        'E_CACHE', 'The cached exact hull was not reused immutably within one bounded entry.');
        return {index: input.index, rgbaSha256: sha(input.bytes), svgSha256: sha(input.row.diagnostic.svgSource),
            capturedGeneration: input.row.diagnostic.svgGeneration, originalHull: original, originalBubble,
            cachedBubble, originalAndCandidateOffAndOnExact: true, immutableCacheHitIdentity: true};
    });
    const fixture = make(inputs[0]), cache = new source.Cache();
    fixture.renderer._localHullCache = cache;
    const revisions = new Set(), hulls = new Set(), transitions = [];
    for (const [index, input] of [inputs[0], inputs[1], inputs[0]].entries()) {
        update(fixture.skin, input);
        const revision = fixture.skin._silhouette.hullRevision;
        requireTrue(revision && !revisions.has(revision), 'E_REVISION', 'Silhouette.update reused an old revision.');
        const actual = source.candidate.call(fixture.renderer, 0), again = source.candidate.call(fixture.renderer, 0);
        const original = source.original.call(fixture.renderer, 0);
        requireTrue(same(actual, original) && actual === again && !hulls.has(actual) && Object.isFrozen(actual) &&
            actual.every(Object.isFrozen) && cache.size === 1 && cache.pointCount === actual.length,
        'E_CACHE', 'The retained skin/cache returned a stale hull, reused an old reference, or retained stale entries.');
        const bubble = check(fixture, input, actual, 'Retained A/B/A ' + index);
        transitions.push({index, rgbaSha256: sha(input.bytes), points: actual.length, bubble,
            freshRevision: true, freshHullReference: true, repeatedLookupIdentity: true,
            originalAndBrowserExact: true, entries: cache.size, pointCount: cache.pointCount});
        revisions.add(revision); hulls.add(actual);
    }
    return {passed: true, scope: 'Exact captured silhouette bytes through original/candidate hull methods and actual Drawable bubble geometry.',
        snapshots, sameSkinAndCacheAcrossTransitions: true, transitions};
}
async function runReplay(options) {
    requireTrue(!fs.existsSync(options.output), 'E_OUTPUT_EXISTS', 'Output must be a new file.');
    const bytes = readPlain(options.report, maximumReportBytes);
    const report = JSON.parse(utf8(bytes)), inputs = validateReport(report);
    const preloaded = Object.keys(require.cache).some(filename => {
        const relative = path.relative(options.sourceRoot, filename);
        return relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative);
    });
    requireTrue(!preloaded, 'E_PRELOADED_SOURCE',
        'Use a fresh one-shot CLI process; source or dependencies from this monorepo are already loaded.');
    const binding = await verifyRendererProfile(options.sourceRoot);
    const renderRoot = path.join(options.sourceRoot, 'packages/scratch-render');
    const packageFilename = path.join(renderRoot, 'package.json');
    const packagePin = normalizedPin(packageFilename, 'packages/scratch-render/package.json', 65536);
    const packageJson = JSON.parse(utf8(readPlain(packageFilename, 65536)));
    requireTrue(packageJson.name === '@scratch/scratch-render' && packageJson.version === '15.2.0',
        'E_SOURCE_PIN', 'The fixed Scratch Editor render source package is required.');
    const sourceNames = ['RenderWebGL.js', 'Drawable.js', 'Silhouette.js', 'Skin.js', 'SVGSkin.js', 'BitmapSkin.js',
        'EffectTransform.js', 'ShaderManager.js', 'LocalHullCache.js', 'Rectangle.js', 'RenderConstants.js', 'util/log.js'];
    const sourceBefore = sourceNames.map(name => normalizedPin(path.join(renderRoot, 'src', name), 'packages/scratch-render/src/' + name));
    const cacheBefore = new Set(Object.keys(require.cache));
    const source = loadSourceMethods(options.sourceRoot, path.join(sdkRoot, binding.original.path));
    const loaded = Object.keys(require.cache).filter(filename => !cacheBefore.has(filename)).sort().map(filename => {
        const relative = path.relative(options.sourceRoot, filename);
        requireTrue(relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative),
            'E_SOURCE_PIN', 'A resolved replay dependency is outside the supplied monorepo.');
        return normalizedPin(filename, relative.split(path.sep).join('/'));
    });
    const offlineReplay = replay(inputs, source);
    const sourceAfter = sourceNames.map(name => normalizedPin(path.join(renderRoot, 'src', name), 'packages/scratch-render/src/' + name));
    const bindingAfter = await verifyRendererProfile(options.sourceRoot);
    requireTrue(same(binding, bindingAfter) && same(packagePin, normalizedPin(packageFilename, packagePin.path, 65536)) &&
        same(sourceBefore, sourceAfter) && loaded.every(pin => same(pin,
        normalizedPin(path.join(options.sourceRoot, pin.path), pin.path))) &&
        readPlain(options.report, maximumReportBytes).equals(bytes), 'E_INPUT_CHANGED', 'Replay inputs changed during execution.');
    const result = {schema: 'soui.captured-silhouette-offline-replay/v1', createdUtc: new Date().toISOString(), node: process.version,
        sourceRoot: options.sourceRoot, input: {path: options.report, bytes: bytes.length, sha256: sha(bytes)},
        driver: normalizedPin(__filename, 'experiments/scratch-renderer-correctness/replay-silhouette.cjs'),
        sourceBinding: {...binding, unpatchedCoreAuthenticatedToUpstream: false,
            executionContract: 'Fresh one-shot CLI; unpatched core/dependency hashes identify actual reads, not a complete upstream-checkout attestation.',
            package: packagePin, sourceBefore, sourceAfter, loadedModules: loaded}, offlineReplay,
        inputBrowserReport: {passed: report.passed, completed: report.completed, failures: report.failures},
        browserQualification: false, noCanvasReadbackOrGlContext: true,
        notProven: ['Why identical SVG decodes produced different pixel inputs', 'Canvas GPU/CPU transitions',
            'The full browser renderer suite', 'VM lifecycle or physical input', 'FPS or input-visible latency']};
    fs.writeFileSync(options.output, JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
    return result;
}

module.exports = {parseArguments, decodeSnapshot, validateReport, verifyRendererProfile, runReplay};
if (require.main === module) Promise.resolve().then(() => runReplay(parseArguments(process.argv.slice(2)))).then(result => {
    console.log(JSON.stringify({offlineReplayPassed: result.offlineReplay.passed,
        inputBrowserReportPassed: result.inputBrowserReport.passed, browserQualification: false}));
}).catch(error => {console.error((error.code || 'E_REPLAY') + ': ' + error.message); process.exitCode = 1;});
