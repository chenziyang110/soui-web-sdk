import { SouiBridgeError, isRecord, utf8Bytes } from './protocol.js';
import type { SouiCapabilityCatalog } from './capabilities.js';

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

/** Counts Unicode scalars and rejects lone surrogates, like the native JSON parser. */
function scalarLength(value: string, code: string): number {
  let count = 0;
  for (let index = 0; index < value.length; ++index) {
    const unit = value.charCodeAt(index);
    if (unit >= 0xd800 && unit <= 0xdbff) {
      const next = value.charCodeAt(++index);
      if (!(next >= 0xdc00 && next <= 0xdfff)) invalid(code, 'JSON strings must contain valid Unicode scalars.');
    } else if (unit >= 0xdc00 && unit <= 0xdfff) invalid(code, 'JSON strings must contain valid Unicode scalars.');
    ++count;
  }
  return count;
}

// Shared with the GUI registry; not part of the root SDK exports.
export function copySouiJson(value: unknown, maximum: number, code: string): SouiJson {
  const ancestors: unknown[] = [];
  let bytes = 0;
  const spend = (amount: number): void => {
    bytes += amount;
    if (bytes > maximum) invalid(code, 'JSON data exceeds the byte limit.');
  };
  const string = (item: string): void => {
    if (item.length > maximum) invalid(code, 'JSON data exceeds the byte limit.');
    scalarLength(item, code);
    spend(utf8Bytes(JSON.stringify(item)));
  };
  const inspect = (item: unknown, depth: number): SouiJson => {
    if (depth > 8) invalid(code, 'JSON depth exceeds eight levels.');
    if (item === null || typeof item === 'boolean') { spend(item === null ? 4 : item ? 4 : 5); return item; }
    if (typeof item === 'string') { string(item); return item; }
    if (typeof item === 'number') {
      if (!Number.isFinite(item) || Math.abs(item) > Number.MAX_SAFE_INTEGER)
        invalid(code, 'JSON numbers must be finite and within the interoperable range.');
      spend(JSON.stringify(item).length);
      return item;
    }
    if (!Array.isArray(item) && (!isRecord(item) ||
        (Object.getPrototypeOf(item) !== Object.prototype && Object.getPrototypeOf(item) !== null)))
      invalid(code, 'Expected JSON data without custom serializers.');
    if (ancestors.indexOf(item) >= 0) invalid(code, 'Circular JSON data is not supported.');
    ancestors.push(item);
    spend(2);
    let result: SouiJson;
    if (Array.isArray(item)) {
      if (item.length > maximum) invalid(code, 'JSON data exceeds the byte limit.');
      const array: SouiJson[] = [];
      for (let index = 0; index < item.length; ++index) {
        if (!Object.prototype.hasOwnProperty.call(item, index)) invalid(code, 'Sparse JSON arrays are not supported.');
        if (index > 0) spend(1);
        array.push(inspect(item[index], depth + 1));
      }
      result = array;
    } else {
      const object: { [key: string]: SouiJson } = Object.create(null);
      let count = 0;
      for (const key of Object.keys(item)) {
        if (count++ > 0) spend(1);
        string(key); spend(1);
        object[key] = inspect((item as Record<string, unknown>)[key], depth + 1);
      }
      result = object;
    }
    ancestors.pop();
    return result;
  };
  const copied = inspect(value, 1);
  let encoded: string;
  try { encoded = JSON.stringify(copied); }
  catch { return invalid(code, 'JSON serialization failed.'); }
  if (utf8Bytes(encoded) > maximum) invalid(code, 'JSON data exceeds the byte limit.');
  return JSON.parse(encoded) as SouiJson;
}

function keys(value: Record<string, unknown>, allowed: string[], required: string[], code: string): void {
  if (Object.keys(value).some(key => allowed.indexOf(key) < 0) ||
      required.some(key => !Object.prototype.hasOwnProperty.call(value, key)))
    invalid(code, 'Unsupported or missing schema keyword.');
}

function bound(value: unknown, maximum: number, code: string): number {
  if (!Number.isSafeInteger(value) || (value as number) < 0 || (value as number) > maximum)
    invalid(code, 'Schema bound is invalid.');
  return value as number;
}

