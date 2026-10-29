'use strict';
const test = require('node:test');
const {after} = test;
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {parseArguments, decodeSnapshot, validateReport, verifyRendererProfile, runReplay} = require('./replay-silhouette.cjs');

const temporaryParent = fs.realpathSync(os.tmpdir());
const temporary = fs.mkdtempSync(path.join(temporaryParent, 'soui-silhouette-replay-'));
after(() => {
    assert.equal(path.dirname(temporary), temporaryParent);
    assert.match(path.basename(temporary), /^soui-silhouette-replay-/);
    fs.rmSync(temporary, {recursive: true, force: true});
});
function snapshot(bytes) {
    let value = 2166136261;
    for (const byte of bytes) value = Math.imul(value ^ byte, 16777619) >>> 0;
    return {bytes: bytes.length, base64: Buffer.from(bytes).toString('base64'), fnv1a32: value.toString(16).padStart(8, '0')};
}
function reportFixture() {
    const meta = {skinSize: [1, 1], rotationCenter: [0, 0, 0], scale: [100, 100], direction: 90,
        largestMIPScale: 1, silhouetteWidth: 1, silhouetteHeight: 1, naturalWidth: 1, naturalHeight: 1,
        backingWidth: 1, backingHeight: 1, svgLoaded: true};
    const row = (bytes, generation) => ({before: {...meta}, beforePixels: {...meta}, after: {...meta}, rgbaBytes: 4,
        hull: [[0, 0], [1, 0], [0, 1]], bounds: {bubble: {value: {bottom: 0, left: 0, right: 1, top: 1}}},
        diagnostic: {silhouette: snapshot(bytes), svgSource: 'data:image/svg+xml;utf8,%3Csvg%2F%3E', svgGeneration: generation,
            enabledEffects: 0, effectUniforms: {u_brightness: 0, u_color: 0, u_fisheye: 1, u_ghost: 1, u_mosaic: 1, u_pixelate: 0, u_whirl: 0},
            modelMatrix: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
            inverseMatrix: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
            cpuTouchMode: 'nearest', skinUseNearest: true,
            sourceCanvas: {width: 1, height: 1, extraReadbackPerformed: false}}});
    return {schema: 'soui.scratch-renderer-correctness/v1', correctnessOnly: true, completed: false, passed: false,
        failures: ['Preserved browser failure'], page: {rendererProfile: 'local-hull-cache-v1',
            sourceCommit: 'b4b934aaf6641a539325a971249234f4a50a2f03'},
        svgVariantOracle: row([0, 255, 0, 255], 2),
        svgFinalOracles: [row([0, 254, 0, 255], 5), row([0, 254, 0, 255], 5)]};
}

test('CLI requires every path explicitly and rejects duplicate/unknown arguments', () => {
    const parsed = parseArguments(['--source-root', 'source', '--report', 'report.json', '--output', 'new.json']);
    assert.equal(parsed.sourceRoot, path.resolve('source'));
    for (const argv of [[], ['--source-root', 'source'], ['--report', 'first', '--report', 'second'],
        ['--arbitrary', 'script'], ['--source-root', '--report']]) {
        assert.throws(() => parseArguments(argv), {code: 'E_USAGE'});
    }
});

test('RGBA decoder preserves exact bytes and rejects truncation, whitespace and pad-bit aliases', () => {
    const bytes = Buffer.from([0, 255, 0, 255]), correct = snapshot(bytes);
    assert.deepEqual(decodeSnapshot(correct, 4), bytes);
    for (const changed of [{...correct, base64: correct.base64.slice(0, -1)},
        {...correct, base64: correct.base64 + '\n'}, {...correct, base64: 'AP8A/x=='},
        {...correct, bytes: 3}, {...correct, fnv1a32: '00000000'}]) {
        assert.throws(() => decodeSnapshot(changed, 4), {code: 'E_RGBA'});
    }
    assert.throws(() => decodeSnapshot(correct, 65540), {code: 'E_RGBA'});
});

test('validation-only fixture keeps browser failure and both exact final inputs', () => {
    const report = reportFixture(), before = JSON.stringify(report);
    const inputs = validateReport(report);
    assert.equal(inputs.length, 3);
    assert.ok(!inputs[0].bytes.equals(inputs[1].bytes));
    assert.ok(inputs[1].bytes.equals(inputs[2].bytes));
    assert.equal(report.passed, false);
    assert.equal(JSON.stringify(report), before);
});

