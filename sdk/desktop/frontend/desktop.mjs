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

function windowState(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    fail('invalid_window_state', 'Expected an observed minimized/maximized snapshot');
  }
  const { minimized, maximized } = value;
  if (typeof minimized !== 'boolean' || typeof maximized !== 'boolean') {
    fail('invalid_window_state', 'Expected an observed minimized/maximized snapshot');
  }
  // Both flags may be true: a minimized window can retain its maximized state.
  return { minimized, maximized };
}

// Requires explicit native enable_window_controls permission. Acceptance is not
// an observed transition; only getState/stateChanged supply window state.
export const windowControls = Object.freeze({
  minimize() { return invoke('desktop.window.minimize', {}); },
  maximize() { return invoke('desktop.window.maximize', {}); },
  restore() { return invoke('desktop.window.restore', {}); },
  getState() { return invoke('desktop.window.getState', {}).then(windowState); },
  onStateChanged(listener) {
    if (typeof listener !== 'function') throw new TypeError('Expected a window state listener');
    return on('desktop.window.stateChanged', value => listener(windowState(value)));
  }
});

function serviceAcceptance(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value) || value.accepted !== true) {
    fail('invalid_service_acceptance', 'Expected desktop service acceptance');
  }
  return { accepted: true };
}

// Count strict UTF-8 without requiring TextEncoder or allocating a second copy.
// Reject lone surrogates rather than silently replacing malformed native text.
function boundedTextBytes(text, limit, code) {
  if (typeof text !== 'string' || text.length > limit) {
    fail(code, `Expected desktop text within ${limit} UTF-8 bytes`);
  }
  let bytes = 0;
  for (let i = 0; i < text.length; ++i) {
    const unit = text.charCodeAt(i);
    if (unit === 0) fail(code, 'Desktop service text must not contain NUL');
    if (unit >= 0xd800 && unit <= 0xdbff) {
      const low = text.charCodeAt(++i);
      if (!(low >= 0xdc00 && low <= 0xdfff)) fail(code, 'Expected valid UTF-8 service text');
      bytes += 4;
    } else {
      if (unit >= 0xdc00 && unit <= 0xdfff) fail(code, 'Expected valid UTF-8 service text');
      bytes += unit < 0x80 ? 1 : unit < 0x800 ? 2 : 3;
    }
    if (bytes > limit) fail(code, `Desktop text exceeds ${limit} UTF-8 bytes`);
  }
  return bytes;
}

function boundedServiceText(text, code) {
  boundedTextBytes(text, 65536, code);
  return text;
}

function clipboardText(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    fail('invalid_clipboard_text', 'Expected a native clipboard text result');
  }
  return boundedServiceText(value.text, 'invalid_clipboard_text');
}

function directorySelection(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    fail('invalid_directory_selection', 'Expected a native directory selection');
  }
  const { cancelled, path } = value;
  const text = boundedServiceText(path, 'invalid_directory_selection');
  if (typeof cancelled !== 'boolean' || (cancelled ? text !== '' : text === '')) {
    fail('invalid_directory_selection', 'Expected cancellation with an empty path or a selected path');
  }
  return { cancelled, path: text };
}

function fileDialogKeys(value, allowed, code, exact = false) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    fail(code, 'Expected a file dialog record');
  }
  const prototype = Object.getPrototypeOf(value);
  if (prototype !== null && prototype !== Object.prototype) fail(code, 'Expected a plain file dialog record');
  const keys = Reflect.ownKeys(value);
  if ((exact && keys.length !== allowed.length) ||
      keys.some(key => !allowed.includes(key))) {
    fail(code, 'Unexpected or missing file dialog fields');
  }
  return keys;
}

