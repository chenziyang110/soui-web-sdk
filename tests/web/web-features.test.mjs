import test from 'node:test';
import assert from 'node:assert/strict';
import {inspectSouiWebFeatures, requireSouiWebFeatures} from '../../sdk/web/dist/index.js';

test('absence is reported per capability without guessing from a browser name', () => {
  const report = inspectSouiWebFeatures({Promise, Proxy, CSS: {supports: (p, v) => p === 'display' && v === 'grid'}});
  assert.equal(report.protocol, 'soui.web-features/1');
  assert.equal(report.scope, 'surface-detection');
  assert.equal(report.features.promise, 'available');
  assert.equal(report.features.cssGrid, 'available');
  assert.equal(report.features.cssContainerQueries, 'missing');
  assert.equal(report.features.webGpuInterface, 'missing');
});
test('throwing environment probes stay unknown and do not suppress other observations', () => {
  const target = {Promise, get navigator() {throw new Error('unavailable');}};
  const report = inspectSouiWebFeatures(target);
  assert.equal(report.features.webGpuInterface, 'unknown');
  assert.equal(report.features.promise, 'available');
});
test('required missing or unknown features fail before application mounting', () => {
  assert.throws(() => requireSouiWebFeatures(['resizeObserver'], {}), {code: 'E_WEB_FEATURES_UNAVAILABLE'});
  assert.throws(() => requireSouiWebFeatures(['cssGrid'], {CSS: {supports() {throw new Error();}}}), {code: 'E_WEB_FEATURES_UNAVAILABLE'});
  assert.equal(requireSouiWebFeatures(['promise'], {Promise}).features.promise, 'available');
});
test('typos, duplicates and malformed requirements cannot silently pass', () => {
  for (const value of [null, {}, ['bogus'], ['promise', 'promise'], new Array(13).fill('promise')])
    assert.throws(() => requireSouiWebFeatures(value, {Promise}), {code: 'E_INVALID_FEATURES'});
});
test('server-side inspection makes no browser support claim', () => {
  assert.ok(Object.values(inspectSouiWebFeatures().features).every(value => value === 'unknown'));
});
