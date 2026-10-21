// Modern Soui::Desktop only. The injected host owns transport and session lifetime.
export const MAX_ASSET_BYTES = 256 * 1024;

export class DesktopAdapterError extends Error {
  constructor(code, message) {
    super(message);
    this.name = 'DesktopAdapterError';
    this.code = code;
  }
}

function fail(code, message) {
  throw new DesktopAdapterError(`desktop_adapter_${code}`, message);
}

// No polling: presence does not imply that the native session is still live.
export function requireDesktop() {
  const host = typeof window === 'undefined' ? undefined : window.souiDesktop;
  if (host === undefined || host === null) {
    fail('missing_host', 'Soui::Desktop host is unavailable in this document');
  }
  if (host.version !== 1 || typeof host.invoke !== 'function' ||
      typeof host.on !== 'function' || typeof host.readAsset !== 'function') {
    fail('incompatible_host', 'Expected the Soui::Desktop version 1 host API');
  }
  return host;
}

// Return the original promise/unsubscribe function and let native errors pass through.
export function invoke(method, params) {
  return requireDesktop().invoke(method, params);
}

export function on(event, listener) {
  return requireDesktop().on(event, listener);
}

export function readAsset(path) {
  return requireDesktop().readAsset(path);
}

const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

// Validate lengths before allocation; reject noncanonical padding and unused bits.
export function assetBytes(asset) {
  if (!asset || typeof asset !== 'object') {
    fail('invalid_asset', 'Expected an asset with mime, integer size and base64');
  }
  // Snapshot before validation: callers can supply objects with accessors, not
  // only the plain JSON records returned by the native host. Every subsequent
  // bound and allocation must use the same validated primitive values.
  const { mime, size, base64 } = asset;
  if (typeof mime !== 'string' || !Number.isSafeInteger(size) || size < 0 ||
      typeof base64 !== 'string') {
    fail('invalid_asset', 'Expected an asset with mime, integer size and base64');
  }
  if (size > MAX_ASSET_BYTES) {
    fail('asset_too_large', 'Asset exceeds the 256 KiB decoding limit');
  }
  if (base64.length !== Math.ceil(size / 3) * 4) {
    fail('invalid_asset', 'Asset base64 length does not match its size');
  }
  const padding = size % 3 === 0 ? 0 : 3 - size % 3;
  const dataLength = base64.length - padding;
  for (let i = 0; i < base64.length; ++i) {
    if (i >= dataLength ? base64[i] !== '=' : alphabet.indexOf(base64[i]) < 0) {
      fail('invalid_asset', 'Asset base64 is not canonical');
    }
  }
  if (padding && (alphabet.indexOf(base64[dataLength - 1]) &
      (padding === 2 ? 15 : 3)) !== 0) {
    fail('invalid_asset', 'Asset base64 has nonzero unused bits');
  }
  const bytes = new Uint8Array(size);
  let offset = 0;
  for (let i = 0; i < base64.length; i += 4) {
    const a = alphabet.indexOf(base64[i]);
    const b = alphabet.indexOf(base64[i + 1]);
    const c = alphabet.indexOf(base64[i + 2]);
    const d = alphabet.indexOf(base64[i + 3]);
    bytes[offset++] = (a << 2) | (b >> 4);
    if (offset < size) bytes[offset++] = (b << 4) | (c >> 2);
    if (offset < size) bytes[offset++] = (c << 6) | d;
  }
  return bytes;
}

export function assetText(asset) {
  const bytes = assetBytes(asset);
  if (typeof TextDecoder === 'undefined') {
    fail('text_decoder_unavailable', 'A fatal UTF-8 TextDecoder is required');
  }
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
  } catch {
    fail('invalid_utf8', 'Asset is not valid UTF-8 text');
  }
}

export function assetJson(asset) {
  const text = assetText(asset);
  try {
    return JSON.parse(text);
  } catch {
    fail('invalid_json', 'Asset is not valid JSON');
  }
}
