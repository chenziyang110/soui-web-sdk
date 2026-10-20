'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');
const executable = process.env.SOUI_DESKTOP_CONTRACT_EXECUTABLE || process.argv[2];
assert.ok(executable, 'Pass the desktop contract executable to load the actual generated preload');
const generated = spawnSync(executable, ['--preload'], { encoding: 'utf8', maxBuffer: 1024 * 1024 });
assert.equal(generated.status, 0, generated.stderr || generated.error?.message);
const preload = generated.stdout;
const token = 'test-navigation-token';

// VM transport only: this exercises generated JavaScript, not a native engine.
function fixture({ transport = 'chrome', subframe = false, navigationToken = token } = {}) {
  const posts = [], timers = new Map(), events = new Map();
  let nextTimer = 0;
  const window = { addEventListener: (name, callback) => events.set(name, callback) };
  window.top = subframe ? {} : window;
  const postMessage = text => posts.push(JSON.parse(text));
  if (transport === 'chrome') window.chrome = { webview: { postMessage } };
  if (transport === 'webkit') window.webkit = { messageHandlers: { soui: { postMessage } } };
  const context = vm.createContext({
    window, TextEncoder,
    setTimeout: (callback, ms) => { const id = ++nextTimer; timers.set(id, { callback, ms }); return id; },
    clearTimeout: id => timers.delete(id),
  });
  let source = preload;
  if (navigationToken !== token) {
    const generated = spawnSync(executable, ['--preload', navigationToken], { encoding: 'utf8' });
    assert.equal(generated.status, 0, generated.stderr || generated.error?.message);
    source = generated.stdout;
  }
  vm.runInContext(source, context);
  return {
    window, posts, timers,
    run: code => vm.runInContext(code, context),
    deliver: value => window.__souiDesktopDeliver({ v: 1, token: navigationToken, ...value }),
    hide: () => events.get('pagehide')?.(),
  };
}

test('top-only injection exposes a frozen capability without a token property', () => {
  const child = fixture({ subframe: true });
  assert.equal(child.window.souiDesktop, undefined);
  assert.equal(child.window.__souiDesktopDeliver, undefined);
  const f = fixture();
  assert.equal(Object.isFrozen(f.window.souiDesktop), true);
  assert.deepEqual(Object.keys(f.window.souiDesktop).sort(), ['invoke', 'on', 'version']);
  assert.equal(Object.getOwnPropertyDescriptor(f.window, 'souiDesktop').writable, false);
  assert.equal(Object.getOwnPropertyDescriptor(f.window, '__souiDesktopDeliver').configurable, false);
});

test('Promise transport works through chrome and WebKit and ignores wrong tokens', async () => {
  for (const transport of ['chrome', 'webkit']) {
    const f = fixture({ transport });
    const pending = f.run("window.souiDesktop.invoke('echo', {text: '中文'})");
    assert.equal(f.posts.length, 1);
    assert.equal(f.posts[0].token, token);
    assert.equal(f.posts[0].params.text, '中文');
    f.deliver({ id: 1, token: 'stale-token', ok: true, result: 'wrong' });
    assert.equal(f.timers.size, 1);
    f.deliver({ id: 1, ok: true, result: { text: 'done' } });
    assert.deepEqual(await pending, { text: 'done' });
    assert.equal(f.timers.size, 0);
  }
});

test('native failure preserves its code and duplicate responses have no effect', async () => {
  const f = fixture();
  const pending = f.run("window.souiDesktop.invoke('fail')");
  const rejected = assert.rejects(pending, error => error.code === 'denied' && error.message === 'No access');
  f.deliver({ id: 1, ok: false, error: { code: 'denied', message: 'No access' } });
  await rejected;
  f.deliver({ id: 1, ok: true, result: 'late' });
  assert.equal(f.timers.size, 0);
});

test('invalid methods, missing native transport and cyclic parameters reject promptly', async () => {
  const f = fixture();
  await assert.rejects(f.run("window.souiDesktop.invoke('bad method')"), /Invalid method/);
  await assert.rejects(f.run("window.souiDesktop.invoke('x'.repeat(65))"), /Invalid method/);
  await assert.rejects(f.run("(()=>{const value={};value.self=value;return window.souiDesktop.invoke('echo',value)})()"));
  assert.equal(f.posts.length, 0);
  assert.equal(f.timers.size, 0);
  const missing = fixture({ transport: 'none' });
  await assert.rejects(missing.run("window.souiDesktop.invoke('echo')"), /unavailable/);
  assert.equal(missing.timers.size, 0);
});

