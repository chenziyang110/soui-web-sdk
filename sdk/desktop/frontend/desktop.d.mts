/** Modern Soui::Desktop only; generic types do not validate native payloads. */
export interface DesktopAsset {
  readonly mime: string;
  readonly size: number;
  readonly base64: string;
}

export type Unsubscribe = () => void;

/** Raw version-1 preload API. Presence does not guarantee a live session. */
export interface DesktopHost {
  readonly version: 1;
  invoke<TResult = unknown, TParams = unknown>(
    method: string, params?: TParams
  ): Promise<TResult>;
  on<TPayload = unknown>(
    event: string, listener: (payload: TPayload) => void
  ): Unsubscribe;
  readAsset(path: string): Promise<DesktopAsset>;
}

export type DesktopAdapterErrorCode =
  | 'desktop_adapter_missing_host'
  | 'desktop_adapter_incompatible_host'
  | 'desktop_adapter_invalid_window_state'
  | 'desktop_adapter_invalid_service_acceptance'
  | 'desktop_adapter_invalid_clipboard_text'
  | 'desktop_adapter_invalid_directory_selection'
  | 'desktop_adapter_invalid_asset'
  | 'desktop_adapter_asset_too_large'
  | 'desktop_adapter_text_decoder_unavailable'
  | 'desktop_adapter_invalid_utf8'
  | 'desktop_adapter_invalid_json';

/** Only local adapter failures use these codes; native errors pass through. */
export class DesktopAdapterError extends Error {
  readonly code: DesktopAdapterErrorCode;
  constructor(code: DesktopAdapterErrorCode, message: string);
}

export const MAX_ASSET_BYTES: 262144;
/** Throws immediately if no compatible host exists; does not poll for readiness. */
export function requireDesktop(): DesktopHost;
export function invoke<TResult = unknown, TParams = unknown>(
  method: string, params?: TParams
): Promise<TResult>;
export function on<TPayload = unknown>(
  event: string, listener: (payload: TPayload) => void
): Unsubscribe;
export function readAsset(path: string): Promise<DesktopAsset>;
/** System-observed flags, not mutually exclusive or inferred from a request. */
export interface WindowState {
  readonly minimized: boolean;
  readonly maximized: boolean;
}
/** The request was accepted; the window manager may not have applied it yet. */
export interface WindowControlAcceptance {
  readonly accepted: true;
}
/** Requires native options.enable_window_controls; never polls or retries. */
export const windowControls: Readonly<{
  minimize(): Promise<WindowControlAcceptance>;
  maximize(): Promise<WindowControlAcceptance>;
  restore(): Promise<WindowControlAcceptance>;
  /** Rejects when no observed state is available; malformed snapshots also reject. */
  getState(): Promise<WindowState>;
  /** Validates snapshots; returns the native unsubscribe function unchanged. */
  onStateChanged(listener: (state: WindowState) => void): Unsubscribe;
}>;
/** Accepted is not proof that the window manager has applied an operation. */
export interface DesktopServiceAcceptance {
  readonly accepted: true;
}
/** Requires default-off options.services.window_properties, not window controls.
 * Malformed responses reject with desktop_adapter_invalid_service_acceptance.
 * Native input/permission errors pass through unchanged; no retries.
 */
export const windowProperties: Readonly<{
  /** UTF-8 title at most 4096 bytes, without NUL. */
  setTitle(title: string): Promise<DesktopServiceAcceptance>;
  /** Integer dimensions in the inclusive range 64..16384. */
  resize(width: number, height: number): Promise<DesktopServiceAcceptance>;
  show(visible: boolean): Promise<DesktopServiceAcceptance>;
  focus(): Promise<DesktopServiceAcceptance>;
}>;
export const clipboard: Readonly<{
  /** Requires options.services.clipboard_read. Empty text is valid; malformed,
   * NUL-containing or over-64-KiB UTF-8 results reject with desktop_adapter_invalid_clipboard_text.
   */
  readText(): Promise<string>;
  /** Requires independent options.services.clipboard_write. At most 32768 UTF-8
   * bytes without NUL; native validates inputs. Malformed acceptance rejects with
   * desktop_adapter_invalid_service_acceptance. Native errors pass through.
   */
  writeText(text: string): Promise<DesktopServiceAcceptance>;
}>;
/** Cancellation is explicit; successful path is nonempty, valid UTF-8 without
 * NUL, at most 65536 bytes. A selection does not grant file access.
 */
export type DirectorySelection =
  | { readonly cancelled: true; readonly path: '' }
  | { readonly cancelled: false; readonly path: string };
export const dialogs: Readonly<{
  /** Requires options.services.directory_dialog. Malformed results reject with
   * desktop_adapter_invalid_directory_selection. Native errors pass through,
   * rather than being converted to cancellation. No implicit permission prompts.
   */
  chooseDirectory(): Promise<DirectorySelection>;
}>;
/** Synchronously decode a bounded, canonical base64 asset response. */
export function assetBytes(asset: DesktopAsset): Uint8Array;
/** Synchronously decode UTF-8; malformed input throws rather than replacing bytes. */
export function assetText(asset: DesktopAsset): string;
/** Parses JSON only. T is a compile-time assertion, not runtime schema validation. */
export function assetJson<T = unknown>(asset: DesktopAsset): T;

declare global {
  interface Window {
    readonly souiDesktop?: DesktopHost;
  }
}
