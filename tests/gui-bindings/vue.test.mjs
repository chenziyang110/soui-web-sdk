import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { prepareDOM, deferred, flush, installed, nativeFixture } from './helpers.mjs';

const dom = prepareDOM();
const require = createRequire(import.meta.url);
const { createApp, defineComponent, h, ref, shallowRef, nextTick, KeepAlive } = require('vue');
const sdk = await installed(new URL('./', import.meta.url), '@soui/native-sdk');
const { useSouiGuiNode } = await installed(new URL('./', import.meta.url), '@soui/gui-vue');
test.after(() => dom.window.close());
function fixture(t, component, gui = sdk.createSouiGui(), connected = true) {
  const container = document.createElement('main'); if (connected) document.body.append(container);
  const errors = [];
  const app = createApp(component);
  app.config.errorHandler = error => errors.push(error);
  let alive = true;
  const unmount = () => { if (alive) { alive = false; app.unmount(); } };
  t.after(() => { unmount(); gui.dispose(); container.remove(); });
  app.mount(container);
  return { gui, container, app, errors, unmount };
}
const node = gui => gui.snapshot().nodes[0];
function button(gui, source, element = () => 'button') {
  return defineComponent({ setup() {
    const bind = useSouiGuiNode(gui, source);
    return () => h(element(), { ref: bind }, 'private DOM text');
  } });
}
const options = (changes = {}) => ({ id: 'order:42:approve', role: 'button', name: 'Approve',
  actions: { activate: () => null }, ...changes });

test('Vue real connected mount/unmount borrows the shared GUI', async t => {
  const gui = sdk.createSouiGui();
  const f = fixture(t, button(gui, () => options()), gui);
  await nextTick();
  assert.equal(f.container.firstChild.isConnected, true);
  assert.equal(gui.snapshot().nodes.length, 1);
  assert.equal(await gui.perform({ id: node(gui).id, action: 'activate' }), null);
  f.unmount();
  assert.deepEqual(gui.snapshot().nodes, []);
  assert.equal(gui.disposed, false);
  assert.deepEqual(f.errors, []);
});

test('Vue post-render callbacks and state update without removing an observation', async t => {
  const gui = sdk.createSouiGui(), revision = ref(1);
  const Child = defineComponent({ props: ['revision'], setup(props) {
    const bind = useSouiGuiNode(gui, () => { const value = props.revision;
      return options({ actions: { activate: () => value }, state: () => ({ state: { revision: value } }) }); });
    return () => h('button', { ref: bind }, String(props.revision));
  } });
  const f = fixture(t, { setup: () => () => h(Child, { revision: revision.value }) }, gui);
  await nextTick();
  const wait = gui.waitFor({ id: node(gui).id, state: { revision: 2 }, timeoutMs: 1000 });
  revision.value = 2; await nextTick();
  assert.equal((await wait).state.revision, 2);
  assert.equal(await gui.perform({ id: node(gui).id, action: 'activate' }), 2);
  assert.deepEqual(f.errors, []);
});

test('Vue getter dependencies update even when the DOM itself has no reactive read', async t => {
  const gui = sdk.createSouiGui(), revision = ref(1);
  const f = fixture(t, button(gui, () => { const value = revision.value;
    return options({ actions: { activate: () => value } }); }), gui);
  await nextTick(); revision.value = 2; await nextTick();
  assert.equal(await gui.perform({ id: node(gui).id, action: 'activate' }), 2);
  assert.deepEqual(f.errors, []);
});

test('Vue KeepAlive removes inactive generations and re-registers on activation', async t => {
  const gui = sdk.createSouiGui(), active = ref(true), old = deferred();
  const Child = button(gui, () => options({ actions: { activate: () => old.promise } }));
  const Other = defineComponent({ setup: () => () => h('p', null, 'Other') });
  const f = fixture(t, { setup: () => () => h(KeepAlive, null, {
    default: () => active.value ? h(Child, { key: 'child' }) : h(Other, { key: 'other' })
  }) }, gui);
  await nextTick();
  const removed = assert.rejects(gui.perform({ id: node(gui).id, action: 'activate' }), { code: 'E_GUI_REMOVED' });
  await flush(); active.value = false; await nextTick(); await removed;
  assert.deepEqual(gui.snapshot().nodes, []);
  active.value = true; await nextTick();
  old.resolve('old'); await flush();
  assert.equal(gui.snapshot().nodes.length, 1);
  assert.equal(node(gui).busy, false);
  assert.deepEqual(f.errors, []);
});

