import { copySouiJson } from './action-contracts.js';
import type { SouiJson } from './action-contracts.js';
import { isRecord, SouiBridgeError, utf8Bytes } from './protocol.js';
import type { SouiNativeEvent } from './protocol.js';
import type { SouiGui, SouiGuiCommandContext } from './gui.js';

export interface SouiGuiCommand {
  protocol: 'soui.gui.command/1'; requestId: string; documentId: string; generation: string;
  operation: 'perform' | 'snapshot' | 'waitFor'; id: string; action: string;
  inputJson: string; effectId: '' | 'native'; timeoutMs: number;
}
export interface SouiGuiBinding { readonly ready: Promise<void>; readonly disposed: boolean; dispose(): void }
type Request = (method: string, params: unknown) => Promise<unknown>;
const keys = ['protocol', 'requestId', 'documentId', 'generation', 'operation', 'id', 'action', 'inputJson', 'effectId', 'timeoutMs'];
function id(value: unknown): value is string { return typeof value === 'string' && /^[A-Za-z0-9_.:-]{1,128}$/.test(value); }
function invalid(): never { throw new SouiBridgeError('E_GUI_COMMAND', 'The native GUI command is invalid.'); }
function objectJson(value: unknown): Record<string, SouiJson> {
  if (typeof value !== 'string' || utf8Bytes(value) > 32768) return invalid();
  let parsed: unknown;
  try { parsed = JSON.parse(value); } catch { return invalid(); }
  if (!isRecord(parsed)) return invalid();
  return copySouiJson(parsed, 32768, 'E_GUI_COMMAND') as Record<string, SouiJson>;
}
/** Shape validation is separate from authority: only the native coordinator grants an effect. */
export function readGuiCommand(value: unknown): SouiGuiCommand {
  value = copySouiJson(value, 65536, 'E_GUI_COMMAND');
  if (!isRecord(value) || Object.keys(value).length !== keys.length || Object.keys(value).some(key => keys.indexOf(key) < 0) ||
      value.protocol !== 'soui.gui.command/1' || !id(value.requestId) || !id(value.documentId) || !id(value.generation) ||
      !id(value.id) || ['perform', 'snapshot', 'waitFor'].indexOf(value.operation as string) < 0 ||
      (value.operation === 'perform' ? !id(value.action) : value.action !== '') ||
      (value.effectId !== '' && value.effectId !== 'native') ||
      (value.operation !== 'perform' && value.effectId !== '') ||
      !Number.isSafeInteger(value.timeoutMs) || (value.timeoutMs as number) < 1 || (value.timeoutMs as number) > 10000) return invalid();
  objectJson(value.inputJson);
  if (utf8Bytes(JSON.stringify(value)) > 65536) return invalid();
  return { ...value } as unknown as SouiGuiCommand;
}

// Client-owned route. One request has one closure; it never installs ambient
// Agent authority into an ordinary page client or handler.
export class GuiCommandRoute implements SouiGuiBinding {
  public readonly ready: Promise<void>;
  private stopped = false;
  private bound = false;
  private sequence = 0;
  private documentId = '';
  private generation = '';
  private readonly seen = new Set<string>();
  private readonly active = new Set<string>();
  public constructor(private readonly gui: SouiGui, private readonly request: Request,
                      private readonly alive: () => boolean, private readonly report: (error: unknown) => void) {
    this.ready = Promise.resolve().then(() => {
      if (this.stopped || !alive()) throw new SouiBridgeError('E_DISPOSED', 'The GUI route was closed.');
      return request('gui.bind', {});
    }).then(value => {
      if (this.stopped || !alive()) throw new SouiBridgeError('E_DISPOSED', 'The GUI route was closed.');
      if (!isRecord(value) || Object.keys(value).length !== 1 || value.accepted !== true) return invalid();
      this.bound = true;
    });
    void this.ready.catch(error => { this.dispose(); report(error); });
  }
  public get disposed(): boolean { return this.stopped; }
  public dispose(): void { this.stopped = true; this.active.clear(); }
  public receive(event: SouiNativeEvent): void {
    if (this.stopped || !this.bound || !this.alive() || event.eventName !== 'gui.command' ||
        event.taskId !== undefined || event.sequence <= this.sequence) return;
    try {
      event = copySouiJson(event, 65536, 'E_GUI_COMMAND') as unknown as SouiNativeEvent;
      if (Object.keys(event).length !== 4 || Object.keys(event).some(key => ['sessionId', 'sequence', 'eventName', 'payload'].indexOf(key) < 0)) return invalid();
      const command = readGuiCommand(event.payload);
      if (this.stopped || !this.alive()) return;
      if ((this.documentId && (this.documentId !== command.documentId || this.generation !== command.generation)) ||
          this.seen.has(command.requestId)) return;
      // Finite per-document replay ledger; exhaustion closes the route rather
      // than evicting an identity which could then be executed again.
      if (this.seen.size >= 256 || this.active.size >= 16) throw new SouiBridgeError('E_BUSY', 'The GUI route is full.');
      this.sequence = event.sequence; this.documentId = command.documentId; this.generation = command.generation;
      this.seen.add(command.requestId); this.active.add(command.requestId);
      void this.execute(command);
    } catch (error) { this.dispose(); this.report(error); }
  }
  private async execute(command: SouiGuiCommand): Promise<void> {
    const deadline = Date.now() + command.timeoutMs;
    const current = () => !this.stopped && this.alive() && !this.gui.disposed &&
      this.active.has(command.requestId) && Date.now() < deadline;
    let consumed = false;
    try {
    const input = objectJson(command.inputJson);
    const context: SouiGuiCommandContext = { get active() { return current(); }, effect: async supplied => {
      if (!current() || consumed || command.effectId !== 'native')
        throw new SouiBridgeError('E_GUI_EFFECT_USED', 'This GUI effect is unavailable.');
      consumed = true; // Never retry, including a rejected or lost response.
      const encoded = JSON.stringify(copySouiJson(supplied === undefined ? input : supplied, 32768, 'E_INVALID_PARAMS'));
      const value = await this.request('gui.effect', { requestId: command.requestId, documentId: command.documentId,
        effectId: command.effectId, inputJson: encoded });
      if (!current()) throw new SouiBridgeError('E_DISPOSED', 'The GUI effect response is stale.');
      if (!isRecord(value) || Object.keys(value).length !== 1) return invalid();
      return objectJson(value.resultJson);
    } };
    let ok = false;
    let resultJson = '{}';
    try {
      if (!current()) return;
      const result = await this.gui.executeNativeCommand(command, input, context);
      if (!isRecord(result)) return invalid();
      resultJson = JSON.stringify(copySouiJson(result, 32768, 'E_GUI_COMMAND'));
      ok = true;
    } catch { /* Fixed failed ACK; no application exception or raw secret text. */ }
    try {
      if (!current()) return;
      const accepted = await this.request('gui.ack', { requestId: command.requestId, documentId: command.documentId, ok, resultJson });
      if (current() && (!isRecord(accepted) || Object.keys(accepted).length !== 1 || accepted.accepted !== true)) return invalid();
    } catch (error) { if (current()) this.report(error); }
    } catch (error) { if (current()) this.report(error); }
    finally { this.active.delete(command.requestId); }
  }
}
