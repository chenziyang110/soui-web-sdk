// Behavioral comparison using real React/Redux and the fixed upstream sources.
// SOUI_SCRATCH_TEST_ROOT points to an installed, read-only corresponding source tree.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {createRequire} = require('node:module');
const root = process.env.SOUI_SCRATCH_TEST_ROOT;
if (!root) throw Error('Set SOUI_SCRATCH_TEST_ROOT to an installed Scratch source tree');
const dependency = createRequire(path.join(root, 'package.json'));
const {JSDOM} = dependency('jsdom');
const dom = new JSDOM('<!doctype html><html><body></body></html>', {url: 'https://fixed-scratch.invalid'});
for (const key of ['window', 'document', 'navigator', 'HTMLElement', 'HTMLLIElement', 'SVGElement', 'Element', 'Node', 'MutationObserver'])
    Object.defineProperty(global, key, {value: dom.window[key], configurable: true, writable: true});
global.IS_REACT_ACT_ENVIRONMENT = true;
const React = dependency('react');
const Redux = dependency('redux');
const ReactRedux = dependency('react-redux');
const PropTypes = dependency('prop-types');
const {render, fireEvent, act, cleanup} = dependency('@testing-library/react');
const babel = dependency('@babel/core');
const gui = path.join(root, 'packages/scratch-gui');
const fixed = path.join(__dirname, 'shared-sprite-subscriptions-v1');
const targets = {
    item: path.join(gui, 'src/containers/sprite-selector-item.jsx'),
    list: path.join(gui, 'src/components/sprite-selector/sprite-list.jsx'),
    context: path.join(gui, 'src/lib/sprite-selector-store-context.jsx')
};
class FixtureVM {}
let clock = 10000;
const originalDateNow = Date.now;
Date.now = () => clock;

function fixture(candidate) {
    const cache = new Map();
    const metrics = {leafSelectors: 0, sharedSelectors: 0, rows: new Map(), actions: [], contextValues: []};
    const substitutes = new Map(candidate ? [
        [targets.item, path.join(fixed, 'sprite-selector-item.jsx')],
        [targets.list, path.join(fixed, 'sprite-list.jsx')],
        [targets.context, path.join(fixed, 'sprite-selector-store-context.jsx')]
    ] : []);
    function visual(props) {
        return React.createElement('div', {'data-row': props.name, className: props.className},
            React.createElement('img', {src: props.costumeURL || '', alt: props.name}),
            ...['Click', 'DeleteButtonClick', 'DuplicateButtonClick', 'ExportButtonClick', 'PointerEnter', 'PointerLeave'].map(name =>
                React.createElement('button', {key: name, 'data-action': name, onClick: props['on' + name]}, name)));
    }
    function box({children, componentRef, element, className}) {
        return React.createElement(element || 'div', {ref: componentRef, className}, children);
    }
    function confirmation({onOk, onCancel}) {
        return React.createElement('aside', null,
            React.createElement('button', {'data-confirm': 'ok', onClick: onOk}, 'confirm'),
            React.createElement('button', {'data-confirm': 'cancel', onClick: onCancel}, 'cancel'));
    }
    function instrumentConnect(filename) {
        return (mapState, ...args) => {
            let observed = mapState;
            if (filename === targets.item) observed = function (state, ownProps) {
                metrics.leafSelectors++; return mapState(state, ownProps);
            };
            if (filename === targets.context) observed = function (state) {
                metrics.sharedSelectors++; return mapState(state);
            };
            return ReactRedux.connect(observed, ...args);
        };
    }
    function load(filename) {
        if (cache.has(filename)) return cache.get(filename).exports;
        const module = {exports: {}};
        cache.set(filename, module);
        const source = fs.readFileSync(substitutes.get(filename) || filename, 'utf8');
        const transformed = babel.transformSync(source, {filename, babelrc: false, configFile: false,
            presets: [[dependency.resolve('@babel/preset-env'), {targets: {node: 'current'}, modules: 'commonjs'}],
                dependency.resolve('@babel/preset-react')]}).code;
        function local(specifier) {
            if (specifier === 'react-redux') return {...ReactRedux, connect: instrumentConnect(filename)};
            if (specifier === '@scratch/scratch-vm') return FixtureVM;
            if (specifier === '@scratch/scratch-svg-renderer') return {inlineSvgFonts: text => text};
            if (!specifier.startsWith('.')) return dependency(specifier);
            let target = path.resolve(path.dirname(filename), specifier);
            if (target.endsWith('.css')) return new Proxy({}, {get: (_, key) => key === '__esModule' ? false : String(key)});
            if (target.endsWith(path.join('src', 'gui-config'))) return {GUIStoragePropType: PropTypes.object};
            if (target.endsWith(path.join('components', 'sprite-selector-item', 'sprite-selector-item.jsx'))) return visual;
            if (target.endsWith(path.join('components', 'box', 'box.jsx'))) return box;
            if (target.endsWith(path.join('components', 'delete-confirmation-prompt', 'delete-confirmation-prompt.jsx'))) return confirmation;
            if (!path.extname(target)) target = ['.js', '.jsx'].map(extension => target + extension).find(value => fs.existsSync(substitutes.get(value) || value));
            if (!target) throw Error('Missing relative fixture dependency ' + specifier);
            return load(target);
        }
        new vm.Script('(function(require,module,exports){' + transformed + '\n})', {filename}).runInThisContext()(local, module, module.exports);
        return module.exports;
    }
    const ItemModule = load(targets.item);
    const Item = ItemModule.default;
    const originalRender = Item.WrappedComponent.prototype.render;
    Item.WrappedComponent.prototype.render = function () {
        metrics.rows.set(this.props.id, {...this.props});
        return originalRender.call(this);
    };
    const List = load(targets.list).default;
    const Context = candidate ? load(targets.context) : null;
    const hoverReducer = load(path.join(gui, 'src/reducers/hovered-target.js'));
    const dragReducer = load(path.join(gui, 'src/reducers/asset-drag.js'));
    function store() {
        const initial = {scratchGui: {config: {storage: {scratchStorage: {AssetType: {ImageVector: {}}}}}, vm: new FixtureVM(),
            assetDrag: {dragging: false, currentOffset: null, img: null}, hoveredTarget: {sprite: null, receivedBlocks: false}, targets: {}}, locales: {isRtl: false}};
        return Redux.createStore((state = initial, action) => {
            metrics.actions.push(action);
            if (action.type === 'fixture/targets') return {...state, scratchGui: {...state.scratchGui, targets: action.targets}};
            if (action.type === 'fixture/replace') return {...state, scratchGui: {...state.scratchGui, ...action.fields}};
            return {...state, scratchGui: {...state.scratchGui,
                assetDrag: dragReducer.default(state.scratchGui.assetDrag, action),
                hoveredTarget: hoverReducer.default(state.scratchGui.hoveredTarget, action)}};
        });
    }
    return {metrics, Item, List, Context, load, store, hoverReducer, dragReducer};
}
const asset = id => ({assetId: id, assetType: {}, encodeDataURI: () => 'data:image/x-test,' + id});
const sprite = (id, value = asset('a')) => ({id, name: 'sprite-' + id, order: Number(id) || 0,
    costume: {asset: value, name: 'costume-' + id, bitmapResolution: 1, rotationCenterX: 16, rotationCenterY: 12}});