test('replay refuses effects and mismatched raster, generation or observation premises', () => {
    const mutations = [
        report => {report.svgVariantOracle.diagnostic.enabledEffects = 1;},
        report => {report.svgVariantOracle.diagnostic.effectUniforms.u_ghost = 0;},
        report => {report.svgFinalOracles[0].after.direction = 45;},
        report => {report.svgFinalOracles[0].diagnostic.svgGeneration = 2;},
        report => {report.svgVariantOracle.diagnostic.sourceCanvas.extraReadbackPerformed = true;},
        report => {report.svgFinalOracles[0].diagnostic.svgSource += 'changed';},
        report => {report.svgFinalOracles[1].diagnostic.silhouette = snapshot([1, 254, 0, 255]);}
    ];
    for (const mutate of mutations) {
        const report = reportFixture(); mutate(report);
        assert.throws(() => validateReport(report), error => ['E_EFFECTS', 'E_PREMISE'].includes(error.code));
    }
});

test('missing/nonfinite geometry and wrong profile are rejected rather than normalized', () => {
    for (const mutate of [report => {report.svgVariantOracle.hull = [];},
        report => {report.svgVariantOracle.hull[0][0] = NaN;},
        report => {delete report.svgVariantOracle.bounds.bubble.value;},
        report => {report.page.rendererProfile = 'stock';}]) {
        const report = reportFixture(); mutate(report);
        assert.throws(() => validateReport(report), error => ['E_ORACLE', 'E_REPORT'].includes(error.code));
    }
});

test('registered source pins accept LF/CRLF equivalence and reject source mutation', async () => {
    const source = path.join(temporary, 'source'), target = path.join(source, 'packages/scratch-render/src');
    fs.mkdirSync(target, {recursive: true});
    const recipe = path.resolve(__dirname, '../../demos/scratch/upstream/renderer/local-hull-cache-v1');
    for (const name of ['RenderWebGL.js', 'Silhouette.js', 'LocalHullCache.js']) {
        const text = fs.readFileSync(path.join(recipe, name), 'utf8').replace(/\r\n/g, '\n');
        fs.writeFileSync(path.join(target, name), text.replace(/\n/g, '\r\n'));
    }
    const actual = await verifyRendererProfile(source);
    assert.equal(actual.pins.length, 3);
    assert.ok(actual.pins.some(pin => pin.sha256 !== pin.normalizedSha256));
    fs.appendFileSync(path.join(target, 'RenderWebGL.js'), '\n// changed source\n');
    await assert.rejects(verifyRendererProfile(source), {code: 'E_SOURCE_PIN'});
});

test('existing output remains byte-for-byte intact and oversized input is refused before parsing', async () => {
    const output = path.join(temporary, 'keep.json'); fs.writeFileSync(output, 'keep');
    await assert.rejects(runReplay({sourceRoot: temporary, report: 'unused', output}), {code: 'E_OUTPUT_EXISTS'});
    assert.equal(fs.readFileSync(output, 'utf8'), 'keep');
    const oversized = path.join(temporary, 'oversized.json'); fs.writeFileSync(oversized, Buffer.alloc(4 * 1024 * 1024 + 1));
    const absentOutput = path.join(temporary, 'not-created.json');
    await assert.rejects(runReplay({sourceRoot: temporary, report: oversized, output: absentOutput}), {code: 'E_INPUT_FILE'});
    assert.equal(fs.existsSync(absentOutput), false);
});

test('replay rejects a preloaded monorepo module instead of binding old memory to current files', async () => {
    const report = path.join(temporary, 'preloaded-report.json');
    const output = path.join(temporary, 'preloaded-output.json');
    fs.writeFileSync(report, JSON.stringify(reportFixture()));
    const cachedFilename = path.join(temporary, 'packages/scratch-render/src/preloaded.js');
    require.cache[cachedFilename] = {exports: {stale: true}};
    try {
        await assert.rejects(runReplay({sourceRoot: temporary, report, output}), {code: 'E_PRELOADED_SOURCE'});
        assert.equal(fs.existsSync(output), false);
    } finally {delete require.cache[cachedFilename];}
});
