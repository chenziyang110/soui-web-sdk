import test, {after} from 'node:test';
import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir, mkdtemp, copyFile, rm} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {resolveRendererProfile, rendererPatchDescriptors, expectedPatchRecords, assertRendererOriginals,
    applyRendererReplacements, loadRendererReplacements, verifyPatchedSources, verifySourcePaths,
    recipeSourceMembers, snapshotRecipeInputs, verifyRecipeInputsUnchanged, verifyShippedRecipe}
    from './renderer-profiles.mjs';
import {distInventory, verifySourceVendor, verifyRuntimeSourceSelection} from './provenance.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const pinned = path.join(here, 'test-fixtures/renderer-profiles');
const temporaryParent = path.resolve(tmpdir());
const temporary = await mkdtemp(path.join(temporaryParent, 'soui-renderer-profiles-'));
after(() => {
    assert.equal(path.dirname(path.resolve(temporary)), temporaryParent);
    assert.match(path.basename(temporary), /^soui-renderer-profiles-/);
    return rm(temporary, {recursive: true, force: true});
});
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const reducer = await readFile(path.join(here, 'targets-reducer.js'));
const reducerSha = sha(reducer.toString('utf8').replace(/\r\n/g, '\n'));
const guiProfile = 'local-hull-cache-gui-shared-v1';
const guiPaths = ['packages/scratch-gui/src/containers/sprite-selector-item.jsx',
    'packages/scratch-gui/src/components/sprite-selector/sprite-list.jsx',
    'packages/scratch-gui/src/lib/sprite-selector-store-context.jsx'];

async function sourceFixture () {
    const root = await mkdtemp(path.join(temporary, 'source-'));
    for (const [relative, source] of [
        ['packages/scratch-render/src/RenderWebGL.js', 'RenderWebGL.original.js'],
        ['packages/scratch-render/src/Silhouette.js', 'Silhouette.original.js'],
        ['packages/scratch-vm/src/engine/runtime.js', 'runtime.with-counters.js'],
        [guiPaths[0], 'sprite-selector-item.original.jsx'],
        [guiPaths[1], 'sprite-list.original.jsx']
    ]) {
        await mkdir(path.dirname(path.join(root, relative)), {recursive: true});
        await copyFile(path.join(pinned, source), path.join(root, relative));
    }
    const gui = path.join(root, 'packages/scratch-gui/src/reducers/targets.js');
    await mkdir(path.dirname(gui), {recursive: true});
    await writeFile(gui, reducer);
    return root;
}

test('profile enum and returned renderer descriptors are closed', () => {
    assert.equal(resolveRendererProfile(), 'stock');
    assert.equal(resolveRendererProfile('local-hull-cache-v1'), 'local-hull-cache-v1');
    assert.equal(resolveRendererProfile(guiProfile), guiProfile);
    for (const value of ['../stock', 'LOCAL-HULL-CACHE-V1', 'arbitrary', null, {}, '']) {
        assert.throws(() => resolveRendererProfile(value));
    }
    assert.equal(expectedPatchRecords('stock', reducerSha).length, 2);
    assert.equal(expectedPatchRecords('local-hull-cache-v1', reducerSha).length, 5);
    assert.equal(expectedPatchRecords(guiProfile, reducerSha).length, 8);
    const rows = rendererPatchDescriptors('local-hull-cache-v1');
    assert.equal(rows.length, 3);
    assert.equal(rows[2].beforeSha256, null);
    rows[0].afterSha256 = '0'.repeat(64);
    assert.notEqual(rendererPatchDescriptors('local-hull-cache-v1')[0].afterSha256, rows[0].afterSha256);
    const shared = rendererPatchDescriptors(guiProfile);
    assert.deepEqual(shared.slice(0, 3), rendererPatchDescriptors('local-hull-cache-v1'));
    assert.deepEqual(shared.slice(3).map(item => item.path), guiPaths);
    assert.equal(shared[5].beforeSha256, null);
    assert.deepEqual(rendererPatchDescriptors('local-hull-cache-v1').map(item => item.afterSha256), [
        '6d05af5925e2b0aa8e76adf131a703c6a3a30f83452e7012d5211265dae37731',
        '9ba6f39d38b25cbfb8686f0ca93926e72f48152f0f2d2b4cc950972048b05b71',
        '119d906641725593a9d47bd8a9dcb73970820a8e331e21af518b1be5f9d27695'
    ]);
});

