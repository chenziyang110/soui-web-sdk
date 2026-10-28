'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname,
  '../../sdk/resources/devtools/miniblink/overrides/front_end/sdk/InspectorBackend.js'), 'utf8');
function frontend() {
  const errors = [];
  const context = vm.createContext({window: {}, WebInspector: {Object: function() {}},
    console: {error: (...args) => errors.push(args.join(' ')), log: () => {}}});
  vm.runInContext(source, context, {timeout: 1000});
  return {context, errors, api: context.InspectorBackendClass};
}

test('protocol diagnostics omit bodies, arbitrary error text and nonnumeric codes', () => {
  const {errors, api} = frontend();
  const body = {message: 'fixture-body-canary', data: 'fixture-body-canary', code: -32001,
    toJSON() {throw new Error('A protocol diagnostic serialized a body');}};
  api.reportProtocolError('fixture-error-canary', {error: body, result: body});
  api.reportProtocolError('fixture-error-canary', {error: {code: 'fixture-code-canary'}});
  assert.deepEqual(errors, ['E_PROTOCOL_DISPATCH code=-32001', 'E_PROTOCOL_DISPATCH']);
});

test('automatic diagnostic output has a fixed lifetime quota', () => {
  const {errors, api} = frontend();
  for (let i = 0; i < 10000; ++i) api.reportProtocolError('fixture-body-canary', {});
  assert.equal(errors.length, 64);
  assert.ok(errors.every(message => message === 'E_PROTOCOL_DISPATCH'));
});

test('request errors retain the original panel callback while logs omit data', () => {
  const {errors, api} = frontend();
  const agent = new api.AgentPrototype('Fixture');
  agent._hasErrorData.fixture = true;
  const data = {content: 'fixture-body-canary'};
  let received;
  agent.dispatchResponse({id: 'fixture-id-canary', error: {
    code: -32001, message: 'fixture-message-canary', data}}, 'fixture',
    (message, detail) => {received = {message, detail};});
  assert.equal(received.message, 'fixture-message-canary');
  assert.equal(received.detail, data);
  assert.deepEqual(errors, ['E_PROTOCOL_REQUEST code=-32001']);
});

test('client wrappers preserve their fallback without printing backend errors', () => {
  const {context, errors} = frontend();
  const fallback = {};
  let received;
  context.InspectorBackend.wrapClientCallback(value => {received = value;},
    'fixture-prefix-canary', undefined, fallback)('fixture-body-canary');
  assert.equal(received, fallback);
  assert.deepEqual(errors, ['E_PROTOCOL_CLIENT']);
});
