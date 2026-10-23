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
  | 'desktop_adapter_invalid_file_dialog_options'
  | 'desktop_adapter_invalid_open_file_selection'
  | 'desktop_adapter_invalid_save_file_selection'
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
export interface OpenFileOptions {
  /** Optional title, at most 4096 UTF-8 bytes without NUL. */
  readonly title?: string;
  /** At most 16 entries of 1..16 ASCII letters/digits, normalized to lowercase
   * and deduplicated in order. Empty means all files. UI hint, not content validation.
   */
  readonly extensions?: readonly string[];
  /** Defaults to false; true permits at most 32 paths. */
  readonly multiple?: boolean;
}
export interface SaveFileOptions {
  /** Optional title, at most 4096 UTF-8 bytes without NUL. */
  readonly title?: string;
  /** Same extension rules as OpenFileOptions; the OS may adjust the final suffix. */
  readonly extensions?: readonly string[];
  /** Empty or a portable basename of at most 255 UTF-8 bytes. No path separators,
   * control characters, Windows-invalid/reserved names, or trailing dots/spaces.
   */
  readonly defaultName?: string;
}
/** Native absolute paths, each nonempty and at most 32768 UTF-8 bytes without NUL;
 * at most 32 paths and 128 KiB total. Selecting paths does not grant file access.
 */
export type OpenFileSelection =
  | { readonly cancelled: true; readonly paths: [] }
  | { readonly cancelled: false; readonly paths: string[] };
export type SaveFileSelection =
  | { readonly cancelled: true; readonly path: '' }
  | { readonly cancelled: false; readonly path: string };
export const dialogs: Readonly<{
  /** Requires options.services.directory_dialog. Malformed results reject with
   * desktop_adapter_invalid_directory_selection. Native errors pass through,
   * rather than being converted to cancellation. No implicit permission prompts.
   */
  chooseDirectory(): Promise<DirectorySelection>;
  /** Requires independent default-off options.services.file_open_dialog.
   * Invalid options throw desktop_adapter_invalid_file_dialog_options before
   * dispatch; malformed responses reject with desktop_adapter_invalid_open_file_selection.
   * Only native E_CANCELLED is converted to cancellation by the host; all errors
   * reaching this facade pass through unchanged. No filesystem reads or grants.
   */
  openFiles(options?: OpenFileOptions): Promise<OpenFileSelection>;
  /** Requires independent default-off options.services.file_save_dialog. Selects
   * a path only, without creating/truncating/writing a file or granting access.
   * Invalid options throw desktop_adapter_invalid_file_dialog_options; malformed
   * results reject with desktop_adapter_invalid_save_file_selection. Successful
   * path is nonempty and at most 32768 UTF-8 bytes; other errors pass through.
   */
  saveFile(options?: SaveFileOptions): Promise<SaveFileSelection>;
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
