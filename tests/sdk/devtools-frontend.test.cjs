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

// The paired raw DOMModel/InspectElementModeController use the boolean
// command, whereas the supplied generated command table declares the enum
// replacement. Exercise registration plus the actual request/response path.
function inspectorConnection() {
  const instance = frontend();
  // InspectorBackend normally receives this utility from platform/utilities.js.
  vm.runInContext('Array.prototype.peekLast = function() {return this[this.length - 1];};',
    instance.context);
  instance.context.InspectorBackend.registerCommand('DOM.setInspectMode', [
    {name: 'mode', type: 'string', optional: false},
    {name: 'highlightConfig', type: 'object', optional: true},
  ], [], false);
  const connection = new instance.api.Connection();
  const messages = [];
  connection.sendMessage = message => messages.push(JSON.parse(JSON.stringify(message)));
  return {...instance, connection, messages, agent: connection.agent('DOM')};
}

test('paired Inspector registers the boolean picker command supported by MiniBlink', () => {
  const {agent, connection, messages} = inspectorConnection();
  const highlightConfig = {showInfo: true, contentColor: {r: 100, g: 100, b: 100, a: 0.5}};
  const replies = [];
  agent.setInspectModeEnabled(true, false, highlightConfig, error => replies.push(error));
  agent.setInspectModeEnabled(true, true, highlightConfig, error => replies.push(error));
  agent.setInspectModeEnabled(false, false, highlightConfig, error => replies.push(error));
  assert.deepEqual(messages, [false, true, false].map((inspectUAShadowDOM, index) => ({
    id: index + 1, method: 'DOM.setInspectModeEnabled',
    params: {enabled: index < 2, inspectUAShadowDOM, highlightConfig},
  })));
  for (const message of messages) connection.dispatch({id: message.id, result: {}});
  assert.deepEqual(replies, [null, null, null]);
  assert.equal(connection._pendingResponsesCount, 0);
});

test('picker errors reach their original callback and do not report successful activation', () => {
  const {agent, connection, messages, errors} = inspectorConnection();
  let reply;
  agent.setInspectModeEnabled(true, false, {}, error => {reply = error;});
  connection.dispatch({id: messages[0].id, error: {code: -32001, message: 'picker-refused'}});
  assert.equal(reply, 'picker-refused');
  assert.deepEqual(errors, ['E_PROTOCOL_REQUEST code=-32001']);
  assert.equal(connection._pendingResponsesCount, 0);
});
