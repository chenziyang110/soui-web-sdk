import { SouiBridgeError, isRecord, utf8Bytes } from './protocol.js';
import type { SouiCapabilityCatalog } from './capabilities.js';
import * as core from './action-contract-core.js';

export type SouiJson = null | boolean | number | string | SouiJson[] | { [key: string]: SouiJson };
export type SouiActionSchema =
  | { type: 'object'; properties: Record<string, SouiActionSchema>; required?: string[]; additionalProperties: false }
  | { type: 'array'; items: SouiActionSchema; minItems?: number; maxItems: number }
  | { type: 'string'; minLength?: number; maxLength: number; enum?: string[] }
  | { type: 'number' | 'integer'; minimum?: number; maximum?: number }
  | { type: 'boolean' | 'null' };

/** Deliberately limited schema vocabulary; unsupported keywords are rejected. */
export interface SouiActionContract {
  protocol: 'soui.action/1';
  title: string;
  description: string;
  inputSchema: Extract<SouiActionSchema, { type: 'object' }>;
  outputSchema: Extract<SouiActionSchema, { type: 'object' }>;
}

export interface SouiToolDefinition {
  name: string;
  description: string;
  inputSchema: SouiActionContract['inputSchema'];
}

function invalid(code: string, message: string): never { throw new SouiBridgeError(code, message); }
function translated<T>(read: () => T): T {
  try { return read(); } catch (error) {
    if (error instanceof core.ActionContractError) throw new SouiBridgeError(error.code, error.message);
    throw error;
  }
}

// Shared with the GUI registry; not part of the root SDK exports.
export function copySouiJson(value: unknown, maximum: number, code: string): SouiJson {
  return translated(() => core.copySouiJson(value, maximum, code)) as SouiJson;
}
export function readSouiActionContract(raw: unknown, code = 'E_INVALID_RESPONSE'): SouiActionContract {
  return translated(() => core.readSouiActionContract(raw, code)) as SouiActionContract;
}
export function validateSouiActionInput(contract: SouiActionContract, input: unknown): void {
  translated(() => core.validateSouiActionInput(contract, input));
}
export function validateSouiActionOutput(contract: SouiActionContract, output: unknown): void {
  translated(() => core.validateSouiActionOutput(contract, output));
}

/** Descriptions are advisory. Calling a tool still uses the authorized native client. */
export function souiToolDefinitions(catalog: SouiCapabilityCatalog): SouiToolDefinition[] {
  if (!isRecord(catalog) || catalog.protocol !== 'soui.capabilities/1' || typeof catalog.sdkVersion !== 'string' ||
      catalog.sdkVersion.length === 0 || catalog.sdkVersion.length > 128 || !Array.isArray(catalog.methods) || catalog.methods.length > 4096)
    invalid('E_INVALID_RESPONSE', 'Expected a bounded capability catalog.');
  const names: string[] = [];
  const output: SouiToolDefinition[] = [];
  for (const method of catalog.methods) {
    if (!isRecord(method) || typeof method.name !== 'string' || !/^[A-Za-z0-9_.:-]{1,128}$/.test(method.name) ||
        typeof method.capability !== 'string' || method.capability.length === 0 || utf8Bytes(method.capability) > 512 || names.indexOf(method.name) >= 0)
      invalid('E_INVALID_RESPONSE', 'Invalid or duplicate tool method.');
    names.push(method.name);
    if (method.action === undefined) continue;
    const action = readSouiActionContract(method.action);
    output.push({ name: method.name, description: action.description || action.title, inputSchema: action.inputSchema });
  }
  return output;
}
