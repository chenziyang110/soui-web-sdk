(function () {
    'use strict';
    // Only the native consumer replaces this fixed envelope. No script or
    // method name is accepted from the page, command line, or Bridge caller.
    const envelope = __SOUI_CORRECTNESS_ENVELOPE__;
    if (window.__souiRendererCorrectnessStarted) throw Error('A fresh document is required');
    window.__souiRendererCorrectnessStarted = true;
    const started = performance.now();
    const deadline = started + 80000;
    const report = {schema: 'soui.scratch-renderer-correctness/v1', correctnessOnly: true,
        completed: false, passed: false, assertions: 0, cases: [], failures: [],
        scope: 'Actual WebGL renderer cache off/on differential, fixed explicit inputs; no VM execution or FPS qualification',
        notCovered: ['full VM clone/project reload/undo/redo', 'context loss and restore',
            'physical input or presented frames', '131072-point capacity saturation'],
        page: {href: location.href, userAgent: navigator.userAgent,
            rendererProfile: document.documentElement.dataset.scratchRendererProfile || null,
            sourceCommit: document.documentElement.dataset.scratchSourceCommit || null}};
    const contexts = [];
    const finite = value => {
        if (typeof value === 'number' && !Number.isFinite(value)) throw Error('Non-finite geometry');
        if (Array.isArray(value)) return value.map(finite);
        if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(key => [key, finite(value[key])]));
        return value;
    };
    function assert(condition, reason) {
        report.assertions++;
        if (!condition) throw Error(reason);
    }
    function equal(left, right, reason) {
        const matches = JSON.stringify(finite(left)) === JSON.stringify(finite(right));
        if (!matches && !report.firstDifference) report.firstDifference = {reason, left, right};
        assert(matches, reason);
    }
    function alive() {
        if (performance.now() >= deadline) throw Error('Fixed fixture overall deadline exceeded');
    }
    const pause = () => new Promise(resolve => setTimeout(resolve, 10));
    async function loaded(skin) {
        const until = Math.min(deadline, performance.now() + 5000);
        // The SVG Promise resolves when normalized dimensions are known. It
        // does not wait for HTMLImageElement.onload and a usable texture.
        while (!skin._svgImageLoaded) {
            if (performance.now() >= until) throw Error('Real SVG image decode did not finish');
            await pause();
        }
        alive();
    }
    function image(variant = 0, transparent = false) {
        const result = new ImageData(32, 24);
        for (let y = 0; y < 24; y++) for (let x = 0; x < 32; x++) {
            const offset = (y * 32 + x) * 4;
            const solid = variant ? x > 10 && y > 2 && y < 21 : x > 2 && x < 24 && y > 3 && y < 19 && x + y > 12;
            result.data[offset] = variant ? 0 : 255;
            result.data[offset + 1] = variant ? 255 : 0;
            result.data[offset + 3] = solid && !transparent ? 255 : 0;
        }
        return result;
    }
    const svg = variant => '<svg xmlns="http://www.w3.org/2000/svg" width="64" height="48" viewBox="0 0 64 48">' +
        (variant ? '<path fill="#00ff00" d="M8 5H52V40H30Z"/>' : '<path fill="#ff0000" d="M4 4L58 12L20 43Z"/>') + '</svg>';
    function make(Renderer, enabled) {
        const canvas = document.createElement('canvas');
        canvas.width = 480; canvas.height = 360;
        // clientWidth/clientHeight must be real: pick() maps CSS coordinates.
        // This fixed offscreen canvas never changes host focus or cursor.
        canvas.style.cssText = 'position:fixed;left:-10000px;top:0;width:480px;height:360px;pointer-events:none';
        document.body.appendChild(canvas);
        const renderer = new Renderer(canvas);
        renderer.setLayerGroupOrdering(['background', 'pen', 'sprite']);
        renderer.setLocalHullCacheEnabled(enabled);
        const value = {renderer, canvas, skins: new Set(), drawables: new Map()};
        contexts.push(value);
        return value;
    }
    function drawable(context, skin, group = 'sprite') {
        const id = context.renderer.createDrawable(group);
        context.drawables.set(id, group);
        context.renderer.updateDrawableProperties(id, {skinId: skin, position: [0, 0],
            direction: 90, scale: [100, 100], visible: true});
        return id;
    }
    function bitmap(context, variant = 0, transparent = false) {
        const id = context.renderer.createBitmapSkin(image(variant, transparent), 1, [16, 12]);
        context.skins.add(id); return id;
    }
    function rawHull(context, id) {
        return context.renderer._getConvexHullPointsForDrawable(id).map(point => Array.from(point));
    }
    function observed(operation) {
        try {return {value: operation()};}
        catch (error) {return {throws: {name: String(error.name), message: String(error.message)}};}
    }
    function bounds(context, id) {
        const r = context.renderer;
        // Upstream getBoundsForBubble throws for an empty hull. Compare the
        // actual outcome, preserving that pre-existing limitation explicitly.
        return {bounds: r.getBounds(id), bubble: observed(() => r.getBoundsForBubble(id)),
            fence: r.getFencedPositionOfDrawable(id, [260, -190])};
    }
    function queries(context, ids) {
        const r = context.renderer;
        const result = {points: [], touch: [], picks: [], colors: []};
        for (const id of ids) {
            for (const point of [[0, 0], [5, 5], [-17, 9], [30, 20]]) {
                result.points.push(r.drawableTouchingScratchPoint(id, ...point));
            }
            result.touch.push(r.isTouchingDrawables(id, ids));
            for (const mode of ['ForceCPU', 'ForceGPU']) {
                r.setUseGpuMode(mode);
                result.colors.push(r.isTouchingColor(id, [255, 255, 255]), r.isTouchingColor(id, [255, 0, 0]));
            }
        }
        r.setUseGpuMode('Automatic');
        for (const point of [[240, 180], [245, 175], [223, 171], [270, 160]]) result.picks.push(r.pick(...point, 1, 1, ids));
        return result;
    }
    function pixels(context) {
        const r = context.renderer;
        r.draw();
        const width = r.canvas.width, height = r.canvas.height;
        assert(width > 0 && height > 0 && width <= 4096 && height <= 4096, 'Canvas backing dimensions exceed fixed fixture budget');
        const data = new Uint8Array(width * height * 4);
        const gl = r.gl;
        // Queries use their own FBOs. draw() restores the default target.
        gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, data);
        assert(gl.getError() === gl.NO_ERROR, 'Unexpected WebGL error during pixel snapshot');
        return data;
    }
    function comparePixels(a, b, name) {
        const left = pixels(a), right = pixels(b);
        equalPixels(left, right, name);
        return left.length;
    }
    function equalPixels(left, right, name) {
        let difference = -1;
        for (let index = 0; index < left.length; index++) if (left[index] !== right[index]) { difference = index; break; }
        assert(left.length === right.length && difference === -1, name + ': rendered RGBA differs at byte ' + difference);
    }
    function staticReuse(context, id, sibling, name) {
        const r = context.renderer, skin = r._allDrawables[id].skin;
        const first = r._getConvexHullPointsForDrawable(id);
        const cached = r._localHullCache.get(skin, skin._silhouette.hullRevision, ...skin.size);
        assert(first === cached && first === r._getConvexHullPointsForDrawable(id) &&
            first === r._getConvexHullPointsForDrawable(sibling) && Object.isFrozen(first) && first.every(Object.isFrozen),
        name + ': actual static SVG cache hit/repeated/shared reuse was not exercised');
        return first;
    }
    function raster(context, id) {
        const r = context.renderer, drawable = r._allDrawables[id], skin = drawable.skin;
        return {skinSize: Array.from(skin.size), rotationCenter: Array.from(skin.rotationCenter),
            scale: Array.from(drawable.scale), direction: drawable._direction,
            largestMIPScale: skin._largestMIPScale, silhouetteWidth: skin._silhouette._width,
            silhouetteHeight: skin._silhouette._height, svgLoaded: skin._svgImageLoaded,
            naturalWidth: skin._svgImage && skin._svgImage.naturalWidth,
            naturalHeight: skin._svgImage && skin._svgImage.naturalHeight,
            backingWidth: r.canvas.width, backingHeight: r.canvas.height};
    }
    function byteSnapshot(bytes) {
        assert(bytes && bytes.length <= 65536, 'Diagnostic byte snapshot exceeds fixed 64KiB limit');
        let checksum = 2166136261, binary = '';
        for (let index = 0; index < bytes.length; index++) {
            checksum = Math.imul(checksum ^ bytes[index], 16777619) >>> 0;
            binary += String.fromCharCode(bytes[index]);
        }
        return {bytes: bytes.length, fnv1a32: checksum.toString(16).padStart(8, '0'), base64: btoa(binary)};
    }
    function byteDifference(left, right) {
        let differentBytes = 0, differentPixels = 0, maximumChannelDelta = 0;
        let previousDifferentPixel = -1;
        const firstDifferences = [];
        for (let index = 0; index < Math.max(left.length, right.length); index++) {
            if (left[index] === right[index]) continue;
            differentBytes++;
            const pixel = Math.floor(index / 4);
            if (pixel !== previousDifferentPixel) {differentPixels++; previousDifferentPixel = pixel;}
            maximumChannelDelta = Math.max(maximumChannelDelta, Math.abs((left[index] || 0) - (right[index] || 0)));
            if (firstDifferences.length < 32) firstDifferences.push({offset: index, pixel: Math.floor(index / 4), left: left[index], right: right[index]});
        }
        return {equal: left.length === right.length && differentBytes === 0, leftBytes: left.length,
            rightBytes: right.length, differentBytes, differentPixels, maximumChannelDelta, firstDifferences};
    }
    function svgDiagnostic(context, id) {
        const r = context.renderer, drawable = r._allDrawables[id], skin = drawable.skin;
        const source = String(skin._svgImage.src);
        assert(source.length <= 65536, 'Fixed SVG URI exceeds diagnostic limit');
        // These are the actual ImageData.data bytes passed by createMIP to
        // Silhouette.update. Copy the existing buffer, without another Canvas
        // readback that could perturb Chromium's GPU/CPU backend heuristic.
        const silhouette = byteSnapshot(skin._silhouette._colorData);
        const canvas = skin._canvas;
        const uniforms = drawable.getUniforms();
        const effectUniforms = {};
        for (const key of Object.keys(uniforms)) if (/^u_(fisheye|whirl|pixelate|mosaic|color|brightness|ghost)$/.test(key)) effectUniforms[key] = uniforms[key];
        return {svgSource: source, svgGeneration: skin._svgGeneration,
            silhouette, sourceCanvas: {width: canvas.width, height: canvas.height, extraReadbackPerformed: false},
            enabledEffects: drawable.enabledEffects, effectUniforms,
            skinUseNearest: skin.useNearest(drawable.scale, drawable),
            cpuTouchMode: drawable.isTouching === drawable._isTouchingNearest ? 'nearest' :
                drawable.isTouching === drawable._isTouchingLinear ? 'linear' : 'other',
            inverseMatrix: Array.from(drawable._inverseMatrix),
            modelMatrix: Array.from(uniforms.u_modelMatrix),
            observation: 'Existing createMIP ImageData.data copied; no extra source Canvas readback'};
    }
    function svgOracle(context, id) {
        const before = raster(context, id), hullBeforePixels = rawHull(context, id);
        const beforePixels = raster(context, id);
        const rgba = pixels(context);
        // draw() can create a higher SVG MIP because canvas backing pixels
        // include devicePixelRatio. Compare the known variant only after the
        // same draw/MIP promotion, never a pre-draw hull against a post-draw one.
        const after = raster(context, id), hull = rawHull(context, id);
        context.renderer._allDrawables[id].setConvexHullDirty();
        const rectangle = bounds(context, id);
        const diagnostic = svgDiagnostic(context, id);
        return {before, beforePixels, hullBeforePixels, after, hull, bounds: rectangle, pixels: rgba, diagnostic};
    }
    function oracleMetadata(value) {
        return {before: value.before, beforePixels: value.beforePixels, after: value.after, hullBeforePixels: value.hullBeforePixels,
            hull: value.hull, bounds: value.bounds, rgbaBytes: value.pixels.length, diagnostic: value.diagnostic};
    }
    function compare(a, b, ids, name, withPixels = true) {
        alive();
        const entry = {name, assertionsBefore: report.assertions};
        for (const id of ids) {
            equal(rawHull(a, id), rawHull(b, id), name + ': local convex hull differs');
            const before = bounds(a, id), after = bounds(b, id);
            equal(before, after, name + ': bounds/bubble/fence differs');
            if (before.bubble.throws) {
                if (!entry.apiExceptions) entry.apiExceptions = [];
                entry.apiExceptions.push({id, operation: 'getBoundsForBubble', ...before.bubble.throws});
            }
        }
        equal(queries(a, ids), queries(b, ids), name + ': pick/touch/color differs');
        if (withPixels) entry.equalRgbaBytes = comparePixels(a, b, name);
        entry.assertions = report.assertions - entry.assertionsBefore;
        report.cases.push(entry);
    }
    function updateBoth(pair, method, ...args) {
        for (const item of pair) item.renderer[method](...args);
    }
    async function run() {
        const vm = window.souiScratch && window.souiScratch.vm;
        if (!window.souiScratch || !window.souiScratch.ready || !vm || !vm.renderer) throw Error('Fresh Scratch document is not ready');
        if (typeof vm.renderer.setLocalHullCacheEnabled !== 'function') throw Error('The loaded bundle has no registered local-hull-cache profile');
        vm.stopAll();
        const Renderer = vm.renderer.constructor;
        const a = make(Renderer, false), b = make(Renderer, true), pair = [a, b];
        report.gl = pair.map(item => ({version: item.renderer.gl.getParameter(item.renderer.gl.VERSION),
            renderer: item.renderer.gl.getParameter(item.renderer.gl.RENDERER),
            cache: item.renderer._localHullCache !== null, clientWidth: item.canvas.clientWidth, clientHeight: item.canvas.clientHeight,
            backingWidth: item.canvas.width, backingHeight: item.canvas.height, devicePixelRatio}));
        equal([a.canvas.clientWidth, a.canvas.clientHeight], [480, 360], 'Off canvas CSS size');
        equal([b.canvas.clientWidth, b.canvas.clientHeight], [480, 360], 'On canvas CSS size');
        const bitmapIds = pair.map(item => bitmap(item));
        equal(bitmapIds[0], bitmapIds[1], 'Deterministic bitmap identity');
        const mainIds = pair.map((item, index) => drawable(item, bitmapIds[index]));
        const sharedIds = pair.map((item, index) => drawable(item, bitmapIds[index]));
        const main = mainIds[0], shared = sharedIds[0];
        equal(mainIds[0], mainIds[1], 'Deterministic main identity');
        equal(sharedIds[0], sharedIds[1], 'Deterministic shared identity');
        compare(a, b, [main, shared], 'bitmap shared skin');
        const first = b.renderer._getConvexHullPointsForDrawable(main);
        const second = b.renderer._getConvexHullPointsForDrawable(shared);
        assert(first === second && Object.isFrozen(first) && first.every(Object.isFrozen), 'Actual browser did not reuse immutable local hull');
        assert(b.renderer._allDrawables[main]._transformedHullPoints !== b.renderer._allDrawables[shared]._transformedHullPoints,
            'World-space hull arrays alias across drawables');
        assert(first.length >= 3, 'Initial hull is vacuously empty');
        for (const item of pair) {
            const r = item.renderer;
            assert(r.drawableTouchingScratchPoint(main, 0, 0) && !r.drawableTouchingScratchPoint(main, 100, 100), 'Known inside/outside points failed');
            // The fixed upstream returns false when candidate bounds are all
            // excluded (distinct from its later ID_NONE/-1 sample path).
            assert(r.pick(240, 180, 1, 1, [main, shared]) === shared && r.pick(0, 0, 1, 1, [main, shared]) === false, 'Known positive/negative picks failed');
            assert(r.isTouchingDrawables(main, [shared]), 'Known overlapping shared skin was not touching');
            for (const mode of ['ForceCPU', 'ForceGPU']) {
                r.setUseGpuMode(mode);
                assert(r.isTouchingColor(main, [255, 0, 0]) && !r.isTouchingColor(main, [0, 0, 255]), 'Known true/false color query failed: ' + mode);
            }
            r.setUseGpuMode('Automatic');
            const rendered = pixels(item);
            let colored = 0;
            for (let index = 0; index < rendered.length; index += 4) if (rendered[index] !== 255 || rendered[index + 1] !== 255 || rendered[index + 2] !== 255) colored++;
            assert(colored > 0, 'Pixel equality was vacuously all background');
        }
        report.cases.push({name: 'known positive and negative geometry/pick/color and nonbackground pixels'});
        for (const [direction, scale] of [[29, [37, 37]], [173, [-120, 80]], [270, [400, 400]]]) {
            updateBoth(pair, 'updateDrawableDirectionScale', main, direction, scale);
            compare(a, b, [main, shared], 'bitmap transform ' + direction);
        }
        updateBoth(pair, 'updateDrawableDirectionScale', main, 90, [100, 100]);
        for (const item of pair) item.renderer.updateBitmapSkin(bitmapIds[0], image(1), 1, [4, 8]);
        compare(a, b, [main, shared], 'same-size bitmap edit and rotation center');
        for (const effect of ['fisheye', 'whirl', 'pixelate', 'mosaic', 'color', 'brightness', 'ghost']) {
            const cached = b.renderer._getConvexHullPointsForDrawable(main);
            updateBoth(pair, 'updateDrawableEffect', main, effect, 35);
            compare(a, b, [main, shared], 'effect ' + effect);
            const after = b.renderer._getConvexHullPointsForDrawable(main);
            if (['fisheye', 'whirl', 'pixelate', 'mosaic'].includes(effect)) {
                assert(after !== cached && !Object.isFrozen(after), 'Shape effect did not take the original hull path: ' + effect);
            } else assert(after === cached && Object.isFrozen(after), 'Non-shape effect failed to reuse immutable local hull: ' + effect);
            updateBoth(pair, 'updateDrawableEffect', main, effect, 0);
        }
        updateBoth(pair, 'updateDrawableVisible', main, false);
        compare(a, b, [main, shared], 'hidden drawable');
        updateBoth(pair, 'updateDrawableVisible', main, true);
        updateBoth(pair, 'updateDrawableScale', main, [0, 0]);
        compare(a, b, [main, shared], 'zero scale', false);
        updateBoth(pair, 'updateDrawableScale', main, [100, 100]);
        for (const item of pair) item.renderer.updateBitmapSkin(bitmapIds[0], image(0, true), 1, [16, 12]);
        compare(a, b, [main, shared], 'fully transparent empty hull');
        assert(b.renderer._getConvexHullPointsForDrawable(main).length === 0, 'Transparent bitmap hull is not empty');
        assert(b.renderer._localHullCache.get(b.renderer._allSkins[bitmapIds[0]],
            b.renderer._allSkins[bitmapIds[0]]._silhouette.hullRevision, 32, 24) !== undefined, 'Empty hull was not retained');
        for (const item of pair) await item.renderer.updateSVGSkin(bitmapIds[0], svg(0), [32, 24]);
        await Promise.all(pair.map(item => loaded(item.renderer._allSkins[bitmapIds[0]])));
        compare(a, b, [main, shared], 'bitmap to real SVG reskin');
        const decodedHull = staticReuse(b, main, shared, 'decoded SVG');
        const revisions = pair.map(item => item.renderer._allSkins[bitmapIds[0]]._silhouette.hullRevision);
        const widths = pair.map(item => item.renderer._allSkins[bitmapIds[0]]._silhouette._width);
        updateBoth(pair, 'updateDrawableScale', main, [800, 800]);
        compare(a, b, [main, shared], 'actual SVG higher MIP');
        for (let index = 0; index < pair.length; index++) {
            const silhouette = pair[index].renderer._allSkins[bitmapIds[0]]._silhouette;
            assert(silhouette.hullRevision !== revisions[index] && silhouette._width > widths[index], 'SVG test did not create a genuinely new higher MIP');
        }
        const mipHull = staticReuse(b, main, shared, 'higher SVG MIP');
        assert(mipHull !== decodedHull, 'Higher SVG MIP reused an old revision hull');
        updateBoth(pair, 'updateDrawableScale', main, [100, 100]);
        const beforeEditRevision = b.renderer._allSkins[bitmapIds[0]]._silhouette.hullRevision;
        for (const item of pair) await item.renderer.updateSVGSkin(bitmapIds[0], svg(1), [12, 18]);
        await Promise.all(pair.map(item => loaded(item.renderer._allSkins[bitmapIds[0]])));
        compare(a, b, [main, shared], 'same-size SVG edit and rotation center');
        const editedHull = staticReuse(b, main, shared, 'same-size SVG edit');
        assert(editedHull !== mipHull && b.renderer._allSkins[bitmapIds[0]]._silhouette.hullRevision !== beforeEditRevision,
            'Same-size SVG edit reused old silhouette/hull revision');
        const variantOne = svgOracle(a, main);
        report.svgVariantOracle = oracleMetadata(variantOne);
        const pending = pair.map(item => item.renderer.updateSVGSkin(bitmapIds[0], svg(0), [32, 24]));
        assert(pair.every(item => !item.renderer._allSkins[bitmapIds[0]]._svgImageLoaded), 'Pending SVG precondition was missed');
        compare(a, b, [main, shared], 'pending SVG fallback', false);
        await Promise.all(pending);
        await Promise.all(pair.map(item => loaded(item.renderer._allSkins[bitmapIds[0]])));
        const superseded = [];
        for (const item of pair) {
            superseded.push(item.renderer.updateSVGSkin(bitmapIds[0], svg(0), [32, 24]));
            superseded.push(item.renderer.updateSVGSkin(bitmapIds[0], svg(1), [12, 18]));
        }
        await Promise.all(superseded);
        await Promise.all(pair.map(item => loaded(item.renderer._allSkins[bitmapIds[0]])));
        compare(a, b, [main, shared], 'consecutive SVG last generation');
        // Collect both paths and all pixel differences before the first strong
        // oracle can throw. Earlier evidence stopped at the first hull delta.
        const finals = pair.map(item => svgOracle(item, main));
        report.svgFinalOracles = finals.map(oracleMetadata);
        report.svgFinalPixelDifferences = finals.map(final => byteDifference(final.pixels, variantOne.pixels));
        report.svgSameStateHullRuns = pair.map(item => Array.from({length: 8}, () => rawHull(item, main)));
        report.svgRepeatedIdenticalDecode = [];
        for (let repeat = 0; repeat < 3; repeat++) {
            for (const item of pair) await item.renderer.updateSVGSkin(bitmapIds[0], svg(1), [12, 18]);
            await Promise.all(pair.map(item => loaded(item.renderer._allSkins[bitmapIds[0]])));
            report.svgRepeatedIdenticalDecode.push(pair.map(item => {
                const snapshot = svgOracle(item, main);
                return {...oracleMetadata(snapshot), pixelDifferenceFromKnownVariant: byteDifference(snapshot.pixels, variantOne.pixels)};
            }));
        }
        // This observation is deliberately last: all original strong-oracle
        // inputs, same-state runs and decode-trace snapshots are already saved.
        // It cannot establish the earlier backend and is labelled by generation.
        report.svgDeferredSourceCanvasReadbacks = pair.map(item => {
            const skin = item.renderer._allDrawables[main].skin, canvas = skin._canvas;
            assert(canvas.width * canvas.height * 4 <= 65536, 'Deferred SVG canvas exceeds diagnostic limit');
            return {svgGeneration: skin._svgGeneration, width: canvas.width, height: canvas.height,
                rgba: byteSnapshot(skin._context.getImageData(0, 0, canvas.width, canvas.height).data),
                intrusiveReadback: true, occursAfterAllPrimaryOracleAndDecodeTraceSamples: true};
        });
        const knownVariantCase = {name: 'consecutive SVG known-variant oracle', assertionsBefore: report.assertions, passed: false};
        try {
            // Keep this strong oracle and its exact comparisons. A failure
            // remains a whole-run failure while independent later cases run.
            for (const final of finals) {
                equal(final.after, variantOne.after, 'Consecutive SVG oracle raster/MIP preconditions differ');
                equal(final.hull, variantOne.hull, 'Consecutive SVG update did not retain known final variant hull');
                equal(final.bounds, variantOne.bounds, 'Consecutive SVG update did not retain known final variant bounds');
                equalPixels(final.pixels, variantOne.pixels, 'Consecutive SVG update final variant pixel oracle');
            }
            knownVariantCase.passed = true;
        } catch (error) {
            if (!error || error.message !== 'Consecutive SVG update did not retain known final variant hull') throw error;
            report.failures.push({case: knownVariantCase.name, message: String(error && error.message || error),
                stack: String(error && error.stack || '').slice(0, 8192)});
            knownVariantCase.failureIndex = report.failures.length - 1;
        }
        knownVariantCase.assertions = report.assertions - knownVariantCase.assertionsBefore;
        report.cases.push(knownVariantCase);
        // Reset both paths to the same known bitmap before pen and capacity
        // cases. No unknown SVG geometry is carried into their prerequisites.
        for (const item of pair) item.renderer.updateBitmapSkin(bitmapIds[0], image(), 1, [16, 12]);
        compare(a, b, [main, shared], 'SVG to bitmap reskin');
        const penIds = pair.map(item => {const id = item.renderer.createPenSkin(); item.skins.add(id); return id;});
        const penDrawables = pair.map((item, index) => drawable(item, penIds[index], 'pen'));
        equal(penIds[0], penIds[1], 'Pen skin identity'); equal(penDrawables[0], penDrawables[1], 'Pen drawable identity');
        for (const item of pair) {
            item.renderer.penLine(penIds[0], {diameter: 7, color4f: [0, 0, 1, 1]}, -25, -20, 30, 25);
            item.renderer.penStamp(penIds[0], main);
        }
        compare(a, b, [main, shared, penDrawables[0]], 'dynamic pen line and stamp');
        for (const item of pair) item.renderer.penClear(penIds[0]);
        compare(a, b, [main, shared, penDrawables[0]], 'dynamic pen clear');
        // Eviction uses actual bitmap skins and the actual cache implementation.
        // One shared drawable prevents a large draw list or GL query workload.
        for (let index = 0; index < 1025; index++) {
            alive();
            const id = b.renderer.createBitmapSkin(new ImageData(new Uint8ClampedArray([255, 0, 0, 255]), 1, 1), 1, [0, 0]);
            b.skins.add(id); b.renderer.updateDrawableSkinId(shared, id);
            b.renderer._getConvexHullPointsForDrawable(shared);
        }
        assert(b.renderer._localHullCache.size === 1024, 'Actual browser cache did not enforce entry capacity');
        assert(b.renderer._localHullCache.pointCount <= 131072, 'Actual browser cache point bound exceeded');
        report.cases.push({name: 'actual 1025-skin entry eviction', entries: b.renderer._localHullCache.size,
            points: b.renderer._localHullCache.pointCount});
        for (const item of pair) {
            for (const [id, group] of item.drawables) item.renderer.destroyDrawable(id, group);
            item.drawables.clear();
            for (const id of item.skins) item.renderer.destroySkin(id);
            item.skins.clear();
        }
        assert(b.renderer._localHullCache.size === 0 && b.renderer._localHullCache.pointCount === 0, 'Destroyed skins retained cache entries/points');
        report.cases.push({name: 'destroy drawable/skin and empty cache'});
        report.completed = true; report.passed = report.failures.length === 0;
    }
    Promise.resolve().then(run).catch(error => {
        report.failures.push({message: String(error && error.message || error), stack: String(error && error.stack || '').slice(0,8192)});
    }).then(() => {
        for (const item of contexts) {
            try {
                for (const [id, group] of item.drawables) item.renderer.destroyDrawable(id, group);
                for (const id of item.skins) item.renderer.destroySkin(id);
                item.renderer.setLocalHullCacheEnabled(false);
                const extension = item.renderer.gl.getExtension('WEBGL_lose_context');
                if (extension) extension.loseContext();
            } catch (error) {report.passed = false; report.failures.push({cleanup: String(error)});}
            item.canvas.remove();
        }
        report.elapsedMs = performance.now() - started;
        chrome.webview.postMessage(JSON.stringify({kind: 'renderer.correctness.report', ...envelope, report}));
    });
    return {started: true};
})()
