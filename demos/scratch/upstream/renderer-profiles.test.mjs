import test, {after} from 'node:test';
import assert from 'node:assert/strict';
import {readFile, writeFile, mkdir, mkdtemp, copyFile, rm} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {resolveRendererProfile, rendererPatchDescriptors, expectedPatchRecords, assertRendererOriginals,
    applyRendererReplacements, loadRendererReplacements, verifyPatchedSources, verifySourcePaths,
    recipeSourceMembers, snapshotRecipeInputs, verifyRecipeInputsUnchanged, verifyShippedRecipe}
    from './renderer-profiles.mjs';
import {distInventory, verifySourceVendor} from './provenance.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const pinned = path.join(here, 'test-fixtures/renderer-profiles');
const temporary = await mkdtemp(path.join(tmpdir(), 'soui-renderer-profiles-'));
after(() => rm(temporary, {recursive: true, force: true}));
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
const reducer = await readFile(path.join(here, 'targets-reducer.js'));
const reducerSha = sha(reducer.toString('utf8').replace(/\r\n/g, '\n'));

async function sourceFixture () {
    const root = await mkdtemp(path.join(temporary, 'source-'));
    for (const [relative, source] of [
        ['packages/scratch-render/src/RenderWebGL.js', 'RenderWebGL.original.js'],
        ['packages/scratch-render/src/Silhouette.js', 'Silhouette.original.js'],
        ['packages/scratch-vm/src/engine/runtime.js', 'runtime.with-counters.js']
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
    for (const value of ['../stock', 'LOCAL-HULL-CACHE-V1', 'arbitrary', null, {}, '']) {
        assert.throws(() => resolveRendererProfile(value));
    }
    assert.equal(expectedPatchRecords('stock', reducerSha).length, 2);
    assert.equal(expectedPatchRecords('local-hull-cache-v1', reducerSha).length, 5);
    const rows = rendererPatchDescriptors('local-hull-cache-v1');
    assert.equal(rows.length, 3);
    assert.equal(rows[2].beforeSha256, null);
    rows[0].afterSha256 = '0'.repeat(64);
    assert.notEqual(rendererPatchDescriptors('local-hull-cache-v1')[0].afterSha256, rows[0].afterSha256);
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
});

test('shipped recipe must match current fixed sources with no missing or extra members', async () => {
    assert.equal(recipeSourceMembers('stock').length, 7);
    assert.equal(recipeSourceMembers('local-hull-cache-v1').length, 10);
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
