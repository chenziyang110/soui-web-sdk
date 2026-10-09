import type { SouiCapabilityPage, SouiDescribeOptions } from './capabilities.js';
import type { SouiTaskQueryParams } from './task-query.js';
export type { SouiTaskQueryParams } from './task-query.js';

export interface SouiNativeEvent {
  sessionId: string;
  taskId?: string;
  sequence: number;
  eventName: string;
  payload: unknown;
}

export interface SouiNativePort {
  request(envelope: string): Promise<string>;
  onEvent(listener: (event: SouiNativeEvent) => void): () => void;
  /** Release this port's listeners; this does not cancel native work. */
  dispose?(): void;
}

/** Relative native authorization lifetime. A renewal keeps the document identity. */
export interface SouiSessionLease {
  generation: number;
  expiresInMs: number;
  renewAfterMs: number;
}

export function readSessionLease(value: unknown, code = 'E_INVALID_RESPONSE'): SouiSessionLease {
  if (!isRecord(value) || !Number.isSafeInteger(value.generation) || (value.generation as number) < 1 ||
      !Number.isSafeInteger(value.expiresInMs) || (value.expiresInMs as number) < 1 ||
      (value.expiresInMs as number) > 1800000 || !Number.isSafeInteger(value.renewAfterMs) ||
      (value.renewAfterMs as number) < 0 || (value.renewAfterMs as number) > 1500000 ||
      (value.renewAfterMs as number) > (value.expiresInMs as number))
    throw new SouiBridgeError(code, 'Native authorization lease fields are invalid.');
  return { generation: value.generation as number, expiresInMs: value.expiresInMs as number,
    renewAfterMs: value.renewAfterMs as number };
}

export interface SouiBridgeRequest {
  requestId: string;
  sessionId: string;
  origin: string;
  token: string;
  method: string;
  params: unknown;
  timeoutMs: number;
}

export interface SouiBridgeResponse {
  requestId: string;
  ok: boolean;
  result?: unknown;
  error?: { code: string; message: string };
}

/** Methods installed by json_bridge itself, without Workbench services. */
export interface SouiCoreMethodMap {
  'bridge.describe': { params: SouiDescribeOptions; result: SouiCapabilityPage };
  'bridge.probe.report': { params: { ready: true }; result: { ready: true } };
  'task.start': { params: { kind: string; operationId: string; input: Record<string, unknown> };
    result: { taskId: string; accepted: boolean; duplicate: boolean } };
  'task.query': { params: SouiTaskQueryParams; result: { taskId: string; state: string;
    progress: number; message: string; sequence: number; result?: Record<string, unknown> } };
  'task.cancel': { params: { taskId: string }; result: { taskId: string;
    state: string; cancelRequested: boolean } };
  'task.subscribe': { params: { taskId: string; eventName: 'task.progress' };
    result: { subscriptionId: string } };
  'task.unsubscribe': { params: { subscriptionId: string }; result: { removed: boolean } };
}

/** Optional desktop capability installed by json_bridge::enable_clipboard_service(). */
export interface SouiClipboardMethodMap {
  'desktop.clipboard.writeText': { params: { text: string }; result: { applied: true } };
}

/** Optional page controls. None grants a page an Agent scope or user gesture. */
export interface SouiGuiControlMethodMap {
  'gui.bind': { params: {}; result: { accepted: true } };
  'gui.effect': { params: { requestId: string; documentId: string; effectId: 'native'; inputJson: string };
    result: { resultJson: string } };
  'gui.ack': { params: { requestId: string; documentId: string; ok: boolean; resultJson: string }; result: { accepted: true } };
}

/** Optional directory picker installed by json_bridge::enable_directory_service(). */
export interface SouiDirectoryMethodMap {
  'desktop.dialog.pickDirectory': { params: {}; result: { cancelled: boolean; path: string } };
}

/** Optional window capability installed by json_bridge::enable_window_service(). */
export interface SouiWindowMethodMap {
  'desktop.window.setTitle': { params: { title: string }; result: { applied: true } };
  'desktop.window.beginDrag': { params: {}; result: { started: true } };
  'desktop.window.minimize': { params: {}; result: { minimized: true } };
  'desktop.window.toggleMaximize': { params: {}; result: { maximized: boolean } };
  'desktop.window.close': { params: {}; result: { accepted: true } };
}

export class SouiBridgeError extends Error {
  public constructor(public readonly code: string, message: string, public readonly requestId = '') {
    super(message);
    this.name = 'SouiBridgeError';
  }
}

export const MAX_MESSAGE_BYTES = 64 * 1024;

export function isRecord(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

export function nonEmptyString(value: unknown, maximum = 512): value is string {
  return typeof value === 'string' && value.length > 0 && value.length <= maximum;
}

// TextEncoder is not part of the Chrome 49 target. Count without allocating a copy.
export function utf8Bytes(text: string): number {
  let bytes = 0;
  for (let i = 0; i < text.length; i++) {
    const code = text.charCodeAt(i);
    if (code < 0x80) bytes++;
    else if (code < 0x800) bytes += 2;
    else if (code >= 0xd800 && code <= 0xdbff && i + 1 < text.length &&
             text.charCodeAt(i + 1) >= 0xdc00 && text.charCodeAt(i + 1) <= 0xdfff) {
      bytes += 4;
      i++;
    } else bytes += 3;
  }
  return bytes;
}

export function readResponse(raw: unknown, requestId: string): SouiBridgeResponse {
  if (typeof raw !== 'string' || utf8Bytes(raw) > MAX_MESSAGE_BYTES)
    throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native response is not a bounded JSON string.', requestId);
  let response: unknown;
  try { response = JSON.parse(raw); }
  catch { throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native response is not valid JSON.', requestId); }
  if (!isRecord(response) || typeof response.ok !== 'boolean' || response.requestId !== requestId)
    throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native response correlation or envelope is invalid.', requestId);
  if (!response.ok) {
    if (!isRecord(response.error) || !nonEmptyString(response.error.code) || typeof response.error.message !== 'string')
      throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native error envelope is invalid.', requestId);
    throw new SouiBridgeError(response.error.code, response.error.message, requestId);
  }
  return response as unknown as SouiBridgeResponse;
}

export function isNativeEvent(value: unknown): value is SouiNativeEvent {
  return isRecord(value) && nonEmptyString(value.sessionId) && nonEmptyString(value.eventName) &&
    (value.taskId === undefined || nonEmptyString(value.taskId)) &&
    Number.isSafeInteger(value.sequence) && (value.sequence as number) > 0 && 'payload' in value;
}

export function integerOption(value: number | undefined, fallback: number, maximum: number, name: string): number {
  const result = value === undefined ? fallback : value;
  if (!Number.isSafeInteger(result) || result < 1 || result > maximum)
    throw new SouiBridgeError('E_INVALID_OPTIONS', `${name} must be an integer from 1 to ${maximum}.`);
  return result;
}
