import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { prepareDOM, deferred, flush, installed, nativeFixture } from './helpers.mjs';

export async function runReactTests(profile) {
  const dom = prepareDOM();
  const require = createRequire(new URL('package.json', profile));
  const React = require('react');
  const { createRoot } = require('react-dom/client');
  const { createElement: h, act, StrictMode, Suspense, startTransition } = React;
  const sdk = await installed(profile, '@soui/native-sdk');
  const { useSouiGuiNode } = await installed(profile, '@soui/gui-react');
  test.after(() => dom.window.close());
  function fixture(t, gui = sdk.createSouiGui()) {
    const container = document.createElement('main'); document.body.append(container);
    const root = createRoot(container);
    let alive = true;
    const unmount = async () => { if (alive) { alive = false; await act(async () => root.unmount()); } };
    t.after(async () => { await unmount(); gui.dispose(); container.remove(); });
    return { gui, container, root, unmount, render: async value => { await act(async () => root.render(value)); } };
  }
  function Button({ gui, value = 0, name = 'Approve', id = 'order:42:approve', actions, expose, as = 'button', disabled = false }) {
    const ref = useSouiGuiNode(gui, { id, role: 'button', name, expose,
      actions: actions || { activate: () => value }, state: () => ({ state: { revision: value } }) });
    return h(as, { ref, disabled }, 'private DOM text');
  }
  const node = gui => gui.snapshot().nodes[0];

  test(`React ${React.version}: StrictMode registrations and teardown borrow the shared GUI`, async t => {
    const f = fixture(t);
    await f.render(h(StrictMode, null, h(Button, { gui: f.gui })));
    assert.equal(f.gui.snapshot().nodes.length, 1);
    assert.equal(f.container.firstChild.isConnected, true);
    assert.equal(await f.gui.perform({ id: node(f.gui).id, action: 'activate' }), 0);
    await f.unmount();
    assert.deepEqual(f.gui.snapshot().nodes, []);
    assert.equal(f.gui.disposed, false);
  });

  test('React committed callbacks and state update without removing an existing observation', async t => {
    const f = fixture(t);
    await f.render(h(Button, { gui: f.gui, value: 1 }));
    const wait = f.gui.waitFor({ id: node(f.gui).id, state: { revision: 2 }, timeoutMs: 1000 });
    await f.render(h(Button, { gui: f.gui, value: 2 }));
    assert.equal((await wait).state.revision, 2);
    assert.equal(await f.gui.perform({ id: node(f.gui).id, action: 'activate' }), 2);
  });

  test('React suspended uncommitted renders cannot replace the last committed callback', async t => {
    const f = fixture(t), pending = deferred();
    function Choice({ value }) {
      const ref = useSouiGuiNode(f.gui, { id: 'choice', role: 'button', name: 'Choice', actions: { choose: () => value } });
      if (value === 2) throw pending.promise;
      return h('button', { ref }, 'Choice');
    }
    const tree = value => h(Suspense, { fallback: h('p', null, 'Loading') }, h(Choice, { value }));
    await f.render(tree(1));
    await act(async () => startTransition(() => f.root.render(tree(2))));
    assert.equal(await f.gui.perform({ id: 'choice', action: 'choose' }), 1);
    await f.render(tree(3));
    assert.equal(await f.gui.perform({ id: 'choice', action: 'choose' }), 3);
    await act(async () => pending.resolve());
  });

  test('React structural changes reject old work and protect an id reused by a new generation', async t => {
    const f = fixture(t), old = deferred();
    await f.render(h(Button, { gui: f.gui, actions: { activate: () => old.promise } }));
    const running = f.gui.perform({ id: node(f.gui).id, action: 'activate' });
    const removed = assert.rejects(running, { code: 'E_GUI_REMOVED' });
    await flush();
    await f.render(h(Button, { gui: f.gui, name: 'Replaced', actions: { activate: () => 'new' } }));
    await removed;
    old.resolve('old'); await flush();
    assert.equal(node(f.gui).busy, false);
    assert.equal(await f.gui.perform({ id: node(f.gui).id, action: 'activate' }), 'new');
  });

  test('React DOM ref replacement and action-name changes each remove the old observation', async t => {
    const f = fixture(t);
    await f.render(h(Button, { gui: f.gui }));
    let removed = assert.rejects(f.gui.waitFor({ id: node(f.gui).id, state: { impossible: true } }), { code: 'E_GUI_REMOVED' });
    const oldElement = f.container.firstChild;
    await f.render(h(Button, { gui: f.gui, as: 'div' })); await removed;
    assert.notEqual(f.container.firstChild, oldElement);
    removed = assert.rejects(f.gui.waitFor({ id: node(f.gui).id, state: { impossible: true } }), { code: 'E_GUI_REMOVED' });
    await f.render(h(Button, { gui: f.gui, as: 'div', actions: { other: () => null } })); await removed;
    assert.deepEqual(node(f.gui).actions, ['other']);
  });

  test('React stable business ids survive list reorder without cancelling observations', async t => {
    const f = fixture(t);
    const tree = (ids, value) => h('div', null, ids.map(id => h(Button, { gui: f.gui, key: id, id: `row:${id}`, value })));
    await f.render(tree([1, 2], 0));
    const wait = f.gui.waitFor({ id: 'row:1', state: { revision: 1 }, timeoutMs: 1000 });
    await f.render(tree([2, 1], 1));
    assert.equal((await wait).id, 'row:1');
    assert.deepEqual(f.gui.snapshot().nodes.map(value => value.id), ['row:1', 'row:2']);
  });

  test('React changing the borrowed GUI removes only this binding from the old registry', async t => {
    const f = fixture(t), next = sdk.createSouiGui(); t.after(() => next.dispose());
    await f.render(h(Button, { gui: f.gui }));
    const removed = assert.rejects(f.gui.waitFor({ id: node(f.gui).id, state: { impossible: true } }), { code: 'E_GUI_REMOVED' });
    await f.render(h(Button, { gui: next })); await removed;
    assert.deepEqual(f.gui.snapshot().nodes, []);
    assert.equal(next.snapshot().nodes.length, 1);
    assert.equal(f.gui.disposed, false);
  });

  test('React duplicate ids reach the error boundary rather than replacing another registration', async t => {
    const f = fixture(t), errors = [];
    class Boundary extends React.Component {
      state = { failed: false };
      static getDerivedStateFromError() { return { failed: true }; }
      componentDidCatch(error) { errors.push(error); }
      render() { return this.state.failed ? null : this.props.children; }
    }
    const original = console.error;
    // React 18 logs caught errors as well as invoking the boundary.
    console.error = (...args) => { if (!args.some(value => String(value).includes('E_GUI_DUPLICATE_ID') ||
      String(value).includes('This GUI id') || String(value).includes('The above error occurred'))) original(...args); };
    try { await f.render(h(Boundary, null, h(Button, { gui: f.gui }), h(Button, { gui: f.gui }))); }
    finally { console.error = original; }
    assert.ok(errors.some(error => error.code === 'E_GUI_DUPLICATE_ID'));
    assert.equal(f.gui.disposed, false);
    assert.deepEqual(f.gui.snapshot().nodes, []);
  });

  test('React exposes only explicit projections and expose=false removes the registration', async t => {
    const f = fixture(t);
    function Private({ expose }) {
      const ref = useSouiGuiNode(f.gui, { id: 'private', role: 'textbox', name: 'Safe label', expose,
        actions: { submit: () => null } });
      return h('input', { ref, type: 'password', defaultValue: 'DOM-SECRET', 'data-private': 'PROP-SECRET' });
    }
    await f.render(h(Private, { expose: false })); assert.deepEqual(f.gui.snapshot().nodes, []);
    await f.render(h(Private, { expose: true }));
    assert.deepEqual(node(f.gui).state, {});
    assert.equal(JSON.stringify(f.gui.snapshot()).includes('SECRET'), false);
    const removed = assert.rejects(f.gui.waitFor({ id: 'private', state: { ready: true } }), { code: 'E_GUI_REMOVED' });
    await f.render(h(Private, { expose: false })); await removed;
    assert.deepEqual(f.gui.snapshot().nodes, []);
  });

  test('React native denial propagates without retry or a synthesized user gesture', async t => {
    const f = fixture(t), native = nativeFixture(t, sdk);
    await f.render(h(Button, { gui: f.gui, disabled: true, actions: { activate: () => native.client.call('app.denied', {}) } }));
    await assert.rejects(f.gui.perform({ id: node(f.gui).id, action: 'activate' }), { code: 'E_GUI_DISABLED' });
    assert.deepEqual(native.methods, []);
    await f.render(h(Button, { gui: f.gui, actions: { activate: () => native.client.call('app.denied', {}) } }));
    await assert.rejects(f.gui.perform({ id: node(f.gui).id, action: 'activate' }), { code: 'E_CAPABILITY_DENIED' });
    assert.deepEqual(native.methods, ['app.denied']);
    assert.deepEqual(native.requests[0].params, {});
    assert.deepEqual(Object.keys(native.requests[0]).sort(), ['method', 'origin', 'params', 'requestId', 'sessionId', 'timeoutMs', 'token']);
  });

  test('React timeout and unmount do not cancel native work; cancellation is an explicit action', async t => {
    const f = fixture(t), native = nativeFixture(t, sdk);
    const actions = { activate: () => native.client.call('app.work', {}), cancel: () => native.client.cancel('task-1') };
    await f.render(h(Button, { gui: f.gui, actions }));
    await assert.rejects(f.gui.perform({ id: node(f.gui).id, action: 'activate', timeoutMs: 10 }), { code: 'E_TIMEOUT' });
    assert.equal(node(f.gui).busy, true);
    await f.unmount();
    assert.deepEqual(native.methods, ['app.work']);
    native.work.resolve({ completed: true }); await flush();
    // A separate live registration may deliberately expose cancellation; removal never does.
    const g = fixture(t, f.gui);
    await g.render(h(Button, { gui: f.gui, actions: { cancel: actions.cancel } }));
    assert.deepEqual(await f.gui.perform({ id: node(f.gui).id, action: 'cancel' }), { taskId: 'task-1', state: 'running', cancelRequested: true });
    assert.deepEqual(native.methods, ['app.work', 'task.cancel']);
  });
}