test('driver rejects unknown profiles/options before any upstream build', () => {
    const script = path.join(here, 'build.mjs');
    for (const extra of [['--renderer-profile', 'unknown'], ['--patch-file', 'arbitrary.patch'],
        ['--renderer-profile', 'stock', '--renderer-profile', 'stock']]) {
        const result = spawnSync(process.execPath, [script, '--source-root',
            path.join(temporary, 'missing-source'), '--output', path.join(temporary, 'missing-output'), ...extra],
        {encoding: 'utf8', windowsHide: true});
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, /Unknown Scratch renderer profile|Usage:/);
        assert.equal(result.stdout, '');
    }
});

test('fixed replacement assets match reviewed r2 SHA and stock reads none', async () => {
    assert.deepEqual(await loadRendererReplacements('stock'), []);
    const rows = await loadRendererReplacements('local-hull-cache-v1');
    assert.equal(rows.length, 3);
    for (const row of rows) assert.equal(sha(row.text), row.afterSha256);
    const shared = await loadRendererReplacements(guiProfile);
    assert.equal(shared.length, 6);
    assert.deepEqual(shared.slice(0, 3), rows);
    for (const row of shared) assert.equal(sha(row.text), row.afterSha256);
});

test('new shared GUI profile accepts only its exact eight patches and cannot masquerade as an older profile', async () => {
    const root = await sourceFixture();
    await assertRendererOriginals(root, guiProfile);
    await applyRendererReplacements(root, guiProfile);
    const eight = expectedPatchRecords(guiProfile, reducerSha);
    await verifyPatchedSources(root, guiProfile, eight, reducerSha);
    for (const wrong of [eight.slice(0, 5), eight.slice(0, 7), [...eight, eight[7]],
        eight.map((row, index) => index === 7 ? {...row, beforeSha256: '0'.repeat(64)} : row),
        [...eight.slice(0, 7), {...eight[7], path: '../unregistered-context.jsx'}]]) {
        await assert.rejects(verifyPatchedSources(root, guiProfile, wrong, reducerSha), /registered profile closure/);
    }
    const gui = (await loadRendererReplacements(guiProfile)).slice(3);
    for (const oldProfile of ['stock', 'local-hull-cache-v1']) {
        const old = await sourceFixture();
        await applyRendererReplacements(old, oldProfile);
        for (const item of gui) {
            const destination = path.join(old, item.path);
            await mkdir(path.dirname(destination), {recursive: true});
            await writeFile(destination, item.text);
        }
        await assert.rejects(verifyPatchedSources(old, oldProfile, expectedPatchRecords(oldProfile, reducerSha), reducerSha),
            /source mismatch|already exists/);
        for (const item of gui.slice(0, 2)) {
            const hidden = await sourceFixture();
            await applyRendererReplacements(hidden, oldProfile);
            await writeFile(path.join(hidden, item.path), item.text);
            // No added context exists: this must reject the changed existing
            // GUI source itself, rather than only the new path boundary.
            await assert.rejects(verifyPatchedSources(hidden, oldProfile,
                expectedPatchRecords(oldProfile, reducerSha), reducerSha), /source mismatch/);
        }
    }
});

test('GUI originals and new context are checked before replacement or overwrite', async () => {
    const root = await sourceFixture();
    await writeFile(path.join(root, guiPaths[0]), 'changed original');
    await assert.rejects(applyRendererReplacements(root, guiProfile), /source mismatch/);
    const existing = await sourceFixture(), context = path.join(existing, guiPaths[2]);
    await mkdir(path.dirname(context), {recursive: true});
    await writeFile(context, 'keep');
    await assert.rejects(applyRendererReplacements(existing, guiProfile), /already exists/);
    assert.equal(await readFile(context, 'utf8'), 'keep');
    assert.equal(sha((await readFile(path.join(existing, 'packages/scratch-render/src/RenderWebGL.js'), 'utf8')).replace(/\r\n/g, '\n')),
        'f31804dba07683b38677d5be88afe8ee6f35906ae9ce763afd9d19bede23c7a2');
});

