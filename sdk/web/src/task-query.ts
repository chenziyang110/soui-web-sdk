import { SouiBridgeError, isRecord } from './protocol.js';
import type { SouiToolDefinition } from './action-contracts.js';

/** Runtime still validates exact fields, bounded ASCII, and native ownership. */
export type SouiTaskQueryParams =
  | { taskId: string; operationId?: never }
  | { operationId: string; taskId?: never };

/** Copies one selector once, preventing a getter/toJSON from changing the wire shape. */
export function readTaskQueryParams(value: unknown, requestId = ''): SouiTaskQueryParams {
  const invalid = () => new SouiBridgeError('E_INVALID_PARAMS',
    'Expected exactly one bounded ASCII taskId or operationId (1-128 letters, digits, -_.:).', requestId);
  try {
    if (!isRecord(value) || Object.getOwnPropertyNames(value).length !== 1 ||
        Object.keys(value).length !== 1 || Object.getOwnPropertySymbols(value).length !== 0) throw invalid();
    const name = Object.keys(value)[0];
    if (name !== 'taskId' && name !== 'operationId') throw invalid();
    const selector = value[name];
    // A negated character class also rejects a final newline; JS `$` does not.
    if (typeof selector !== 'string' || selector.length < 1 || selector.length > 128 ||
        /[^A-Za-z0-9_.:-]/.test(selector)) throw invalid();
    return name === 'taskId' ? { taskId: selector } : { operationId: selector };
  } catch { throw invalid(); }
}

/** Limited schema cannot express XOR/ASCII; the client and native enforce both. */
export function taskQueryToolDefinition(): SouiToolDefinition {
  return {
    name: 'task.query',
    description: 'Read a retained task in the current native authorization scope. Provide exactly one taskId or operationId; '+
      'each is 1-128 ASCII letters, digits, or -_.:. Runtime validates XOR and ASCII beyond this limited schema. '+
      'This does not start, replay or cancel work. Not-found does not prove that the operation never executed.',
    inputSchema: { type: 'object', properties: {
      taskId: { type: 'string', minLength: 1, maxLength: 128 },
      operationId: { type: 'string', minLength: 1, maxLength: 128 },
    }, additionalProperties: false },
  };
}
