import { SouiBridgeError, integerOption, isRecord, nonEmptyString, utf8Bytes } from './protocol.js';
import { readSouiActionContract } from './action-contracts.js';
import type { SouiActionContract } from './action-contracts.js';

/** Registration and permission information, not proof of business preconditions. */
export interface SouiMethodCapability {
  name: string;
  capability: string;
  action?: SouiActionContract;
}

export interface SouiDescribeOptions {
  cursor?: number;
  limit?: number;
}

export interface SouiCapabilityCatalog {
  protocol: 'soui.capabilities/1';
  sdkVersion: string;
  methods: SouiMethodCapability[];
}

export interface SouiCapabilityPage extends SouiCapabilityCatalog {
  nextCursor: number | null;
}

export interface SouiDiscoveryOptions {
  pageSize?: number;
  /** Bounds total retained entries and prevents an unbounded discovery loop. */
  maxMethods?: number;
}

function checkOptions(options: unknown, fields: string[]): void {
  if (!isRecord(options) || Object.keys(options).some(key => fields.indexOf(key) < 0))
    throw new SouiBridgeError('E_INVALID_OPTIONS', `Expected only these options: ${fields.join(', ')}.`);
}

export function describeOptions(options: SouiDescribeOptions): { cursor: number; limit: number } {
  checkOptions(options, ['cursor', 'limit']);
  const cursor = options.cursor === undefined ? 0 : options.cursor;
  if (!Number.isSafeInteger(cursor) || cursor < 0)
    throw new SouiBridgeError('E_INVALID_OPTIONS', 'Describe cursor must be a nonnegative safe integer.');
  return { cursor, limit: integerOption(options.limit, 32, 64, 'limit') };
}

export function readCapabilityPage(raw: unknown, cursor: number, limit: number): SouiCapabilityPage {
  const invalid = () => new SouiBridgeError('E_INVALID_RESPONSE', 'Native capability page is invalid.');
  if (!isRecord(raw) || raw.protocol !== 'soui.capabilities/1' ||
      !nonEmptyString(raw.sdkVersion, 128) || !Array.isArray(raw.methods) || raw.methods.length > limit)
    throw invalid();
  const methods: SouiMethodCapability[] = [];
  let previous = '';
  for (const method of raw.methods) {
    if (!isRecord(method) || !nonEmptyString(method.name, 128) ||
        !/^[A-Za-z0-9_.:-]+$/.test(method.name) || method.name <= previous ||
        !nonEmptyString(method.capability, 512) || utf8Bytes(method.capability) > 512)
      throw invalid();
    const entry: SouiMethodCapability = { name: method.name, capability: method.capability };
    if (method.action !== undefined) entry.action = readSouiActionContract(method.action);
    methods.push(entry);
    previous = method.name;
  }
  if (raw.nextCursor !== null &&
      (!Number.isSafeInteger(raw.nextCursor) || methods.length === 0 || raw.nextCursor !== cursor + methods.length))
    throw invalid();
  return { protocol: 'soui.capabilities/1', sdkVersion: raw.sdkVersion,
    methods, nextCursor: raw.nextCursor as number | null };
}

export async function discoverCapabilities(
  describe: (options: SouiDescribeOptions) => Promise<SouiCapabilityPage>,
  options: SouiDiscoveryOptions,
): Promise<SouiCapabilityCatalog> {
  checkOptions(options, ['pageSize', 'maxMethods']);
  const pageSize = integerOption(options.pageSize, 32, 64, 'pageSize');
  const maximum = integerOption(options.maxMethods, 1024, 4096, 'maxMethods');
  const methods: SouiMethodCapability[] = [];
  let cursor = 0;
  let sdkVersion: string | undefined;
  for (;;) {
    const page = await describe({ cursor, limit: Math.min(pageSize, maximum - methods.length) });
    if ((sdkVersion !== undefined && page.sdkVersion !== sdkVersion) ||
        (methods.length > 0 && page.methods.length > 0 &&
         page.methods[0]!.name <= methods[methods.length - 1]!.name))
      throw new SouiBridgeError('E_INVALID_RESPONSE', 'The native capability catalog changed during discovery.');
    sdkVersion = page.sdkVersion;
    methods.push(...page.methods);
    if (page.nextCursor === null) return { protocol: 'soui.capabilities/1', sdkVersion, methods };
    if (methods.length >= maximum)
      throw new SouiBridgeError('E_LIMIT_CAPABILITIES', 'The capability catalog exceeds maxMethods.');
    cursor = page.nextCursor;
  }
}