function checkSchema(raw: unknown, code: string): asserts raw is SouiActionSchema {
  if (!isRecord(raw)) invalid(code, 'An action schema must be an object.');
  switch (raw.type) {
    case 'object': {
      keys(raw, ['type', 'properties', 'required', 'additionalProperties'], ['type', 'properties', 'additionalProperties'], code);
      if (!isRecord(raw.properties) || raw.additionalProperties !== false || Object.keys(raw.properties).length > 64)
        invalid(code, 'Object schemas require bounded properties and additionalProperties:false.');
      for (const key of Object.keys(raw.properties)) {
        if (key.length === 0 || utf8Bytes(key) > 128) invalid(code, 'Property names must contain 1–128 UTF-8 bytes.');
        checkSchema(raw.properties[key], code);
      }
      if (raw.required !== undefined) {
        if (!Array.isArray(raw.required) || raw.required.length > 64 ||
            raw.required.some((key, index, all) => typeof key !== 'string' || all.indexOf(key) !== index ||
              !Object.prototype.hasOwnProperty.call(raw.properties, key)))
          invalid(code, 'Required properties must be unique declared property names.');
      }
      return;
    }
    case 'array': {
      keys(raw, ['type', 'items', 'minItems', 'maxItems'], ['type', 'items', 'maxItems'], code);
      const maximum = bound(raw.maxItems, 1024, code);
      if (bound(raw.minItems === undefined ? 0 : raw.minItems, 1024, code) > maximum)
        invalid(code, 'Array schema bounds are reversed.');
      checkSchema(raw.items, code);
      return;
    }
    case 'string': {
      keys(raw, ['type', 'minLength', 'maxLength', 'enum'], ['type', 'maxLength'], code);
      const maximum = bound(raw.maxLength, 32768, code);
      const minimum = bound(raw.minLength === undefined ? 0 : raw.minLength, 32768, code);
      if (minimum > maximum) invalid(code, 'String schema bounds are reversed.');
      if (raw.enum !== undefined && (!Array.isArray(raw.enum) || raw.enum.length < 1 || raw.enum.length > 64 ||
          raw.enum.some((item, index, all) => typeof item !== 'string' || all.indexOf(item) !== index)))
        invalid(code, 'String enums must contain unique string values.');
      return;
    }
    case 'number': case 'integer': {
      keys(raw, ['type', 'minimum', 'maximum'], ['type'], code);
      for (const key of ['minimum', 'maximum']) {
        const value = raw[key];
        if (value !== undefined && (typeof value !== 'number' || !Number.isFinite(value) || Math.abs(value) > Number.MAX_SAFE_INTEGER))
          invalid(code, 'Numeric schema bounds must be interoperable numbers.');
      }
      if (raw.minimum !== undefined && raw.maximum !== undefined && (raw.minimum as number) > (raw.maximum as number))
        invalid(code, 'Numeric schema bounds are reversed.');
      return;
    }
    case 'boolean': case 'null': keys(raw, ['type'], ['type'], code); return;
    default: invalid(code, 'Unsupported action schema type.');
  }
}

export function readSouiActionContract(raw: unknown, code = 'E_INVALID_RESPONSE'): SouiActionContract {
  // Schema depth is measured independently, rather than including its descriptor wrapper.
  if (!isRecord(raw)) invalid(code, 'Expected an action descriptor.');
  keys(raw, ['protocol', 'title', 'description', 'inputSchema', 'outputSchema'],
    ['protocol', 'title', 'description', 'inputSchema', 'outputSchema'], code);
  if (raw.protocol !== 'soui.action/1' || typeof raw.title !== 'string' || raw.title.length === 0 ||
      utf8Bytes(raw.title) > 256 || typeof raw.description !== 'string' || utf8Bytes(raw.description) > 1024)
    invalid(code, 'Action descriptor metadata is invalid.');
  scalarLength(raw.title, code); scalarLength(raw.description, code);
  const inputSchema = copySouiJson(raw.inputSchema, 8192, code);
  const outputSchema = copySouiJson(raw.outputSchema, 8192, code);
  checkSchema(inputSchema, code); checkSchema(outputSchema, code);
  if (inputSchema.type !== 'object' || outputSchema.type !== 'object')
    invalid(code, 'Action input and output schemas must have object roots.');
  const result: SouiActionContract = { protocol: 'soui.action/1', title: raw.title,
    description: raw.description, inputSchema, outputSchema };
  if (utf8Bytes(JSON.stringify(result)) > 16384) invalid(code, 'Action descriptor exceeds 16 KiB.');
  return result;
}

function matches(schema: SouiActionSchema, value: SouiJson): boolean {
  switch (schema.type) {
    case 'object':
      return isRecord(value) && (schema.required || []).every(key => Object.prototype.hasOwnProperty.call(value, key)) &&
        Object.keys(value).every(key => Object.prototype.hasOwnProperty.call(schema.properties, key) && matches(schema.properties[key], value[key]));
    case 'array': return Array.isArray(value) && value.length >= (schema.minItems || 0) &&
      value.length <= schema.maxItems && value.every(item => matches(schema.items, item));
    case 'string': return typeof value === 'string' && scalarLength(value, 'E_INVALID_PARAMS') >= (schema.minLength || 0) &&
      scalarLength(value, 'E_INVALID_PARAMS') <= schema.maxLength && (!schema.enum || schema.enum.indexOf(value) >= 0);
    case 'number': case 'integer': return typeof value === 'number' && (schema.type !== 'integer' || Number.isSafeInteger(value)) &&
      (schema.minimum === undefined || value >= schema.minimum) && (schema.maximum === undefined || value <= schema.maximum);
    case 'boolean': return typeof value === 'boolean';
    case 'null': return value === null;
  }
}

export function validateSouiActionInput(contract: SouiActionContract, input: unknown): void {
  const valid = readSouiActionContract(contract);
  if (!matches(valid.inputSchema, copySouiJson(input, 65536, 'E_INVALID_PARAMS')))
    invalid('E_INVALID_PARAMS', 'Action input does not satisfy its declared schema.');
}

export function validateSouiActionOutput(contract: SouiActionContract, output: unknown): void {
  const valid = readSouiActionContract(contract);
  if (!matches(valid.outputSchema, copySouiJson(output, 65536, 'E_SCHEMA_INVALID')))
    invalid('E_SCHEMA_INVALID', 'Action result does not satisfy its declared schema.');
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