test('actual pinned source copies accept only exact two/five patch closures', async () => {
    const root = await sourceFixture();
    const stock = expectedPatchRecords('stock', reducerSha);
    await verifyPatchedSources(root, 'stock', stock, reducerSha);
    await assertRendererOriginals(root, 'local-hull-cache-v1');
    await applyRendererReplacements(root, 'local-hull-cache-v1');
    const five = expectedPatchRecords('local-hull-cache-v1', reducerSha);
    await verifyPatchedSources(root, 'local-hull-cache-v1', five, reducerSha);
    for (const wrong of [five.slice(0, 4), [...five, {...five[0], path: '../escape'}], [...five, five[4]],
        five.map((row, i) => i === 4 ? {...row, beforeSha256: '0'.repeat(64)} : row)]) {
        await assert.rejects(verifyPatchedSources(root, 'local-hull-cache-v1', wrong, reducerSha),
            /registered profile closure/);
    }
    await assert.rejects(verifyPatchedSources(root, 'stock', stock, reducerSha), /source mismatch/);
    await writeFile(path.join(root, five[4].path), 'changed helper');
    await assert.rejects(verifyPatchedSources(root, 'local-hull-cache-v1', five, reducerSha), /source differs/);
});

test('original mutations and pre-existing new source are rejected without overwrite', async () => {
    const first = await sourceFixture();
    await writeFile(path.join(first, 'packages/scratch-render/src/RenderWebGL.js'), 'wrong original');
    await assert.rejects(assertRendererOriginals(first, 'local-hull-cache-v1'), /source mismatch/);
    const second = await sourceFixture();
    const helper = path.join(second, 'packages/scratch-render/src/LocalHullCache.js');
    await writeFile(helper, 'keep');
    await assert.rejects(applyRendererReplacements(second, 'local-hull-cache-v1'), /already exists/);
    assert.equal(await readFile(helper, 'utf8'), 'keep');
});

test('pinned source membership includes new untracked LocalHullCache explicitly', async () => {
    const paths = JSON.parse(await readFile(path.join(pinned, 'source-paths.json'), 'utf8'));
    assert.equal(paths.length, 3411);
    assert.doesNotThrow(() => verifySourcePaths(paths, 'stock'));
    const extra = 'packages/scratch-render/src/LocalHullCache.js';
    assert.ok(!paths.includes(extra));
    assert.doesNotThrow(() => verifySourcePaths([...paths, extra], 'local-hull-cache-v1'));
    for (const wrong of [paths, [...paths, extra, extra], [...paths.slice(1), extra, 'unregistered.js']]) {
        assert.throws(() => verifySourcePaths(wrong, 'local-hull-cache-v1'), /source member closure/);
    }
    const shared = [...paths, extra, guiPaths[2]];
    assert.equal(shared.length, 3413);
    assert.equal(verifySourcePaths(shared, guiProfile), '541c6e1309389b3a9229cdc41bcfe84785c112b4a976ab2e953478983160e0e4');
    for (const wrong of [[...paths, extra], [...shared, guiPaths[2]], [...paths, extra, 'packages/scratch-gui/src/lib/unknown.jsx']]) {
        assert.throws(() => verifySourcePaths(wrong, guiProfile), /source member closure/);
    }
    assert.throws(() => verifySourcePaths(shared, 'local-hull-cache-v1'), /source member closure/);
});

test('shipped recipe must match current fixed sources with no missing or extra members', async () => {
    assert.equal(recipeSourceMembers('stock').length, 7);
    assert.equal(recipeSourceMembers('local-hull-cache-v1').length, 10);
    assert.equal(recipeSourceMembers(guiProfile).length, 13);
    const root = await mkdtemp(path.join(temporary, 'recipe-'));
    const inputs = await snapshotRecipeInputs('local-hull-cache-v1');
    for (const input of inputs) {
        const to = path.join(root, input.path);
        await mkdir(path.dirname(to), {recursive: true});
        await writeFile(to, input.bytes);
    }
    const inventory = await distInventory(root);
    await verifyShippedRecipe(root, 'local-hull-cache-v1', inventory);
    await assert.rejects(verifyShippedRecipe(root, 'local-hull-cache-v1', inventory.slice(1)), /member closure/);
    await writeFile(path.join(root, 'unknown.js'), 'x');
    await assert.rejects(verifyShippedRecipe(root, 'local-hull-cache-v1', await distInventory(root)), /member closure/);
    await writeFile(path.join(root, inputs[0].path), 'different recipe');
    await assert.rejects(verifyShippedRecipe(root, 'local-hull-cache-v1', inventory), /recipe source differs/);
});