function listProps(items, overrides = {}) {
    return {items, editingTarget: items[0]?.id, hoveredTarget: {sprite: null, receivedBlocks: false}, selectedId: items[0]?.id,
        raised: false, onDrop() {}, onDeleteSprite() {}, onDuplicateSprite() {}, onExportSprite() {}, onSelectSprite() {}, ...overrides};
}
function mounted(f, props, selectedStore = f.store()) {
    const tree = (p, s) => React.createElement(ReactRedux.Provider, {store: s}, React.createElement(f.List, p));
    const result = render(tree(props, selectedStore));
    return {...result, store: selectedStore, update(next, nextStore = selectedStore) {result.rerender(tree(next, nextStore));}};
}
function rowView(f) {
    return [...f.metrics.rows].map(([id, props]) => ({id, name: props.name, assetId: props.asset?.assetId,
        dragging: props.dragging, receivedBlocks: props.receivedBlocks, selected: props.selected, index: props.index}));
}

test('240 real connected leaves lose irrelevant selector fanout while outputs/actions match', t => {
    const original = fixture(false), candidate = fixture(true), items = Array.from({length: 240}, (_, id) => sprite(String(id)));
    const props = listProps(items), a = mounted(original, props), b = mounted(candidate, props);
    original.metrics.leafSelectors = candidate.metrics.leafSelectors = candidate.metrics.sharedSelectors = 0;
    const beforeA = original.metrics.actions.length, beforeB = candidate.metrics.actions.length;
    act(() => {for (let step = 0; step < 8; step++) {a.store.dispatch({type: 'fixture/targets', targets: {step}}); b.store.dispatch({type: 'fixture/targets', targets: {step}});}});
    assert.equal(original.metrics.leafSelectors, 240 * 8);
    assert.equal(candidate.metrics.leafSelectors, 0);
    assert.equal(candidate.metrics.sharedSelectors, 8);
    assert.equal(original.metrics.actions.length - beforeA, 8);
    assert.equal(candidate.metrics.actions.length - beforeB, 8);
    assert.deepEqual(rowView(candidate), rowView(original));
    assert.equal(b.container.innerHTML, a.container.innerHTML);
    t.diagnostic(JSON.stringify({items: 240, targetsDispatchesPerStore: 8, originalLeafSelectorCalls: original.metrics.leafSelectors,
        candidateLeafSelectorCalls: candidate.metrics.leafSelectors, candidateSharedSelectorCalls: candidate.metrics.sharedSelectors,
        matchingDomAndLeafOutput: true}));
    a.unmount(); b.unmount(); cleanup();
});