test('request limit is UTF-8 bytes, including exact boundary and multibyte text', async () => {
  const large = fixture();
  await assert.rejects(large.run("window.souiDesktop.invoke('echo', '汉'.repeat(30000))"), /too large/);
  assert.equal(large.posts.length, 0);
  assert.equal(large.timers.size, 0);
  const base = Buffer.byteLength(JSON.stringify({ v: 1, token, id: 1, method: 'echo', params: '' }));
  const f = fixture();
  const pending = f.run(`window.souiDesktop.invoke('echo', 'a'.repeat(${65536 - base}))`);
  assert.equal(Buffer.byteLength(JSON.stringify(f.posts[0])), 65536);
  f.deliver({ id: 1, ok: true, result: true });
  assert.equal(await pending, true);
  const over = fixture();
  await assert.rejects(over.run(`window.souiDesktop.invoke('echo', 'a'.repeat(${65537 - base}))`), /too large/);
  assert.equal(over.posts.length, 0);
});

test('pending request capacity is bounded and timeout releases the slot', async () => {
  const f = fixture();
  const pending = Array.from({ length: 256 }, () => f.run("window.souiDesktop.invoke('echo')").catch(error => error.message));
  await assert.rejects(f.run("window.souiDesktop.invoke('echo')"), /limit reached/);
  assert.equal(f.posts.length, 256);
  for (const [id, timer] of [...f.timers]) {
    assert.equal(timer.ms, 30000);
    f.timers.delete(id);
    timer.callback();
  }
  assert.ok((await Promise.all(pending)).every(message => /timed out/.test(message)));
  const next = f.run("window.souiDesktop.invoke('echo')");
  assert.equal(f.posts.at(-1).id, 257);
  f.deliver({ id: 257, ok: true, result: true });
  await next;
});

test('pagehide rejects pending Promises and closes the document capability', async () => {
  const f = fixture();
  const pending = f.run("window.souiDesktop.invoke('echo')");
  const rejected = assert.rejects(pending, /replaced/);
  f.hide();
  await rejected;
  assert.equal(f.timers.size, 0);
  await assert.rejects(f.run("window.souiDesktop.invoke('echo')"), /closed/);
  assert.throws(() => f.run("window.souiDesktop.on('event',()=>{})"), /Invalid event listener/);
  f.deliver({ id: 1, ok: true, result: 'stale' });
  assert.equal(f.posts.length, 1);
});

test('events support unsubscribe and preserve JSON __proto__ as an own property', () => {
  const f = fixture();
  f.run("window.received=[];window.unsubscribe=window.souiDesktop.on('changed', v=>window.received.push(v))");
  const value = JSON.parse('{"__proto__":{"unexpected":true},"text":"quote\\"\\\\\\u2028"}');
  f.deliver({ event: 'changed', value });
  assert.equal(f.window.received.length, 1);
  assert.equal(Object.hasOwn(f.window.received[0], '__proto__'), true);
  assert.equal(f.window.received[0].unexpected, undefined);
  f.run('window.unsubscribe()');
  f.deliver({ event: 'changed', value: 2 });
  assert.equal(f.window.received.length, 1);
});

test('independent windows keep request IDs and responses scoped to their navigation token', async () => {
  const first = fixture({ navigationToken: 'first-window-token' });
  const second = fixture({ navigationToken: 'second-window-token' });
  const one = first.run("window.souiDesktop.invoke('echo')");
  const two = second.run("window.souiDesktop.invoke('echo')");
  assert.equal(first.posts[0].id, 1);
  assert.equal(second.posts[0].id, 1);
  assert.notEqual(first.posts[0].token, second.posts[0].token);
  first.deliver({ token: second.posts[0].token, id: 1, ok: true, result: 'wrong window' });
  second.deliver({ token: first.posts[0].token, id: 1, ok: true, result: 'wrong window' });
  assert.equal(first.timers.size, 1);
  assert.equal(second.timers.size, 1);
  first.deliver({ id: 1, ok: true, result: 'first' });
  second.deliver({ id: 1, ok: true, result: 'second' });
  assert.equal(await one, 'first');
  assert.equal(await two, 'second');
});

test('closing one window leaves another live and a new document rejects old responses', async () => {
  const first = fixture({ navigationToken: 'closing-window-token' });
  const second = fixture({ navigationToken: 'live-window-token' });
  const abandoned = first.run("window.souiDesktop.invoke('echo')");
  const rejection = assert.rejects(abandoned, /replaced/);
  first.hide();
  await rejection;
  const independent = second.run("window.souiDesktop.invoke('echo')");
  second.deliver({ id: 1, ok: true, result: 'still live' });
  assert.equal(await independent, 'still live');
  const replacement = fixture({ navigationToken: 'replacement-navigation-token' });
  const newRequest = replacement.run("window.souiDesktop.invoke('echo')");
  replacement.deliver({ token: first.posts[0].token, id: 1, ok: true, result: 'stale result' });
  assert.equal(replacement.timers.size, 1);
  replacement.deliver({ id: 1, ok: true, result: 'new result' });
  assert.equal(await newRequest, 'new result');
  assert.deepEqual(Object.keys(replacement.window.souiDesktop).sort(), ['invoke', 'on', 'version']);
});