test('shared GUI shipped recipe includes all three reviewed GUI assets and rejects missing/mixed closures', async () => {
    const root = await mkdtemp(path.join(temporary, 'shared-recipe-'));
    const inputs = await snapshotRecipeInputs(guiProfile);
    assert.equal(inputs.length, 13);
    assert.equal(inputs.filter(input => input.path.includes('/gui/shared-sprite-subscriptions-v1/')).length, 3);
    for (const input of inputs) {
        const destination = path.join(root, input.path);
        await mkdir(path.dirname(destination), {recursive: true});
        await writeFile(destination, input.bytes);
    }
    const inventory = await distInventory(root);
    await verifyShippedRecipe(root, guiProfile, inventory);
    await assert.rejects(verifyShippedRecipe(root, 'local-hull-cache-v1', inventory), /member closure/);
    await assert.rejects(verifyShippedRecipe(root, guiProfile,
        inventory.filter(input => !input.path.endsWith('/sprite-selector-store-context.jsx'))), /member closure/);
    const changed = inputs.find(input => input.path.endsWith('/sprite-selector-store-context.jsx'));
    await writeFile(path.join(root, changed.path), 'modified context asset');
    await assert.rejects(verifyShippedRecipe(root, guiProfile, inventory), /recipe source differs/);
});

test('copied seven/ten-file recipe modules reproduce old branches without needing new GUI assets', async () => {
    for (const profile of ['stock', 'local-hull-cache-v1']) {
        const root = await mkdtemp(path.join(temporary, 'copied-recipe-'));
        for (const input of await snapshotRecipeInputs(profile)) {
            const destination = path.join(root, input.path);
            await mkdir(path.dirname(destination), {recursive: true});
            await writeFile(destination, input.bytes);
        }
        const copied = await import(pathToFileURL(path.join(root, 'demos/scratch/upstream/renderer-profiles.mjs')).href);
        assert.equal(copied.recipeSourceMembers(profile).length, profile === 'stock' ? 7 : 10);
        assert.equal((await copied.loadRendererReplacements(profile)).length, profile === 'stock' ? 0 : 3);
        const source = await sourceFixture();
        await copied.applyRendererReplacements(source, profile);
        await copied.verifyPatchedSources(source, profile, copied.expectedPatchRecords(profile, reducerSha), reducerSha);
        await assert.rejects(copied.loadRendererReplacements(guiProfile), /ENOENT/);
    }
});

test('recipe modification during build is not accepted as the initial driver', async () => {
    const root = await mkdtemp(path.join(temporary, 'changed-'));
    const source = path.join(root, 'driver.js');
    const bytes = Buffer.from('original');
    await writeFile(source, bytes);
    const input = {path: 'upstream/build.mjs', source, bytes};
    await verifyRecipeInputsUnchanged([input]);
    await writeFile(source, 'changed');
    await assert.rejects(verifyRecipeInputsUnchanged([input]), /changed during source build/);
});

test('legacy output shape is rejected before reading any build receipt', async () => {
    const root = await mkdtemp(path.join(temporary, 'legacy-'));
    // Deliberately no source-build.json: this is a boundary fixture, not a build result.
    for (const name of ['dist', 'sources']) await mkdir(path.join(root, name));
    for (const name of ['LICENSE', 'package-lock.json']) await writeFile(path.join(root, name), 'fixture');
    await assert.rejects(verifySourceVendor(root), /unexpected or missing members/);
});

test('SourceVendor rejects a new profile relabeled onto an old patch record before reading missing build payloads', async () => {
    const root = await mkdtemp(path.join(temporary, 'mixed-vendor-'));
    // Deliberately incomplete boundary fixture, never a synthesized build result.
    for (const name of ['dist', 'sources', 'recipe']) await mkdir(path.join(root, name));
    for (const name of ['LICENSE', 'package-lock.json']) await writeFile(path.join(root, name), 'boundary fixture');
    await writeFile(path.join(root, 'source-build.json'), JSON.stringify({rendererProfile: guiProfile,
        patches: expectedPatchRecords('local-hull-cache-v1', reducerSha)}));
    await assert.rejects(verifySourceVendor(root), /exact registered profile closure/);
});