test('asset throttle stays inside global drag/hover updates, including mutable costume notifications', () => {
    const fixtures = [fixture(false), fixture(true)], states = fixtures.map(f => ({f, items: [sprite('one')]}));
    for (const s of states) {s.props = listProps(s.items); s.view = mounted(s.f, s.props);}
    clock += 100;
    for (const s of states) {s.items[0].costume.asset = asset('b'); s.view.update({...s.props, items: [...s.items]});}
    for (const s of states) assert.equal(s.f.metrics.rows.get('one').asset.assetId, 'a');
    clock += 100;
    act(() => {for (const s of states) {
        s.view.store.dispatch(s.f.dragReducer.updateAssetDrag({dragging: true}));
        s.view.store.dispatch(s.f.hoverReducer.setHoveredSprite('one'));
        s.view.store.dispatch(s.f.hoverReducer.setReceivedBlocks(true));
    }});
    for (const s of states) {
        const row = s.f.metrics.rows.get('one');
        assert.equal(row.dragging, true); assert.equal(row.receivedBlocks, true); assert.equal(row.asset.assetId, 'a');
    }
    clock += 301;
    for (const s of states) {s.items[0].costume.size = [64, 48]; s.view.update({...s.props, items: [...s.items]});}
    for (const s of states) assert.equal(s.f.metrics.rows.get('one').asset.assetId, 'b');
    assert.deepEqual(rowView(states[0].f), rowView(states[1].f));
    for (const s of states) s.view.unmount(); cleanup();
});

test('own props bypass throttle; selection/rename/reorder/delete/restore and handlers match', () => {
    const fixtures = [fixture(false), fixture(true)], states = fixtures.map(f => ({f, calls: [], items: [sprite('one'), sprite('two')]}));
    for (const s of states) {
        s.props = listProps(s.items, {onSelectSprite: id => s.calls.push(['select', id]), onDeleteSprite: id => s.calls.push(['delete', id]),
            onDuplicateSprite: id => s.calls.push(['duplicate', id]), onExportSprite: id => s.calls.push(['export', id])});
        s.view = mounted(s.f, s.props);
    }
    clock += 100;
    for (const s of states) {
        s.items[0].costume.asset = asset('c'); s.items[0].name = 'renamed';
        s.props = {...s.props, items: [...s.items].reverse(), selectedId: 'two'}; s.view.update(s.props);
        assert.equal(s.f.metrics.rows.get('one').asset.assetId, 'c');
        for (const name of ['Click', 'DuplicateButtonClick', 'ExportButtonClick', 'DeleteButtonClick'])
            fireEvent.click(s.view.container.querySelector('[data-row="renamed"] [data-action="' + name + '"]'));
        fireEvent.click(s.view.container.querySelector('[data-confirm="ok"]'));
        s.view.update({...s.props, items: [s.items[1]]});
        s.view.update({...s.props, items: s.items});
    }
    assert.deepEqual(states[1].calls, states[0].calls);
    assert.deepEqual(states[1].calls, [['select', 'one'], ['duplicate', 'one'], ['export', 'one'], ['delete', 'one']]);
    assert.equal(states[1].view.container.innerHTML, states[0].view.container.innerHTML);
    for (const s of states) s.view.unmount(); cleanup();
});