test('Vue structural change isolates old asynchronous results from a reused id', async t => {
  const gui = sdk.createSouiGui(), old = deferred(), current = shallowRef(options({ actions: { activate: () => old.promise } }));
  const f = fixture(t, button(gui, () => current.value), gui);
  await nextTick();
  const removed = assert.rejects(gui.perform({ id: node(gui).id, action: 'activate' }), { code: 'E_GUI_REMOVED' });
  await flush(); current.value = options({ name: 'Replaced', actions: { activate: () => 'new' } });
  await nextTick(); await removed; old.resolve('old'); await flush();
  assert.equal(await gui.perform({ id: node(gui).id, action: 'activate' }), 'new');
  assert.equal(node(gui).busy, false);
  assert.deepEqual(f.errors, []);
});

test('Vue DOM ref and action-name replacement reject the old observation', async t => {
  const gui = sdk.createSouiGui(), tag = ref('button'), current = shallowRef(options());
  const f = fixture(t, button(gui, () => current.value, () => tag.value), gui);
  await nextTick();
  let removed = assert.rejects(gui.waitFor({ id: node(gui).id, state: { impossible: true } }), { code: 'E_GUI_REMOVED' });
  const previous = f.container.firstChild; tag.value = 'div'; await nextTick(); await removed;
  assert.notEqual(previous, f.container.firstChild);
  removed = assert.rejects(gui.waitFor({ id: node(gui).id, state: { impossible: true } }), { code: 'E_GUI_REMOVED' });
  current.value = options({ actions: { other: () => null } }); await nextTick(); await removed;
  assert.deepEqual(node(gui).actions, ['other']);
  assert.deepEqual(f.errors, []);
});

test('Vue keyed business ids survive list reorder with pending observations intact', async t => {
  const gui = sdk.createSouiGui(), rows = ref([1, 2]), revision = ref(0);
  const Child = defineComponent({ props: ['id', 'revision'], setup(props) {
    const bind = useSouiGuiNode(gui, () => { const value = props.revision;
      return options({ id: `row:${props.id}`, state: () => ({ state: { revision: value } }) }); });
    return () => h('button', { ref: bind }, String(props.id));
  } });
  const f = fixture(t, { setup: () => () => h('div', null, rows.value.map(id => h(Child, { key: id, id, revision: revision.value }))) }, gui);
  await nextTick();
  const wait = gui.waitFor({ id: 'row:1', state: { revision: 1 }, timeoutMs: 1000 });
  rows.value = [2, 1]; revision.value = 1; await nextTick();
  assert.equal((await wait).id, 'row:1');
  assert.deepEqual(gui.snapshot().nodes.map(value => value.id), ['row:1', 'row:2']);
  assert.deepEqual(f.errors, []);
});

test('Vue DOM/props remain private and expose=false removes registered observations', async t => {
  const gui = sdk.createSouiGui(), expose = ref(false);
  const Private = { setup() {
    const bind = useSouiGuiNode(gui, () => options({ expose: expose.value }));
    return () => h('input', { ref: bind, type: 'password', value: 'DOM-SECRET', 'data-private': 'PROP-SECRET' });
  } };
  const f = fixture(t, Private, gui); await nextTick();
  assert.deepEqual(gui.snapshot().nodes, []);
  expose.value = true; await nextTick();
  assert.deepEqual(node(gui).state, {});
  assert.equal(JSON.stringify(gui.snapshot()).includes('SECRET'), false);
  const removed = assert.rejects(gui.waitFor({ id: node(gui).id, state: { ready: true } }), { code: 'E_GUI_REMOVED' });
  expose.value = false; await nextTick(); await removed;
  assert.deepEqual(gui.snapshot().nodes, []);
  assert.deepEqual(f.errors, []);
});

