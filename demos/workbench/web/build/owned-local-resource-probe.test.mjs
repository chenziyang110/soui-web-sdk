import test from 'node:test';
import assert from 'node:assert/strict';
import { hasWorkbenchLocalResources } from '../packages/sdk/owned-local-resource-probe.ts';

const host = 'a'.repeat(32), document = '0'.repeat(32);
const owned = (entry = 'react.html') => 'https://' + host + '.soui-local.invalid/' + document + '/' + entry;
const location = href => ({ protocol: new URL(href).protocol, href });
const accepted = href => hasWorkbenchLocalResources(location(href));

test('owned resource probe: preserves legacy file documents', () => {
  assert.equal(accepted('file:///C:/Workbench/resources/index.html'), true);
  assert.equal(accepted('file:///C:/Workbench/resources/react.html'), true);
  assert.equal(accepted('FILE:///C:/Workbench/resources/react.html'), true);
});

test('owned resource probe: accepts only the actual two native entry names', () => {
  assert.equal(accepted(owned('index.html')), true);
  assert.equal(accepted(owned('react.html')), true);
  for (const entry of ['index-miniblink.html', 'react-miniblink.html', 'other.html', 'index.html/extra', '%69ndex.html'])
    assert.equal(accepted(owned(entry)), false, entry);
});

test('owned resource probe: requires exact native host and document nonce boundaries', () => {
  for (const nonce of [host.slice(1), host + 'a', 'g'.repeat(32), host.toUpperCase()])
    assert.equal(accepted('https://' + nonce + '.soui-local.invalid/' + document + '/react.html'), false, nonce);
  for (const nonce of [document.slice(1), document + '0', 'g'.repeat(32)])
    assert.equal(accepted('https://' + host + '.soui-local.invalid/' + nonce + '/react.html'), false, nonce);
  assert.equal(accepted('https://' + host + '.soui-local.invalid.evil/' + document + '/react.html'), false);
  assert.equal(accepted('https://' + host + '.soui-local.invalid/x/' + document + '/react.html'), false);
});

test('owned resource probe: rejects credentials and all explicit ports including 443', () => {
  const authority = host + '.soui-local.invalid';
  for (const replacement of ['user@' + authority, 'user:password@' + authority, authority + ':443', authority + ':8443'])
    assert.equal(accepted(owned().replace(authority, replacement)), false, replacement);
});

test('owned resource probe: rejects ordinary HTTPS, other schemes and URL suffixes', () => {
  for (const href of ['https://example.test/' + document + '/react.html',
    'https://localhost/' + document + '/react.html', owned().replace('https:', 'http:'),
    'data:text/html,ready', 'blob:' + owned(), 'about:blank',
    owned() + '?query=1', owned() + '#route', owned() + '\n', owned() + '\r\n'])
    assert.equal(accepted(href), false, href);
  assert.equal(hasWorkbenchLocalResources({ protocol: 'http:', href: owned() }), false);
});