test('sentinel fallback, raw receivedBlocks result, VM/storage/store replacement and cleanup', () => {
    const f = fixture(true), first = f.store(), second = f.store();
    const props = listProps([sprite('one')]), view = mounted(f, props, first);
    const old = f.metrics.rows.get('one');
    act(() => first.dispatch({type: 'fixture/replace', fields: {vm: new FixtureVM(), config: {storage: {scratchStorage: {AssetType: {ImageVector: {}}}}}, hoveredTarget: {sprite: 'one', receivedBlocks: 0}}}));
    const replacement = f.metrics.rows.get('one');
    assert.notEqual(replacement.vm, old.vm); assert.notEqual(replacement.storage, old.storage); assert.equal(replacement.receivedBlocks, 0);
    view.update(props, second);
    const latest = f.metrics.rows.get('one');
    assert.equal(latest.vm, second.getState().scratchGui.vm); assert.equal(latest.storage, second.getState().scratchGui.config.storage);
    const oldCount = first.getState().scratchGui.assetDrag.dragging;
    let callbackReturn;
    act(() => {callbackReturn = latest.onDrag({dragging: true});});
    assert.deepEqual(callbackReturn, f.dragReducer.updateAssetDrag({dragging: true}));
    let hoverReturn;
    act(() => {hoverReturn = latest.dispatchSetHoveredSprite('one');});
    assert.equal(hoverReturn, undefined);
    assert.equal(second.getState().scratchGui.assetDrag.dragging, true); assert.equal(first.getState().scratchGui.assetDrag.dragging, oldCount);
    view.unmount();
    const selectors = f.metrics.sharedSelectors;
    act(() => second.dispatch({type: 'fixture/targets', targets: {afterUnmount: true}}));
    assert.equal(f.metrics.sharedSelectors, selectors);
    const Named = f.Context && f.load(targets.item).SpriteSelectorItemWithContext;
    assert.equal(typeof Named, 'function');
    const fallback = render(React.createElement(ReactRedux.Provider, {store: second}, React.createElement(Named,
        {id: 'fallback', name: 'fallback', asset: asset('f'), className: '', selected: false, onClick() {}})));
    assert.equal(f.metrics.rows.get('fallback').vm, second.getState().scratchGui.vm);
    fallback.unmount(); cleanup();
});

test('shared value/callback identities and connect merge precedence survive parent rerenders', () => {
    const f = fixture(true), selectedStore = f.store(), values = [];
    const SharedProvider = f.Context.default, Context = f.Context.SpriteSelectorStoreContext;
    const Named = f.load(targets.item).SpriteSelectorItemWithContext;
    function tree() {
        return React.createElement(ReactRedux.Provider, {store: selectedStore},
            React.createElement(SharedProvider, null, React.createElement(Context.Consumer, null, shared => {
                values.push(shared);
                return React.createElement(React.Fragment, null, ...[0, '0'].map((id, index) => React.createElement(Named,
                    {key: index, id, name: 'typed-' + index, asset: asset('typed'), className: '', selected: false,
                        vm: 'own-vm', storage: 'own-storage', dragging: 'own-dragging', receivedBlocks: 'own-blocks',
                        onDrag() {throw Error('own callback must be overridden');}, onClick() {}})));
            })));
    }
    const view = render(tree()), originalValue = values.at(-1);
    assert.notEqual(originalValue, f.Context.MISSING_SPRITE_SELECTOR_STORE);
    assert.equal(f.metrics.rows.get(0).vm, selectedStore.getState().scratchGui.vm);
    assert.equal(f.metrics.rows.get(0).dragging, false);
    for (let step = 0; step < 3; step++) {
        act(() => selectedStore.dispatch({type: 'fixture/targets', targets: {step}}));
        view.rerender(tree());
        assert.equal(values.at(-1), originalValue);
        assert.equal(values.at(-1).onDrag, originalValue.onDrag);
        assert.equal(values.at(-1).dispatchSetHoveredSprite, originalValue.dispatchSetHoveredSprite);
    }
    act(() => selectedStore.dispatch({type: 'fixture/replace', fields: {hoveredTarget: {sprite: 0, receivedBlocks: true}}}));
    assert.equal(f.metrics.rows.get(0).receivedBlocks, true);
    assert.equal(f.metrics.rows.get('0').receivedBlocks, false);
    assert.notEqual(values.at(-1), originalValue);
    assert.equal(values.at(-1).onDrag, originalValue.onDrag);
    view.unmount(); cleanup();
});