function fileDialogOptions(options, save) {
  const code = 'invalid_file_dialog_options';
  const keys = fileDialogKeys(options, ['title', 'extensions', save ? 'defaultName' : 'multiple'], code);
  const params = {};
  // Snapshot and validate each supplied value once, then send only our own
  // record. Accessors/toJSON cannot replace validated fields during transport.
  for (const key of keys) {
    const value = options[key];
    if (key === 'title') {
      boundedTextBytes(value, 4096, code);
      params.title = value;
    } else if (key === 'extensions') {
      if (!Array.isArray(value)) fail(code, 'Expected an array of file extensions');
      const count = value.length;
      if (!Number.isInteger(count) || count < 0 || count > 16) fail(code, 'Expected at most 16 file extensions');
      const extensions = [];
      for (let i = 0; i < count; ++i) {
        const extension = value[i];
        if (typeof extension !== 'string' || extension.length < 1 || extension.length > 16 ||
            /[^A-Za-z0-9]/.test(extension)) {
          fail(code, 'Expected 1..16 ASCII letters or digits per extension');
        }
        const normalized = extension.toLowerCase();
        if (!extensions.includes(normalized)) extensions.push(normalized);
      }
      params.extensions = extensions;
    } else if (key === 'multiple') {
      if (typeof value !== 'boolean') fail(code, 'Expected a boolean multiple option');
      params.multiple = value;
    } else {
      boundedTextBytes(value, 255, code);
      // Match the native portable basename rules, not a browser OS heuristic.
      const stem = value.split('.')[0].replace(/[ .]+$/, '').replace(/[a-z]/g, c => c.toUpperCase());
      if (/[\x00-\x1f\x7f-\x9f<>:"/\\|?*]/.test(value) || /[ .]$/.test(value) ||
          /^(CON|PRN|AUX|NUL|CLOCK\$|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])$/.test(stem)) {
        fail(code, 'Expected a portable default basename without reserved names');
      }
      params.defaultName = value;
    }
  }
  return params;
}

function openFileSelection(value, multiple) {
  const code = 'invalid_open_file_selection';
  fileDialogKeys(value, ['cancelled', 'paths'], code, true);
  const { cancelled, paths } = value;
  if (typeof cancelled !== 'boolean' || !Array.isArray(paths)) {
    fail(code, 'Expected an explicit open-file selection');
  }
  const count = paths.length;
  if (!Number.isInteger(count) || (cancelled ? count !== 0 : count < 1 || count > (multiple ? 32 : 1))) {
    fail(code, 'Unexpected number of selected file paths');
  }
  const selected = [];
  let total = 0;
  for (let i = 0; i < count; ++i) {
    const path = paths[i];
    total += boundedTextBytes(path, 32768, code);
    if (path === '' || total > 128 * 1024) fail(code, 'Selected file paths exceed their bounds');
    selected.push(path);
  }
  // The native host validates absolute paths using the actual platform's
  // filesystem semantics. Do not guess the host OS from browser path strings.
  return { cancelled, paths: selected };
}

function saveFileSelection(value) {
  const code = 'invalid_save_file_selection';
  fileDialogKeys(value, ['cancelled', 'path'], code, true);
  const { cancelled, path } = value;
  boundedTextBytes(path, 32768, code);
  if (typeof cancelled !== 'boolean' || (cancelled ? path !== '' : path === '')) {
    fail(code, 'Expected cancellation with an empty path or a selected file path');
  }
  return { cancelled, path };
}

// All services require independent, default-off native permissions. Inputs are
// validated by native dispatch; these facades never grant permission or retry.
export const windowProperties = Object.freeze({
  setTitle(title) { return invoke('desktop.window.setTitle', { title }).then(serviceAcceptance); },
  resize(width, height) {
    return invoke('desktop.window.resize', { width, height }).then(serviceAcceptance);
  },
  show(visible) { return invoke('desktop.window.show', { visible }).then(serviceAcceptance); },
  focus() { return invoke('desktop.window.focus', {}).then(serviceAcceptance); }
});

export const clipboard = Object.freeze({
  readText() { return invoke('desktop.clipboard.readText', {}).then(clipboardText); },
  writeText(text) { return invoke('desktop.clipboard.writeText', { text }).then(serviceAcceptance); }
});

export const dialogs = Object.freeze({
  chooseDirectory() { return invoke('desktop.dialog.chooseDirectory', {}).then(directorySelection); },
  openFiles(options = {}) {
    const params = fileDialogOptions(options, false);
    const multiple = params.multiple === true;
    return invoke('desktop.dialog.openFiles', params).then(value => openFileSelection(value, multiple));
  },
  saveFile(options = {}) {
    const params = fileDialogOptions(options, true);
    return invoke('desktop.dialog.saveFile', params).then(saveFileSelection);
  }
});

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