async function runtimeFixture(profile) {
    const root = await mkdtemp(path.join(temporary, 'runtime-'));
    await mkdir(path.join(root, 'scratch-legal'));
    const bundle = Buffer.from('runtime selection boundary fixture; not a Scratch build');
    const record = {rendererProfile: profile, patches: expectedPatchRecords(profile, reducerSha),
        sourceCommit: 'b4b934aaf6641a539325a971249234f4a50a2f03', sourceLockSha256: 'b'.repeat(64),
        upstreamSourceVersion: '15.2.0', referenceArtifactVersion: '15.3.0', bundleSha256: sha(bundle)};
    const identity = {record, recordSha256: 'a'.repeat(64)};
    const runtime = {...structuredClone(record), schema: 'soui.scratch-runtime-source/v1', fullSourceRecordSha256: identity.recordSha256};
    const html = `<html lang="zh-CN" data-scratch-source-commit="${record.sourceCommit}" data-scratch-source-version="15.2.0" data-scratch-renderer-profile="${profile}"><body></body></html>`;
    await writeFile(path.join(root, 'scratch-gui-standalone.js'), bundle);
    await writeFile(path.join(root, 'scratch-legal/source-build.json'), JSON.stringify(runtime));
    await writeFile(path.join(root, 'index.html'), html);
    return {root, identity, runtime, html};
}

test('runtime selection accepts exact existing and new source profiles, and preserves reference-only packaging', async () => {
    for (const profile of ['stock', 'local-hull-cache-v1', guiProfile]) {
        const fixture = await runtimeFixture(profile);
        await verifyRuntimeSourceSelection(fixture.root, fixture.identity);
        await writeFile(path.join(fixture.root, 'index.html'), '<!-- prolog -->\n<!doctype html>\n' +
            fixture.html.replace('<body>', '<body><script>const text="<html fake>";</script>'));
        await verifyRuntimeSourceSelection(fixture.root, fixture.identity);
        await assert.rejects(verifyRuntimeSourceSelection(fixture.root, null), /explicit SourceVendor/);
    }
    const reference = await mkdtemp(path.join(temporary, 'reference-'));
    await verifyRuntimeSourceSelection(reference, null);
});

test('runtime selection rejects changed or missing profile/patch metadata without relabeling old records', async () => {
    for (const mutate of [record => {record.rendererProfile = 'local-hull-cache-v1';},
        record => {delete record.rendererProfile;},
        record => {record.patches = expectedPatchRecords('local-hull-cache-v1', reducerSha);},
        record => {record.patches[7].afterSha256 = '0'.repeat(64);}]) {
        const fixture = await runtimeFixture(guiProfile); mutate(fixture.runtime);
        await writeFile(path.join(fixture.root, 'scratch-legal/source-build.json'), JSON.stringify(fixture.runtime));
        await assert.rejects(verifyRuntimeSourceSelection(fixture.root, fixture.identity), /runtime does not match/);
    }
});

test('runtime selection checks the unique actual HTML identity, not a stale label plus comment or duplicate', async () => {
    for (const change of [
        html => html.replace(guiProfile, 'local-hull-cache-v1'),
        html => html.replace(` data-scratch-renderer-profile="${guiProfile}"`, ''),
        html => html.replace(`data-scratch-renderer-profile="${guiProfile}"`,
            `data-scratch-renderer-profile="local-hull-cache-v1" data-scratch-renderer-profile="${guiProfile}"`),
        html => `<!-- data-scratch-renderer-profile="${guiProfile}" -->` + html.replace(guiProfile, 'local-hull-cache-v1'),
        html => `<!-- ${html} --><body>No explicit root</body>`,
        html => `<script>const fake=${JSON.stringify(html)};</script><body>No explicit root</body>`,
        html => html.replace('<html ', '<html-custom ')
    ]) {
        const fixture = await runtimeFixture(guiProfile);
        await writeFile(path.join(fixture.root, 'index.html'), change(fixture.html));
        await assert.rejects(verifyRuntimeSourceSelection(fixture.root, fixture.identity), /HTML does not identify/);
    }
});