test('Vue native denial propagates once and a disabled DOM element cannot execute', async t => {
  const gui = sdk.createSouiGui(), disabled = ref(true), native = nativeFixture(t, sdk);
  const Child = { setup() {
    const bind = useSouiGuiNode(gui, () => options({ actions: { activate: () => native.client.call('app.denied', {}) } }));
    return () => h('button', { ref: bind, disabled: disabled.value }, 'Approve');
  } };
  const f = fixture(t, Child, gui); await nextTick();
  await assert.rejects(gui.perform({ id: node(gui).id, action: 'activate' }), { code: 'E_GUI_DISABLED' });
  assert.deepEqual(native.methods, []);
  disabled.value = false; await nextTick();
  await assert.rejects(gui.perform({ id: node(gui).id, action: 'activate' }), { code: 'E_CAPABILITY_DENIED' });
  assert.deepEqual(native.methods, ['app.denied']);
  assert.deepEqual(native.requests[0].params, {});
  assert.deepEqual(Object.keys(native.requests[0]).sort(), ['method', 'origin', 'params', 'requestId', 'sessionId', 'timeoutMs', 'token']);
  assert.deepEqual(f.errors, []);
});

test('Vue timeout/unmount do not cancel native work; task.cancel is only explicit', async t => {
  const gui = sdk.createSouiGui(), native = nativeFixture(t, sdk);
  const f = fixture(t, button(gui, () => options({ actions: { activate: () => native.client.call('app.work', {}) } })), gui);
  await nextTick();
  await assert.rejects(gui.perform({ id: node(gui).id, action: 'activate', timeoutMs: 10 }), { code: 'E_TIMEOUT' });
  assert.equal(node(gui).busy, true);
  f.unmount(); assert.deepEqual(native.methods, ['app.work']);
  native.work.resolve({ completed: true }); await flush();
  const g = fixture(t, button(gui, () => options({ actions: { cancel: () => native.client.cancel('task-1') } })), gui);
  await nextTick();
  assert.deepEqual(await gui.perform({ id: node(gui).id, action: 'cancel' }), { taskId: 'task-1', state: 'running', cancelRequested: true });
  assert.deepEqual(native.methods, ['app.work', 'task.cancel']);
  assert.deepEqual([...f.errors, ...g.errors], []);
});

test('Vue duplicate ids and disconnected mount failures reach the framework error handler', async t => {
  const gui = sdk.createSouiGui(), Child = button(gui, () => options());
  const duplicate = fixture(t, { setup: () => () => h('div', null, [h(Child), h(Child)]) }, gui);
  await nextTick();
  assert.ok(duplicate.errors.some(error => error.code === 'E_GUI_DUPLICATE_ID'));
  assert.equal(gui.snapshot().nodes.length, 1);
  const isolated = sdk.createSouiGui();
  const disconnected = fixture(t, button(isolated, () => options()), isolated, false);
  await nextTick();
  assert.ok(disconnected.errors.some(error => error.code === 'E_INVALID_PARAMS'));
  assert.deepEqual(isolated.snapshot().nodes, []);
});

test('Vue invalid action updates remove old observers instead of accepting an array as a map', async t => {
  for (const changed of [{ actions: [() => null] }, { actions: { activate: 'not a function' } },
    { expose: 'false' }, { state: 'not a function' }]) {
    const gui = sdk.createSouiGui(), current = shallowRef(options());
    const f = fixture(t, button(gui, () => current.value), gui);
    await nextTick();
    const observation = gui.waitFor({ id: node(gui).id, state: { impossible: true }, timeoutMs: 1000 })
      .then(() => null, error => error);
    current.value = options(changed); await nextTick();
    assert.ok(f.errors.some(error => error instanceof TypeError));
    assert.deepEqual(gui.snapshot().nodes, []);
    assert.equal((await observation).code, 'E_GUI_REMOVED');
  }
});

test('Vue options getter failure withdraws the previous generation through the framework error handler', async t => {
  const gui = sdk.createSouiGui(), broken = ref(false);
  const f = fixture(t, button(gui, () => { if (broken.value) throw new Error('Expected getter failure'); return options(); }), gui);
  await nextTick();
  const observation = gui.waitFor({ id: node(gui).id, state: { impossible: true }, timeoutMs: 1000 }).then(() => null, error => error);
  broken.value = true; await nextTick();
  assert.ok(f.errors.some(error => error.message === 'Expected getter failure'));
  assert.deepEqual(gui.snapshot().nodes, []);
  assert.equal((await observation).code, 'E_GUI_REMOVED');
});