test('real Sortable HOC keeps drag ordering and drop payload identical', () => {
    const states = [fixture(false), fixture(true)].map(f => ({f, drops: [], items: [sprite('one'), sprite('two'), sprite('three')]}));
    for (const s of states) {
        s.props = listProps(s.items, {onDrop: value => s.drops.push(value)});
        s.view = mounted(s.f, s.props);
        const wrapper = s.view.container.querySelector('.scrollWrapper');
        wrapper.getBoundingClientRect = () => ({left: 0, top: 0, right: 100, bottom: 40});
        [...s.view.container.querySelectorAll('li')].forEach((node, index) => {
            node.getBoundingClientRect = () => ({left: 0, right: 100, top: index * 10, bottom: index * 10 + 10});
        });
        act(() => s.view.store.dispatch(s.f.dragReducer.updateAssetDrag({dragging: true, dragType: 'SPRITE', index: 0,
            currentOffset: {x: 5, y: 25}})));
        assert.deepEqual([...s.view.container.querySelectorAll('li')].map(node => node.style.order), ['2', '0', '1']);
        act(() => s.view.store.dispatch(s.f.dragReducer.updateAssetDrag({dragging: false})));
        assert.equal(s.drops.length, 1);
        assert.equal(s.drops[0].index, 0); assert.equal(s.drops[0].newIndex, 2); assert.equal(s.drops[0].dragType, 'SPRITE');
    }
    assert.deepEqual(states[1].drops, states[0].drops);
    assert.equal(states[1].view.container.innerHTML, states[0].view.container.innerHTML);
    for (const s of states) s.view.unmount(); cleanup();
});

test('legacy default export and context fallback keep selector updates and unsubscribe', () => {
    const f = fixture(true), selectedStore = f.store();
    const element = React.createElement(ReactRedux.Provider, {store: selectedStore},
        React.createElement(f.Item, {id: 'legacy', name: 'legacy', asset: asset('legacy'), className: '', onClick() {}}));
    const view = render(element), mountedCalls = f.metrics.leafSelectors;
    act(() => selectedStore.dispatch({type: 'fixture/targets', targets: {step: 1}}));
    assert.equal(f.metrics.leafSelectors, mountedCalls + 1);
    act(() => selectedStore.dispatch(f.dragReducer.updateAssetDrag({dragging: true})));
    assert.equal(f.metrics.rows.get('legacy').dragging, true);
    view.unmount();
    const unmountedCalls = f.metrics.leafSelectors;
    act(() => selectedStore.dispatch({type: 'fixture/targets', targets: {step: 2}}));
    assert.equal(f.metrics.leafSelectors, unmountedCalls);
    cleanup();
});

test('explicit store/custom context preserve the original mixed-store and empty-list behavior', () => {
    for (const mode of ['store', 'context']) {
        const states = [fixture(false), fixture(true)].map(f => ({f, a: f.store(), b: f.store()}));
        for (const s of states) {
            const Custom = React.createContext(null);
            const props = listProps([sprite('one')], mode === 'store' ? {store: s.b} : {context: Custom});
            const list = React.createElement(s.f.List, props);
            const child = mode === 'context' ? React.createElement(ReactRedux.Provider, {store: s.b, context: Custom}, list) : list;
            const view = render(React.createElement(ReactRedux.Provider, {store: s.a}, child));
            // react-redux deliberately does not propagate a connect own-store
            // into descendants. Preserve that existing default-leaf context.
            assert.equal(s.f.metrics.rows.get('one').vm, s.a.getState().scratchGui.vm);
            act(() => s.f.metrics.rows.get('one').onDrag({dragging: true}));
            assert.equal(s.a.getState().scratchGui.assetDrag.dragging, true);
            assert.equal(s.b.getState().scratchGui.assetDrag.dragging, false);
            view.unmount();
            const empty = React.createElement(s.f.List, listProps([], mode === 'store' ? {store: s.b} : {context: Custom}));
            const standalone = mode === 'context' ? React.createElement(ReactRedux.Provider, {store: s.b, context: Custom}, empty) : empty;
            let mountedEmpty;
            assert.doesNotThrow(() => {mountedEmpty = render(standalone);});
            assert.equal(mountedEmpty.container.querySelectorAll('li').length, 0);
            mountedEmpty.unmount();
        }
        cleanup();
    }
});

test('explicit-store compatibility path does not consume a surrounding shared list value', () => {
    for (const candidate of [false, true]) {
        const f = fixture(candidate), a = f.store(), b = f.store(), c = f.store();
        const list = React.createElement(f.List, listProps([sprite('nested')], {store: b}));
        const inner = React.createElement(ReactRedux.Provider, {store: c}, list);
        const scoped = candidate ? React.createElement(f.Context.default, null, inner) : inner;
        const view = render(React.createElement(ReactRedux.Provider, {store: a}, scoped));
        assert.equal(f.metrics.rows.get('nested').vm, c.getState().scratchGui.vm);
        act(() => f.metrics.rows.get('nested').onDrag({dragging: true}));
        assert.equal(c.getState().scratchGui.assetDrag.dragging, true);
        assert.equal(a.getState().scratchGui.assetDrag.dragging, false);
        assert.equal(b.getState().scratchGui.assetDrag.dragging, false);
        view.unmount();
    }
    cleanup();
});

test.after(() => {Date.now = originalDateNow; cleanup(); dom.window.close();});
